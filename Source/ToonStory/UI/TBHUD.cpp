// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "TBHUD.h"
#include "UI/TBGameViewportClient.h"
#include "Core/TBLobbySettings.h"
#include "TBPaperStyle.h"
#include "TBUIText.h"
#include "TBOptionsMenu.h"
#include "Audio/TBAudioSubsystem.h"
#include "Character/TBCharacter.h"
#include "Core/TBPlayerState.h"
#include "Core/TBGameState.h"
#include "Core/TBController.h"
#include "Core/TBSession.h"
#include "Core/TBGameHelpers.h"
#include "GamePlay/TBBox.h"
#include "Components/SceneComponent.h"
#include "Engine/Canvas.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

void ATBHUD::NotifyHitBoxClick(FName Name)
{
	Super::NotifyHitBoxClick(Name);
	LastClickedButton = Name;
	LastButtonClickTime = GetWorld()->GetTimeSeconds();
	auto* PC = Cast<ATBController>(GetOwningPlayerController());
	const auto* State = TB::GS(GetWorld());
	if (!PC || !State)
	{
		return;
	}
	// The shared options window owns input until it closes; preserve the stage draft underneath.
	if (Options.IsValid() && Options->IsInViewport()) return;
	if (auto* Audio = PC->GetGameInstance()->GetSubsystem<UTBAudioSubsystem>())
	{
		if (Name != TEXT("Ready") && Name != TEXT("Child") && Name != TEXT("Toy") && Name != TEXT("Either") &&
		    Name != TEXT("MoreChildren") && Name != TEXT("FewerChildren") && Name != TEXT("MoreToys") && Name != TEXT("FewerToys") &&
		    Name != TEXT("LessTime") && Name != TEXT("MoreTime") && Name != TEXT("FewerBatteries") && Name != TEXT("MoreBatteries"))
			Audio->PlaySoundEvent(Name == TEXT("Resume") || Name == TEXT("Leave") || Name == TEXT("CloseSettings") || Name == TEXT("CancelStage") ? ETBSoundEvent::UIBack : ETBSoundEvent::UIClick);
	}
	if (bConfirmingLeave)
	{
		if (Name == TEXT("CancelLeave")) bConfirmingLeave = false;
		else if (Name == TEXT("ConfirmLeave") && !State->bTravelPending)
		{
			bConfirmingLeave = false;
			PC->TBLeave();
		}
		return;
	}
	if (HandleSafetyClick(Name)) return;
	if (Name == TEXT("OpenOptions"))
	{
		Options = UTBOptionsMenu::Open(PC);
		return;
	}
	if (Name == TEXT("Resume"))
	{
		PC->ToggleLeaveMenu();
		return;
	}
	if (Name == TEXT("Leave") && !State->bTravelPending && (State->Phase == ETBPhase::Lobby || PC->IsLeaveMenuOpen()))
	{
		bConfirmingLeave = true;
		return;
	}
	if (Name == TEXT("RetryLobbyReturn") && State->IsLobbyReturnStopped() && PC->HasAuthority())
	{
		PC->TBLobby();
		return;
	}
	if (State->Phase != ETBPhase::Lobby)
	{
		return;
	}
	if (HandleLobbyPanelClick(Name) || TBLobbySettings::HandleClick(PC, Name)) return;
	if (Name == TEXT("Ready"))
	{
		PC->TBReady();
	}
	else if (Name == TEXT("Start"))
	{
		PC->TBStart();
	}
	else if (Name == TEXT("Child"))
	{
		PC->TBPrefer(1);
	}
	else if (Name == TEXT("Toy"))
	{
		PC->TBPrefer(2);
	}
	else if (Name == TEXT("Either"))
	{
		PC->TBPrefer(0);
	}
	else if (Name == TEXT("MoreChildren") || Name == TEXT("FewerChildren"))
	{
		PC->TBRules(State->Settings.Humans + (Name == TEXT("MoreChildren") ? 1 : -1), State->Settings.Toys,
		            State->Settings.Duration, State->Settings.RequiredItems);
	}
	else if (Name == TEXT("MoreToys") || Name == TEXT("FewerToys"))
	{
		PC->TBRules(State->Settings.Humans, State->Settings.Toys + (Name == TEXT("MoreToys") ? 1 : -1),
		            State->Settings.Duration, State->Settings.RequiredItems);
	}
}

