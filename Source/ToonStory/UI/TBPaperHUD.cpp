// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "TBHUD.h"
#include "TBPaperStyle.h"
#include "TBUIText.h"
#include "Core/TBLobbySettings.h"
#include "Core/TBChat.h"
#include "Core/TBPlayerState.h"
#include "Core/TBGameState.h"
#include "Core/TBController.h"
#include "Input/TBKeyLabels.h"
#include "Core/TBSession.h"
#include "Core/TBGameHelpers.h"
#include "Character/TBCharacter.h"
#include "Data/TBRuleData.h"
#include "Data/TBPresentationData.h"
#include "GamePlay/TBBox.h"
#include "GamePlay/Events/TBMomArrivalZone.h"
#include "GamePlay/TBBoxEscapeCourse.h"
#include "GamePlay/TBRuleMath.h"
#include "Components/SceneComponent.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "GlobalRenderResources.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"

static TAutoConsoleVariable<int32> CVarTBDebugHUD(TEXT("tb.DebugHUD"), 0, TEXT("Show detailed match diagnostics."));

void ATBHUD::BeginStageDraft()
{
	if (const auto* State = TB::GS(GetWorld()))
	{
		StageDraft = State->Settings;
		LastDraftNPCCounts = StageDraft.NPCHumans + StageDraft.NPCToys > 0
		    ? FIntPoint(StageDraft.NPCHumans, StageDraft.NPCToys) : FIntPoint(0, 1);
		DraftStage = State->SelectedStage;
		DraftPlayerCount = StageDraft.Humans + StageDraft.Toys;
		StageDraftNotice = NAME_None;
		NormalizeStageDraft();
	}
}

void ATBHUD::NormalizeStageDraft()
{
	const auto* State = TB::GS(GetWorld());
	if (!State) return;
	int32 Count = 0, HumanPreferences = 0, ToyPreferences = 0;
	for (const APlayerState* Player : State->PlayerArray)
	{
		const auto* Info = Cast<ATBPlayerState>(Player);
		if (!Info || Info->IsABot()) continue;
		++Count;
		HumanPreferences += Info->Preference == ETBTeam::Human ? 1 : 0;
		ToyPreferences += Info->Preference == ETBTeam::Toy ? 1 : 0;
	}
	if (DraftPlayerCount != Count) StageDraftNotice = TEXT("Stage.RosterChanged");
	DraftPlayerCount = Count;
	StageDraft.NPCHumans = FMath::Clamp(StageDraft.NPCHumans, 0, FMath::Max(0, State->PlayerLimit - Count));
	StageDraft.NPCToys = FMath::Clamp(StageDraft.NPCToys, 0,
	    FMath::Max(0, State->PlayerLimit - Count - StageDraft.NPCHumans));
	const FTBSettings Defaults;
	StageDraft.Humans = TBRuleMath::AutoHumanPlayers(Count, HumanPreferences, ToyPreferences,
	    StageDraft.NPCHumans, StageDraft.NPCToys, Defaults.Humans, Defaults.Toys);
	StageDraft.Toys = Count - StageDraft.Humans;
	StageDraft.Duration = FMath::Clamp(StageDraft.Duration, TB::Rules().Match.MinDuration, TB::Rules().Match.MaxDuration);
	StageDraft.RequiredItems = FMath::Clamp(StageDraft.RequiredItems, 1, TBLobbySettings::StageItemCount(DraftStage));
}

bool ATBHUD::HandleLobbyPanelClick(FName Name)
{
	if (Name == TEXT("OpenSettings") || Name == TEXT("ChangeStage"))
	{
		const auto* State = TB::GS(GetWorld());
		if (!State || State->Phase != ETBPhase::Lobby || State->bTravelPending) return true;
		if (!bStagePickerOpen) BeginStageDraft();
		bLobbySettingsOpen = true;
		bStagePickerOpen = true;
		return true;
	}
	if (Name == TEXT("CloseSettings") || Name == TEXT("CancelStage"))
	{
		bLobbySettingsOpen = false;
		bStagePickerOpen = false;
		DraftStage = INDEX_NONE;
		StageDraftNotice = NAME_None;
		return true;
	}
	if (Name == TEXT("Ready") || Name == TEXT("Start"))
	{
		return bStagePickerOpen;
	}
	if (!bLobbySettingsOpen)
	{
		return true;
	}
	if (Name == TEXT("Child") || Name == TEXT("Toy") || Name == TEXT("Either"))
	{
		if (auto* PC = Cast<ATBController>(GetOwningPlayerController()))
			PC->TBPrefer(Name == TEXT("Child") ? 1 : Name == TEXT("Toy") ? 2 : 0);
		return true;
	}
	const int32 Stage = Name == TEXT("StageExpanded") ? TBLobbySettings::DemoStageId
	                    : Name == TEXT("StagePlayroom") ? TBLobbySettings::HouseStageId
	                                                    : INDEX_NONE;
	if (bStagePickerOpen)
	{
		auto* PC = Cast<ATBController>(GetOwningPlayerController());
		auto* State = TB::GS(GetWorld());
		if (!PC || !PC->HasAuthority() || !PC->IsLocalController() || !State ||
		    State->Phase != ETBPhase::Lobby || State->bTravelPending) return true;
		NormalizeStageDraft();
		const auto& Match = TB::Rules().Match;
		if (Name == TEXT("SaveStage"))
		{
			if (!FPackageName::DoesPackageExist(TBLobbySettings::StagePath(DraftStage)))
			{
				StageDraftNotice = TEXT("Stage.SaveFailed");
				return true;
			}
			PC->ServerSelectStage(DraftStage);
			if (State->SelectedStage != DraftStage)
			{
				StageDraftNotice = TEXT("Stage.SaveFailed");
				return true;
			}
			StageDraft.RequiredItems = FMath::Clamp(StageDraft.RequiredItems, 1,
			    FMath::Max(1, TBLobbySettings::AvailableItems(GetWorld())));
			PC->ServerRules(StageDraft);
			bLobbySettingsOpen = false;
			bStagePickerOpen = false;
			DraftStage = INDEX_NONE;
			StageDraftNotice = NAME_None;
			return true;
		}
		if (Stage != INDEX_NONE) DraftStage = Stage;
		else if (Name == TEXT("DraftNextStage"))
			DraftStage = DraftStage == TBLobbySettings::HouseStageId ? TBLobbySettings::DemoStageId : TBLobbySettings::HouseStageId;
		else if (Name == TEXT("DraftLessTime")) StageDraft.Duration -= Match.DurationStep;
		else if (Name == TEXT("DraftMoreTime")) StageDraft.Duration += Match.DurationStep;
		else if (Name == TEXT("DraftFewerItems")) --StageDraft.RequiredItems;
		else if (Name == TEXT("DraftMoreItems")) ++StageDraft.RequiredItems;
		else if (Name == TEXT("ToggleDraftNPCs"))
		{
			if (StageDraft.NPCHumans + StageDraft.NPCToys > 0)
			{
				LastDraftNPCCounts = FIntPoint(StageDraft.NPCHumans, StageDraft.NPCToys);
				StageDraft.NPCHumans = StageDraft.NPCToys = 0;
			}
			else
			{
				StageDraft.NPCHumans = LastDraftNPCCounts.X;
				StageDraft.NPCToys = LastDraftNPCCounts.Y;
			}
		}
		else if (Name == TEXT("DraftMoreNPCHumans") || Name == TEXT("DraftFewerNPCHumans") ||
		         Name == TEXT("DraftMoreNPCToys") || Name == TEXT("DraftFewerNPCToys"))
		{
			if (StageDraft.NPCHumans + StageDraft.NPCToys <= 0) return true;
			if ((Name == TEXT("DraftMoreNPCHumans") || Name == TEXT("DraftMoreNPCToys")) &&
			    DraftPlayerCount + StageDraft.NPCHumans + StageDraft.NPCToys >= State->PlayerLimit) return true;
			LastDraftNPCCounts = FIntPoint(StageDraft.NPCHumans, StageDraft.NPCToys);
			if (Name == TEXT("DraftMoreNPCHumans")) ++StageDraft.NPCHumans;
			else if (Name == TEXT("DraftFewerNPCHumans")) --StageDraft.NPCHumans;
			else if (Name == TEXT("DraftMoreNPCToys")) ++StageDraft.NPCToys;
			else --StageDraft.NPCToys;
		}
		NormalizeStageDraft();
		if (StageDraft.NPCHumans + StageDraft.NPCToys > 0)
			LastDraftNPCCounts = FIntPoint(StageDraft.NPCHumans, StageDraft.NPCToys);
		return true;
	}
	return bStagePickerOpen || Stage != INDEX_NONE || Name == TEXT("StageDemo");
}

