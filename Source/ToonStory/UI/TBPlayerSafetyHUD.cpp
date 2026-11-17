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
	const float X = (Canvas->ClipX - 740 * S) * .5f, Y = (Canvas->ClipY - 630 * S) * .5f;
	DrawRect(FLinearColor(.025f, .019f, .015f, .76f), 0, 0, Canvas->ClipX, Canvas->ClipY);
	PaperPanel(TBPaper::Paper, X, Y, 740 * S, 630 * S);
	DrawArt(TEXT("Tape_Cream"), X + 325 * S, Y - 16 * S, 90 * S, 34 * S);
	auto Text = [&](const FString& Value, float PX, float PY, float Scale = 1.f, FLinearColor Color = TBPaper::Ink)
	{ DrawFittedText(Value, Color, X + PX * S, Y + PY * S, (704 - PX) * S, Scale * S); };
	auto Button = [&](FName Id, const FString& Label, float PX, float PY, float W, bool Danger = false)
	{ DrawButton(Id, Label, X + PX * S, Y + PY * S, W * S, 44 * S, true, false, Danger); };
	DrawArt(TEXT("Icon_PlayerRatio"), X + 36 * S, Y + 30 * S, 44 * S, 44 * S);
	DrawFittedText(TBText::Literal(TEXT("Root.Players.Title")), TBPaper::Ink, X + 100 * S, Y + 34 * S, 600 * S, 1.5f * S, true);
	if (KickSelection.IsValid() && !State->PlayerArray.Contains(KickSelection.Get())) KickSelection.Reset();
	if (KickSelection.IsValid())
	{
		PaperPanel(TBPaper::Cream, X + 36 * S, Y + 126 * S, 668 * S, 254 * S);
		DrawFittedText(TBText::Literal(TEXT("Root.Players.KickTitle")), TBPaper::Danger, X + 60 * S, Y + 150 * S, 620 * S, 1.3f * S, true);
		DrawFittedText(KickSelection->GetPlayerName(), TBPaper::Ink, X + 60 * S, Y + 209 * S, 620 * S, 1.2f * S);
		Text(TBText::Literal(TEXT("Root.Players.Confirm")), 60, 268, 1.f);
		Text(TBText::Literal(TEXT("Root.Players.Rejoin")), 60, 316, .9f, TBPaper::Muted);
		Button(TEXT("CancelKick"), TBText::Literal(TEXT("Root.Common.Cancel")), 36, 430, 320);
		Button(TEXT("ConfirmKick"), TBText::Literal(TEXT("Root.Players.Kick")), 384, 430, 320, true);
	}
	else
	{
		Text(PC->HasAuthority() ? TBText::Literal(TEXT("Root.Players.HostHint")) : TBText::Literal(TEXT("Root.Players.GuestHint")), 36, 91, .9f, TBPaper::Muted);
		int32 Row = 0;
		for (APlayerState* Player : State->PlayerArray)
		{
			if (!Player || Row >= 8) continue;
			const float PY = 133 + Row++ * 48;
			DrawRect(Row % 2 ? TBPaper::Cream : FLinearColor(.94f, .91f, .84f), X + 36 * S, Y + PY * S, 668 * S, 43 * S);
			DrawArt(Player->IsABot() ? TEXT("Icon_Npc") : TEXT("Icon_PlayerRatio"), X + 48 * S, Y + (PY + 6) * S, 30 * S, 30 * S);
			DrawFittedText(Player->GetPlayerName(), TBPaper::Ink, X + 94 * S, Y + (PY + 14) * S, 430 * S, .95f * S);
			if (Player == PC->PlayerState) Text(TBText::Literal(TEXT("Root.Common.You")), 568, PY + 14, .9f, TBPaper::Teal);
			else if (Player->IsABot()) Text(TEXT("NPC"), 568, PY + 14, .9f, TBPaper::Muted);
			else if (PC->HasAuthority() && !State->bTravelPending)
				Button(FName(*FString::Printf(TEXT("Kick_%d"), Player->GetPlayerId())), TBText::Literal(TEXT("Root.Players.Remove")), 566, PY, 126, true);
		}
		Text(TBText::TranslateNotice(PC->Notice).Left(65), 36, 530, .8f, TBPaper::Muted);
		Button(TEXT("ClosePlayers"), TBText::Literal(TEXT("Root.Common.Back")), 466, 568, 238);
	}
}