bool ATBHUD::DismissConfirmation()
{
	if (bConfirmingLeave)
	{
		bConfirmingLeave = false;
		return true;
	}
	if (bPlayersOpen)
	{
		NotifyHitBoxClick(KickSelection.IsValid() ? TEXT("CancelKick") : TEXT("ClosePlayers"));
		return true;
	}
	return false;
}

bool ATBHUD::IsChatBlocked() const
{
	// 設定の開閉はロビーを離れても残るので、ロビーの間だけ見る。
	const auto* State = TB::GS(GetWorld());
	return bConfirmingLeave || bPlayersOpen || (bLobbySettingsOpen && State && State->Phase == ETBPhase::Lobby) ||
	       (Options.IsValid() && Options->IsInViewport());
}

// HUDは状態の表示だけを行う。勝敗や取得の判定はサーバー側で行う。
void ATBHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || UTBGameViewportClient::IsUIHidden(GetWorld()))
	{
		return;
	}
	const auto* State = TB::GS(GetWorld());
	auto* PC = Cast<ATBController>(GetOwningPlayerController());
	if (!State || !PC)
	{
		return;
	}
	if (State->Phase != ETBPhase::Lobby || State->bTravelPending)
	{
		bLobbySettingsOpen = false;
		bStagePickerOpen = false;
		DraftStage = INDEX_NONE;
		StageDraftNotice = NAME_None;
	}
	if (State->Phase == ETBPhase::Results || State->Phase == ETBPhase::Aborted ||
	    (State->Phase == ETBPhase::Playing && !PC->IsLeaveMenuOpen()))
	{
		bConfirmingLeave = false;
		bPlayersOpen = false;
		KickSelection.Reset();
		if (Options.IsValid() && Options->IsInViewport())
		{
			Options->Dismiss();
		}
	}
	if (State->bTravelPending) bConfirmingLeave = false;
	if (bConfirmingLeave)
	{
		const bool Host = PC->HasAuthority();
		DrawConfirmation(TBText::String(Host ? TEXT("Root.Room.ConfirmClose") : TEXT("Root.Room.ConfirmLeave")),
		    Host ? TBText::String(TEXT("Root.Room.CloseDetail")) : FString(), TEXT("CancelLeave"), TEXT("ConfirmLeave"),
		    TBText::String(Host ? TEXT("Root.Room.Close") : TEXT("Stage.Leave")));
		return;
	}
	if (bPlayersOpen)
	{
		DrawPlayers();
		return;
	}
	if (State->Phase == ETBPhase::Lobby)
	{
		DrawLobby();
		return;
	}
	if (State->Phase == ETBPhase::Results || State->Phase == ETBPhase::Aborted)
	{
		DrawResults(*State);
		DrawResultCover(*State);
	}
	else
	{
		DrawMatch(*State, *PC);
		if (PC->IsLeaveMenuOpen()) DrawPause();
	}
}