void ATBHUD::DrawLobby()
{
	auto* PC = Cast<ATBController>(GetOwningPlayerController());
	auto* State = TB::GS(GetWorld());
	if (!PC || !State)
	{
		return;
	}
	const float S = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	const float X = (Canvas->ClipX - 1200 * S) * .5f;
	const float Y = (Canvas->ClipY - 740 * S) * .5f;
	const FLinearColor Ink = TBPaper::Ink, Muted = TBPaper::Muted, Accent = TBPaper::Accent;
	auto Text =
	    [&](const FString& Value, float PX, float PY, float Scale = 1.f, FLinearColor Color = FLinearColor::White)
	{ DrawText(Value, Color, X + PX * S, Y + PY * S, PaperFont, Scale * S); };
	auto Panel = [&](float PX, float PY, float W, float H, FLinearColor Color)
	{
		if (H > 20)
		{
			PaperPanel(Color, X + PX * S, Y + PY * S, W * S, H * S);
		}
		else
		{
			DrawRect(Color, X + PX * S, Y + PY * S, W * S, H * S);
		}
	};
	auto DrawNotice = [&](float PY, float Width, float AvailableHeight, bool IncludeStatus = false)
	{
		constexpr float FontScale = .85f;
		float TextWidth = 0, TextHeight = 0;
		GetTextSize(TEXT("あAg"), TextWidth, TextHeight, PaperFont, FontScale);
		const float LineHeight = FMath::Max(16.f, TextHeight) + 6.f;
		const int32 MaxLines = FMath::FloorToInt((AvailableHeight - 16.f) / LineHeight);
		if (MaxLines < 1) return;
		const float TextArea = Width - 28.f;
		auto Fits = [&](const FString& Value)
		{
			GetTextSize(Value, TextWidth, TextHeight, PaperFont, FontScale);
			return TextWidth <= TextArea;
		};
		auto Ellipsize = [&](FString Value)
		{
			while (!Value.IsEmpty() && !Fits(Value + TEXT("…"))) Value.LeftChopInline(1);
			return Value + TEXT("…");
		};
		TArray<FString> Lines;
		if (IncludeStatus)
		{
			if (auto* Session = PC->GetGameInstance()->GetSubsystem<UTBSession>())
			{
				FString Status = TBPaper::Status(Session->Status).Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" "));
				// Keep space for the notice even when the session status is long.
				if (!Status.IsEmpty() && (PC->Notice.IsEmpty() || MaxLines > 1))
					Lines.Add(Fits(Status) ? Status : Ellipsize(Status));
			}
		}
		FString Line;
		const FString Message = TBText::TranslateNotice(PC->Notice).Replace(TEXT("\r"), TEXT(""));
		for (int32 Index = 0; Index < Message.Len(); ++Index)
		{
			const TCHAR Character = Message[Index] == TEXT('\t') ? TEXT(' ') : Message[Index];
			const bool NewLine = Character == TEXT('\n');
			if (NewLine || !Fits(Line + FString::Chr(Character)))
			{
				Lines.Add(Line);
				Line.Reset();
				if (Lines.Num() == MaxLines)
				{
					Lines.Last() = Ellipsize(Lines.Last());
					break;
				}
			}
			if (!NewLine) Line.AppendChar(Character);
		}
		if (!Line.IsEmpty() && Lines.Num() < MaxLines) Lines.Add(Line);
		if (Lines.IsEmpty()) return;
		Panel(28, PY, Width, 16.f + Lines.Num() * LineHeight, TBPaper::Cream);
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
			Text(Lines[Index], 42, PY + 8.f + 10.f * FontScale + Index * LineHeight, FontScale, Ink);
	};
	auto Button = [&](FName Id, const FString& Label, float PX, float PY, float W,
	                  bool Enabled = true, bool Selected = false, float Height = 42)
	{
		DrawButton(Id, Label, X + PX * S, Y + PY * S, W * S, Height * S, Enabled, Selected,
		    Id == TEXT("Leave") || Id.ToString().StartsWith(TEXT("Kick_")));
	};
	auto Art = [&](FName Name, float PX, float PY, float W, float H)
	{ DrawArt(Name, X + PX * S, Y + PY * S, W * S, H * S); };
	const auto* Local = PC->GetPlayerState<ATBPlayerState>();
	int32 ReadyCount = 0, PlayerCount = 0;
	for (APlayerState* Player : State->PlayerArray)
	{
		if (const auto* P = Cast<ATBPlayerState>(Player); P && !P->IsABot())
		{
			ReadyCount += P->bReady ? 1 : 0;
			++PlayerCount;
		}
	}
	const bool EveryoneReady = PlayerCount > 0 && ReadyCount == PlayerCount &&
	    PlayerCount == State->Settings.Humans + State->Settings.Toys;
	const bool EnoughPlayers = State->Settings.Humans + State->Settings.NPCHumans >= 1 &&
	    State->Settings.Toys + State->Settings.NPCToys >= 1;
	const bool CanStart = PC->HasAuthority() && EveryoneReady && EnoughPlayers && !State->bTravelPending;
	if (!bLobbySettingsOpen)
	{
		const float Bottom = (Canvas->ClipY - Y) / S - 84;
		FramePanel(X + 28 * S, Y + (Bottom - 42) * S, 1144 * S, 110 * S);
		Art(TEXT("Tape_Cream"), 52, Bottom - 53, 104, 22);
		Art(TEXT("Icon_Check"), 1130, Bottom - 32, 24, 24);
		Text(!EnoughPlayers ? TBText::Literal(TEXT("Canvas.NeedsTeams"))
		     : !EveryoneReady ? TBText::Literal(TEXT("Canvas.WaitReady"))
		     : State->bTravelPending ? TBText::Literal(TEXT("Canvas.TravelStage")) : TBText::Literal(TEXT("Canvas.EveryoneReady")),
		     48, Bottom - 30, .9f, Ink);
		const FString ReadyKey = TB::KeyLabel(PC, TB::Keys::LobbyReady);
		const FString StartKey = TB::KeyLabel(PC, TB::Keys::LobbyStart);
		Button(TEXT("Ready"),
		       Local && Local->bReady ? FString::Format(TBText::Literal(TEXT("Canvas.UnreadyKey")), {*ReadyKey}) : FString::Format(TBText::Literal(TEXT("Canvas.ReadyKey")), {*ReadyKey}),
		       48, Bottom, 240, Local != nullptr && !State->bTravelPending, Local && Local->bReady);
		Text(FString::Format(TBText::Literal(TEXT("Canvas.ReadyCount")), {ReadyCount, PlayerCount}), 308, Bottom + 2, 1.05f, Ink);
		DrawProgress(float(ReadyCount) / FMath::Max(1, PlayerCount), X + 308 * S, Y + (Bottom + 30) * S, 220 * S, 8 * S, TBPaper::Teal);
		Button(TEXT("OpenSettings"), TBText::Literal(TEXT("Canvas.SettingsKey")), 622, Bottom, 216);
		Button(TEXT("Start"),
		       PC->HasAuthority() ? FString::Format(TBText::Literal(TEXT("Canvas.StartKey")), {*StartKey}) : FString(TBText::Literal(TEXT("Canvas.WaitHost"))),
		       858, Bottom, 294, CanStart, CanStart);
		DrawNotice(Bottom - 166, 1144, 110);
		return;
	}
	DrawRect(FLinearColor(.025f, .019f, .015f, .64f), 0, 0, Canvas->ClipX, Canvas->ClipY);
	if (bStagePickerOpen)
	{
		// Guests view the host's current selection instead of retaining an editable snapshot.
		if (DraftStage == INDEX_NONE || !PC->HasAuthority()) BeginStageDraft();
		NormalizeStageDraft();
		const bool Host = PC->HasAuthority() && PC->IsLocalController() && !State->bTravelPending;
		const bool House = DraftStage == TBLobbySettings::HouseStageId;
		const int32 NPCCount = StageDraft.NPCHumans + StageDraft.NPCToys;
		const bool CanAddNPC = DraftPlayerCount + NPCCount < State->PlayerLimit;
		auto Number = [&](const TCHAR* Key, int32 Value)
		{ return FText::Format(TBText::Get(Key), FText::AsNumber(Value)).ToString(); };
		auto Label = [&](const TCHAR* Key, float PX, float PY, float Width, float Size = 1.f)
		{ DrawFittedText(TBText::String(Key), Ink, X + PX * S, Y + PY * S, Width * S, Size * S, true); };
		float MX = 0, MY = 0;
		PC->GetMousePosition(MX, MY);
		auto Hovered = [&](float PX, float PY, float Width, float Height)
		{ return MX >= X + PX * S && MX < X + (PX + Width) * S && MY >= Y + PY * S && MY < Y + (PY + Height) * S; };
		auto FlatButton = [&](FName Id, const FString& Caption, float PX, float PY, float Width, float Height,
		                      bool Enabled, FLinearColor Fill, float Size = .85f, bool Selected = false)
		{
			const bool Over = Enabled && Hovered(PX, PY, Width, Height);
			RoundedPanel(!Enabled ? FMath::Lerp(Fill, TBPaper::Cream, .65f) : Over && !Selected ? TBPaper::Gold : Fill,
			    X + PX * S, Y + PY * S, Width * S, Height * S, 5, Over ? 3 : 2.5f);
			float TW = 0, TH = 0;
			GetTextSize(Caption, TW, TH, HeadingFont, Size * S);
			const float Fit = FMath::Min(1.f, (Width - 16) * S / FMath::Max(1.f, TW));
			DrawFittedText(Caption, !Enabled ? Muted : Selected && (Id == TEXT("Child") || Id == TEXT("Toy")) ? TBPaper::Paper : Ink,
			    X + (PX + 8) * S, Y + PY * S + (Height * S - TH * Fit) * .5f + 10 * Size * S * Fit,
			    (Width - 16) * S, Size * S, true, true);
			if (Selected)
			{
				const float CX = X + (PX + 8) * S, CY = Y + (PY + Height * .5f) * S;
				const FLinearColor CheckColor = Id == TEXT("Either") ? Ink : TBPaper::Paper;
				DrawSmoothPolygon({{CX, CY}, {CX + 2 * S, CY - 2 * S}, {CX + 7 * S, CY + 3 * S},
				    {CX + 5 * S, CY + 5 * S}}, CheckColor);
				DrawSmoothPolygon({{CX + 4 * S, CY + 2 * S}, {CX + 11 * S, CY - 6 * S},
				    {CX + 13 * S, CY - 4 * S}, {CX + 6 * S, CY + 4 * S}}, CheckColor);
			}
			if (Enabled) AddHitBox({X + PX * S, Y + PY * S}, {Width * S, Height * S}, Id, true);
		};
		auto Wrapped = [&](const FString& Copy, float PX, float PY, float Width, float Size, int32 MaxLines, FLinearColor Color)
		{
			// Keep body copy legible in both languages; wrap at spaces when available, or at Japanese glyphs.
			FString Remaining = Copy;
			for (int32 Row = 0; Row < MaxLines && !Remaining.IsEmpty(); ++Row)
			{
				int32 End = 0, BreakAt = INDEX_NONE;
				float TW = 0, TH = 0;
				while (End < Remaining.Len() && Remaining[End] != TEXT('\n'))
				{
					GetTextSize(Remaining.Left(End + 1), TW, TH, PaperFont, Size * S);
					if (TW > Width * S && End > 0) break;
					if (Remaining[End] == TEXT(' ')) BreakAt = End;
					++End;
				}
				if (End < Remaining.Len() && Remaining[End] != TEXT('\n') && BreakAt > 0) End = BreakAt;
				FString Line = Remaining.Left(End);
				Remaining.RightChopInline(End);
				Remaining.TrimStartInline();
				if (Row == MaxLines - 1 && !Remaining.IsEmpty())
				{
					while (!Line.IsEmpty())
					{
						GetTextSize(Line + TEXT("…"), TW, TH, PaperFont, Size * S);
						if (TW <= Width * S) break;
						Line.LeftChopInline(1);
					}
					Line += TEXT("…");
				}
				DrawText(Line, Color, X + PX * S, Y + (PY + Row * 17) * S, PaperFont, Size * S);
			}
		};
		auto Numerals = [&](const FString& Value, float PX, float PY, float Width, float Height, float Size)
		{
			UFont* Face = NumericFont ? NumericFont.Get() : PaperFont.Get();
			float TW = 0, TH = 0;
			GetTextSize(Value, TW, TH, Face, Size * S);
			const float Fit = FMath::Min(1.f, Width * S / FMath::Max(1.f, TW));
			DrawText(Value, Ink, X + PX * S + (Width * S - TW * Fit) * .5f,
			    Y + PY * S + (Height * S - TH * Fit) * .5f + 10 * Size * S * Fit, Face, Size * S * Fit);
		};
		auto Stepper = [&](FName Less, FName More, const FString& Value, float PX, float PY, float Width,
		                   bool CanLess, bool CanMore, float Height = 32.f, float ValueSize = 1.35f)
		{
			FlatButton(Less, TEXT("−"), PX, PY, Height, Height, Host && CanLess, TBPaper::Paper, .95f);
			FlatButton(More, TEXT("+"), PX + Width - Height, PY, Height, Height, Host && CanMore, TBPaper::Paper, .95f);
			Numerals(Value, PX + Height + 4, PY, Width - Height * 2 - 8, Height, ValueSize);
		};

		// One continuous sheet matches the approved mock; all copy and controls stay live.
		RoundedPanel(TBPaper::Shadow, X + 4 * S, Y + 6 * S, 1200 * S, 740 * S, 24, 0, TBPaper::Shadow);
		RoundedPanel(TBPaper::Cream, X, Y, 1200 * S, 740 * S, 24, 4);
		RoundedPanel(TBPaper::Gold, X + 4 * S, Y + 4 * S, 1192 * S, 132 * S, 20, 0);
		DrawRect(TBPaper::Cream, X + 4 * S, Y + 118 * S, 1192 * S, 24 * S);
		DrawRect(TBPaper::Outline, X + 4 * S, Y + 116 * S, 1192 * S, 3 * S);
		Art(TEXT("Icon_Stage"), 32, 28, 70, 70);
		DrawFittedText(TBText::String(TEXT("Stage.Eyebrow")), Muted, X + 124 * S, Y + 34 * S, 650 * S, .72f * S);
		Label(TEXT("Stage.Title"), 122, 66, 650, 1.65f);
		FlatButton(TEXT("OpenOptions"), TEXT(""), 830, 37, 108, 44, true, TBPaper::Teal);
		Art(TEXT("Icon_Settings"), 842, 47, 24, 24);
		Label(TEXT("Stage.Options"), 874, 55, 54, .8f);
		FlatButton(TEXT("Players"), TEXT(""), 956, 31, 212, 58, true, TBPaper::Paper);
		DrawFittedText(TBText::String(TEXT("Stage.Room")), Muted, X + 972 * S, Y + 54 * S, 70 * S, .7f * S, true);
		Numerals(FText::Format(TBText::Get(TEXT("Stage.RoomPlayers")), PlayerCount, State->PlayerLimit).ToString(),
		    1052, 31, 100, 58, 1.4f);

		Label(TEXT("Stage.RulesTitle"), 32, 158, 232, 1.08f);
		DrawSmoothPolygon({{X + 275 * S, Y + 158 * S}, {X + 472 * S, Y + 153 * S},
		    {X + 472 * S, Y + 160 * S}, {X + 275 * S, Y + 165 * S}}, TBPaper::Teal);
		for (const float LineY : {274.f, 344.f, 650.f})
			DrawRect(Muted.CopyWithNewOpacity(.3f), X + 32 * S, Y + LineY * S, (LineY == 650 ? 1136 : 440) * S, 2 * S);
		Art(TEXT("Icon_Hourglass"), 40, 204, 32, 40);
		Label(TEXT("Stage.Time"), 86, 214, 210, .94f);
		DrawFittedText(TBText::String(TEXT("Stage.TimeHelp")), Muted, X + 86 * S, Y + 245 * S, 210 * S, .73f * S);
		const int32 Seconds = FMath::RoundToInt(StageDraft.Duration);
		Stepper(TEXT("DraftLessTime"), TEXT("DraftMoreTime"),
		    Number(Seconds % 60 == 0 ? TEXT("Stage.Minutes") : TEXT("Stage.Seconds"), Seconds % 60 == 0 ? Seconds / 60 : Seconds),
		    316, 210, 156, StageDraft.Duration > TB::Rules().Match.MinDuration, StageDraft.Duration < TB::Rules().Match.MaxDuration);
		Art(TEXT("Icon_BatteryPickup"), 40, 282, 32, 40);
		Label(TEXT("Stage.Batteries"), 86, 291, 210, .94f);
		DrawFittedText(TBText::String(TEXT("Stage.BatteryHelp")), Muted, X + 86 * S, Y + 322 * S, 210 * S, .73f * S);
		Stepper(TEXT("DraftFewerItems"), TEXT("DraftMoreItems"), Number(TEXT("Stage.Items"), StageDraft.RequiredItems),
		    316, 286, 156, StageDraft.RequiredItems > 1, StageDraft.RequiredItems < TBLobbySettings::StageItemCount(DraftStage));

		RoundedPanel(FMath::Lerp(TBPaper::Cream, TBPaper::Gold, .35f), X + 32 * S, Y + 358 * S, 440 * S, 168 * S, 10, 2.5f);
		Art(TEXT("Icon_PlayerRatio"), 46, 370, 28, 28);
		Label(TEXT("Canvas.ChooseTeam"), 84, 382, 260, .8f);
		DrawFittedText(TBText::String(TEXT("Stage.PersonalChoice")), Muted, X + 354 * S, Y + 383 * S, 104 * S, .6f * S, false, true);
		const bool CanChooseTeam = Local && !State->bTravelPending;
		const ETBTeam Preference = Local ? Local->Preference : ETBTeam::None;
		FlatButton(TEXT("Child"), TBText::String(TEXT("Canvas.Human")), 46, 408, 129, 38, CanChooseTeam,
		    Preference == ETBTeam::Human ? TBPaper::HumanBlue : TBPaper::Paper, .8f, Preference == ETBTeam::Human);
		FlatButton(TEXT("Toy"), TBText::String(TEXT("Canvas.Toy")), 187, 408, 129, 38, CanChooseTeam,
		    Preference == ETBTeam::Toy ? Accent : TBPaper::Paper, .8f, Preference == ETBTeam::Toy);
		FlatButton(TEXT("Either"), TBText::String(TEXT("Canvas.Either")), 328, 408, 130, 38, CanChooseTeam,
		    Preference == ETBTeam::None ? TBPaper::Teal : TBPaper::Paper, .8f, Preference == ETBTeam::None);
		DrawFittedText(FText::Format(TBText::Get(TEXT("Stage.PreferenceSelected")),
		    TBText::Get(Preference == ETBTeam::Human ? TEXT("Canvas.Human") : Preference == ETBTeam::Toy ? TEXT("Canvas.Toy") : TEXT("Canvas.Either"))).ToString(),
		    Ink, X + 46 * S, Y + 463 * S, 412 * S, .72f * S);
		Wrapped(TBText::String(TEXT("Stage.TeamHelp")), 46, 486, 412, .65f, 2, Muted);

		Art(TEXT("Icon_Npc"), 40, 536, 38, 42);
		Label(TEXT("Stage.NPCs"), 86, 549, 270, .87f);
		DrawFittedText(Number(TEXT("Stage.NPCTotal"), NPCCount), Muted, X + 86 * S, Y + 575 * S, 264 * S, .72f * S);
		const bool NPCEnabled = NPCCount > 0;
		const bool CanToggleNPC = Host && (NPCEnabled || CanAddNPC);
		const float ToggleX = X + 412 * S, ToggleY = Y + 546 * S;
		DrawFittedText(TBText::String(NPCEnabled ? TEXT("Options.On") : TEXT("Options.Off")), Muted,
		    X + 358 * S, Y + 558 * S, 46 * S, .65f * S, false, true);
		RoundedPanel(NPCEnabled ? TBPaper::Teal : FMath::Lerp(TBPaper::Cream, Muted, .35f), ToggleX, ToggleY, 60 * S, 32 * S, 16, 2.5f);
		RoundedPanel(CanToggleNPC ? TBPaper::Paper : TBPaper::Cream,
		    ToggleX + (NPCEnabled ? 33 : 5) * S, ToggleY + 5 * S, 22 * S, 22 * S, 11, 1);
		if (CanToggleNPC) AddHitBox({ToggleX, ToggleY}, {60 * S, 32 * S}, TEXT("ToggleDraftNPCs"), true);
		if (NPCEnabled)
		{
			for (int32 Team = 0; Team < 2; ++Team)
			{
				const float PX = Team == 0 ? 32.f : 262.f;
				const FLinearColor TeamColor = Team == 0 ? TBPaper::HumanBlue : Accent;
				RoundedPanel(TBPaper::Paper, X + PX * S, Y + 592 * S, 210 * S, 42 * S, 6, 1.5f, Muted.CopyWithNewOpacity(.65f));
				DrawFittedText(TBText::String(Team == 0 ? TEXT("Stage.Human") : TEXT("Stage.Toy")), TeamColor,
				    X + (PX + 10) * S, Y + 608 * S, 88 * S, .71f * S, true);
				Stepper(Team == 0 ? TEXT("DraftFewerNPCHumans") : TEXT("DraftFewerNPCToys"),
				    Team == 0 ? TEXT("DraftMoreNPCHumans") : TEXT("DraftMoreNPCToys"),
				    FString::FromInt(Team == 0 ? StageDraft.NPCHumans : StageDraft.NPCToys), PX + 112, 600, 90,
				    (Team == 0 ? StageDraft.NPCHumans : StageDraft.NPCToys) > 0, CanAddNPC, 26, 1.25f);
			}
		}
		else
		{
			Wrapped(TBText::String(CanAddNPC ? TEXT("Stage.NPCDisabledHint") : TEXT("Stage.NPCFullHint")), 46, 612, 412, .74f, 1, Muted);
		}

		Label(TEXT("Stage.ChooseStage"), 512, 158, 520, 1.08f);
		DrawFittedText(House ? TEXT("01 / 02") : TEXT("02 / 02"), Ink, X + 1080 * S, Y + 158 * S, 88 * S, .95f * S, false, true);
		RoundedPanel(TBPaper::Outline, X + 512 * S, Y + 192 * S, 656 * S, 328 * S, 14, 0);
		if (const auto* Preview = ArtTextures.Find(House ? TEXT("StagePreview_House") : TEXT("StagePreview_Demo")); Preview && *Preview)
		{
			// Cover the preview area, preserving the source aspect ratio and a smooth rounded silhouette.
			const float LX = X + 516 * S, LY = Y + 196 * S, PW = 648 * S, PH = 320 * S, R = 10 * S;
			const FVector2D Center(LX + PW * .5f, LY + PH * .5f);
			const float Aspect = float(Preview->Get()->GetSizeX()) / FMath::Max(1, Preview->Get()->GetSizeY());
			const FVector2D UVSize(FMath::Min(1.f, (PW / PH) / Aspect), FMath::Min(1.f, Aspect / (PW / PH)));
			auto UV = [&](FVector2D P)
			{ return FVector2D(.5, .5) + FVector2D((P.X - Center.X) / PW, (P.Y - Center.Y) / PH) * UVSize; };
			TArray<FVector2D> Edge, Fringe;
			const FVector2D Centers[] = {{LX + PW - R, LY + R}, {LX + PW - R, LY + PH - R},
			    {LX + R, LY + PH - R}, {LX + R, LY + R}};
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				for (int32 Segment = 0; Segment <= 10; ++Segment)
				{
					const float Angle = (-90.f + Corner * 90.f + Segment * 9.f) * PI / 180.f;
					const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
					Edge.Add(Centers[Corner] + Direction * R);
					Fringe.Add(Centers[Corner] + Direction * (R + 1.3f));
				}
				if (Corner == 0 || Corner == 2)
				{
					// Split the side at the gradient start so its alpha stays continuous across the fan.
					Edge.Emplace(Corner == 0 ? LX + PW : LX, Center.Y);
					Fringe.Emplace(Corner == 0 ? LX + PW + 1.3f : LX - 1.3f, Center.Y);
				}
			}
			TArray<FCanvasUVTri> Triangles, Shade;
			auto Triangle = [&](TArray<FCanvasUVTri>& Into, FVector2D A, FVector2D B, FVector2D C,
			                    FLinearColor CA, FLinearColor CB, FLinearColor CC)
			{
				FCanvasUVTri& T = Into.AddDefaulted_GetRef();
				T.V0_Pos = A; T.V1_Pos = B; T.V2_Pos = C;
				T.V0_UV = UV(A); T.V1_UV = UV(B); T.V2_UV = UV(C);
				T.V0_Color = CA; T.V1_Color = CB; T.V2_Color = CC;
			};
			auto ShadeAt = [&](FVector2D P)
			{ return FLinearColor(0, 0, 0, FMath::Max(0., (P.Y - Center.Y) / (PH * .5)) * .68f); };
			for (int32 I = 0; I < Edge.Num(); ++I)
			{
				const int32 N = (I + 1) % Edge.Num();
				Triangle(Triangles, Center, Edge[I], Edge[N], FLinearColor::White, FLinearColor::White, FLinearColor::White);
				Triangle(Triangles, Edge[I], Fringe[I], Fringe[N], FLinearColor::White, FLinearColor(1, 1, 1, 0), FLinearColor(1, 1, 1, 0));
				Triangle(Triangles, Edge[I], Fringe[N], Edge[N], FLinearColor::White, FLinearColor(1, 1, 1, 0), FLinearColor::White);
				Triangle(Shade, Center, Edge[I], Edge[N], FLinearColor::Transparent, ShadeAt(Edge[I]), ShadeAt(Edge[N]));
				Triangle(Shade, Edge[I], Fringe[I], Fringe[N], ShadeAt(Edge[I]), FLinearColor::Transparent, FLinearColor::Transparent);
				Triangle(Shade, Edge[I], Fringe[N], Edge[N], ShadeAt(Edge[I]), FLinearColor::Transparent, ShadeAt(Edge[N]));
			}
			FCanvasTriangleItem Photo(Triangles, Preview->Get()->GetResource());
			Photo.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Photo);
			FCanvasTriangleItem Gradient(Shade, GWhiteTexture);
			Gradient.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Gradient);
		}
		RoundedPanel(TBPaper::Teal, X + 1004 * S, Y + 209 * S, 146 * S, 32 * S, 4, 2);
		DrawFittedText(TBText::String(TEXT("Stage.Preview")), Ink, X + 1013 * S, Y + 220 * S, 128 * S, .65f * S, true, true);
		DrawText(House ? TEXT("01") : TEXT("02"), TBPaper::Gold, X + 538 * S, Y + 452 * S, PaperFont, .9f * S);
		DrawFittedText(TBText::String(House ? TEXT("Stage.House") : TEXT("Stage.Demo")), TBPaper::Paper,
		    X + 538 * S, Y + 483 * S, 600 * S, 1.38f * S, true);
		auto Arrow = [&](FName Id, const TCHAR* Glyph, float PX)
		{
			DrawFittedText(Glyph, Host ? Hovered(PX, 536, 34, 44) ? TBPaper::Teal : Ink : Muted,
			    X + PX * S, Y + 547 * S, 34 * S, 2.f * S, true, true);
			if (Host) AddHitBox({X + PX * S, Y + 536 * S}, {34 * S, 44 * S}, Id, true);
		};
		Arrow(House ? TEXT("StageExpanded") : TEXT("StagePlayroom"), TEXT("‹"), 508);
		Arrow(TEXT("DraftNextStage"), TEXT("›"), 1138);
		RoundedPanel(House ? TBPaper::Teal : TBPaper::Cream, X + 552 * S, Y + 548 * S, 13 * S, 13 * S, 6.5f, 2);
		RoundedPanel(House ? TBPaper::Cream : TBPaper::Teal, X + 574 * S, Y + 548 * S, 13 * S, 13 * S, 6.5f, 2);
		Wrapped(TBText::String(House ? TEXT("Stage.HouseDescription") : TEXT("Stage.DemoDescription")), 612, 550, 510, .74f, 2, Muted);
		RoundedPanel(TBPaper::Teal, X + 780 * S, Y + 600 * S, 9 * S, 9 * S, 4.5f, 1, Muted);
		DrawFittedText(TBText::String(TEXT("Stage.Explore")), Muted, X + 798 * S, Y + 601 * S, 90 * S, .72f * S);
		RoundedPanel(TBPaper::Gold, X + 900 * S, Y + 600 * S, 9 * S, 9 * S, 4.5f, 1, Muted);
		DrawFittedText(TBText::String(TEXT("Stage.HideAndSeek")), Muted, X + 918 * S, Y + 601 * S, 180 * S, .72f * S);

		const bool Dirty = DraftStage != State->SelectedStage || !FMath::IsNearlyEqual(StageDraft.Duration, State->Settings.Duration) ||
		    StageDraft.RequiredItems != State->Settings.RequiredItems || StageDraft.NPCHumans != State->Settings.NPCHumans ||
		    StageDraft.NPCToys != State->Settings.NPCToys;
		const bool HasNotice = !StageDraftNotice.IsNone();
		RoundedPanel(HasNotice ? Accent : TBPaper::Teal, X + 32 * S, Y + 700 * S, 7 * S, 7 * S, 3.5f, 0);
		Wrapped(TBText::String(HasNotice ? *StageDraftNotice.ToString() : !Host ? TEXT("Stage.HostOnly") :
		    Dirty ? TEXT("Stage.UnsavedRules") : TEXT("Stage.SavedRules")), 48, 699, 482, .73f, 2, HasNotice ? Accent : Muted);
		FlatButton(TEXT("Leave"), TBText::String(TEXT("Stage.Leave")), 554, 684, 146, 40, true, TBPaper::Cream, .76f);
		auto Action = [&](FName Id, const TCHAR* Key, float PX, float Width, FLinearColor Color, bool Enabled)
		{
			const bool Over = Enabled && Hovered(PX, 676, Width, 58);
			PaperSign(Enabled ? Color : FMath::Lerp(Color, TBPaper::Cream, .65f),
			    X + PX * S, Y + (676 - (Over ? 2 : 0)) * S, Width * S, 58 * S);
			DrawFittedText(TBText::String(Key), Enabled ? Ink : Muted, X + (PX + 14) * S,
			    Y + (696 - (Over ? 2 : 0)) * S, (Width - 28) * S, 1.08f * S, true, true);
			if (Enabled) AddHitBox({X + PX * S, Y + 676 * S}, {Width * S, 58 * S}, Id, true);
		};
		Action(TEXT("CancelStage"), TEXT("Stage.Cancel"), 744, 180, TBPaper::Teal, true);
		Action(TEXT("SaveStage"), TEXT("Stage.SaveAction"), 946, 222, TBPaper::Gold, Host);
		return;
	}

}

