// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Core/TBGameState.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "MediaPlayer.h"
#include "MoviePlayer.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UI/Loading/STBLoadingScreen.h"
#include "UI/Loading/TBLoadingScreenSettings.h"
#include "UI/Loading/TBLoadingScreenSubsystem.h"
#include "UI/TBUIText.h"
#include "UnrealClient.h"

// 暗転が始まったら、おもちゃの周期は次の周へ進まない。既定は 見せる1.6 + 走る2.2 + 隠れる0.7 = 4.5 秒。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBLoadingScreenToyStop, "ToonStory.Review.LoadingScreenToyStop",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTBLoadingScreenToyStop::RunTest(const FString&)
{
	// 隠れている時間の終わりぎわで始まれば、その時刻で止まり、次の周に入らない。
	const double Hidden = STBLoadingScreen::ToyStopElapsed(4.45, 1.6, 2.2, .7);
	TestEqual(TEXT("暗転が隠れ際で始まれば、その時刻で止まる"), Hidden, 4.45);
	TestTrue(TEXT("次の周に入らない"), Hidden < 4.5);
	// 走っている途中で始まれば、その周の走り終わりまで動く。
	TestEqual(TEXT("走っている途中なら走り終わりで止まる"), STBLoadingScreen::ToyStopElapsed(2.0, 1.6, 2.2, .7), 3.8);
	TestEqual(TEXT("2周目でも同じ"), STBLoadingScreen::ToyStopElapsed(4.5 + 1.0, 1.6, 2.2, .7), 4.5 + 3.8);
	// 隠れ際が周の頭に近い側でも、その周から出ない。
	TestEqual(TEXT("隠れはじめの時刻で止まる"), STBLoadingScreen::ToyStopElapsed(3.9, 1.6, 2.2, .7), 3.9);
	// 隠れる間が0でも、次の周には入らない。
	TestTrue(TEXT("隠れる間が0でも次の周に入らない"), STBLoadingScreen::ToyStopElapsed(1.0, 1.6, 2.2, 0.) < 3.8);
	return true;
}

namespace
{
	// Render the real loading widget once per configured toy; never save the temporary CDO values.
	class FLoadingIllustrationReview : public IAutomationLatentCommand
	{
	public:
		FLoadingIllustrationReview(FAutomationTestBase* InTest, UGameViewportClient* InViewport)
		    : Test(InTest), Viewport(InViewport), Settings(GetMutableDefault<UTBLoadingScreenSettings>()),
		      OldToys(Settings->Toys), OldPeek(Settings->PeekSeconds), OldRun(Settings->RunSeconds),
		      OldHidden(Settings->HiddenSeconds), OldFadeIn(Settings->FadeInSeconds),
		      OldLanguage(TBText::GetLanguage()), Deadline(FPlatformTime::Seconds() + 90)
		{
			FString Language;
			if (FParse::Value(FCommandLine::Get(), TEXT("UIReviewLanguage="), Language))
				bFailed = !Test->TestTrue(TEXT("Loading review language activates"), TBText::SetLanguage(Language));
			Settings->PeekSeconds = .2f;
			Settings->RunSeconds = 12.f;
			Settings->HiddenSeconds = 1.f;
			Settings->FadeInSeconds = 0.f;
		}

		~FLoadingIllustrationReview() override
		{
			RemoveScreen();
			Settings->Toys = OldToys;
			Settings->PeekSeconds = OldPeek;
			Settings->RunSeconds = OldRun;
			Settings->HiddenSeconds = OldHidden;
			Settings->FadeInSeconds = OldFadeIn;
			if (TBText::GetLanguage() != OldLanguage) TBText::SetLanguage(OldLanguage);
		}

