// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "TBLoadingScreenSubsystem.h"
#include "UI/TBUIText.h"
#include "STBLoadingScreen.h"
#include "TBLoadingScreenSettings.h"
#include "Character/TBCharacter.h"
#include "Core/TBController.h"
#include "Core/TBGameState.h"
#include "Core/TBLobbySettings.h"
#include "Core/TBPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Async/Async.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "MoviePlayer.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/SWeakWidget.h"

bool UTBLoadingScreenSubsystem::IsCovering(const UGameInstance* Instance)
{
	const auto* Loading = UGameInstance::GetSubsystem<UTBLoadingScreenSubsystem>(Instance);
	return Loading && Loading->IsCoveringViewport();
}

bool UTBLoadingScreenSubsystem::IsClosingOverDeparture(const UWorld* World)
{
	if (!World) return false;
	const auto* Loading = UGameInstance::GetSubsystem<UTBLoadingScreenSubsystem>(World->GetGameInstance());
	if (!Loading || !Loading->Overlay || Loading->TravelFromWorld.Get() != World) return false;
	const auto* State = World->GetGameState<ATBGameState>();
	if (!State || (State->Phase != ETBPhase::Lobby && State->Phase != ETBPhase::Results && State->Phase != ETBPhase::Aborted))
		return false;
	return FPlatformTime::Seconds() - Loading->TravelShownAt < GetDefault<UTBLoadingScreenSettings>()->FadeInSeconds * .5f;
}

bool UTBLoadingScreenSubsystem::DeferForCurtain(UWorld* World, const FString& URL, FSimpleDelegate Travel)
{
	auto* Loading = World ? UGameInstance::GetSubsystem<UTBLoadingScreenSubsystem>(World->GetGameInstance()) : nullptr;
	if (!Loading || !World->GetGameViewport() || !FApp::CanEverRender() ||
	    GetDefault<UTBLoadingScreenSettings>()->FadeInSeconds <= 0.f) return false;
	Loading->NotePreClientTravel(URL, TRAVEL_Absolute, false);
	if (!Loading->Screen) return false;
	Loading->Screen->Restart();
	Loading->Screen->HoldDepartureCurtain();
	Loading->TravelShownAt = Loading->Screen->GetShownAt();
	Loading->PendingDeparture = MoveTemp(Travel);
	return true;
}

void UTBLoadingScreenSubsystem::SealDeparture(UWorld* World)
{
	if (!World || World->GetGameInstance() != GetGameInstance()) return;
	ShowOverlay();
	if (Screen) Screen->FinishDeparture(GetDefault<UTBLoadingScreenSettings>()->FadeInSeconds);
}

// FURLはマップ名がないとき既定のマップ（タイトル）を返すため使わない。
FString UTBLoadingScreenSubsystem::MapNameFromURL(const FString& URL)
{
	// 「ホスト:ポート」だけで、マップがない行き先は空にする。
	FString Path = URL;
	int32 Index = INDEX_NONE;
	if (Path.FindChar('?', Index)) Path.LeftInline(Index);
	return Path.FindLastChar('/', Index) ? Path.Mid(Index + 1) : FString();
}

bool UTBLoadingScreenSubsystem::IsReturningLabel(const FString& DestMap, bool bReturningFromState)
{
	return DestMap.IsEmpty() ? bReturningFromState : TBLobbySettings::IsLobbyMapName(DestMap);
}

bool UTBLoadingScreenSubsystem::ShouldRewriteLabel(bool bNew, bool bHardLoad, int8 Recorded, bool bBack)
{
	// 読み直しのあと（記録が-1）は、ロード中の絵の文字をそのまま続けるので、新しく作ったときしか書かない。
	return !bHardLoad && (bNew || (Recorded >= 0 && Recorded != int8(bBack)));
}

void UTBLoadingScreenSubsystem::NotePreClientTravel(const FString& PendingURL, ETravelType, bool bSeamless)
{
	// マップ名がない行き先（部屋に入るときのIP:ポートなど）では、古い行き先を残さず空にする。
	TravelDestMap = MapNameFromURL(PendingURL);
	TravelFromWorld = GetGameInstance()->GetWorld();
	bTravelSeamless = bSeamless;
	ExitBeganAt = 0.;
	if (TravelShownAt <= 0.) TravelShownAt = FPlatformTime::Seconds();
	ShowOverlay();
	if (Screen)
	{
		Screen->SetStatus(TBText::Get(TEXT("Root.Loading.Prepare")));
		Screen->SetExit(0.f);
	}
}