// ママのひと言。おもちゃが勝ったら散らかった部屋を叱り、人間が勝ったら片付けをほめる。絵は共通。
void ATBHUD::DrawMomBubble(const ATBGameState& MatchState, float CenterX, float CenterY, float Width, float Height, float Alpha)
{
	const float S = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	const bool bToys = MatchState.Winner == ETBWinner::Toys;
	const FString Lead = bToys ? TBText::Literal(TEXT("Canvas.MomAngryLead")) : TBText::Literal(TEXT("Canvas.MomHappyLead"));
	const FString Line = bToys ? TBText::Literal(TEXT("Canvas.MomAngry")) : TBText::Literal(TEXT("Canvas.MomHappy"));
	auto Fade = [Alpha](FLinearColor Color) { Color.A *= Alpha; return Color; };
	PaperPanel(Fade(TBPaper::Cream), CenterX - Width * .5f, CenterY - Height * .5f, Width, Height);
	float LeadW = 0, LeadH = 0, LineW = 0, LineH = 0;
	GetTextSize(Lead, LeadW, LeadH, PaperFont, 1.4f * S);
	GetTextSize(Line, LineW, LineH, PaperFont, 1.9f * S);
	const float Top = CenterY - (LeadH + LineH) * .5f;
	DrawText(Lead, Fade(bToys ? TBPaper::Accent : FLinearColor::FromSRGBColor(FColor(81, 134, 120))), CenterX - LeadW * .5f, Top, PaperFont, 1.4f * S);
	DrawText(Line, Fade(TBPaper::Ink), CenterX - LineW * .5f, Top + LeadH, PaperFont, 1.9f * S);
	// 吹き出しの左上に、誰のひと言かを出す。
	const float TagX = CenterX - FMath::Min(Width * .5f - 16 * S, LineW * .5f + 40 * S);
	PaperPanel(Fade(TBPaper::Accent), TagX, Top - 44 * S, 76 * S, 36 * S);
	DrawText(TBText::Literal(TEXT("Canvas.Mom")), Fade(TBPaper::Cream), TagX + 16 * S, Top - 38 * S, PaperFont, .95f * S);
}