		virtual bool Update() override
		{
			if (bFailed) return true;
			if (!Viewport.IsValid() || !Viewport->Viewport || FPlatformTime::Seconds() > Deadline)
			{
				Test->AddError(TEXT("Loading illustration review lost its viewport or timed out"));
				return true;
			}
			if (!Texture.IsValid())
			{
				Texture.Reset(OldToys[Index].LoadSynchronous());
				if (!Test->TestNotNull(TEXT("Configured loading illustration loads"), Texture.Get())) return true;
			}
#if WITH_EDITOR
			if (Texture->IsCompiling()) return false;
#endif
			if (!Screen.IsValid())
			{
				if (!Test->TestTrue(TEXT("Loading illustration has high-resolution source pixels"),
				        Texture->GetSizeX() >= 512 && Texture->GetSizeY() >= 512) ||
				    !Test->TestTrue(TEXT("Loading illustration has generated mips"), Texture->GetNumMips() > 1)) return true;
				Settings->Toys = {OldToys[Index]};
				Screen = SNew(STBLoadingScreen);
				Screen->SetStatus(TBText::Get(TEXT("Root.Loading.Prepare")));
				Viewport->AddViewportWidgetContent(Screen.ToSharedRef(), 100000);
				// Construct starts at 40% of Peek. Leave headroom for a slow high-resolution capture frame.
				ReadyAt = FPlatformTime::Seconds() + Settings->PeekSeconds * .6 + Settings->RunSeconds * .4;
				return false;
			}
			if (!bRequested)
			{
				if (FPlatformTime::Seconds() < ReadyAt) return false;
				if (!Test->TestFalse(TEXT("Loading toy is visible during the run capture"), Screen->IsToyHidden())) return true;
				Size = Viewport->Viewport->GetSizeXY();
				int32 RequestedWidth = Size.X, RequestedHeight = Size.Y;
				FParse::Value(FCommandLine::Get(), TEXT("ResX="), RequestedWidth);
				FParse::Value(FCommandLine::Get(), TEXT("ResY="), RequestedHeight);
				if (!Test->TestEqual(TEXT("Loading capture uses the requested viewport width"), Size.X, RequestedWidth) ||
				    !Test->TestEqual(TEXT("Loading capture uses the requested viewport height"), Size.Y, RequestedHeight)) return true;
				const FString Folder = FPaths::ProjectSavedDir() / TEXT("UIRefresh/LoadingIllustrations") /
				    TBText::GetLanguage() / FString::Printf(TEXT("%dx%d"), Size.X, Size.Y);
				IFileManager::Get().MakeDirectory(*Folder, true);
				Path = Folder / (Texture->GetName() + TEXT(".png"));
				PreviousTimestamp = IFileManager::Get().GetTimeStamp(*Path);
				FScreenshotRequest::RequestScreenshot(Path, true, false);
				bRequested = true;
				return false;
			}
			if (FScreenshotRequest::IsScreenshotRequested()) return false;
			TArray<uint8> PNG;
			if (!Test->TestTrue(TEXT("Fresh loading screenshot was written"),
			        IFileManager::Get().GetTimeStamp(*Path) != PreviousTimestamp &&
			        FFileHelper::LoadFileToArray(PNG, *Path) && PNG.Num() > 1024)) return true;
			// Read PNG IHDR dimensions so a viewport-sized file, not an OS-clamped image, is verified.
			const uint8 Signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
			if (!Test->TestTrue(TEXT("Loading capture is a PNG with IHDR"),
			        FMemory::Memcmp(PNG.GetData(), Signature, sizeof(Signature)) == 0 &&
			        PNG[12] == 'I' && PNG[13] == 'H' && PNG[14] == 'D' && PNG[15] == 'R')) return true;
			auto ReadSize = [&PNG](int32 Offset)
			{
				return int32((uint32(PNG[Offset]) << 24) | (uint32(PNG[Offset + 1]) << 16) |
				    (uint32(PNG[Offset + 2]) << 8) | uint32(PNG[Offset + 3]));
			};
			Test->TestEqual(TEXT("Loading PNG width matches viewport"), ReadSize(16), Size.X);
			Test->TestEqual(TEXT("Loading PNG height matches viewport"), ReadSize(20), Size.Y);
			Test->AddInfo(FString::Printf(TEXT("Rendered loading illustration: %s"), *Path));
			RemoveScreen();
			Texture.Reset();
			bRequested = false;
			return ++Index == OldToys.Num();
		}