bool UTBLoadingScreenSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !CastChecked<UGameInstance>(Outer)->IsDedicatedServerInstance() && GetDefault<UTBLoadingScreenSettings>()->bEnabled;
}

void UTBLoadingScreenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FilmPosters.SetNum(STBLoadingScreen::EditionCount);
	if (FApp::CanEverRender() && FModuleManager::Get().LoadModule(TEXT("ImageWrapper")))
	{
		for (int32 Index = 0; Index < STBLoadingScreen::EditionCount; ++Index)
		{
			const FString Path = FPaths::ProjectContentDir() / TEXT("Movies") /
				(FString(STBLoadingScreen::MovieName(Index)) + TEXT("Poster.jpg"));
			PendingPosters.Add(Async(EAsyncExecution::ThreadPool, [Path]() -> TSharedPtr<FImage>
			{
				TSharedPtr<FImage> Image = MakeShared<FImage>();
				return FImageUtils::LoadImage(*Path, *Image) ? Image : nullptr;
			}));
		}
	}
	PreClientTravelHandle = GetGameInstance()->OnNotifyPreClientTravel().AddUObject(this, &ThisClass::NotePreClientTravel);
	PreLoadHandle = FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this, &ThisClass::PreLoadMap);
	FWorldDelegates::OnSeamlessTravelTransition.AddUObject(this, &ThisClass::SealDeparture);
	PostLoadHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::PostLoadMap);
	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::Failed);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &ThisClass::TravelFailed);
	}
}

void UTBLoadingScreenSubsystem::Deinitialize()
{
	GetGameInstance()->OnNotifyPreClientTravel().Remove(PreClientTravelHandle);
	FCoreUObjectDelegates::PreLoadMapWithContext.Remove(PreLoadHandle);
	FWorldDelegates::OnSeamlessTravelTransition.RemoveAll(this);
	PendingDeparture.Unbind();
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle);
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	StopMovie();
	RemoveOverlay();
	Screen.Reset();
	MovieScreen.Reset();
	for (auto& Future : PendingPosters)
		if (Future.IsValid()) Future.Wait();
	Super::Deinitialize();
}

void UTBLoadingScreenSubsystem::FinishPostersIfReady()
{
	for (int32 Index = 0; Index < PendingPosters.Num(); ++Index)
	{
		auto& Future = PendingPosters[Index];
		if (!Future.IsValid() || !Future.IsReady()) continue;
		if (const auto Image = Future.Consume())
		{
			FilmPosters[Index] = FImageUtils::CreateTexture2DFromImage(*Image);
			if (Index == FilmEdition && Screen) Screen->SetViewportPoster(FilmPosters[Index]);
		}
	}
}

void UTBLoadingScreenSubsystem::SelectFilmForTravel()
{
	if (bFilmSelected) return;
	FilmEdition = (FilmEdition + 1) % STBLoadingScreen::EditionCount;
	bFilmSelected = true;
	Screen.Reset();
}

