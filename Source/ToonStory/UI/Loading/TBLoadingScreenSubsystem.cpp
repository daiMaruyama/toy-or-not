// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "TBLoadingScreenSubsystem.h"
#include "STBLoadingScreen.h"
#include "TBLoadingScreenSettings.h"
#include "Character/TBCharacter.h"
#include "Core/TBController.h"
#include "Core/TBGameState.h"
#include "Core/TBLobbySettings.h"
#include "Core/TBPlayerState.h"
#include "Core/TBSession.h"
#include "UI/TBPaperStyle.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameMapsSettings.h"
#include "Misc/PackageName.h"
#include "MoviePlayer.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/SWeakWidget.h"

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
}

bool UTBLoadingScreenSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && GetDefault<UTBLoadingScreenSettings>()->bEnabled;
}

void UTBLoadingScreenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	for (const auto& Path : GetDefault<UTBLoadingScreenSettings>()->Toys)
	{
		if (UTexture2D* Texture = Path.LoadSynchronous())
		{
			Texture->bForceMiplevelsToBeResident = true;
			ToyTextures.Add(Texture);
		}
	}
	PreClientTravelHandle = GetGameInstance()->OnNotifyPreClientTravel().AddUObject(this, &ThisClass::NotePreClientTravel);
	PreLoadHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &ThisClass::PreLoadMap);
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
	FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle);
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	StopMovie();
	RemoveOverlay();
	MovieScreen.Reset();
	Super::Deinitialize();
}

// マップを丸ごと読み直す直前。読み込みの間はゲームの処理が止まるので、MoviePlayer に描かせる。
void UTBLoadingScreenSubsystem::PreLoadMap(const FString& MapName)
{
	const UWorld* World = GetGameInstance()->GetWorld();
	// 起動直後の最初の読み込みと、エディタのプレイには出さない。裏で切り替える移動はかぶせる方で扱う。
	if (!bHadWorld || GIsEditor || !IsMoviePlayerEnabled() || (World && World->IsInSeamlessTravel())) return;
	RemoveOverlay();
	MovieScreen = SNew(STBLoadingScreen);
	// 部屋の操作中ならその状態を、そうでなければ行き先から決める（部屋をつくった直後は操作が終わっている）。
	const auto* Session = GetGameInstance()->GetSubsystem<UTBSession>();
	const bool bToTitle = FPackageName::GetShortName(MapName) == FPackageName::GetShortName(UGameMapsSettings::GetGameDefaultMap());
	MovieScreen->SetStatus(FText::FromString(Session && Session->IsBusy() ? TBPaper::Status(Session->Status)
	                                         : bToTitle                   ? FString(TEXT("タイトルにもどっています…"))
	                                                                      : FString(TEXT("部屋に入っています…"))));
	MovieScreen->Restart();
	FLoadingScreenAttributes Attributes;
	Attributes.WidgetLoadingScreen = MovieScreen;
	Attributes.bAutoCompleteWhenLoadingCompletes = true;
	Attributes.bMoviesAreSkippable = false;
	Attributes.MinimumLoadingScreenDisplayTime = 0.f;
	GetMoviePlayer()->SetupLoadingScreen(Attributes);
	if (!GetMoviePlayer()->IsMovieCurrentlyPlaying()) GetMoviePlayer()->PlayMovie();
	bHardLoad = true;
}

// 読み終わったら、同じ絵をゲーム画面にかぶせ直し、キャラが出るまで待ってから薄れさせる。
void UTBLoadingScreenSubsystem::PostLoadMap(UWorld* World)
{
	bHadWorld = true;
	// マップを丸ごと読み直したら行き先は使い終わり。裏で切り替える移動は、この通知がGameStateの届く前にも来るので、Tickの到着判定に任せる。
	if (!bTravelSeamless) TravelDestMap.Reset();
	if (!bHardLoad) return;
	LoadedAt = FPlatformTime::Seconds();
	ShowOverlay();
}

void UTBLoadingScreenSubsystem::Failed(UWorld*, UNetDriver*, ENetworkFailure::Type, const FString&)
{
	TravelDestMap.Reset();
	StopMovie();
	bHardLoad = false;
}

void UTBLoadingScreenSubsystem::TravelFailed(UWorld*, ETravelFailure::Type, const FString&)
{
	TravelDestMap.Reset();
	StopMovie();
	bHardLoad = false;
}

void UTBLoadingScreenSubsystem::StopMovie()
{
	if (IsMoviePlayerEnabled() && GetMoviePlayer()->IsMovieCurrentlyPlaying()) GetMoviePlayer()->StopMovie();
}