	private:
		void RemoveScreen()
		{
			if (Screen.IsValid() && Viewport.IsValid()) Viewport->RemoveViewportWidgetContent(Screen.ToSharedRef());
			Screen.Reset();
		}
		FAutomationTestBase* Test;
		TWeakObjectPtr<UGameViewportClient> Viewport;
		UTBLoadingScreenSettings* Settings;
		TArray<TSoftObjectPtr<UTexture2D>> OldToys;
		float OldPeek, OldRun, OldHidden, OldFadeIn;
		FString OldLanguage, Path;
		double Deadline, ReadyAt = 0;
		TStrongObjectPtr<UTexture2D> Texture;
		TSharedPtr<STBLoadingScreen> Screen;
		FIntPoint Size = FIntPoint::ZeroValue;
		FDateTime PreviousTimestamp;
		int32 Index = 0;
		bool bRequested = false, bFailed = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBLoadingIllustrations, "ToonStory.UI.VisualReview.LoadingIllustrations",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBLoadingIllustrations::RunTest(const FString&)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("UIReviewScreenshots")))
	{
		AddInfo(TEXT("Use -UIReviewScreenshots in a fresh rendered standalone process to capture loading illustrations"));
		return true;
	}
	auto* Viewport = GEngine ? GEngine->GameViewport.Get() : nullptr;
	auto* World = Viewport ? Viewport->GetWorld() : nullptr;
	if (!TestTrue(TEXT("Loading review requires a rendered standalone game viewport"), Viewport && Viewport->Viewport &&
	        World && World->WorldType == EWorldType::Game && World->GetNetMode() == NM_Standalone) ||
	    !TestEqual(TEXT("Five loading illustrations are configured"), GetDefault<UTBLoadingScreenSettings>()->Toys.Num(), 5) ||
	    !TestFalse(TEXT("No other screenshot is pending"), FScreenshotRequest::IsScreenshotRequested())) return false;
	ADD_LATENT_AUTOMATION_COMMAND(FLoadingIllustrationReview(this, Viewport));
	return true;
}