// マップを丸ごと読み直す直前。読み込みの間はゲームの処理が止まるので、MoviePlayer に描かせる。
void UTBLoadingScreenSubsystem::PreLoadMap(const FWorldContext& Context, const FString& MapName)
{
	const UWorld* World = GetGameInstance()->GetWorld();
	if (!bHadWorld || Context.OwningGameInstance != GetGameInstance() || (World && World->IsInSeamlessTravel())) return;
	SealDeparture(GetGameInstance()->GetWorld());
	FinishPostersIfReady();
	SelectFilmForTravel();
	const FText Status = TBText::Get(TEXT("Root.Loading.Prepare"));
	bHardLoad = true;
	LoadedAt = FPlatformTime::Seconds();
	if (TravelShownAt <= 0.) TravelShownAt = LoadedAt;
	// PIE viewports share one editor process. Keep each transition in the owning viewport.
	if (GIsEditor || !IsMoviePlayerEnabled())
	{
		MovieScreen.Reset();
		ShowOverlay();
		if (Screen) Screen->SetStatus(Status);
		return;
	}
	MovieScreen = SNew(STBLoadingScreen).Backdrop(ETBLoadingBackdrop::NativeMovie).Edition(FilmEdition).Poster(FilmPosters[FilmEdition]);
	if (Screen) MovieScreen->ContinueFrom(*Screen);
	MovieScreen->FinishDeparture(GetDefault<UTBLoadingScreenSettings>()->FadeInSeconds);
	MovieScreen->SetStatus(Status);
	FLoadingScreenAttributes Attributes;
	Attributes.WidgetLoadingScreen = MovieScreen;
	Attributes.MoviePaths.Add(STBLoadingScreen::MovieName(FilmEdition));
	Attributes.PlaybackType = MT_LoadingLoop;
	Attributes.bAutoCompleteWhenLoadingCompletes = true;
	Attributes.bMoviesAreSkippable = false;
	Attributes.MinimumLoadingScreenDisplayTime = 0.f;
	GetMoviePlayer()->SetupLoadingScreen(Attributes);
	if (!GetMoviePlayer()->PlayMovie())
	{
		// A registered streamer can still refuse the file. A widget-only loading screen remains valid.
		MovieScreen->ShowNativePoster();
		Attributes.MoviePaths.Reset();
		GetMoviePlayer()->SetupLoadingScreen(Attributes);
		GetMoviePlayer()->PlayMovie();
	}
	else if (GetMoviePlayer()->GetMovieName().IsEmpty())
	{
		MovieScreen->ShowNativePoster();
	}
}

// 読み終わったら、同じ絵をゲーム画面にかぶせ直し、キャラが出るまで待ってから薄れさせる。
void UTBLoadingScreenSubsystem::PostLoadMap(UWorld* World)
{
	if (!World || World->GetGameInstance() != GetGameInstance()) return;
	bHadWorld = true;
	// マップを丸ごと読み直したら行き先は使い終わり。裏で切り替える移動は、この通知がGameStateの届く前にも来るので、Tickの到着判定に任せる。
	if (!bTravelSeamless) TravelDestMap.Reset();
	if (!bHardLoad) return;
	LoadedAt = FPlatformTime::Seconds();
	ShowOverlay();
}

void UTBLoadingScreenSubsystem::Failed(UWorld* World, UNetDriver*, ENetworkFailure::Type, const FString&)
{
	EndFailedTravel(World);
}

void UTBLoadingScreenSubsystem::TravelFailed(UWorld* World, ETravelFailure::Type, const FString&)
{
	EndFailedTravel(World);
}

void UTBLoadingScreenSubsystem::EndFailedTravel(UWorld* World)
{
	// A failed map handoff can report a null world after this context has lost its old one.
	if (World ? World->GetGameInstance() != GetGameInstance() : GetGameInstance()->GetWorld() != nullptr) return;
	TravelDestMap.Reset();
	StopMovie();
	PendingDeparture.Unbind();
	if (Screen) Screen->FinishDeparture(GetDefault<UTBLoadingScreenSettings>()->FadeInSeconds);
	bFilmSelected = false;
	bHardLoad = false;
	bTravelSeamless = false;
	TravelShownAt = 0.;
	ExitBeganAt = 0.;
	SwitchUntil = 0.;
	RemoveOverlay();
}

void UTBLoadingScreenSubsystem::StopMovie()
{
	if (MovieScreen && IsMoviePlayerEnabled() && GetMoviePlayer()->IsMovieCurrentlyPlaying()) GetMoviePlayer()->StopMovie();
}

void UTBLoadingScreenSubsystem::ShowOverlay()
{
	UGameViewportClient* Viewport = GetGameInstance()->GetGameViewportClient();
	if (!Viewport) return;
	// 読み込み直後にエンジンが画面の部品を組み直して外すことがあるので、外れていたら付け直す。
	if (Overlay && OverlayViewport.Get() == Viewport && Overlay->IsParentValid()) return;
	const bool bNew = !Overlay;
	if (Overlay && OverlayViewport.IsValid()) OverlayViewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
	SelectFilmForTravel();
	if (!Screen)
		Screen = SNew(STBLoadingScreen).Backdrop(ETBLoadingBackdrop::ViewportMovie).Edition(FilmEdition).Poster(FilmPosters[FilmEdition]);
	if (bNew)
	{
		Screen->Restart();
		if (bHardLoad && MovieScreen) Screen->ContinueFrom(*MovieScreen);
		if (bHardLoad) Screen->FinishDeparture(GetDefault<UTBLoadingScreenSettings>()->FadeInSeconds);
		Screen->SetMovieVisible(true, bHardLoad && MovieScreen ? MovieScreen->GetShownAt() : 0.);
	}
	Overlay = SNew(SWeakWidget).PossiblyNullContent(Screen);
	// チャットやメニューより手前、確認用の表示より奥。
	Viewport->AddViewportWidgetContent(Overlay.ToSharedRef(), 900);
	OverlayViewport = Viewport;
}