void ATBHUD::DrawPause()
{
	const float S = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	const float X = (Canvas->ClipX - 704 * S) * .5f, Y = (Canvas->ClipY - 296 * S) * .5f;
	DrawRect(FLinearColor(0, 0, 0, .54f), 0, 0, Canvas->ClipX, Canvas->ClipY);
	RoundedPanel(TBPaper::Paper, X, Y, 704 * S, 296 * S, 20, 3);
	DrawArt(TEXT("PauseHeader"), X + 128 * S, Y + 24 * S, 92 * S, 76 * S);
	DrawFittedText(TBText::String(TEXT("Canvas.PauseTitle")), TBPaper::Ink, X + 24 * S, Y + 107 * S, 300 * S, 1.65f * S, true, true);
	DrawFittedText(TBText::String(TEXT("Canvas.OnlineContinues")), TBPaper::Muted, X + 22 * S, Y + 153 * S, 304 * S, .88f * S, false, true);
	DrawRect(TBPaper::Outline.CopyWithNewOpacity(.18f), X + 343 * S, Y + 31 * S, 2 * S, 234 * S);
	const FName Ids[] = {TEXT("Resume"), TEXT("Unstuck"), TEXT("Players")};
	const TCHAR* Labels[] = {TEXT("Canvas.Resume"), TEXT("Canvas.ResetPosition"), TEXT("Root.Players.Title")};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Ids); ++Index)
	{
		DrawButton(Ids[Index], TBText::String(Labels[Index]), X + 371 * S,
		    Y + (39 + 76 * Index) * S, 297 * S, 61 * S);
	}
	const FName Tools[] = {TEXT("OpenOptions"), TEXT("Leave")};
	const FName Images[] = {TEXT("Button_Gear"), TEXT("Button_Leave")};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Tools); ++Index)
	{
		const float Left = X + (103 + Index * 80) * S, Top = Y + 203 * S;
		DrawArt(Images[Index], Left, Top, 60 * S, 56 * S);
		AddHitBox(FVector2D(Left, Top), FVector2D(60 * S, 56 * S), Tools[Index], true);
	}
}

// 通常HUDに内部状態だけを追加する。ロビー・結果画面の操作は置き換えない。
void ATBHUD::DrawDebugHUD()
{
	const auto* State = TB::GS(GetWorld());
	if (!State)
	{
		return;
	}
	const float Scale = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	float Y = 155.f * Scale;
	auto Line = [this, Scale, &Y](const FString& Text)
	{
		DrawText(Text, FLinearColor::Yellow, 24.f * Scale, Y, PaperFont, Scale);
		Y += 22.f * Scale;
	};
	for (const APlayerState* Player : State->PlayerArray)
	{
		if (const auto* Info = Cast<ATBPlayerState>(Player))
		{
			Line(FString::Printf(TEXT("%s | Team:%d State:%d Frozen:%d"), *Info->GetPlayerName(), int32(Info->Team),
			                     int32(Info->ToyState), Info->bFrozen));
		}
	}
	const auto* Viewer =
	    GetOwningPlayerController() ? GetOwningPlayerController()->GetPlayerState<ATBPlayerState>() : nullptr;
	if (Viewer && Viewer->Team == ETBTeam::Toy && State->Box)
	{
		Line(FString::Printf(TEXT("Rescue: %.0f%% | Rescuers: %d"), State->Box->RescueProgress * 100,
		                     State->Box->Rescuers));
	}
	// おもちゃ本人にだけ見られ方を出す。人間には出さない。
	if (const auto* Toy = Cast<ATBCharacter>(GetOwningPawn()); Toy && Viewer && Viewer->Team == ETBTeam::Toy)
	{
		Line(FString::Format(TBText::Literal(TEXT("Canvas.DebugGaze")), {Toy->IsGazeFrozen() ? TBText::Literal(TEXT("Canvas.DebugFrozen"))
		                                    : Toy->IsGazed()      ? TBText::Literal(TEXT("Canvas.DebugGazed"))
		                                    : Toy->IsSeen()       ? TBText::Literal(TEXT("Canvas.DebugSeen"))
		                                                          : TBText::Literal(TEXT("Canvas.DebugNone"))}));
	}
}

void ATBHUD::NotifyHitBoxBeginCursorOver(FName BoxName)
{
    Super::NotifyHitBoxBeginCursorOver(BoxName);
    if (auto* PC = GetOwningPlayerController())
        if (auto* Audio = PC->GetGameInstance()->GetSubsystem<UTBAudioSubsystem>()) Audio->PlaySoundEvent(ETBSoundEvent::UIHover);
}