namespace
{
	// Read actual Slate pixels: an advancing frame index alone also passes for a black texture.
	class FLoadingFilmReview : public IAutomationLatentCommand
	{
	public:
		FLoadingFilmReview(FAutomationTestBase* InTest, UGameViewportClient* InViewport, int32 InEdition)
		    : Test(InTest), Viewport(InViewport), Edition(InEdition)
		{
			}
		void Start()
		{
			Deadline = FPlatformTime::Seconds() + 30.;
			FString RequestedLanguage;
			if (FParse::Value(FCommandLine::Get(), TEXT("UIReviewLanguage="), RequestedLanguage))
			{
				PreviousLanguage = TBText::GetLanguage();
				Test->TestTrue(TEXT("The requested film caption language is available"), TBText::SetLanguage(RequestedLanguage));
			}
			Screen = SNew(STBLoadingScreen).Backdrop(ETBLoadingBackdrop::ViewportMovie).Edition(Edition);
			Screen->SetStatus(TBText::Get(TEXT("Root.Loading.Prepare")));
			Viewport->AddViewportWidgetContent(Screen.ToSharedRef(), 100000);
		}
		~FLoadingFilmReview() override
		{
			if (Viewport.IsValid() && Screen) Viewport->RemoveViewportWidgetContent(Screen.ToSharedRef());
			if (!PreviousLanguage.IsEmpty()) TBText::SetLanguage(PreviousLanguage);
		}
		bool Update() override
		{
			if (Viewport.IsValid() && !Screen) Start();
			if (!Viewport.IsValid() || FPlatformTime::Seconds() > Deadline)
			{
				Test->AddError(TEXT("The loading film did not render two poses and loop within 30 seconds"));
				return true;
			}
			if (bCaptureCurtain)
			{
				if (bCheckDeparture && !Screen->HasPaintedClosed()) return false;
				if (bCheckReveal && Screen->GetFilmReveal() < .4f) return false;
				TArray<FColor> Pixels;
				FIntVector Size(0);
				if (!Test->TestTrue(TEXT("The transition curtain renders"), FSlateApplication::Get().TakeScreenshot(Screen.ToSharedRef(), Pixels, Size) && !Pixels.IsEmpty())) return true;
				if (bCheckDeparture)
				{
					for (const int32 Y : {2, Size.Y / 2, Size.Y - 3})
						for (const int32 X : {2, Size.X / 2, Size.X - 3})
						{
							const FColor Cloth = Pixels[Y * Size.X + X];
							Test->TestTrue(TEXT("Departure cloth covers the center and edges before travel"),
								Cloth.R > 1.7f * Cloth.G && Cloth.R > 1.4f * Cloth.B);
						}
					bCheckDeparture = false;
					Screen->FinishDeparture(GetDefault<UTBLoadingScreenSettings>()->FadeInSeconds);
					bCheckReveal = true;
					return false;
				}
				if (bCheckReveal)
				{
					auto ClothFraction = [&](int32 X)
					{
						int32 Red = 0, Count = 0;
						for (int32 Y = Size.Y / 4; Y < Size.Y * 3 / 4; Y += 3)
						{
							const FColor Color = Pixels[Y * Size.X + X];
							Red += Color.R > 1.7f * Color.G && Color.R > 1.4f * Color.B;
							++Count;
						}
						return float(Red) / Count;
					};
					// 幕は来た左へ戻って開くので、右から先に映像が見える。
					Test->TestTrue(TEXT("The film is visible on the right while the opening cloth still covers the left"),
						ClothFraction(Size.X * 19 / 20) < .9f && ClothFraction(Size.X / 10) > .95f);
					TArray64<uint8> PNG;
					FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
					const FString Folder = FPaths::ProjectSavedDir() / TEXT("UIRefresh/LoadingFilm") / TBText::GetLanguage();
					Test->TestTrue(TEXT("Rendered opening saved"), FFileHelper::SaveArrayToFile(PNG, *(Folder / TEXT("OpeningCurtain.png"))));
					bCheckReveal = false;
					Screen->SetExit(.34f);
					return false;
				}
				int32 RedSamples = 0, Samples = 0, Lightest = 0, Darkest = 255;
				for (int32 X = Size.X / 8; X < Size.X / 2; X += 4)
				{
					const FColor Color = Pixels[Size.Y / 2 * Size.X + X];
					RedSamples += Color.R > 1.7f * Color.G && Color.R > 1.4f * Color.B;
					Darkest = FMath::Min(Darkest, int32(Color.R));
					Lightest = FMath::Max(Lightest, int32(Color.R));
					++Samples;
				}
				Test->TestTrue(TEXT("The closing curtain is red with visible cloth folds"), RedSamples > Samples * .95f && Lightest - Darkest > 35);
				TArray64<uint8> PNG;
				FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
				const FString Folder = FPaths::ProjectSavedDir() / TEXT("UIRefresh/LoadingFilm") / TBText::GetLanguage();
				Test->TestTrue(TEXT("Rendered stage curtain saved"), FFileHelper::SaveArrayToFile(PNG, *(Folder / TEXT("StageCurtain.png"))));
				return true;
			}
			UMediaPlayer* Player = Screen->GetPlaybackForReview();
			if (!Player || !Player->IsPlaying()) return false;
			const double Now = Player->GetTime().GetTotalSeconds();
			const bool bWrapped = Now + .5 < Previous;
			Previous = Now;
			const double FirstPose = GetDefault<UTBLoadingScreenSettings>()->FadeInSeconds + .15;
			if (Captured < 2 && Now >= (Captured == 0 ? FirstPose : FirstPose + 1.1))
			{
				TArray<FColor> Pixels;
				FIntVector Size;
				if (!FSlateApplication::Get().TakeScreenshot(Screen.ToSharedRef(), Pixels, Size) || Pixels.IsEmpty())
				{
					Test->AddError(TEXT("Could not read the rendered loading film"));
					return true;
				}
				TArray<FColor> Samples;
				for (int32 Y = Size.Y / 4; Y < Size.Y * 3 / 4; Y += 8)
					for (int32 X = Size.X / 4; X < Size.X * 3 / 4; X += 8)
						Samples.Add(Pixels[Y * Size.X + X]);
				if (Captured == 0) FirstSamples = MoveTemp(Samples);
				else
				{
					int32 Changed = 0;
					for (int32 I = 0; I < FMath::Min(Samples.Num(), FirstSamples.Num()); ++I)
						Changed += FMath::Abs(int32(Samples[I].R) - FirstSamples[I].R) > 12;
					Test->TestTrue(TEXT("The visible toys change pose between decoded frames"), Changed > Samples.Num() / 100);
				}
				const FString Folder = FPaths::ProjectSavedDir() / TEXT("UIRefresh/LoadingFilm") / TBText::GetLanguage();
				IFileManager::Get().MakeDirectory(*Folder, true);
				TArray64<uint8> PNG;
				FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
				Test->TestTrue(TEXT("Rendered film frame saved"), FFileHelper::SaveArrayToFile(PNG,
				    *(Folder / FString::Printf(TEXT("%s-Pose%d.png"), STBLoadingScreen::MovieName(Edition), ++Captured))));
			}
			if (Captured != 2 || !bWrapped) return false;
			Test->TestTrue(TEXT("The comic film loops after the last panel"), Player->IsLooping() && Player->GetDuration().GetTotalSeconds() > 4.);
			if (Edition == 0)
			{
				Screen->Restart();
				Screen->HoldDepartureCurtain();
				bCheckDeparture = true;
				bCaptureCurtain = true;
				return false;
			}
			return true;
		}
	private:
		FAutomationTestBase* Test;
		TWeakObjectPtr<UGameViewportClient> Viewport;
		TSharedPtr<STBLoadingScreen> Screen;
		TArray<FColor> FirstSamples;
		FString PreviousLanguage;
		int32 Edition;
		double Deadline = 0., Previous = 0.;
		int32 Captured = 0;
		bool bCaptureCurtain = false;
		bool bCheckDeparture = false;
		bool bCheckReveal = false;
	};