// 吹き出しが広がって画面を覆い、覆いきったら薄れて結果の画面が出る。結果の画面より手前に描く。
void ATBHUD::DrawResultCover(const ATBGameState& MatchState)
{
	const auto* PC = Cast<ATBController>(GetOwningPlayerController());
	if (MatchState.Phase != ETBPhase::Results || MatchState.ResultStyle != ETBResultStyle::Mom || !PC) return;
	const float S = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	const auto& Feel = TB::PresentationData().Result;
	const float Elapsed = PC->ResultPresentationElapsed(MatchState);
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	const float Cover = TBRuleMath::ResultMomCover(Elapsed, Feel.DecisiveSeconds, Feel.CoverSeconds, Feel.RevealSeconds);
	if (Cover <= 0.f) return;
	const bool bGrowing = Elapsed < Feel.CoverSeconds;
	const float Grow = bGrowing ? Cover : 1.f;
	const float BubbleW = FMath::Lerp(620 * S, W + 120 * S, FMath::Square(Grow));
	const float BubbleH = FMath::Lerp(170 * S, H + 120 * S, FMath::Square(Grow));
	const float Appear = bGrowing ? FMath::Min(1.f, Cover * 6.f) : Cover;
	DrawMomBubble(MatchState, W * .5f, H * .5f, BubbleW, BubbleH, Appear);
}