void UTBLoadingScreenSubsystem::RemoveOverlay()
{
	if (Overlay && OverlayViewport.IsValid()) OverlayViewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
	Overlay.Reset();
	OverlayViewport.Reset();
	// Keep this game's decoder and render resource alive across world teardown, and pause while hidden.
	if (Screen) Screen->SetMovieVisible(false);
	LabelReturning = -1;
}

// コントローラーが画面を黒くしている間（試合への移動・配置待ち・ロビーへの戻り）。
// 人間がおもちゃの隠れるのを待つカウントダウンは試合の一部なので、かぶせずにそのまま見せる。
bool UTBLoadingScreenSubsystem::ShouldCoverSeamless() const
{
	const auto* Controller = Cast<ATBController>(GetGameInstance()->GetFirstLocalPlayerController());
	if (!Controller || !Controller->ShouldHidePreparationView() || Controller->IsShowingEntranceApproach()) return false;
	const auto* State = Controller->GetWorld() ? Controller->GetWorld()->GetGameState<ATBGameState>() : nullptr;
	const auto* Info = Controller->GetPlayerState<ATBPlayerState>();
	const bool bCountdown = State && Info && State->Phase == ETBPhase::Playing && Info->Team == ETBTeam::Human &&
	                        State->HidePreparation.bValid && State->HidePreparation.bHiding;
	return !bCountdown;
}

// 通信では体が先に届き、チームは少し遅れて届く。届くまでは見た目が決まらない。
bool UTBLoadingScreenSubsystem::AppearancesReady(UWorld* World)
{
	if (!World) return true;
	for (TActorIterator<ATBCharacter> It(World); It; ++It)
		if (!It->HasAppearance()) return false;
	return true;
}