	class FLoadingFilmTravelReview : public IAutomationLatentCommand
	{
	public:
		explicit FLoadingFilmTravelReview(FAutomationTestBase* InTest) : Test(InTest) {}
		~FLoadingFilmTravelReview() override
		{
			if (StartHandle.IsValid()) GetMoviePlayer()->OnMoviePlaybackStarted().Remove(StartHandle);
		}
		bool Update() override
		{
			auto* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
			if (!StartHandle.IsValid())
			{
				if (!Test->TestTrue(TEXT("Native loading movie is enabled in the standalone game"), World && IsMoviePlayerEnabled()))
					return true;
				StartHandle = GetMoviePlayer()->OnMoviePlaybackStarted().AddLambda([this]
				{
					ActiveMovie = GetMoviePlayer()->GetMovieName();
				});
				Destination = World->GetMapName().EndsWith(TEXT("Title")) ? TEXT("TB_Lobby") : TEXT("Title");
				RequestedAt = FPlatformTime::Seconds();
				Deadline = RequestedAt + 120.;
				const FString Path = TEXT("/Game/Maps/") + Destination;
				const auto Travel = FSimpleDelegate::CreateWeakLambda(World, [this, World, Path]()
				{
					const auto* Loading = World->GetGameInstance()->GetSubsystem<UTBLoadingScreenSubsystem>();
					if (Loading && !GIsEditor)
						Test->TestFalse(TEXT("Only the poster is revealed before the native movie starts"),
							Loading->IsViewportMoviePlayingForReview());
					Test->TestTrue(TEXT("Hard travel waits for both curtain passes"), FPlatformTime::Seconds() - RequestedAt >=
						GetDefault<UTBLoadingScreenSettings>()->FadeInSeconds);
					UGameplayStatics::OpenLevel(World, FName(*Path));
				});
				if (!Test->TestTrue(TEXT("A rendered departure waits for the curtain"),
					UTBLoadingScreenSubsystem::DeferForCurtain(World, Path, Travel))) return true;
				return false;
			}
			if (World && World->GetMapName().EndsWith(Destination))
			{
				const auto* Loading = World->GetGameInstance()->GetSubsystem<UTBLoadingScreenSubsystem>();
				if (!Test->TestNotNull(TEXT("Destination owns a loading subsystem"), Loading)) return true;
				if (Loading->IsCoveringViewport())
				{
					if (FPlatformTime::Seconds() < Deadline) return false;
					Test->AddError(TEXT("The film remained over the loaded destination"));
					return true;
				}
				Test->TestEqual(TEXT("Real hard map travel opened the native movie streamer"), ActiveMovie,
				                FString(STBLoadingScreen::MovieName()));
				Test->TestTrue(TEXT("Fast map loads still show the film for the configured minimum"),
				               FPlatformTime::Seconds() - RequestedAt >= GetDefault<UTBLoadingScreenSettings>()->MinimumTravelSeconds);
				if (Destination == TEXT("TB_Lobby"))
				{
					if (auto* State = World->GetGameState<ATBGameState>())
					{
						TGuardValue<bool> SimulateReceivedPending(State->bTravelPending, true);
						State->OnRep_TravelPending();
						Test->TestTrue(TEXT("Replicated departure starts closing over the current lobby"),
							UTBLoadingScreenSubsystem::IsClosingOverDeparture(World));
					}
					else Test->AddError(TEXT("Lobby is missing its replicated game state"));
				}
				return true;
			}
			if (FPlatformTime::Seconds() < Deadline) return false;
			Test->AddError(TEXT("The requested hard map travel did not finish"));
			return true;
		}
	private:
		FAutomationTestBase* Test;
		FDelegateHandle StartHandle;
		FString ActiveMovie, Destination;
		double RequestedAt = 0., Deadline = 0.;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBLoadingFilm, "ToonStory.UI.VisualReview.LoadingFilm",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBLoadingFilm::RunTest(const FString&)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("UIReviewScreenshots")))
	{
		AddInfo(TEXT("Use -UIReviewScreenshots in a rendered game process to exercise video decoding"));
		return true;
	}
	auto* Viewport = GEngine ? GEngine->GameViewport.Get() : nullptr;
	if (!TestNotNull(TEXT("Rendered game viewport"), Viewport)) return false;
	for (int32 Edition = 0; Edition < STBLoadingScreen::EditionCount; ++Edition)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FLoadingFilmReview(this, Viewport, Edition));
	}
	ADD_LATENT_AUTOMATION_COMMAND(FLoadingFilmTravelReview(this));
	return true;
}
#endif