void ATBHUD::DrawResults(const ATBGameState& MatchState)
{
	const float S = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	const float X = Canvas->ClipX * .5f - 340 * S;
	const auto& Feel = TB::PresentationData().Result;
	const auto* Viewer = Cast<ATBController>(GetOwningPlayerController());
	const float Elapsed = Viewer ? Viewer->ResultPresentationElapsed(MatchState) : float(MatchState.ResultElapsed());
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	if (MatchState.Phase == ETBPhase::Results && !(Viewer ? Viewer->IsResultShown() : MatchState.IsResultRevealed())) return;
	// 車とふたのときは、結果の画面にママのひと言を少しのあいだ重ねる。
	const float SinceReveal = Elapsed - Feel.CoverSeconds - Feel.RevealSeconds * .5f;
	if (MatchState.Phase == ETBPhase::Results && MatchState.ResultStyle != ETBResultStyle::Mom &&
	    MatchState.ResultStyle != ETBResultStyle::None && SinceReveal >= 0.f && SinceReveal < Feel.MomLineSeconds)
	{
		const float Alpha = FMath::Min(1.f, SinceReveal / .2f) * FMath::Min(1.f, (Feel.MomLineSeconds - SinceReveal) / .3f);
		DrawMomBubble(MatchState, W * .5f, H * .68f, 560 * S, 150 * S, Alpha);
	}
	const bool ShowError = !MatchState.bTravelPending && !MatchState.LobbyReturnError.IsEmpty();
	PaperSign(TBPaper::Gold, X, 24 * S, 680 * S, (ShowError ? 196 : 126) * S);
	DrawArt(TEXT("Tape_Cream"), X + 296 * S, 13 * S, 88 * S, 30 * S);
	DrawArt(TEXT("Icon_Crown"), X + 24 * S, 45 * S, 62 * S, 62 * S);
	DrawFittedText(MatchState.Phase == ETBPhase::Aborted ? TBText::Literal(TEXT("Canvas.Again")) : TBText::Literal(TEXT("Canvas.Results")),
	    TBPaper::Ink, X + 104 * S, 46 * S, 550 * S, .78f * S);
	const auto* ResultPlayer = GetOwningPlayerController()->GetPlayerState<ATBPlayerState>();
	const FString Title = MatchState.Phase == ETBPhase::Aborted ? TBText::Literal(TEXT("Canvas.GameOver")) :
	    MatchState.bVehicleEscapeResult && ResultPlayer && ResultPlayer->Team == ETBTeam::Toy ?
	        (MatchState.EscapedInVehicle(ResultPlayer) ? TBText::Literal(TEXT("Canvas.Escaped")) : TBText::Literal(TEXT("Canvas.NotEscaped"))) :
	    MatchState.Winner == ETBWinner::Toys ? TBText::Literal(TEXT("Canvas.ToysWin")) : TBText::Literal(TEXT("Canvas.HumansWin"));
	DrawFittedText(Title, MatchState.Winner == ETBWinner::Humans ? TBPaper::HumanBlue : TBPaper::Accent,
	    X + 104 * S, 65 * S, 548 * S, 1.8f * S, true);
	const int32 Seconds = FMath::CeilToInt(FMath::Max(0., MatchState.EndTime - MatchState.GetServerWorldTimeSeconds()));
	if (ShowError)
	{
		DrawFittedText(TBText::TranslateNotice(MatchState.LobbyReturnError), TBPaper::Danger, X + 24 * S, 112 * S, 632 * S, .9f * S);
		if (MatchState.IsLobbyReturnStopped() && GetOwningPlayerController()->HasAuthority())
		{
			DrawButton(TEXT("RetryLobbyReturn"), TBText::Literal(TEXT("Canvas.RetryLobby")), X + 24 * S, 155 * S, 632 * S, 44 * S);
		}
		else
		{
			const FString Status = MatchState.IsLobbyReturnStopped() ? TBText::Literal(TEXT("Canvas.WaitHostRetry")) :
			    FString::Format(TBText::Literal(TEXT("Canvas.RetrySeconds")), {Seconds});
			DrawFittedText(Status, TBPaper::Muted, X + 24 * S, 163 * S, 632 * S, S);
		}
		return;
	}
	const FString Status = MatchState.bTravelPending ? TBText::Literal(TEXT("Canvas.TravelLobby")) :
	    FString::Format(TBText::Literal(TEXT("Canvas.LobbySeconds")), {Seconds});
	DrawFittedText(Status, TBPaper::Muted, X + 104 * S, 116 * S, 548 * S, .9f * S);
	auto* PC = Cast<ATBController>(GetOwningPlayerController());
	if (MatchState.Phase != ETBPhase::Results || !PC) return;
	const float Y = 168 * S;
	const auto* Mine = PC->GetPawn<ATBCharacter>();
	if (Mine && Mine->IsCelebrating())
	{
		// 勝った人は自分のキャラを映しているので、エモートを促す。
		const FString HairKey = ResultPlayer && ResultPlayer->Team == ETBTeam::Human
		    ? FString::Format(TBText::Literal(TEXT("Canvas.EmoteExtra")), {*TB::KeyLabel(PC, TB::Keys::EmoteHairDisappear)}) : FString();
		PaperPanel(TBPaper::Cream, X, Y, 680 * S, 78 * S);
		DrawText(MatchState.Winner == ETBWinner::Humans ? TBText::Literal(TEXT("Canvas.TidiedCelebrate")) : TBText::Literal(TEXT("Canvas.Celebrate")),
		         TBPaper::Ink, X + 24 * S, Y + 10 * S, PaperFont, S);
		DrawText(FString::Format(TBText::Literal(TEXT("Canvas.EmoteKeys")), {*TB::KeyLabel(PC, TB::Keys::EmoteWave), *TB::KeyLabel(PC, TB::Keys::EmoteCheer),
		                         *TB::KeyLabel(PC, TB::Keys::EmoteDance), *TB::KeyLabel(PC, TB::Keys::EmoteSurprise), *HairKey}),
		         TBPaper::Muted, X + 24 * S, Y + 42 * S, PaperFont, .8f * S);
		return;
	}
	// 観戦中は、映している勝者の名前と、切り替えられるときだけ切り替えのキーを出す。
	const auto* Watched = PC->GetResultViewCharacter();
	if (!Watched) return;
	const bool bTeamWon = ResultPlayer && ((ResultPlayer->Team == ETBTeam::Toy && MatchState.Winner == ETBWinner::Toys) ||
	                                       (ResultPlayer->Team == ETBTeam::Human && MatchState.Winner == ETBWinner::Humans));
	const int32 Count = PC->ResultViewCandidates().Num();
	PaperPanel(TBPaper::Cream, X, Y, 680 * S, (Count > 1 ? 78 : 48) * S);
	DrawFittedText(FString::Format(TBText::Literal(TEXT("Canvas.Spectating")), {bTeamWon ? TEXT("") : TBText::Literal(TEXT("Canvas.NextTime")), *Watched->TBPS()->GetPlayerName()}),
	         TBPaper::Ink, X + 24 * S, Y + 13 * S, 632 * S, S);
	if (Count > 1)
		DrawText(FString::Format(TBText::Literal(TEXT("Canvas.NextPlayer")), {*TB::KeyLabel(PC, TB::Keys::ResultsNext)}), TBPaper::Muted, X + 24 * S, Y + 42 * S,
		         PaperFont, .8f * S);
}