void UTBLoadingScreenSubsystem::ShowOverlay()
{
	UGameViewportClient* Viewport = GetGameInstance()->GetGameViewportClient();
	if (!Viewport) return;
	// 読み込み直後にエンジンが画面の部品を組み直して外すことがあるので、外れていたら付け直す。
	if (Overlay && OverlayViewport.Get() == Viewport && Overlay->IsParentValid()) return;
	const TSharedPtr<STBLoadingScreen> Keep = Screen;
	const int8 KeepLabel = LabelReturning;
	RemoveOverlay();
	Screen = Keep;
	// 付け直しでは、Screenと同じく文字の向きの記録も引き継ぐ。
	if (Keep) LabelReturning = KeepLabel;
	if (!Screen)
	{
		Screen = SNew(STBLoadingScreen);
		if (bHardLoad && MovieScreen) Screen->ContinueFrom(*MovieScreen);
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
	Screen.Reset();
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
	const auto* Settings = GetDefault<UTBLoadingScreenSettings>();
	bool bCover = ShouldCoverSeamless();
	// ロビーと試合の切り替わり。読み込みはほとんどないが、いきなり画面が変わらないよう、少しの間かぶせてフェードでつなぐ。
	UWorld* World = GetGameInstance()->GetWorld();
	// 行き先のマップに着いてGameStateが届いたら、行き先は使い終わり。つなぎのマップでは残す。
	if (!TravelDestMap.IsEmpty() && World && World != TravelFromWorld.Get() && World->GetGameState<ATBGameState>() &&
	    World->GetMapName().EndsWith(TravelDestMap))
		TravelDestMap.Reset();
	if (const auto* State = World ? World->GetGameState<ATBGameState>() : nullptr)
	{
		const uint8 Phase = uint8(State->Phase);
		if (PhaseWorld.Get() == World && Phase != LastPhase &&
		    (Phase == uint8(ETBPhase::Lobby) || LastPhase == uint8(ETBPhase::Lobby)))
		{
			SwitchUntil = FPlatformTime::Seconds() + Settings->MatchSwitchSeconds;
		}
		// ロビーに戻ってきたところか、試合へ向かうところか。試合への移動はロビーのうちに始まる。
		bReturning = State->Phase == ETBPhase::Lobby && !State->bTravelPending;
		PhaseWorld = World;
		LastPhase = Phase;
	}
	if (FPlatformTime::Seconds() < SwitchUntil) bCover = true;
	// 読み終わっても、MoviePlayer はしばらく画面の一番上に出たまま。消えるまでは下のかぶせを薄れさせない。
	if (bHardLoad && IsMoviePlayerEnabled() && GetMoviePlayer()->IsMovieCurrentlyPlaying())
	{
		LoadedAt = FPlatformTime::Seconds();
		bCover = true;
	}
	else if (bHardLoad)
	{
		// キャラのいない画面（タイトル）は待たずに明ける。
		// 待つ上限は、コントローラーがいなくても効かせる。出てこないままだと、画面がずっと覆われてしまう。
		const auto* Controller = GetGameInstance()->GetFirstLocalPlayerController();
		const bool bTimedOut = FPlatformTime::Seconds() - LoadedAt > Settings->PawnWaitSeconds;
		const bool bReady = bTimedOut || (Controller && (Controller->GetPawn() || !Controller->IsA<ATBController>()));
		if (bReady) bHardLoad = false;
		else bCover = true;
	}
	// みんなの見た目が決まり、走っているおもちゃが箱に隠れきるまでは出しておく。
	// 試合中は隠れる時間や入口の演出が進んでいるので、待たずに明ける。
	const auto* Match = World ? World->GetGameState<ATBGameState>() : nullptr;
	const bool bPlaying = Match && Match->Phase == ETBPhase::Playing;
	if (bCover || bPlaying || !Overlay || !Screen || Screen->GetExit() > 0.f) SettleFrom = 0;
	else
	{
		const double Now = FPlatformTime::Seconds();
		if (SettleFrom <= 0) SettleFrom = Now;
		if (Now - SettleFrom < Settings->SettleWaitSeconds && (!AppearancesReady(World) || !Screen->IsToyHidden())) bCover = true;
	}
	if (bCover)
	{
		const bool bNew = !Overlay;
		ShowOverlay();
		if (!Screen) return;
		// 裏で切り替える移動は、ここから出しはじめる。読み直しのあとは、ロード中の絵をそのまま続ける。
		// 向きは移動を始めたときの行き先を優先し、かぶせが出たままでも向きが変わったら書き直す。
		const bool bBack = IsReturningLabel(TravelDestMap, bReturning);
		if (ShouldRewriteLabel(bNew, bHardLoad, LabelReturning, bBack))
		{
			Screen->SetStatus(FText::FromString(bBack ? TEXT("ロビーにもどっています…") : TEXT("あそぶ準備をしています…")));
			LabelReturning = bBack ? 1 : 0;
			if (bNew) Screen->Restart();
		}
		Screen->SetExit(0.f);
		return;
	}
	if (!Overlay || !Screen) return;
	ShowOverlay();
	// 読み込み直後の1フレームは、読み込みにかかった時間がまるごと入るので、一度に進めすぎない。
	const float Step = FMath::Min(DeltaTime, 1.f / 30.f);
	const float Exit = Settings->FadeOutSeconds > 0.f ? Screen->GetExit() + Step / Settings->FadeOutSeconds : 1.f;
	Screen->SetExit(Exit);
	if (Exit >= 1.f) RemoveOverlay();
}
