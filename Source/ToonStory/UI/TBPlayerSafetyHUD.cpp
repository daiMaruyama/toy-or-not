// Copyright (c) 2026 Mamodou. All Rights Reserved.
#include "UI/TBUIText.h"
#include "TBHUD.h"
#include "TBPaperStyle.h"
#include "Core/TBController.h"
#include "Core/TBGameState.h"
#include "GameFramework/PlayerState.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"

bool ATBHUD::HandleSafetyClick(FName Name)
{
	auto* PC = Cast<ATBController>(GetOwningPlayerController());
	auto* State = GetWorld()->GetGameState<ATBGameState>();
	if (!PC || !State)
	{
		return false;
	}
	if (Name == TEXT("Players"))
	{
		bPlayersOpen = true;
		KickSelection.Reset();
		return true;
	}
	// Lobby rows open the same confirmation used by the in-match participant list.
	if (!bPlayersOpen && State->Phase == ETBPhase::Lobby && bLobbySettingsOpen && !bStagePickerOpen &&
	    PC->HasAuthority() && !State->bTravelPending)
	{
		for (APlayerState* Player : State->PlayerArray)
		{
			if (Player && !Player->IsABot() && Player != PC->PlayerState &&
			    Name == FName(*FString::Printf(TEXT("Kick_%d"), Player->GetPlayerId())))
			{
				KickSelection = Player;
				bPlayersOpen = true;
				return true;
			}
		}
	}
	if (bPlayersOpen)
	{
		if (Name == TEXT("ClosePlayers"))
		{
			bPlayersOpen = false;
			KickSelection.Reset();
		}
		else if (Name == TEXT("CancelKick"))
		{
			bPlayersOpen = State->Phase != ETBPhase::Lobby;
			KickSelection.Reset();
		}
		else if (Name == TEXT("ConfirmKick"))
		{
			if (PC->HasAuthority() && !State->bTravelPending && KickSelection.IsValid() && State->PlayerArray.Contains(KickSelection.Get()))
			{
				PC->ServerKickPlayer(KickSelection->GetPlayerId());
			}
			bPlayersOpen = State->Phase != ETBPhase::Lobby;
			KickSelection.Reset();
		}
		else if (PC->HasAuthority() && !KickSelection.IsValid())
		{
			for (APlayerState* Player : State->PlayerArray)
			{
				if (Player && !Player->IsABot() && Player != PC->PlayerState &&
				    Name == FName(*FString::Printf(TEXT("Kick_%d"), Player->GetPlayerId())))
				{
					KickSelection = Player;
					break;
				}
			}
		}
		return true;
	}
	if (Name == TEXT("Unstuck"))
	{
		PC->ServerUnstuck();
		return true;
	}
	return false;
}