void ATBHUD::DrawMatch(const ATBGameState& MatchState, ATBController& Controller)
{
	const auto* State = &MatchState;
	auto* PC = &Controller;
	const float S = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	// Keep the playfield open; menus retain their own full-size layout.
	const float HUDScale = .8f * S;
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	const FLinearColor Ink = TBPaper::Ink, Accent = TBPaper::Accent, Panel = TBPaper::Paper;
	const auto* Player = Controller.GetPlayerState<ATBPlayerState>();
	if (Controller.ShouldHidePreparationView())
	{
		if (!Controller.IsShowingEntranceApproach()) DrawRect(FLinearColor::Black, 0, 0, W, H);
		const FString Wait = Player && Player->Team == ETBTeam::Human && MatchState.HidePreparation.bValid && MatchState.HidePreparation.bHiding
		    ? FString::Format(TBText::Literal(TEXT("Canvas.WaitHide")), {FMath::CeilToInt(MatchState.HideRemaining())})
		    : TBText::Literal(TEXT("Canvas.Preparing"));
		if (Controller.IsShowingEntranceApproach())
		{
			PaperPanel(Panel, W * .5f - 300 * S, H - 130 * S, 600 * S, 75 * S);
			DrawText(FString::Format(TBText::Literal(TEXT("Canvas.EntranceSeconds")), {FMath::CeilToInt(MatchState.HideRemaining())}),
			         Ink, W * .5f - 265 * S, H - 100 * S, PaperFont, 1.2f * S);
		}
		else DrawText(Wait, TBPaper::Cream, W * .5f - 220 * S, H * .5f, PaperFont, 1.3f * S);
		return;
	}
	DrawGazeEffects(Controller);
	float BannerY = 128 * S;
	auto Banner = [&](const FString& Label, FLinearColor Color)
	{
		PaperSign(TBPaper::Cream, W * .5f - 290 * S, BannerY, 580 * S, 42 * S);
		DrawFittedText(Label, Color == TBPaper::Teal ? TBPaper::Ink : Color, W * .5f - 270 * S,
		    BannerY + 16 * S, 540 * S, 1.05f * S, true, true);
		BannerY += 52 * S;
	};
	if (MatchState.bEscapePhase)
		Banner(Player && Player->Team == ETBTeam::Human ? TBText::Literal(TEXT("Canvas.StopEscape")) :
		    TBText::Literal(TEXT("Canvas.EscapeNow")), TBPaper::Teal);
	if (MatchState.IsPreparing())
		Banner(FString::Format(TBText::Literal(TEXT("Canvas.HideSeconds")), {FMath::CeilToInt(MatchState.HideRemaining())}), TBPaper::Accent);
	const bool Child = Player && Player->Team == ETBTeam::Human;
	if (const auto* Mom = ATBMomArrivalZone::FindActive(GetWorld()); Mom && !MatchState.IsPreparing())
	{
		const auto& Event = Mom->State;
		const FLinearColor Warning = TBPaper::Danger;
		Banner(FString::Format(TBText::Literal(TEXT("Canvas.MomSeconds")), {FMath::CeilToInt(Event.Remaining)}), Warning);
		if (Child)
		{
			Banner(TBText::Literal(TEXT("Canvas.PersuadeMom")), Warning);
			if (Event.bOccupied)
			{
				// 箱にしまう際と同じパネル・ゲージ寸法を使う。
				FramePanel(W*.5f-235*S,H*.60f,470*S,100*S);
				DrawText(FString::Format(TBText::Literal(TEXT("Canvas.Persuading")), {FString::Printf(TEXT("%.1f"), Event.Held), FString::Printf(TEXT("%.1f"), Event.RequiredHold)}),
				         Ink,W*.5f-220*S,H*.60f+19*S,PaperFont,S);
				DrawProgress(Event.Held/FMath::Max(.1f,Event.RequiredHold), W*.5f-220*S, H*.60f+40*S, 440*S, 40*S, TBPaper::Gold);
				DrawText(TBText::Literal(TEXT("Canvas.PersuadePause")),
				         Ink,W*.5f-220*S,H*.60f+86*S,PaperFont,.7f*S);
			}
		}
	}
	if (Player && Player->Team == ETBTeam::Toy && Player->ToyState == ETBToyState::Boxed)
	{
		if (const auto* Course = ATBBoxEscapeCourse::Find(GetWorld(), MatchState.Box))
		{
			DrawText(TBText::Literal(TEXT("Canvas.ClimbEscape")),
			         Accent, 330 * S, H * .65f, PaperFont, 1.1f * S);
		}
	}
	const FLinearColor TeamColor = Child ? TBPaper::HumanBlue : Accent;
	PaperSign(TBPaper::Cream, 24 * HUDScale, 44 * HUDScale, 394 * HUDScale, 80 * HUDScale);
	PaperSign(TBPaper::Gold, 78 * HUDScale, 25 * HUDScale, 126 * HUDScale, 41 * HUDScale);
	DrawFittedText(TBText::String(TEXT("Canvas.Goal")), Ink, 87 * HUDScale, 39 * HUDScale, 108 * HUDScale, 1.08f * HUDScale, true, true);
	// The flag illustration has no text; crop away the baked goal label.
	if (const auto* Flag = ArtTextures.Find(Child ? TEXT("Goal_Human") : TEXT("Goal_Toy")); Flag && *Flag)
		DrawTexture(Flag->Get(), 15 * HUDScale, 22 * HUDScale, 68 * HUDScale, 108 * HUDScale, 0, 0, .19f, 1);
	DrawFittedText(Child ? TBText::Literal(TEXT("Canvas.Human")) : TBText::Literal(TEXT("Canvas.Toy")), TeamColor,
	    256 * HUDScale, 65 * HUDScale, 141 * HUDScale, .75f * HUDScale, true, true);
	const TCHAR* Objective = Child ? TBText::Literal(TEXT("Canvas.HumanObjective")) : TBText::Literal(TEXT("Canvas.ToyObjective"));
	if (MatchState.IsPreparing() && Player && Player->Team == ETBTeam::Toy)
		Objective = TBText::Literal(TEXT("Canvas.HideObjective"));
	FTBEmoteSample Emote;
	if (const auto* EmoteCharacter = Controller.GetPawn<ATBCharacter>(); EmoteCharacter && EmoteCharacter->GetEmoteSample(Emote))
		Objective = Emote.Kind == ETBEmoteKind::Wave ? TBText::Literal(TEXT("Canvas.Waving")) :
		    Emote.Kind == ETBEmoteKind::Cheer ? TBText::Literal(TEXT("Canvas.Cheering")) :
		    Emote.Kind == ETBEmoteKind::HairDisappear ? TBText::Literal(TEXT("Canvas.HidingHair")) :
		    Emote.Kind == ETBEmoteKind::Surprise ? TBText::Literal(TEXT("Canvas.Surprised")) : TBText::Literal(TEXT("Canvas.Dancing"));
	if (Player && Player->Team == ETBTeam::Toy)
	{
		switch (Player->ToyState)
		{
			case ETBToyState::Grabbed:
				Objective = TBText::Literal(TEXT("Canvas.Caught"));
				break;
			case ETBToyState::Carried:
				Objective = TBText::Literal(TEXT("Canvas.Carried"));
				break;
			case ETBToyState::Storing:
				Objective = TBText::Literal(TEXT("Canvas.Storing"));
				break;
			case ETBToyState::Boxed:
				Objective = ATBBoxEscapeCourse::Find(GetWorld(), MatchState.Box)
				    ? TBText::Literal(TEXT("Canvas.BoxClimb")) : TBText::Literal(TEXT("Canvas.BoxWait"));
				break;
			case ETBToyState::Free:
				break;
		}
	}
	DrawFittedText(Objective, Ink, 85 * HUDScale, 88 * HUDScale, 307 * HUDScale, 1.05f * HUDScale, true, true);
	const int32 Seconds = FMath::Max(0, FMath::CeilToInt(MatchState.Remaining()));
	PaperSign(TBPaper::Gold, W * .5f - 110 * HUDScale, 28 * HUDScale, 220 * HUDScale, 80 * HUDScale);
	DrawArt(TEXT("Icon_Hourglass"), W * .5f - 90 * HUDScale, 41 * HUDScale, 42 * HUDScale, 54 * HUDScale);
	const FString Clock = FString::Printf(TEXT("%02d:%02d"), Seconds / 60, Seconds % 60);
	DrawText(Clock, Seconds < 60 ? TBPaper::Danger : Ink, W * .5f - 34 * HUDScale, 57 * HUDScale, NumericFont, 2.7f * HUDScale);
	FramePanel(W - 264 * HUDScale, 24 * HUDScale, 240 * HUDScale, 174 * HUDScale);
	DrawArt(TEXT("Icon_BatteryPickup"), W - 242 * HUDScale, 39 * HUDScale, 54 * HUDScale, 60 * HUDScale);
	DrawText(FString::Printf(TEXT("%d/%d"), MatchState.Collected, MatchState.Settings.RequiredItems), Ink,
	    W - 160 * HUDScale, 58 * HUDScale, NumericFont, 2.5f * HUDScale);
	DrawRect(TBPaper::Teal, W - 246 * HUDScale, 107 * HUDScale, 204 * HUDScale, 3 * HUDScale);
	DrawArt(MatchState.BoxedCount > 0 ? TEXT("Icon_ToyBoxFull") : TEXT("Icon_ToyBoxEmpty"),
	    W - 245 * HUDScale, 121 * HUDScale, 64 * HUDScale, 55 * HUDScale);
	DrawText(FString::Printf(TEXT("%d/%d"), MatchState.BoxedCount, MatchState.Settings.Toys + MatchState.Settings.NPCToys),
	    Ink, W - 160 * HUDScale, 141 * HUDScale, NumericFont, 2.5f * HUDScale);
	if (Player && Player->bFrozen)
	{
		// 赤で怖がらせず、紙の札で知らせる。止まった間の色はセピアの画面が伝える。
		PaperPanel(TBPaper::Cream, W * .5f - 165 * S, H * .7f, 330 * S, 44 * S);
		DrawText(TBText::Literal(TEXT("Canvas.Frozen")), Ink, W * .5f - 146 * S, H * .7f + 12 * S, PaperFont, 1.15f * S);
	}
	if (auto* Character = Cast<ATBCharacter>(Controller.GetPawn()); Character && Character->ContactItem)
	{
		FramePanel(W * .5f - 164 * S, H * .76f, 328 * S, 76 * S);
		DrawProgress(Character->ItemProgress, W * .5f - 146 * S, H * .76f + 40 * S, 292 * S, 27 * S, TBPaper::Gold);
		DrawText(Character->IsGazeFrozen() ? TBText::Literal(TEXT("Canvas.CannotCollect")) : TBText::Literal(TEXT("Canvas.Collecting")), Ink,
		         W * .5f - 146 * S, H * .76f + 18 * S, PaperFont, .95f * S);
	}
	if (Player && Player->Team == ETBTeam::Toy && MatchState.Box && MatchState.Box->Rescuers > 0)
	{
		PaperPanel(TBPaper::Paper, W * .5f - 150 * S, BannerY, 300 * S, 54 * S);
		DrawFittedText(FString::Format(TBText::Literal(TEXT("Canvas.Rescuing")), {FMath::RoundToInt(MatchState.Box->RescueProgress * 100)}), Ink,
		    W * .5f - 134 * S, BannerY + 14 * S, 268 * S, 1.05f * S, false, true);
		DrawProgress(MatchState.Box->RescueProgress, W * .5f - 130 * S, BannerY + 38 * S, 260 * S, 7 * S, TBPaper::Teal);
	}
	if (auto* Character = Cast<ATBCharacter>(Controller.GetPawn());
	    Child && Character && Character->HasCarriedToy() && MatchState.Box)
	{
		const bool NearBox = MatchState.Box->CanStoreFrom(Character);
		FVector2D BoxScreen;
		const FVector BoxLocation = MatchState.Box->InteractionPoint->GetComponentLocation();
		if (Controller.ProjectWorldLocationToScreen(BoxLocation + FVector(0, 0, 90), BoxScreen) &&
		    BoxScreen.X > 70 * S && BoxScreen.X < W - 70 * S && BoxScreen.Y > 150 * S && BoxScreen.Y < H * .7f)
		{
			DrawText(FString::Format(TBText::Literal(TEXT("Canvas.BoxDistance")), {FString::Printf(TEXT("%.1f"), FVector::Distance(Character->GetActorLocation(), BoxLocation) / 100.f)}),
			         Accent, BoxScreen.X - 60 * S, BoxScreen.Y, PaperFont, 1.f * S);
		}
		const bool Storing = Character->StoreStarted >= 0;
		const float Duration = FMath::Max(TB::Rules().Box.StoreSeconds, .01f);
		const float Elapsed =
		    Storing
		        ? FMath::Clamp(float(MatchState.GetServerWorldTimeSeconds() - Character->StoreStarted), 0.f, Duration)
		        : 0.f;
		const FString InteractKey = TB::KeyLabel(PC, TB::Keys::HumanInteract);
		const FString Prompt = Storing
		                           ? FString::Format(TBText::Literal(TEXT("Canvas.StoreHeld")), {*InteractKey, FString::Printf(TEXT("%.1f"), Elapsed), FMath::RoundToInt(Duration)})
		                           : (NearBox ? FString::Format(TBText::Literal(TEXT("Canvas.StorePrompt")), {*InteractKey, FMath::RoundToInt(Duration)})
		                                      : TBText::Literal(TEXT("Canvas.CarryPrompt")));
		FramePanel(W * .5f - 235 * S, H * .73f, 470 * S, 100 * S);
		DrawFittedText(Prompt, Ink, W * .5f - 214 * S, H * .73f + 19 * S, 428 * S, S, true, true);
		DrawProgress(Elapsed / Duration, W * .5f - 220 * S, H * .73f + 40 * S, 440 * S, 40 * S, TBPaper::Gold);
		DrawText(FString::Format(TBText::Literal(TEXT("Canvas.StoreCancel")), {*InteractKey, *TB::KeyLabel(PC, TB::Keys::HumanDrop)}), Ink, W * .5f - 220 * S, H * .73f + 86 * S, PaperFont, .75f * S);
	}
	if (Controller.bStuckHint)
	{
		DrawFittedText(TBText::Literal(TEXT("Canvas.StuckHelp")), TBPaper::Accent, 330 * S, H * .65f, W - 360 * S, S);
	}
	if (!Controller.Notice.IsEmpty())
	{
		PaperPanel(TBPaper::Cream, W - 450 * S, H - 155 * S, 426 * S, 70 * S);
		DrawFittedText(TBText::TranslateNotice(Controller.Notice).Left(80), TBPaper::Ink, W - 428 * S, H - 132 * S, 382 * S, .85f * S);
	}
	DrawRect(Ink, W * .5f - 3 * S, H * .5f - 1 * S, 6 * S, 2 * S);
	DrawRect(Ink, W * .5f - 1 * S, H * .5f - 3 * S, 2 * S, 6 * S);
	if (CVarTBDebugHUD.GetValueOnGameThread() != 0)
	{
		DrawDebugHUD();
	}
}

