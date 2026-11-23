// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "TBHUD.h"
#include "UI/TBGameViewportClient.h"
#include "UI/Loading/TBLoadingScreenSubsystem.h"
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
#include "CanvasTypes.h"
#include "Math/RotationMatrix.h"
#include "Math/TranslationMatrix.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

void ATBHUD::NotifyHitBoxClick(FName Name)
{
	if (UTBLoadingScreenSubsystem::IsCovering(GetGameInstance())) return;
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
	if ((bConfirmingLeave || bPlayersOpen) && State->Phase == ETBPhase::Lobby)
	{
		DrawLobby();
	}
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
	}
	else
	{
		DrawMatch(*State, *PC);
		if (PC->IsLeaveMenuOpen()) DrawPause();
	}
}

void ATBHUD::DrawPause()
{
	const float U = FMath::Min(Canvas->ClipX / 1600.f, Canvas->ClipY / 900.f);
	const float X = (Canvas->ClipX - 768 * U) * .5f, Y = (Canvas->ClipY - 493 * U) * .5f;
	const FLinearColor Ink = FLinearColor::FromSRGBColor(FColor(33, 59, 58));
	const FLinearColor Page = FLinearColor::FromSRGBColor(FColor(255, 242, 212));
	const FLinearColor Tape = FLinearColor::FromSRGBColor(FColor(225, 203, 159));
	DrawRect(FLinearColor::FromSRGBColor(FColor(24, 43, 44, 199)), 0, 0, Canvas->ClipX, Canvas->ClipY);
	auto Rectangle = [&](float Left, float Top, float Width, float Height, FLinearColor Color)
	{
		const float L = X + Left * U, T = Y + Top * U;
		const float R = L + Width * U, B = T + Height * U;
		// Keep the coverage fringe in viewport pixels when the paper or tape is tilted.
		DrawSmoothPolygon({{L, T}, {R, T}, {R, B}, {L, B}}, Color);
	};
	auto Frame = [&](float Left, float Top, float Width, float Height, float Border, FLinearColor Edge, FLinearColor Face)
	{
		Rectangle(Left, Top, Width, Height, Edge);
		Rectangle(Left + Border, Top + Border, Width - 2 * Border, Height - 2 * Border, Face);
	};
	auto BeginTilt = [&](float CenterX, float CenterY, float Degrees)
	{
		const FVector Pivot(X + CenterX * U, Y + CenterY * U, 0);
		Canvas->Canvas->PushRelativeTransform(FTranslationMatrix(-Pivot) *
		    FRotationMatrix(FRotator(0, Degrees, 0)) * FTranslationMatrix(Pivot));
	};
	Rectangle(5, 7, 768, 493, TBPaper::Shadow);
	Frame(0, 0, 768, 493, 2, Tape, Page);
	Frame(12, 12, 744, 469, 3, Ink, Page);
	BeginTilt(384, 68, -2);
	Rectangle(238, 44, 300, 70, Tape);
	Frame(234, 39, 300, 70, 2, Ink, FLinearColor::FromSRGBColor(FColor(35, 78, 80)));
	Rectangle(254, 60, 5, 27, TBPaper::Gold);
	Rectangle(265, 60, 5, 27, TBPaper::Gold);
	DrawFittedText(TBText::String(TEXT("Canvas.PauseTitle")), Page, X + 279 * U, Y + 65 * U, 240 * U, 2.25f * U, true, true);
	Canvas->Canvas->PopTransform();
	DrawFittedText(TBText::String(TEXT("Canvas.OnlineContinues")), Ink, X + 42 * U, Y + 131 * U, 684 * U, .95f * U, false, true);

	float MouseX = 0, MouseY = 0;
	const bool HasMouse = GetOwningPlayerController()->GetMousePosition(MouseX, MouseY);
	auto Hovered = [&](float Left, float Top, float Width, float Height)
	{
		return HasMouse && MouseX >= X + Left * U && MouseX < X + (Left + Width) * U &&
		    MouseY >= Y + Top * U && MouseY < Y + (Top + Height) * U;
	};
	auto Hit = [&](FName Id, float Left, float Top, float Width, float Height)
	{
		AddHitBox({X + Left * U, Y + Top * U}, {Width * U, Height * U}, Id, true);
	};
	const FName Actions[] = {TEXT("Unstuck"), TEXT("Resume"), TEXT("Players")};
	const FName Icons[] = {TEXT("PauseReset"), TEXT("PauseResume"), TEXT("PausePlayers")};
	const TCHAR* Captions[] = {TEXT("Canvas.ResetPosition"), TEXT("Canvas.Resume"), TEXT("Canvas.PausePlayers")};
	const float Angles[] = {-2.f, .6f, 2.5f};
	for (int32 I = 0; I < UE_ARRAY_COUNT(Actions); ++I)
	{
		const float Left = 42.f + I * 234.f;
		const bool Hot = Hovered(Left, 172, 216, 219);
		const float Top = Hot ? 168.f : 172.f;
		const FLinearColor Face = FLinearColor::FromSRGBColor(I == 1 ? FColor(162, 216, 200) : FColor(255, 228, 168));
		BeginTilt(Left + 108, Top + 109, Hot ? 0.f : Angles[I]);
		Rectangle(Left + 2, Top + 5, 216, 219, FLinearColor::FromSRGBColor(FColor(154, 130, 87)));
		Frame(Left, Top, 216, 219, 3, Ink, Hot ? TBPaper::Gold : Face);
		BeginTilt(Left + 108, Top, I == 1 ? 4.f : -4.f);
		Rectangle(Left + 73, Top - 11, 70, 21, Tape.CopyWithNewOpacity(.91f));
		Rectangle(Left + 73, Top + 8, 70, 2, TBPaper::Muted.CopyWithNewOpacity(.24f));
		Canvas->Canvas->PopTransform();
		DrawArt(Icons[I], X + (Left + 62) * U, Y + (Top + 52) * U, 92 * U, 80 * U);
		DrawFittedText(TBText::String(Captions[I]), Ink, X + (Left + 8) * U, Y + (Top + 148) * U,
		    200 * U, 1.16f * U, true, true);
		if (I == 1)
		{
			Frame(Left + 164, Top + 192, 40, 18, 1, Ink, Page);
			DrawFittedText(TEXT("Esc"), Ink, X + (Left + 166) * U, Y + (Top + 200) * U, 36 * U, .68f * U, true, true);
		}
		Canvas->Canvas->PopTransform();
		Hit(Actions[I], Left, 172, 216, 219);
	}
	Rectangle(42, 415, 684, 1.5f, Tape);
	auto FooterButton = [&](FName Id, const TCHAR* Caption, float Right)
	{
		const FString Label = TBText::String(Caption);
		float TextW = 0.f, TextH = 0.f;
		GetTextSize(Label, TextW, TextH, HeadingFont, .95f * U);
		const float Width = FMath::Max(75.f, TextW / U + 30.f);
		const float Left = Right - Width;
		Rectangle(Left + 2, 432, Width, 37, TBPaper::Shadow);
		Frame(Left, 429, Width, 37, 2, Ink, Hovered(Left, 429, Width, 37) ? TBPaper::Gold : Page);
		DrawFittedText(Label, Ink, X + (Left + 5) * U, Y + 445 * U, (Width - 10) * U, .95f * U, true, true);
		Hit(Id, Left, 429, Width, 37);
		return Left - 14;
	};
	const float SettingsRight = FooterButton(TEXT("Leave"), TEXT("Stage.Leave"), 726);
	FooterButton(TEXT("OpenOptions"), TEXT("Stage.Options"), SettingsRight);
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
