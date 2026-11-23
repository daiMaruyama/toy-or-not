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
	using namespace TBPaper;
	const float S = FMath::Min(Canvas->ClipX / 1440.f, Canvas->ClipY / 810.f);
	if (KickSelection.IsValid() && !State->PlayerArray.Contains(KickSelection.Get())) KickSelection.Reset();
	if (KickSelection.IsValid())
	{
		DrawConfirmation(FText::Format(TBText::Get(TEXT("Root.Players.ConfirmName")), FText::FromString(KickSelection->GetPlayerName())).ToString(),
		    TBText::String(TEXT("Root.Players.Rejoin")), TEXT("CancelKick"), TEXT("ConfirmKick"), TBText::String(TEXT("Root.Players.Kick")));
		return;
	}
	const float Width = 520, Height = 130 + 45 * FMath::Min(State->PlayerArray.Num(), 8);
	const float X = (Canvas->ClipX - Width * S) * .5f, Y = (Canvas->ClipY - Height * S) * .5f;
	DrawRect(PrintInk.CopyWithNewOpacity(.65f), 0, 0, Canvas->ClipX, Canvas->ClipY);
	DrawRoomPanel(X, Y, Width * S, Height * S, S, PrintStock);
	DrawRect(PrintSea, X + 26 * S, Y + 23 * S, 5 * S, 30 * S);
	DrawFittedText(TBText::String(TEXT("Canvas.PausePlayers")), PrintInk, X + 39 * S, Y + 34 * S, 421 * S, 1.34f * S, true);
	int32 Row = 0;
	for (APlayerState* Player : State->PlayerArray)
	{
		if (!Player || Row >= 8) continue;
		const float PY = 64 + Row++ * 45;
		DrawRect(PrintQuiet.CopyWithNewOpacity(.35f), X + 25 * S, Y + PY * S, 470 * S, S);
		DrawFittedText(Player->GetPlayerName(), PrintInk, X + 30 * S, Y + (PY + 20) * S, 338 * S, .96f * S, true);
		if (Player == PC->PlayerState || Player->IsABot())
		{
			DrawRect(PrintAqua, X + 413 * S, Y + (PY + 7) * S, 77 * S, 28 * S);
			DrawFittedText(TBText::String(Player->IsABot() ? TEXT("Lobby.NPC") : TEXT("Root.Common.You")), PrintInk,
			    X + 418 * S, Y + (PY + 19) * S, 67 * S, .77f * S, false, true);
		}
		else if (PC->HasAuthority() && !State->bTravelPending)
			DrawRoomButton(FName(*FString::Printf(TEXT("Kick_%d"), Player->GetPlayerId())), TBText::String(TEXT("Root.Players.Remove")),
			    X + 409 * S, Y + (PY + 7) * S, 80 * S, 30 * S, S, PrintCream);
	}
	DrawRoomButton(TEXT("ClosePlayers"), TBText::String(TEXT("Root.Common.Back")), X + 372 * S,
	    Y + (Height - 55) * S, 121 * S, 37 * S, S, PrintHoney);
}

void ATBHUD::DrawConfirmation(const FString& Question, const FString& Detail, FName Cancel, FName Confirm, const FString& Action)
{
	using namespace TBPaper;
	const float S = FMath::Min(Canvas->ClipX / 1440.f, Canvas->ClipY / 810.f);
	const float W = 494, H = Detail.IsEmpty() ? 170 : 202;
	const float X = (Canvas->ClipX - W * S) * .5f, Y = (Canvas->ClipY - H * S) * .5f;
	DrawRect(PrintInk.CopyWithNewOpacity(.65f), 0, 0, Canvas->ClipX, Canvas->ClipY);
	DrawRoomPanel(X, Y, W * S, H * S, S, PrintStock);
	// Two torn strips bridge the paper edge, like the approval mock.
	if (const auto* Tape = ArtTextures.Find(TEXT("Tape_Cream")); Tape && *Tape)
	{
		for (const auto& Corner : {FVector2D(27, 8), FVector2D(W - 25, H - 8)})
			DrawTexture(Tape->Get(), X + (Corner.X - 43) * S, Y + (Corner.Y - 43) * S,
			    86 * S, 86 * S, 0, 0, 1, 1, FLinearColor::White, BLEND_Translucent, 1, false, 12.f, FVector2D(.5, .5));
	}
	float TW = 0, TH = 0;
	GetTextSize(Question, TW, TH, HeadingFont, 1.48f);
	const float LabelW = FMath::Min(W - 70, TW + 38);
	DrawRoomPanel(X + 34 * S, Y + 30 * S, LabelW * S, 48 * S, S * .72f, PrintHoney);
	DrawFittedText(Question, PrintInk, X + 44 * S, Y + 50 * S, (LabelW - 20) * S, 1.48f * S, true, true);
	if (!Detail.IsEmpty())
		DrawFittedText(Detail, PrintQuiet, X + 36 * S, Y + 109 * S, (W - 72) * S, .86f * S);
	DrawRoomButton(Cancel, TBText::String(TEXT("Root.Common.Back")), X + 214 * S,
	    Y + (H - 68) * S, 101 * S, 38 * S, S, PrintCream);
	DrawRoomButton(Confirm, Action, X + 330 * S, Y + (H - 68) * S, 129 * S, 38 * S, S, PrintHoney);
}