void UTBLoadingScreenSubsystem::Tick(float DeltaTime)
{
	FinishPostersIfReady();
	const auto* Settings = GetDefault<UTBLoadingScreenSettings>();
	UWorld* World = GetGameInstance()->GetWorld();
	const double Now = FPlatformTime::Seconds();
	if (PendingDeparture.IsBound())
	{
		ShowOverlay();
		if (Screen && Screen->IsHoldingDeparture())
		{
			if (!Screen->HasPaintedClosed() && Now - TravelShownAt < Settings->FadeInSeconds * .5 + .25) return;
			Screen->FinishDeparture(Settings->FadeInSeconds);
		}
		// Reveal the film in the old viewport before a blocking load can interrupt the motion.
		// An invisible window may stop painting, but must still be able to join or leave a room.
		if (Screen && Screen->GetFilmReveal() < 1.f &&
		    Now - FMath::Max(TravelShownAt, Screen->GetLastRevealPaint()) < Settings->FadeInSeconds + 1.) return;
		FSimpleDelegate Travel = MoveTemp(PendingDeparture);
		if (TravelFromWorld.Get() == World) Travel.ExecuteIfBound();
		return;
	}
	// The result-to-lobby trip can have no preparation mask at all. Cover the engine's
	// complete seamless journey, even when a local controller missed the destination URL.
	const bool bSeamless = World && World->IsInSeamlessTravel();
	if (bSeamless && TravelShownAt == 0.) TravelShownAt = Now;
	bool bCover = bSeamless || ShouldCoverSeamless();
	// 行き先のマップに着いてGameStateが届いたら、行き先は使い終わり。つなぎのマップでは残す。
	if (!TravelDestMap.IsEmpty() && World && World != TravelFromWorld.Get() && World->GetGameState<ATBGameState>() &&
	    World->GetMapName().EndsWith(TravelDestMap))
		TravelDestMap.Reset();
	if (const auto* State = World ? World->GetGameState<ATBGameState>() : nullptr)
	{
		const uint8 Phase = uint8(State->Phase);
		if (TravelShownAt <= 0. && PhaseWorld.Get() == World && Phase != LastPhase &&
		    (Phase == uint8(ETBPhase::Lobby) || LastPhase == uint8(ETBPhase::Lobby)))
		{
			SwitchUntil = FPlatformTime::Seconds() + Settings->MatchSwitchSeconds;
		}
		// ロビーに戻ってきたところか、試合へ向かうところか。試合への移動はロビーのうちに始まる。
		bReturning = State->Phase == ETBPhase::Lobby && !State->bTravelPending;
		PhaseWorld = World;
		LastPhase = Phase;
	}
	const auto* Match = World ? World->GetGameState<ATBGameState>() : nullptr;
	const bool bPlaying = Match && Match->Phase == ETBPhase::Playing;
	if (Now < SwitchUntil) bCover = true;
	// 読み終わっても、MoviePlayer はしばらく画面の一番上に出たまま。消えるまでは下のかぶせを薄れさせない。
	if (bHardLoad && MovieScreen && IsMoviePlayerEnabled() && GetMoviePlayer()->IsMovieCurrentlyPlaying())
	{
		LoadedAt = FPlatformTime::Seconds();
		bCover = true;
	}
	else if (bHardLoad)
	{
		if (MovieScreen && Screen) Screen->ContinueFilmReveal(*MovieScreen);
		// キャラのいない画面（タイトル）は待たずに明ける。
		// 待つ上限は、コントローラーがいなくても効かせる。出てこないままだと、画面がずっと覆われてしまう。
		const auto* Controller = GetGameInstance()->GetFirstLocalPlayerController();
		const bool bTimedOut = FPlatformTime::Seconds() - LoadedAt > Settings->PawnWaitSeconds;
		const bool bReady = bTimedOut || (Controller && (Controller->GetPawn() || !Controller->IsA<ATBController>()));
		if (bReady) bHardLoad = false;
		else bCover = true;
	}
	// みんなの見た目が決まれば、動画の再生位置にかかわらずゲームに戻る。
	// 試合中は隠れる時間や入口の演出が進んでいるので、待たずに明ける。
	if (bCover || bPlaying || !Overlay || !Screen || Screen->GetExit() > 0.f) SettleFrom = 0;
	else
	{
		if (SettleFrom <= 0) SettleFrom = Now;
		if (Now - SettleFrom < Settings->SettleWaitSeconds && !AppearancesReady(World)) bCover = true;
	}
	// A fast GameState update must not cut the film. The server reserves the stage's
	// presentation window before starting the hiding clock, including for a slow guest.
	const bool bMinimum = TravelShownAt > 0. && Now - TravelShownAt < Settings->MinimumTravelSeconds;
	if (bCover || bMinimum)
	{
		ExitBeganAt = 0.;
		const bool bNew = !Overlay;
		ShowOverlay();
		if (!Screen) return;
		// 裏で切り替える移動は、ここから出しはじめる。読み直しのあとは、ロード中の絵をそのまま続ける。
		// 向きは移動を始めたときの行き先を優先し、かぶせが出たままでも向きが変わったら書き直す。
		const bool bBack = IsReturningLabel(TravelDestMap, bReturning);
		if (ShouldRewriteLabel(bNew, bHardLoad, LabelReturning, bBack))
		{
			Screen->SetStatus(TBText::Get(TEXT("Root.Loading.Prepare")));
			LabelReturning = bBack ? 1 : 0;
			// A same-world phase switch can first be observed after its camera changed.
			if (bNew && TravelShownAt <= 0.) Screen->FinishDeparture(Settings->FadeInSeconds);
		}
		Screen->SetExit(0.f);
		return;
	}
	if (!Overlay || !Screen) return;
	ShowOverlay();
	// Start on the first ready frame. The same wall-time duration is used by the server
	// to reserve the full toy hiding countdown after the last curtain pass.
	if (ExitBeganAt <= 0.) ExitBeganAt = Now;
	const float Exit = Settings->FadeOutSeconds > 0.f ? float(Now - ExitBeganAt) / Settings->FadeOutSeconds : 1.f;
	Screen->SetExit(Exit);
	if (Exit >= 1.f)
	{
		ExitBeganAt = 0.;
		TravelShownAt = 0.;
		bFilmSelected = false;
		RemoveOverlay();
	}
}