void ATBHUD::BeginPlay()
{
	Super::BeginPlay();
	PaperFont = TBPaper::Font(this);
	HeadingFont = TBPaper::Font(this, TBPaper::EFontRole::Heading);
	NumericFont = TBPaper::Font(this, TBPaper::EFontRole::Numeric);
	PaperTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/PaperClub/Panel.Panel"));
	const TCHAR* Names[] = {TEXT("Icon_Stage"), TEXT("Icon_Settings"), TEXT("Icon_PlayerRatio"),
	    TEXT("Icon_Npc"), TEXT("Icon_Hourglass"), TEXT("Icon_Crown"), TEXT("Icon_Check"),
	    TEXT("Icon_Battery"), TEXT("Icon_BatteryPickup"), TEXT("Icon_ToyBoxEmpty"), TEXT("Icon_ToyBoxFull"),
	    TEXT("Tape_Cream"), TEXT("Tag_Toy"), TEXT("Tag_Human"),
	    TEXT("Tag_Any"), TEXT("Burst_Yellow"), TEXT("Burst_Teal"), TEXT("TitleLogo"),
	    TEXT("PauseHeader"), TEXT("Underline_Coral"), TEXT("Card_Hanging"), TEXT("Tape_Brown"),
	    TEXT("Card_HangingActive"), TEXT("Goal_Human"), TEXT("Goal_Toy"), TEXT("Progress_Yellow"), TEXT("Progress_Track"),
	    TEXT("Button_Gear"), TEXT("Button_Leave"), TEXT("Mark_TriangleBrown"), TEXT("Label_You"),
	    TEXT("Frame_Settings"), TEXT("StagePreview_Demo"), TEXT("StagePreview_House")};
	for (const TCHAR* Name : Names)
	{
		const FString Asset = FString::Printf(TEXT("T_UI_%s"), Name);
		if (auto* Texture = LoadObject<UTexture2D>(nullptr,
		    *FString::Printf(TEXT("/Game/UI/Designer/Imported/%s.%s"), *Asset, *Asset)))
			ArtTextures.Add(FName(Name), Texture);
	}
}

void ATBHUD::DrawText(const FString& Text, FLinearColor Color, float X, float Y, UFont* Font, float Scale,
                      bool ScalePosition)
{
	if (!Canvas || Scale <= 0.f) return;
	UFont* Face = Font ? Font : PaperFont.Get();
	if (!Face) return;
	if (ScalePosition) { X *= Scale; Y *= Scale; }
	// Rasterize at the displayed size, instead of enlarging a cached 14pt glyph.
	// Keep the authored baseline and dimensions used by the fitted-text layout.
	const float Points = Face->LegacyFontSize * Scale;
	const int32 RasterPoints = FMath::Max(1, FMath::RoundToInt(Points));
	FCanvasTextItem Item(FVector2D(X, Y - 10.f * Scale), FText::FromString(Text),
	    FSlateFontInfo(Face, RasterPoints), Color);
	Item.Scale = FVector2D(Points / RasterPoints);
	Canvas->DrawItem(Item);
}

void ATBHUD::DrawArt(FName Name, float X, float Y, float W, float H, FLinearColor Tint)
{
	const auto* Found = ArtTextures.Find(Name);
	if (!Found || !*Found || W <= 0 || H <= 0) return;
	UTexture2D* Texture = Found->Get();
	const float Scale = FMath::Min(W / FMath::Max(1, Texture->GetSizeX()), H / FMath::Max(1, Texture->GetSizeY()));
	const float AW = Texture->GetSizeX() * Scale, AH = Texture->GetSizeY() * Scale;
	DrawTexture(Texture, X + (W - AW) * .5f, Y + (H - AH) * .5f, AW, AH, 0, 0, 1, 1, Tint);
}

void ATBHUD::DrawFittedText(const FString& Text, FLinearColor Color, float X, float Y, float Width,
                           float Scale, bool Heading, bool Center)
{
	UFont* Font = Heading && HeadingFont ? HeadingFont.Get() : PaperFont.Get();
	float TW = 0, TH = 0;
	GetTextSize(Text, TW, TH, Font, Scale);
	const float Fit = FMath::Min(1.f, Width / FMath::Max(1.f, TW));
	DrawText(Text, Color, X + (Center ? (Width - TW * Fit) * .5f : 0.f), Y, Font, Scale * Fit);
}

void ATBHUD::DrawButton(FName Id, const FString& Label, float X, float Y, float W, float H,
                      bool Enabled, bool Selected, bool Destructive)
{
	const float S = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	float MX = 0, MY = 0;
	const auto* PC = GetOwningPlayerController();
	const bool Hovered = Enabled && PC && PC->GetMousePosition(MX, MY) &&
	    MX >= X && MX < X + W && MY >= Y && MY < Y + H;
	const double Age = GetWorld()->GetTimeSeconds() - LastButtonClickTime;
	const float Lift = Enabled ? (Id == LastClickedButton && Age >= 0 && Age < .18
	    ? -2.f : Hovered ? 2.f : 0.f) * S : 0.f;
	const bool ReadyAction = Id == TEXT("Ready"), StartAction = Id == TEXT("Start");
	FLinearColor Active = Destructive ? TBPaper::Danger : Id == TEXT("Child") ? TBPaper::HumanBlue :
	    Id == TEXT("Toy") ? TBPaper::Accent : TBPaper::Teal;
	if (ReadyAction || StartAction)
	{
		const FLinearColor ActionColor = !Enabled || (ReadyAction && Selected) ? FLinearColor::FromSRGBColor(FColor(105, 105, 105)) :
		    ReadyAction ? TBPaper::ReadyGreen : TBPaper::StartOrange;
		PaperSign(ActionColor, X, Y - Lift, W, H);
		const bool ShowKey = Enabled && PC;
		if (ShowKey)
			DrawKeycap(TB::KeyLabel(PC, ReadyAction ? TB::Keys::LobbyReady : TB::Keys::LobbyStart),
			    X + 12 * S, Y + 8 * S - Lift, 38 * S, H - 16 * S);
		const FString ActionLabel = ReadyAction ? Selected ? TBText::Literal(TEXT("Canvas.Unready")) : TBText::Literal(TEXT("Canvas.Ready")) :
		    Enabled ? TBText::Literal(TEXT("Canvas.Start")) : TBText::Literal(TEXT("Canvas.Waiting"));
		DrawFittedText(ActionLabel, TBPaper::Paper, X + (ShowKey ? 62 : 12) * S,
		    Y + H * .5f - 7 * S - Lift, W - (ShowKey ? 74 : 24) * S, 1.15f * S, true, true);
	}
	else
	{
		const bool Primary = Id == TEXT("ChangeStage") || Id == TEXT("OpenSettings") || Id == TEXT("RetryLobbyReturn") ||
		    Id == TEXT("DraftNextStage");
		const bool Secondary = Id == TEXT("CloseSettings") || Id == TEXT("OpenOptions");
		const FLinearColor Fill = !Enabled ? TBPaper::Lavender : Selected ? Active :
		    Hovered || Primary ? TBPaper::Gold : Secondary ? TBPaper::Teal : TBPaper::Cream;
		if (W > 85 * S && H >= 30 * S) PaperSign(Fill, X, Y - Lift, W, H);
		else PaperPanel(Fill, X, Y - Lift, W, H);
		float TextScale = FMath::Min(1.05f, H / (40 * S)) * S;
		if (H < 30 * S)
		{
			TextScale = FMath::Max(.75f * S, TextScale);
		}
		const bool Heading = H >= 30 * S;
		float TW = 0, TH = 0;
		GetTextSize(Label, TW, TH, Heading ? HeadingFont.Get() : PaperFont.Get(), TextScale);
		const float Fit = FMath::Min(1.f, (W - 16 * S) / FMath::Max(1.f, TW));
		const float TextY = Y + (H - TH * Fit) * .5f + 10 * TextScale * Fit - Lift;
		const bool LightText = Selected && (Id == TEXT("Child") || Id == TEXT("Toy") || Destructive);
		DrawFittedText(Label, !Enabled ? TBPaper::Muted : LightText ? TBPaper::Paper :
		    Destructive ? TBPaper::Danger : TBPaper::Ink,
		    X + 8 * S, TextY, W - 16 * S, TextScale, Heading, true);
	}
	if (Enabled) AddHitBox(FVector2D(X, Y), FVector2D(W, H), Id, true);
}

void ATBHUD::DrawProgress(float Value, float X, float Y, float W, float H, FLinearColor Color)
{
	const float S = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	if (H >= 18 * S)
	{
		const auto* Track = ArtTextures.Find(TEXT("Progress_Track"));
		const auto* Gold = ArtTextures.Find(TEXT("Progress_Yellow"));
		if (Track && *Track && Gold && *Gold)
		{
			const float Scale = FMath::Min(W / 293.f, H / 27.f);
			const float PX = X + (W - 293 * Scale) * .5f, PY = Y + (H - 27 * Scale) * .5f;
			DrawTexture(Track->Get(), PX, PY, 293 * Scale, 27 * Scale, 0, 0, 1, 1);
			const float Fraction = FMath::Clamp(Value, 0.f, 1.f);
			if (Fraction > 0.f)
				DrawTexture(Gold->Get(), PX + 5 * Scale, PY + 5 * Scale, 283 * Scale * Fraction, 17 * Scale,
				    0, 0, Fraction, 1);
			return;
		}
	}
	const float Inset = FMath::Min(2.f * S, H * .2f);
	DrawRect(TBPaper::Outline, X, Y, W, H);
	DrawRect(FLinearColor(.80f, .77f, .68f), X + Inset, Y + Inset, W - 2 * Inset, H - 2 * Inset);
	const float Fill = FMath::Clamp(Value, 0.f, 1.f) * (W - 2 * Inset);
	if (Fill > 0.f)
	{
		DrawRect(Color, X + Inset, Y + Inset, Fill, H - 2 * Inset);
		DrawRect(FLinearColor(1, 1, 1, .24f), X + Inset, Y + Inset, Fill, (H - 2 * Inset) * .3f);
	}
}