void ATBHUD::DrawPlayers()
{
	auto* PC = Cast<ATBController>(GetOwningPlayerController());
	auto* State = GetWorld()->GetGameState<ATBGameState>();
	if (!PC || !State)
	{
		return;
	}
	const float S = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	if (KickSelection.IsValid() && !State->PlayerArray.Contains(KickSelection.Get())) KickSelection.Reset();
	if (KickSelection.IsValid())
	{
		DrawConfirmation(FText::Format(TBText::Get(TEXT("Root.Players.ConfirmName")), FText::FromString(KickSelection->GetPlayerName())).ToString(),
		    TBText::String(TEXT("Root.Players.Rejoin")), TEXT("CancelKick"), TEXT("ConfirmKick"), TBText::String(TEXT("Root.Players.Kick")));
		return;
	}
	const float CardHeight = 244.f + 48.f * FMath::Min(State->PlayerArray.Num(), 8);
	const float X = (Canvas->ClipX - 740 * S) * .5f, Y = (Canvas->ClipY - CardHeight * S) * .5f;
	DrawRect(FLinearColor(.025f, .019f, .015f, .76f), 0, 0, Canvas->ClipX, Canvas->ClipY);
	FramePanel(X, Y, 740 * S, CardHeight * S);
	PaperSign(TBPaper::Gold, X + 25 * S, Y + 15 * S, 501 * S, 60 * S);
	auto Text = [&](const FString& Value, float PX, float PY, float Scale = 1.f, FLinearColor Color = TBPaper::Ink)
	{ DrawFittedText(Value, Color, X + PX * S, Y + PY * S, (704 - PX) * S, Scale * S); };
	auto Button = [&](FName Id, const FString& Label, float PX, float PY, float W, bool Danger = false)
	{ DrawButton(Id, Label, X + PX * S, Y + PY * S, W * S, 44 * S, true, false, Danger); };
	DrawArt(TEXT("Icon_PlayerRatio"), X + 44 * S, Y + 26 * S, 42 * S, 38 * S);
	DrawFittedText(TBText::Literal(TEXT("Root.Players.Title")), TBPaper::Ink, X + 98 * S, Y + 34 * S, 401 * S, 1.45f * S, true);
	Text(PC->HasAuthority() ? TBText::Literal(TEXT("Root.Players.HostHint")) : TBText::Literal(TEXT("Root.Players.GuestHint")), 36, 91, .9f, TBPaper::Muted);
	int32 Row = 0;
	for (APlayerState* Player : State->PlayerArray)
	{
		if (!Player || Row >= 8) continue;
		const float PY = 133 + Row++ * 48;
		const bool Local = Player == PC->PlayerState;
		const FLinearColor Keyline = FMath::Lerp(TBPaper::Cream, TBPaper::Outline, Local ? .45f : .20f);
		RoundedPanel(Local ? TBPaper::Paper : TBPaper::Cream, X + 36 * S, Y + PY * S, 668 * S, 43 * S, 9, 1.5f, Keyline);
		DrawArt(Player->IsABot() ? TEXT("Icon_Npc") : TEXT("Icon_PlayerRatio"), X + 48 * S, Y + (PY + 6) * S, 30 * S, 30 * S);
		DrawFittedText(Player->GetPlayerName(), TBPaper::Ink, X + 94 * S, Y + (PY + 14) * S, 430 * S, .95f * S);
		if (Local)
		{
			RoundedPanel(TBPaper::Teal, X + 579 * S, Y + (PY + 7) * S, 109 * S, 29 * S, 8, 1.5f);
			DrawFittedText(TBText::Literal(TEXT("Root.Common.You")), TBPaper::Ink, X + 585 * S,
			    Y + (PY + 15) * S, 97 * S, .82f * S, true, true);
		}
		else if (Player->IsABot()) Text(TEXT("NPC"), 568, PY + 14, .9f, TBPaper::Muted);
		else if (PC->HasAuthority() && !State->bTravelPending)
			Button(FName(*FString::Printf(TEXT("Kick_%d"), Player->GetPlayerId())), TBText::Literal(TEXT("Root.Players.Remove")), 566, PY, 126, true);
	}
	DrawRect(TBPaper::Outline.CopyWithNewOpacity(.22f), X + 36 * S, Y + (CardHeight - 91) * S, 668 * S, 1.5f * S);
	DrawFittedText(TBText::TranslateNotice(PC->Notice).Left(65), TBPaper::Muted, X + 36 * S,
	    Y + (CardHeight - 51) * S, 396 * S, .8f * S);
	Button(TEXT("ClosePlayers"), TBText::Literal(TEXT("Root.Common.Back")), 466, CardHeight - 66, 238);
}

void ATBHUD::DrawConfirmation(const FString& Question, const FString& Detail, FName Cancel, FName Confirm, const FString& Action)
{
	const float S = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	const float Width = 550, Height = Detail.IsEmpty() ? 170 : 205;
	const float X = (Canvas->ClipX - Width * S) * .5f, Y = (Canvas->ClipY - Height * S) * .5f;
	DrawRect(FLinearColor(.025f, .019f, .015f, .62f), 0, 0, Canvas->ClipX, Canvas->ClipY);
	RoundedPanel(TBPaper::Paper, X, Y, Width * S, Height * S, 18, 3);
	DrawFittedText(Question, TBPaper::Ink, X + 24 * S, Y + 36 * S, (Width - 48) * S, 1.45f * S, true, true);
	if (!Detail.IsEmpty())
		DrawFittedText(Detail, TBPaper::Muted, X + 24 * S, Y + 85 * S, (Width - 48) * S, .95f * S, false, true);
	const float ButtonY = Y + (Height - 79) * S;
	DrawButton(Cancel, TBText::String(TEXT("Root.Common.Cancel")), X + 55 * S, ButtonY, 210 * S, 48 * S);
	DrawButton(Confirm, Action, X + 285 * S, ButtonY, 210 * S, 48 * S, true, false, true);
}