void ATBHUD::PaperPanel(FLinearColor Tint, float X, float Y, float W, float H)
{
	if (W <= 0 || H <= 0) return;
	const float S = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	auto Layer = [&](FLinearColor Color, float LX, float LY, float LW, float LH)
	{
		if (!PaperTexture)
		{
			DrawRect(Color, LX, LY, LW, LH);
			return;
		}
		const float Edge = FMath::Min(16.f * S, FMath::Min(LW, LH) * .3f);
		const float PX[] = {LX, LX + Edge, LX + LW - Edge, LX + LW};
		const float PY[] = {LY, LY + Edge, LY + LH - Edge, LY + LH};
		const float UV[] = {.04f, .25f, .70f, .93f};
		for (int32 Row = 0; Row < 3; ++Row)
			for (int32 Col = 0; Col < 3; ++Col)
				DrawTexture(PaperTexture, PX[Col], PY[Row], PX[Col + 1] - PX[Col], PY[Row + 1] - PY[Row], UV[Col], UV[Row],
				    UV[Col + 1] - UV[Col], UV[Row + 1] - UV[Row], Color);
	};
	FLinearColor Shadow = TBPaper::Shadow, Outline = TBPaper::Outline;
	Shadow.A *= Tint.A;
	Outline.A *= Tint.A;
	Layer(Shadow, X + 2 * S, Y + 5 * S, W, H);
	Layer(Outline, X, Y, W, H);
	const float Border = FMath::Min(3.f * S, FMath::Min(W, H) * .08f);
	Layer(Tint, X + Border, Y + Border, W - 2 * Border, H - 2 * Border);
}

void ATBHUD::FramePanel(float X, float Y, float W, float H)
{
	const auto* Frame = ArtTextures.Find(TEXT("Frame_Settings"));
	if (!Frame || !*Frame)
	{
		PaperPanel(TBPaper::Cream, X, Y, W, H);
		return;
	}
	const float S = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	const float Edge = FMath::Min(24.f * S, FMath::Min(W, H) * .25f);
	const float UV[] = {0.f, .25f, .75f, 1.f};
	// Keep the original drawn corners and border width, including on large settings cards.
	for (int32 Layer = 0; Layer < 2; ++Layer)
	{
		const float LX = X + (Layer == 0 ? 3 * S : 0), LY = Y + (Layer == 0 ? 5 * S : 0);
		const float PX[] = {LX, LX + Edge, LX + W - Edge, LX + W};
		const float PY[] = {LY, LY + Edge, LY + H - Edge, LY + H};
		for (int32 Row = 0; Row < 3; ++Row)
			for (int32 Col = 0; Col < 3; ++Col)
				DrawTexture(Frame->Get(), PX[Col], PY[Row], PX[Col + 1] - PX[Col], PY[Row + 1] - PY[Row],
				    UV[Col], UV[Row], UV[Col + 1] - UV[Col], UV[Row + 1] - UV[Row],
				    Layer == 0 ? TBPaper::Shadow : FLinearColor::White);
	}
}

void ATBHUD::PaperSign(FLinearColor Fill, float X, float Y, float W, float H)
{
	if (!Canvas || W <= 0.f || H <= 0.f) return;
	const float S = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	// The title's illustrated buttons are gently bowed paper labels. Keep all text live.
	static const TArray<FVector2D> Edge = []
	{
		TArray<FVector2D> Points;
		Points.Reserve(26);
		for (int32 Step = 0; Step <= 12; ++Step)
		{
			const float T = Step / 12.f, U = 1.f - T;
			Points.Add(U * U * FVector2D(.02f, .06f) + 2 * U * T * FVector2D(.5f, .14f) + T * T * FVector2D(.978f, .015f));
		}
		for (int32 Step = 0; Step <= 12; ++Step)
		{
			const float T = Step / 12.f, U = 1.f - T;
			Points.Add(U * U * FVector2D(.995f, .895f) + 2 * U * T * FVector2D(.5f, 1.03f) + T * T * FVector2D(.005f, .9f));
		}
		return Points;
	}();
	auto Layer = [&](FLinearColor Color, float LX, float LY, float LW, float LH)
	{
		TArray<FVector2D> Points;
		Points.Reserve(Edge.Num());
		for (const FVector2D& Point : Edge) Points.Emplace(LX + Point.X * LW, LY + Point.Y * LH);
		DrawSmoothPolygon(Points, Color);
	};
	const float Border = FMath::Min(3.f * S, FMath::Min(W, H) * .1f);
	Layer(TBPaper::Shadow.CopyWithNewOpacity(TBPaper::Shadow.A * Fill.A), X + 2 * S, Y + 4 * S, W, H);
	Layer(TBPaper::Outline.CopyWithNewOpacity(Fill.A), X, Y, W, H);
	Layer(Fill, X + Border, Y + Border, W - 2 * Border, H - 2 * Border);
}

// Canvas triangles do not inherit the scene's AA. A one-pixel coverage fringe keeps
// the authored curved edge sharp without stair steps or overlapping translucent corners.
// Callers supply an ordered outline whose centre sees every edge (rounded cards / bowed labels).
void ATBHUD::DrawSmoothPolygon(const TArray<FVector2D>& Points, FLinearColor Color)
{
	if (!Canvas || Color.A <= 0.f || Points.Num() < 3) return;
	TArray<FVector2D, TInlineAllocator<64>> Boundary;
	for (const FVector2D& Point : Points)
	{
		if (Boundary.IsEmpty() || !Point.Equals(Boundary.Last(), .001f)) Boundary.Add(Point);
	}
	if (Boundary.Num() > 1 && Boundary[0].Equals(Boundary.Last(), .001f)) Boundary.Pop(EAllowShrinking::No);
	const int32 Count = Boundary.Num();
	if (Count < 3) return;
	FVector2D Center = FVector2D::ZeroVector;
	double Area = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector2D& A = Boundary[Index];
		const FVector2D& B = Boundary[(Index + 1) % Count];
		Center += A;
		Area += A.X * B.Y - B.X * A.Y;
	}
	if (FMath::IsNearlyZero(Area)) return;
	Center /= Count;
	const float Direction = Area > 0 ? 1.f : -1.f;
	TArray<FVector2D, TInlineAllocator<64>> Normals, Inner, Outer;
	Normals.SetNumUninitialized(Count);
	Inner.SetNumUninitialized(Count);
	Outer.SetNumUninitialized(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector2D Edge = (Boundary[(Index + 1) % Count] - Boundary[Index]).GetSafeNormal();
		Normals[Index] = FVector2D(Edge.Y, -Edge.X) * Direction;
	}
	// Positions are already in viewport pixels: keep coverage independent of UI scale.
	constexpr float HalfFringe = .65f;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector2D Miter = (Normals[(Index + Count - 1) % Count] + Normals[Index]).GetSafeNormal();
		const double Projection = FMath::Max(.5, FVector2D::DotProduct(Miter, Normals[Index]));
		const FVector2D Offset = Miter * (HalfFringe / Projection);
		Inner[Index] = Boundary[Index] - Offset;
		Outer[Index] = Boundary[Index] + Offset;
	}
	TArray<FCanvasUVTri> Triangles;
	Triangles.Reserve(Count * 3);
	auto Add = [&](FVector2D A, FVector2D B, FVector2D C, FLinearColor CA, FLinearColor CB, FLinearColor CC)
	{
		FCanvasUVTri& Triangle = Triangles.AddDefaulted_GetRef();
		Triangle.V0_Pos = A; Triangle.V1_Pos = B; Triangle.V2_Pos = C;
		Triangle.V0_UV = Triangle.V1_UV = Triangle.V2_UV = FVector2D::ZeroVector;
		Triangle.V0_Color = CA; Triangle.V1_Color = CB; Triangle.V2_Color = CC;
	};
	const FLinearColor Transparent = Color.CopyWithNewOpacity(0.f);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const int32 Next = (Index + 1) % Count;
		Add(Center, Inner[Index], Inner[Next], Color, Color, Color);
		Add(Inner[Index], Outer[Index], Outer[Next], Color, Transparent, Transparent);
		Add(Inner[Index], Outer[Next], Inner[Next], Color, Transparent, Color);
	}
	FCanvasTriangleItem Item(FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector, GWhiteTexture);
	Item.TriangleList = MoveTemp(Triangles);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

// Reference UI uses a simple cream fill and a continuous brown rounded outline.
void ATBHUD::RoundedPanel(FLinearColor Fill, float X, float Y, float W, float H, float Radius,
                         float Border, FLinearColor Outline)
{
	if (!Canvas || W <= 0.f || H <= 0.f) return;
	const float S = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	if (Outline.A <= 0.f) Outline = TBPaper::Outline;
	auto Layer = [&](FLinearColor Color, float LX, float LY, float LW, float LH, float R)
	{
		R = FMath::Clamp(R, 0.f, FMath::Min(LW, LH) * .5f);
		static const TArray<FVector2D> UnitArc = []
		{
			TArray<FVector2D> Result;
			Result.Reserve(44);
			for (int32 Corner = 0; Corner < 4; ++Corner)
				for (int32 Segment = 0; Segment <= 10; ++Segment)
				{
					const float Angle = (-90.f + Corner * 90.f + Segment * 9.f) * PI / 180.f;
					Result.Emplace(FMath::Cos(Angle), FMath::Sin(Angle));
				}
			return Result;
		}();
		TArray<FVector2D> Points;
		Points.Reserve(44);
		const FVector2D Centers[] = {{LX + LW - R, LY + R}, {LX + LW - R, LY + LH - R},
		    {LX + R, LY + LH - R}, {LX + R, LY + R}};
		for (int32 Corner = 0; Corner < 4; ++Corner)
			for (int32 Segment = 0; Segment <= 10; ++Segment)
				Points.Add(Centers[Corner] + UnitArc[Corner * 11 + Segment] * R);
		DrawSmoothPolygon(Points, Color);
	};
	const float B = FMath::Min(Border * S, FMath::Min(W, H) * .25f);
	Layer(Outline, X, Y, W, H, Radius * S);
	Layer(Fill, X + B, Y + B, W - 2 * B, H - 2 * B, FMath::Max(0.f, Radius * S - B));
}

void ATBHUD::DrawKeycap(const FString& Key, float X, float Y, float W, float H)
{
	const float S = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	RoundedPanel(TBPaper::Cream, X, Y, W, H, 5, 2);
	DrawFittedText(Key, TBPaper::Ink, X + 5 * S, Y + H * .5f - 6 * S, W - 10 * S, .95f * S, true, true);
}
