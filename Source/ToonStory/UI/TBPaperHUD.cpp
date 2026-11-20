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
#include "InputCoreTypes.h"
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
		else if (Name == TEXT("DraftNextStage") || Name == TEXT("DraftPreviousStage"))
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
	auto DrawNotice = [&](float PX, float PY, float Width, float AvailableHeight, bool IncludeStatus = false)
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
		Panel(PX, PY, Width, 16.f + Lines.Num() * LineHeight, TBPaper::Cream);
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
			Text(Lines[Index], PX + 14, PY + 8.f + 10.f * FontScale + Index * LineHeight, FontScale, Ink);
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
		const UTBSession* Session = PC->GetGameInstance()->GetSubsystem<UTBSession>();
		const FString RoomOwner = Session ? Session->GetCurrentRoomOwner() : FString();
		const FString Room = RoomOwner.IsEmpty() ? TBText::String(TEXT("Keys.Lobby")) :
		    FText::Format(TBText::Get(TEXT("Canvas.LobbyRoom")), FText::FromString(RoomOwner)).ToString();
		PaperSign(TBPaper::Gold, 24 * S, 22 * S, 160 * S, 38 * S);
		DrawFittedText(Room, Ink, 33 * S, 35 * S, 142 * S, .9f * S, true, true);
		DrawButton(TEXT("Players"), FText::Format(TBText::Get(TEXT("Canvas.LobbyPlayers")),
		    FText::AsNumber(PlayerCount), FText::AsNumber(State->PlayerLimit)).ToString(),
		    194 * S, 22 * S, 155 * S, 38 * S, !State->bTravelPending);
		DrawButton(TEXT("Leave"), TBText::String(PC->HasAuthority() ? TEXT("Root.Room.Close") : TEXT("Stage.Leave")),
		    Canvas->ClipX - 179 * S, 22 * S, 155 * S, 38 * S, !State->bTravelPending);
		const float Bottom = (Canvas->ClipY - Y) / S - 84;
		const bool ShowStatus = !EnoughPlayers || State->bTravelPending;
		const float Top = Bottom - (ShowStatus ? 38.f : 10.f);
		// The horizontal lobby bar hugs the viewport, including on widescreen.
		const float BarX = 24.f * S, BarW = Canvas->ClipX - 2 * BarX, ColumnScale = BarW / 1144.f;
		auto Column = [&](float Offset) { return BarX + ColumnScale * Offset; };
		auto Button = [&](FName Id, const FString& Caption, float Offset, float Width, bool Enabled, bool Selected = false)
		{ DrawButton(Id, Caption, Column(Offset), Y + Bottom * S, Width * ColumnScale, 42 * S, Enabled, Selected); };
		RoundedPanel(TBPaper::Paper, BarX, Y + Top * S, BarW, (Bottom + 55 - Top) * S, 15, 2.f);
		if (ShowStatus)
			DrawFittedText(TBText::String(State->bTravelPending ? TEXT("Canvas.TravelStage") : TEXT("Canvas.NeedsTeams")),
			    Muted, Column(18), Y + (Bottom - 24) * S, BarW - 36 * ColumnScale, .84f * S);
		DrawRect(TBPaper::Outline.CopyWithNewOpacity(.19f), Column(563), Y + (Bottom + 3) * S, 1.5f * S, 36 * S);
		const FString ReadyKey = TB::KeyLabel(PC, TB::Keys::LobbyReady);
		const FString StartKey = TB::KeyLabel(PC, TB::Keys::LobbyStart);
		Button(TEXT("Ready"),
		       Local && Local->bReady ? FString::Format(TBText::Literal(TEXT("Canvas.UnreadyKey")), {*ReadyKey}) : FString::Format(TBText::Literal(TEXT("Canvas.ReadyKey")), {*ReadyKey}),
		       258, 286, Local != nullptr && !State->bTravelPending, Local && Local->bReady);
		DrawFittedText(FString::Format(TBText::Literal(TEXT("Canvas.ReadyCount")), {ReadyCount, PlayerCount}), Ink,
		    Column(18), Y + (Bottom + 3) * S, 224 * ColumnScale, .99f * S, true);
		const float GaugeW = 224 * ColumnScale, GaugeY = Y + (Bottom + 30) * S;
		RoundedPanel(TBPaper::Cream, Column(18), GaugeY, GaugeW, 8 * S, 4, 1, Muted);
		if (ReadyCount > 0)
			RoundedPanel(TBPaper::Teal, Column(18) + S, GaugeY + S, (GaugeW - 2 * S) * ReadyCount / PlayerCount, 6 * S, 3, 0);
		Button(TEXT("OpenSettings"), TBText::Literal(TEXT("Canvas.SettingsKey")), 582, 250, !State->bTravelPending);
		Button(TEXT("Start"),
		       PC->HasAuthority() ? FString::Format(TBText::Literal(TEXT("Canvas.StartKey")), {*StartKey}) : FString(TBText::Literal(TEXT("Canvas.WaitHost"))),
		       846, 280, CanStart, CanStart);
		DrawNotice(656, Bottom - 161, 516, 108);
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
		const FLinearColor RuleEdge = FLinearColor::FromSRGBColor(FColor(134, 95, 64));
		const FLinearColor Mint = FLinearColor::FromSRGBColor(FColor(128, 203, 188));
		auto Label = [&](const TCHAR* Key, float PX, float PY, float Width, float Size = 1.f)
		{ DrawFittedText(TBText::String(Key), Ink, X + PX * S, Y + PY * S, Width * S, Size * S, true); };
		float MX = 0, MY = 0;
		const bool HasPointer = PC->GetMousePosition(MX, MY);
		const bool PointerDown = PC->IsInputKeyDown(EKeys::LeftMouseButton);
		auto Hovered = [&](float PX, float PY, float Width, float Height)
		{ return HasPointer && MX >= X + PX * S && MX < X + (PX + Width) * S && MY >= Y + PY * S && MY < Y + (PY + Height) * S; };
		auto PaperControl = [&](FName Id, const FString& Caption, float PX, float PY, float Width, float Height,
		                        bool Enabled, FLinearColor Fill, float Size = .85f, bool Selected = false, FName Icon = NAME_None)
		{
			const bool Over = Enabled && Hovered(PX, PY, Width, Height);
			const bool Pressed = Over && PointerDown;
			const float FaceY = PY + (Pressed ? 1.5f : Over ? -1.5f : 0.f);
			const float Depth = Enabled && !Pressed ? 2.f : 0.f;
			const FLinearColor Face = !Enabled ? FMath::Lerp(Fill, TBPaper::Paper, .6f)
			    : Pressed ? FMath::Lerp(Fill, Ink, .16f) : Fill;
			const FLinearColor Edge = Selected ? TBPaper::Outline : Enabled ? RuleEdge : Muted;
			RoundedPanel(Edge, X + PX * S, Y + (FaceY + Depth) * S, Width * S, Height * S, 6, 0);
			RoundedPanel(Over && !Pressed ? FMath::Lerp(Face, TBPaper::Paper, .12f) : Face,
			    X + PX * S, Y + FaceY * S, Width * S, Height * S, 6, 1.4f, Edge);
			const bool HasIcon = !Icon.IsNone() && ArtTextures.Contains(Icon);
			if (!Caption.IsEmpty())
			{
				const float Leading = HasIcon ? 30.f : Selected ? 20.f : 0.f;
				const float TextWidth = (Width - 16 - Leading) * S;
				float TW = 0, TH = 0;
				GetTextSize(Caption, TW, TH, HeadingFont, Size * S);
				const float Fit = FMath::Min(1.f, TextWidth / FMath::Max(1.f, TW));
				const float Left = X + (PX + (Width - Leading) * .5f) * S - TW * Fit * .5f;
				if (HasIcon) DrawArt(Icon, Left, Y + (FaceY + (Height - 23) * .5f) * S, 23 * S, 23 * S);
				else if (Selected) DrawCheck(Left, Y + (FaceY + (Height - 14) * .5f) * S, 14 * S, Ink);
				DrawText(Caption, Enabled ? Ink : Muted, Left + Leading * S,
				    Y + FaceY * S + (Height * S - TH * Fit) * .5f + 10 * Size * S * Fit, HeadingFont, Size * S * Fit);
			}
			if (Enabled) AddHitBox({X + PX * S, Y + PY * S}, {Width * S, Height * S}, Id, true);
			return FaceY - PY;
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
			auto Key = [&](FName Id, float KeyX, bool CanChange, bool Plus)
			{
				const bool Enabled = Host && CanChange;
				const bool Over = Enabled && Hovered(KeyX, PY, Height, Height);
				const float FaceY = PY + (Over ? PointerDown ? 1.5f : -1.5f : 0.f);
				const FLinearColor Edge = Enabled ? RuleEdge : FMath::Lerp(RuleEdge, TBPaper::Paper, .62f);
				if (Enabled && !(Over && PointerDown))
					RoundedPanel(Edge, X + KeyX * S, Y + (FaceY + 2) * S, Height * S, Height * S, 5, 0);
				RoundedPanel(Over ? TBPaper::Paper : FLinearColor::FromSRGBColor(FColor(255, 237, 198)),
				    X + KeyX * S, Y + FaceY * S, Height * S, Height * S, 5, 1.3f, Edge);
				const float Bar = Height * .42f, Thick = Height * .085f;
				const float CX = X + (KeyX + Height * .5f) * S, CY = Y + (FaceY + Height * .5f) * S;
				const FLinearColor Symbol = Enabled ? Ink : Edge;
				RoundedPanel(Symbol, CX - Bar * S * .5f, CY - Thick * S * .5f, Bar * S, Thick * S, Thick * .5f, 0);
				if (Plus) RoundedPanel(Symbol, CX - Thick * S * .5f, CY - Bar * S * .5f, Thick * S, Bar * S, Thick * .5f, 0);
				if (Enabled) AddHitBox({X + KeyX * S, Y + PY * S}, {Height * S, Height * S}, Id, true);
			};
			Key(Less, PX, CanLess, false);
			Key(More, PX + Width - Height, CanMore, true);
			Numerals(Value, PX + Height + 4, PY, Width - Height * 2 - 8, Height, ValueSize);
		};

		// Hard cover, cream pages, and two binding staples between the rules and stage photo.
		RoundedPanel(TBPaper::Shadow, X + 3 * S, Y + 53 * S, 1200 * S, 654 * S, 23, 0);
		RoundedPanel(TBPaper::Teal, X - 2 * S, Y + 47 * S, 1204 * S, 653 * S, 22, 3);
		RoundedPanel(TBPaper::Paper, X, Y + 42 * S, 1200 * S, 651 * S, 21, 3);
		RoundedPanel(TBPaper::Gold, X + 3 * S, Y + 45 * S, 1194 * S, 87 * S, 17, 0);
		DrawRect(TBPaper::Gold, X + 3 * S, Y + 108 * S, 1194 * S, 23 * S);
		DrawRect(TBPaper::Outline, X + 3 * S, Y + 131 * S, 1194 * S, 2 * S);
		DrawRect(FMath::Lerp(TBPaper::Paper, TBPaper::Cream, .36f), X + 489 * S, Y + 133 * S, 708 * S, 494 * S);
		DrawRect(Muted.CopyWithNewOpacity(.36f), X + 488 * S, Y + 133 * S, 1.4f * S, 494 * S);
		for (const float StapleY : {196.f, 560.f})
			RoundedPanel(TBPaper::Cream, X + 480 * S, Y + StapleY * S, 18 * S, 5 * S, 2.5f, 1.f, Muted);
		Art(TEXT("Icon_Stage"), 29, 59, 63, 52);
		Label(TEXT("Stage.Title"), 109, 80, 610, 1.65f);
		PaperControl(TEXT("Leave"), TBText::String(Host ? TEXT("Root.Room.Close") : TEXT("Stage.Leave")),
		    645, 65, 164, 43, !State->bTravelPending, TBPaper::Paper, .82f);
		const float RoomLift = PaperControl(TEXT("Players"), TEXT(""), 827, 65, 182, 43, true, TBPaper::Paper);
		DrawFittedText(TBText::String(TEXT("Stage.Room")), Muted, X + 839 * S, Y + (81 + RoomLift) * S, 83 * S, .7f * S, true);
		Numerals(FText::Format(TBText::Get(TEXT("Stage.RoomPlayers")), PlayerCount, State->PlayerLimit).ToString(),
		    922, 65 + RoomLift, 77, 43, 1.08f);
		PaperControl(TEXT("OpenOptions"), TBText::String(TEXT("Stage.Options")), 1025, 65, 143, 43, true,
		    Mint, .85f, false, TEXT("Icon_Settings"));

		Label(TEXT("Stage.RulesTitle"), 32, 158, 232, 1.08f);
		DrawSmoothPolygon({{X + 275 * S, Y + 158 * S}, {X + 472 * S, Y + 153 * S},
		    {X + 472 * S, Y + 160 * S}, {X + 275 * S, Y + 165 * S}}, TBPaper::Teal);
		for (const float LineY : {267.f, 344.f, 465.f})
			DrawRect(Muted.CopyWithNewOpacity(.32f), X + 32 * S, Y + LineY * S, 430 * S, 1.5f * S);
		DrawRect(Muted.CopyWithNewOpacity(.42f), X + 3 * S, Y + 627 * S, 1194 * S, 1.5f * S);
		Art(TEXT("Icon_Hourglass"), 40, 204, 32, 40);
		Label(TEXT("Stage.Time"), 86, 214, 210, .94f);
		const int32 Seconds = FMath::RoundToInt(StageDraft.Duration);
		Stepper(TEXT("DraftLessTime"), TEXT("DraftMoreTime"),
		    Number(Seconds % 60 == 0 ? TEXT("Stage.Minutes") : TEXT("Stage.Seconds"), Seconds % 60 == 0 ? Seconds / 60 : Seconds),
		    316, 210, 156, StageDraft.Duration > TB::Rules().Match.MinDuration, StageDraft.Duration < TB::Rules().Match.MaxDuration);
		Art(TEXT("Icon_BatteryPickup"), 40, 282, 32, 40);
		Label(TEXT("Stage.Batteries"), 86, 291, 210, .94f);
		Stepper(TEXT("DraftFewerItems"), TEXT("DraftMoreItems"), Number(TEXT("Stage.Items"), StageDraft.RequiredItems),
		    316, 286, 156, StageDraft.RequiredItems > 1, StageDraft.RequiredItems < TBLobbySettings::StageItemCount(DraftStage));

		Label(TEXT("Canvas.ChooseTeam"), 32, 367, 430, .97f);
		const bool CanChooseTeam = Local && !State->bTravelPending;
		const ETBTeam Preference = Local ? Local->Preference : ETBTeam::None;
		PaperControl(TEXT("Child"), TBText::String(TEXT("Canvas.Human")), 32, 399, 136, 40, CanChooseTeam,
		    Preference == ETBTeam::Human ? FLinearColor::FromSRGBColor(FColor(156, 213, 224)) : TBPaper::Paper, .8f, Preference == ETBTeam::Human);
		PaperControl(TEXT("Toy"), TBText::String(TEXT("Canvas.Toy")), 179, 399, 136, 40, CanChooseTeam,
		    Preference == ETBTeam::Toy ? FLinearColor::FromSRGBColor(FColor(243, 164, 140)) : TBPaper::Paper, .8f, Preference == ETBTeam::Toy);
		PaperControl(TEXT("Either"), TBText::String(TEXT("Canvas.Either")), 326, 399, 136, 40, CanChooseTeam,
		    Preference == ETBTeam::None ? Mint : TBPaper::Paper, .8f, Preference == ETBTeam::None);

		Art(TEXT("Icon_Npc"), 32, 488, 38, 42);
		Label(TEXT("Stage.NPCs"), 83, 503, 270, .94f);
		const bool NPCEnabled = NPCCount > 0;
		if (NPCEnabled)
			DrawFittedText(Number(TEXT("Stage.NPCTotal"), NPCCount), Muted, X + 83 * S, Y + 531 * S, 260 * S, .72f * S);
		const bool CanToggleNPC = Host && (NPCEnabled || CanAddNPC);
		const float ToggleX = X + 402 * S, ToggleY = Y + 495 * S;
		const bool ToggleOver = CanToggleNPC && Hovered(402, 495, 60, 32);
		const FLinearColor Track = NPCEnabled ? TBPaper::Teal : FMath::Lerp(TBPaper::Cream, Muted, .35f);
		RoundedPanel(Track, ToggleX, ToggleY, 60 * S, 32 * S, 16, 2.5f, CanToggleNPC ? TBPaper::Outline : Muted);
		const float KnobX = ToggleX + (NPCEnabled ? 33 : 5) * S;
		const float KnobY = ToggleY + (ToggleOver ? PointerDown ? 6.f : 4.f : 5.f) * S;
		RoundedPanel(FMath::Lerp(Track, Ink, .4f), KnobX, ToggleY + 7 * S, 22 * S, 22 * S, 11, 0);
		RoundedPanel(CanToggleNPC ? TBPaper::Paper : TBPaper::Cream, KnobX, KnobY, 22 * S, 22 * S, 11, 1);
		if (CanToggleNPC) AddHitBox({ToggleX, ToggleY}, {60 * S, 32 * S}, TEXT("ToggleDraftNPCs"), true);
		if (NPCEnabled)
		{
			for (int32 Team = 0; Team < 2; ++Team)
			{
				const float PX = Team == 0 ? 32.f : 252.f;
				const FLinearColor TeamColor = Team == 0 ? TBPaper::HumanBlue : Accent;
				RoundedPanel(TBPaper::Paper, X + PX * S, Y + 557 * S, 210 * S, 42 * S, 6, 1.5f, Muted.CopyWithNewOpacity(.65f));
				DrawFittedText(TBText::String(Team == 0 ? TEXT("Stage.Human") : TEXT("Stage.Toy")), TeamColor,
				    X + (PX + 10) * S, Y + 571 * S, 88 * S, .71f * S, true);
				Stepper(Team == 0 ? TEXT("DraftFewerNPCHumans") : TEXT("DraftFewerNPCToys"),
				    Team == 0 ? TEXT("DraftMoreNPCHumans") : TEXT("DraftMoreNPCToys"),
				    FString::FromInt(Team == 0 ? StageDraft.NPCHumans : StageDraft.NPCToys), PX + 112, 565, 90,
				    (Team == 0 ? StageDraft.NPCHumans : StageDraft.NPCToys) > 0, CanAddNPC, 26, 1.25f);
			}
		}
		else if (!CanAddNPC)
			Wrapped(TBText::String(TEXT("Stage.NPCFullHint")), 32, 558, 430, .74f, 2, Muted);

		Label(TEXT("Stage.ChooseStage"), 512, 158, 520, 1.08f);
		DrawFittedText(House ? TEXT("01 / 02") : TEXT("02 / 02"), Ink, X + 1080 * S, Y + 158 * S, 88 * S, .95f * S, false, true);
		constexpr float PhotoX = 512, PhotoY = 192, PhotoW = 656, PhotoH = 328;
		RoundedPanel(TBPaper::Shadow, X + (PhotoX + 2) * S, Y + (PhotoY + 4) * S, PhotoW * S, PhotoH * S, 6, 0);
		RoundedPanel(TBPaper::Paper, X + PhotoX * S, Y + PhotoY * S, PhotoW * S, PhotoH * S, 6, 1.5f, Muted);
		if (const auto* Preview = ArtTextures.Find(House ? TEXT("StagePreview_House") : TEXT("StagePreview_Demo")); Preview && *Preview && Preview->Get()->GetResource())
		{
			// Cover the preview area, preserving the source aspect ratio and a smooth rounded silhouette.
			const float LX = X + (PhotoX + 6) * S, LY = Y + (PhotoY + 6) * S;
			const float PW = (PhotoW - 12) * S, PH = (PhotoH - 12) * S, R = 3 * S;
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
		if (const auto* Tape = ArtTextures.Find(TEXT("Tape_Cream")); Tape && *Tape)
		{
			const float TapeSize = 57.f * S;
			DrawTexture(Tape->Get(), X + (PhotoX + PhotoW * .5f) * S - TapeSize * .5f,
			    Y + PhotoY * S - TapeSize * .5f, TapeSize, TapeSize, 0, 0, 1, 1,
			    FLinearColor(1, 1, 1, .93f), BLEND_Translucent, 1.f, false, 45.f, FVector2D(.5, .5));
		}
		RoundedPanel(FLinearColor::FromSRGBColor(FColor(163, 222, 198)), X + 528 * S, Y + 210 * S,
		    110 * S, 30 * S, 5, 1.25f, FLinearColor::FromSRGBColor(FColor(55, 75, 62)));
		DrawFittedText(TBText::String(House ? TEXT("Stage.HideAndSeek") : TEXT("Stage.Explore")), Ink,
		    X + 535 * S, Y + 220 * S, 96 * S, .76f * S, true, true);
		DrawFittedText(TBText::String(House ? TEXT("Stage.House") : TEXT("Stage.Demo")), TBPaper::Paper,
		    X + 538 * S, Y + 483 * S, 600 * S, 1.38f * S, true, false, true);
		auto Arrow = [&](FName Id, bool Previous, float PX)
		{
			constexpr float PY = PhotoY + PhotoH * .5f - 20.f;
			const bool Over = Host && Hovered(PX, PY, 36, 40);
			const float FaceY = PY + (Over ? PointerDown ? 1.f : -1.f : 0.f);
			RoundedPanel(Over ? TBPaper::Gold : TBPaper::Paper, X + PX * S, Y + FaceY * S, 36 * S, 40 * S, 8, 2);
			const float Direction = Previous ? -1.f : 1.f;
			const FVector2D Mid(X + (PX + 18) * S, Y + (FaceY + 20) * S);
			auto Point = [&](float U, float V) { return Mid + FVector2D(Direction * U * S, V * S); };
			const FLinearColor Color = Host ? Ink : Muted;
			DrawSmoothPolygon({Point(-5, -8), Point(-2, -10), Point(7, 0), Point(2, 0)}, Color);
			DrawSmoothPolygon({Point(2, 0), Point(7, 0), Point(-2, 10), Point(-5, 8)}, Color);
			if (Host) AddHitBox({X + PX * S, Y + PY * S}, {36 * S, 40 * S}, Id, true);
		};
		Arrow(TEXT("DraftPreviousStage"), true, PhotoX + 13);
		Arrow(TEXT("DraftNextStage"), false, PhotoX + PhotoW - 49);
		auto Thumbnail = [&](bool IsHouse, float PX)
		{
			constexpr float PY = 545, Width = 319, Height = 58;
			const bool Selected = House == IsHouse;
			const bool Over = Host && Hovered(PX, PY, Width, Height);
			const float FaceY = PY + (Over ? PointerDown ? 1.f : -1.f : 0.f);
			const FLinearColor Edge = FLinearColor::FromSRGBColor(FColor(67, 104, 84));
			if (Selected)
				RoundedPanel(Edge, X + PX * S, Y + (FaceY + 2) * S, Width * S, Height * S, 8, 0);
			RoundedPanel(Selected ? FLinearColor::FromSRGBColor(FColor(200, 231, 212)) : FLinearColor::FromSRGBColor(FColor(249, 233, 200)),
			    X + PX * S, Y + FaceY * S, Width * S, Height * S, 8, Selected ? 2.f : 1.2f, Selected ? Edge : RuleEdge);
			Art(IsHouse ? TEXT("StagePreview_House") : TEXT("StagePreview_Demo"), PX + 7, FaceY + 8, 70, 42);
			DrawFittedText(TBText::String(IsHouse ? TEXT("Stage.House") : TEXT("Stage.Demo")), Ink,
			    X + (PX + 89) * S, Y + (FaceY + 21) * S, 192 * S, .88f * S, true);
			if (Selected)
			{
				RoundedPanel(Edge, X + (PX + Width - 29) * S, Y + (FaceY + 20) * S, 18 * S, 18 * S, 9, 0);
				DrawCheck(X + (PX + Width - 25) * S, Y + (FaceY + 24) * S, 10 * S, TBPaper::Paper);
			}
			if (Host) AddHitBox({X + PX * S, Y + PY * S}, {Width * S, Height * S},
			    IsHouse ? TEXT("StagePlayroom") : TEXT("StageExpanded"), true);
		};
		Thumbnail(true, PhotoX);
		Thumbnail(false, PhotoX + PhotoW - 319);

		const bool Dirty = DraftStage != State->SelectedStage || !FMath::IsNearlyEqual(StageDraft.Duration, State->Settings.Duration) ||
		    StageDraft.RequiredItems != State->Settings.RequiredItems || StageDraft.NPCHumans != State->Settings.NPCHumans ||
		    StageDraft.NPCToys != State->Settings.NPCToys;
		const bool HasNotice = !StageDraftNotice.IsNone();
		if (HasNotice || (Host && Dirty))
		{
			RoundedPanel(HasNotice ? Accent : TBPaper::Teal, X + 32 * S, Y + 658 * S, 7 * S, 7 * S, 3.5f, 0);
			Wrapped(TBText::String(HasNotice ? *StageDraftNotice.ToString() : TEXT("Stage.UnsavedRules")),
			    48, 654, 620, .78f, 2, HasNotice ? Accent : Muted);
		}
		auto Action = [&](FName Id, const TCHAR* Key, float PX, float Width, FLinearColor Color)
		{
			constexpr float PY = 637, Height = 46;
			const bool Over = Hovered(PX, PY, Width, Height);
			const float FaceY = PY + (Over ? PointerDown ? 2.f : -2.f : 0.f);
			PaperSign(Color, X + PX * S, Y + FaceY * S, Width * S, Height * S);
			const FString Caption = TBText::String(Key);
			const float Leading = Id == TEXT("SaveStage") ? 22.f : 0.f;
			float TW = 0, TH = 0;
			GetTextSize(Caption, TW, TH, HeadingFont, S);
			const float Fit = FMath::Min(1.f, (Width - 24 - Leading) * S / FMath::Max(1.f, TW));
			const float Left = X + (PX + (Width - Leading) * .5f) * S - TW * Fit * .5f;
			if (Leading > 0.f) DrawCheck(Left, Y + (FaceY + 16) * S, 14 * S, Ink);
			DrawText(Caption, Ink, Left + Leading * S, Y + (FaceY + Height * .5f) * S + (10 * S - TH * .5f) * Fit, HeadingFont, S * Fit);
			AddHitBox({X + PX * S, Y + PY * S}, {Width * S, Height * S}, Id, true);
		};
		if (Host)
		{
			Action(TEXT("CancelStage"), TEXT("Stage.Cancel"), 860, 145, Mint);
			Action(TEXT("SaveStage"), TEXT("Stage.SaveAction"), 1023, 145, TBPaper::Gold);
		}
		else Action(TEXT("CancelStage"), TEXT("Keys.Close"), 1023, 145, Mint);
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
	const auto* ResultPlayer = GetOwningPlayerController()->GetPlayerState<ATBPlayerState>();
	const bool MissedRide = MatchState.bVehicleEscapeResult && ResultPlayer && ResultPlayer->Team == ETBTeam::Toy &&
	    !MatchState.EscapedInVehicle(ResultPlayer);
	const bool Celebrate = !ShowError && MatchState.Phase != ETBPhase::Aborted && !MissedRide;
	const auto Fill = Celebrate ? (MatchState.Winner == ETBWinner::Humans ? TBPaper::Teal : TBPaper::Gold) : TBPaper::Paper;
	PaperSign(Fill, X, 24 * S, 680 * S, (ShowError ? 196 : 112) * S);
	if (Celebrate) DrawArt(TEXT("Icon_Crown"), X + 33 * S, 52 * S, 48 * S, 48 * S);
	const FString Title = MatchState.Phase == ETBPhase::Aborted ? TBText::Literal(TEXT("Canvas.GameOver")) :
	    MatchState.bVehicleEscapeResult && ResultPlayer && ResultPlayer->Team == ETBTeam::Toy ?
	        (MatchState.EscapedInVehicle(ResultPlayer) ? TBText::Literal(TEXT("Canvas.Escaped")) : TBText::Literal(TEXT("Canvas.NotEscaped"))) :
	    MatchState.Winner == ETBWinner::Toys ? TBText::Literal(TEXT("Canvas.ToysWin")) : TBText::Literal(TEXT("Canvas.HumansWin"));
	DrawFittedText(Title, TBPaper::Ink, X + 91 * S, 49 * S, 498 * S, 1.75f * S, true, true);
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
	DrawFittedText(Status, TBPaper::Muted, X + 40 * S, 95 * S, 600 * S, .87f * S, false, true);
	auto* PC = Cast<ATBController>(GetOwningPlayerController());
	if (MatchState.Phase != ETBPhase::Results || !PC) return;
	const float Y = 168 * S;
	const auto* Mine = PC->GetPawn<ATBCharacter>();
	if (Mine && Mine->IsCelebrating())
	{
		// Winners keep their own character in view without an extra instruction panel.
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
	const auto* LocalCharacter = Controller.GetPawn<ATBCharacter>();
	// Keep destination labels in the constrained camera Canvas, including letterboxed views.
	// A projection alone is insufficient: a point behind the camera must still guide the turn.
	FVector GuideViewLocation;
	FRotator GuideViewRotation;
	Controller.GetPlayerViewPoint(GuideViewLocation, GuideViewRotation);
	const FVector GuideRight = FRotationMatrix(GuideViewRotation).GetUnitAxis(EAxis::Y);
	TArray<FBox2D> GuidePanels;
	GuidePanels.Emplace(FVector2D(W * .5f - 38 * S, H * .5f - 38 * S),
	    FVector2D(W * .5f + 38 * S, H * .5f + 38 * S));
	if (Child && LocalCharacter && LocalCharacter->HasCarriedToy() && LocalCharacter->StoreStarted >= 0 &&
	    IsValid(MatchState.Box) && IsValid(MatchState.Box->InteractionPoint))
		GuidePanels.Emplace(FVector2D(W * .5f - 172 * S, H * .76f - 8 * S),
		    FVector2D(W * .5f + 172 * S, H * .76f + 84 * S));
	auto DirectionCue = [&](const FVector& Destination, const TCHAR* LabelKey, FLinearColor Color,
	    float EdgeLane, bool bReached, const FString& Detail = FString())
	{
		if (!Child || !LocalCharacter || MatchState.IsPreparing() || Destination.ContainsNaN()) return;
		const FVector Delta = Destination - GuideViewLocation;
		const FVector Projected = Canvas->Project(Destination, false);
		const bool bInFront = FVector::DotProduct(Delta, GuideViewRotation.Vector()) > 0;
		const float GuideTop = FMath::Max(190 * S, BannerY + 8 * S);
		const bool bOnScreen = bInFront && !Projected.ContainsNaN() &&
		    Projected.X > 28 * S && Projected.X < W - 28 * S &&
		    Projected.Y > GuideTop && Projected.Y < H - 100 * S;
		// A destination ahead but above/below the view should not ask for a sideways turn.
		const bool bVerticalEdge = bInFront && !Projected.ContainsNaN() && !bOnScreen &&
		    Projected.X > 28 * S && Projected.X < W - 28 * S;
		const bool bAnchored = bOnScreen || bVerticalEdge;
		if (bReached && !bAnchored && Detail.IsEmpty()) return;
		const bool bLeft = FVector::DotProduct(Delta, GuideRight) < 0;
		const float Width = (Detail.IsEmpty() ? 82.f : 156.f) * S;
		const float Height = (Detail.IsEmpty() ? 26.f : 38.f) * S;
		const FVector2D Pin(Projected.X, FMath::Clamp(Projected.Y, double(GuideTop), double(H - 100 * S)));
		const float X = bAnchored ? FMath::Clamp(float(Pin.X) - Width * .5f, 18 * S, W - Width - 18 * S) :
		    bLeft ? 18 * S : W - Width - 18 * S;
		const float MinY = GuideTop, MaxY = H - Height - 100 * S;
		float Y = FMath::Clamp(bAnchored ? float(Pin.Y) + 12 * S : H * EdgeLane, MinY, MaxY);
		// Labels may move aside, but their pin and connector still identify the real world point.
		const float CandidateY[] = {Y, Y - Height - 28 * S, Y + Height + 28 * S,
		    H * EdgeLane, MinY, MaxY};
		for (float Candidate : CandidateY)
		{
			Candidate = FMath::Clamp(Candidate, MinY, MaxY);
			const FBox2D Bounds(FVector2D(X - 6 * S, Candidate - 6 * S),
			    FVector2D(X + Width + 6 * S, Candidate + Height + 6 * S));
			if (!GuidePanels.ContainsByPredicate([&](const FBox2D& Other) { return Bounds.Intersect(Other); }))
			{
				Y = Candidate;
				break;
			}
		}
		GuidePanels.Emplace(FVector2D(X, Y), FVector2D(X + Width, Y + Height));
		auto Stroke = [&](FVector2D A, FVector2D B, float Thickness, FLinearColor Tint)
		{
			const FVector2D Along = (B - A).GetSafeNormal();
			const FVector2D Across(-Along.Y * Thickness * .5f, Along.X * Thickness * .5f);
			DrawSmoothPolygon({A - Across, A + Across, B + Across, B - Across}, Tint);
		};
		auto Dot = [&](FVector2D Center, float Radius, FLinearColor Tint)
		{
			TArray<FVector2D> Points;
			Points.Reserve(24);
			for (int32 I = 0; I < 24; ++I)
			{
				const float Angle = I * UE_TWO_PI / 24;
				Points.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
			}
			DrawSmoothPolygon(Points, Tint);
		};
		if (bAnchored)
		{
			const FVector2D Join(FMath::Clamp(Pin.X, double(X + 12 * S), double(X + Width - 12 * S)),
			    Pin.Y < Y ? Y : Y + Height);
			Stroke(Pin, Join, 1.4f * S, Color.CopyWithNewOpacity(.65f));
			if (bVerticalEdge)
			{
				const float Sign = Projected.Y < Pin.Y ? -1.f : 1.f;
				const FVector2D Tip = Pin + FVector2D(0, 4 * Sign) * S;
				Stroke(Pin + FVector2D(-6, -2 * Sign) * S, Tip, 3.5f * S, Color);
				Stroke(Tip, Pin + FVector2D(6, -2 * Sign) * S, 3.5f * S, Color);
				Dot(Tip, 1.75f * S, Color);
			}
			else
			{
				Dot(Pin, 4 * S, TBPaper::Outline);
				Dot(Pin, 2.3f * S, Color);
			}
		}
		RoundedPanel(TBPaper::Cream.CopyWithNewOpacity(.92f), X, Y, Width, Height, 5, 1.2f);
		float TextX = X + 7 * S, TextWidth = Width - 14 * S;
		if (!bAnchored && !bReached)
		{
			const FVector2D Center(X + (bLeft ? 12 * S : Width - 12 * S), Y + Height * .5f);
			const float Sign = bLeft ? -1.f : 1.f;
			const FVector2D Tip = Center + FVector2D(5 * Sign, 0) * S;
			const FVector2D Top = Center + FVector2D(-3 * Sign, -6) * S;
			const FVector2D Bottom = Center + FVector2D(-3 * Sign, 6) * S;
			Stroke(Top, Tip, 3 * S, Color);
			Stroke(Tip, Bottom, 3 * S, Color);
			Dot(Tip, 1.5f * S, Color);
			if (bLeft) TextX += 16 * S;
			TextWidth -= 16 * S;
		}
		DrawFittedText(TBText::String(LabelKey), Color, TextX, Y + (Detail.IsEmpty() ? 10 : 8) * S,
		    TextWidth, .62f * S, true, true);
		if (!Detail.IsEmpty())
			DrawFittedText(Detail, Ink, X + 7 * S, Y + 25 * S, Width - 14 * S, .60f * S, false, true);
	};
	if (const auto* Mom = ATBMomArrivalZone::FindActive(GetWorld()); Mom && !MatchState.IsPreparing())
	{
		const auto& Event = Mom->State;
		const FLinearColor Warning = TBPaper::Danger;
		Banner(FString::Format(TBText::Literal(TEXT("Canvas.MomSeconds")), {FMath::CeilToInt(Event.Remaining)}), Warning);
		if (Child)
		{
			if (Event.bOccupied)
			{
				GuidePanels.Emplace(FVector2D(W * .5f - 235 * S, H * .60f),
				    FVector2D(W * .5f + 235 * S, H * .60f + 100 * S));
				FramePanel(W*.5f-235*S,H*.60f,470*S,100*S);
				DrawText(FString::Format(TBText::Literal(TEXT("Canvas.Persuading")), {FString::Printf(TEXT("%.1f"), Event.Held), FString::Printf(TEXT("%.1f"), Event.RequiredHold)}),
				         Ink,W*.5f-220*S,H*.60f+19*S,PaperFont,S);
				DrawProgress(Event.Held/FMath::Max(.1f,Event.RequiredHold), W*.5f-220*S, H*.60f+40*S, 440*S, 40*S, TBPaper::Gold);
				DrawText(TBText::Literal(TEXT("Canvas.PersuadePause")),
				         Ink,W*.5f-220*S,H*.60f+86*S,PaperFont,.7f*S);
			}
			if (LocalCharacter)
				DirectionCue(Mom->GetActorLocation() + FVector(0, 0, 60), TEXT("Canvas.MomDestination"),
				    Warning, .36f, Mom->ContainsHuman(*LocalCharacter));
		}
	}
	// Box escape guidance belongs in the existing corner goal, leaving the climb view clear.
	const FLinearColor TeamColor = Child ? TBPaper::HumanBlue : Accent;
	PaperSign(TBPaper::Paper, 24 * HUDScale, 35 * HUDScale, 328 * HUDScale, 72 * HUDScale);
	RoundedPanel(TeamColor, 41 * HUDScale, 54 * HUDScale, 22 * HUDScale, 5 * HUDScale, 2, 0);
	DrawFittedText(TBText::String(Child ? TEXT("Canvas.Human") : TEXT("Canvas.Toy")), TeamColor,
	    71 * HUDScale, 48 * HUDScale, 258 * HUDScale, .77f * HUDScale, true);
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
	DrawFittedText(Objective, Ink, 41 * HUDScale, 74 * HUDScale, 294 * HUDScale, 1.02f * HUDScale, true);
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
	    Child && Character && !MatchState.IsPreparing() && Character->HasCarriedToy() &&
	    IsValid(MatchState.Box) && IsValid(MatchState.Box->InteractionPoint))
	{
		const bool NearBox = MatchState.Box->CanStoreFrom(Character);
		const FVector BoxLocation = MatchState.Box->InteractionPoint->GetComponentLocation();
		const bool Storing = Character->StoreStarted >= 0;
		const float Duration = FMath::Max(TB::Rules().Box.StoreSeconds, .01f);
		const float Elapsed =
		    Storing
		        ? FMath::Clamp(float(MatchState.GetServerWorldTimeSeconds() - Character->StoreStarted), 0.f, Duration)
		        : 0.f;
		const FString InteractKey = TB::KeyLabel(PC, TB::Keys::HumanInteract);
		const FString Prompt = !Storing && NearBox
		    ? FString::Format(TBText::Literal(TEXT("Canvas.StorePrompt")), {*InteractKey, FMath::RoundToInt(Duration)})
		    : FString();
		DirectionCue(BoxLocation + FVector(0, 0, 60), TEXT("Canvas.BoxDestination"), TBPaper::HumanBlue,
		    .52f, NearBox, Prompt);
		if (Storing)
		{
			FramePanel(W * .5f - 164 * S, H * .76f, 328 * S, 76 * S);
			DrawProgress(Elapsed / Duration, W * .5f - 146 * S, H * .76f + 40 * S, 292 * S, 27 * S, TBPaper::Gold);
			DrawFittedText(TBText::String(TEXT("Canvas.StoreProgress")), Ink, W * .5f - 146 * S,
			    H * .76f + 18 * S, 292 * S, .95f * S);
		}
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
	    TEXT("Card_HangingActive"), TEXT("Progress_Yellow"), TEXT("Progress_Track"),
	    TEXT("Button_Gear"), TEXT("Button_Leave"), TEXT("Mark_TriangleBrown"), TEXT("Mark_TriangleYellow"), TEXT("Label_You"),
	    TEXT("Frame_SquareBrown"), TEXT("Frame_SquareLavender"), TEXT("Gauge_Green"), TEXT("Gauge_Orange"), TEXT("Gauge_Gray"),
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
                      bool ScalePosition, bool Outlined)
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
	Item.bOutlined = Outlined;
	Item.OutlineColor = TBPaper::Ink.CopyWithNewOpacity(Color.A);
	Canvas->DrawItem(Item);
}

void ATBHUD::DrawArt(FName Name, float X, float Y, float W, float H, FLinearColor Tint)
{
	const auto* Found = ArtTextures.Find(Name);
	if (!Found || !*Found || W <= 0 || H <= 0) return;
	UTexture2D* Texture = Found->Get();
	const float Scale = FMath::Min(W / FMath::Max(1, Texture->GetSizeX()), H / FMath::Max(1, Texture->GetSizeY()));
	const float AW = Texture->GetSizeX() * Scale, AH = Texture->GetSizeY() * Scale;
	// Some illustrations touch the source border. Sample inside the displayed mip to prevent wrap bleed.
	const float InsetU = .5f / FMath::Max(2.f, FMath::Min(AW, float(Texture->GetSizeX())));
	const float InsetV = .5f / FMath::Max(2.f, FMath::Min(AH, float(Texture->GetSizeY())));
	DrawTexture(Texture, X + (W - AW) * .5f, Y + (H - AH) * .5f, AW, AH,
	    InsetU, InsetV, 1.f - 2.f * InsetU, 1.f - 2.f * InsetV, Tint);
}

void ATBHUD::DrawFittedText(const FString& Text, FLinearColor Color, float X, float Y, float Width,
                           float Scale, bool Heading, bool Center, bool Outlined)
{
	UFont* Font = Heading && HeadingFont ? HeadingFont.Get() : PaperFont.Get();
	float TW = 0, TH = 0;
	GetTextSize(Text, TW, TH, Font, Scale);
	const float Fit = FMath::Min(1.f, Width / FMath::Max(1.f, TW));
	DrawText(Text, Color, X + (Center ? (Width - TW * Fit) * .5f : 0.f), Y, Font, Scale * Fit, false, Outlined);
}

void ATBHUD::DrawCheck(float X, float Y, float Size, FLinearColor Color)
{
	auto Point = [&](float U, float V) { return FVector2D(X + U * Size, Y + V * Size); };
	DrawSmoothPolygon({Point(0, .52f), Point(.13f, .38f), Point(.39f, .64f), Point(.39f, .91f)}, Color);
	DrawSmoothPolygon({Point(.39f, .64f), Point(.87f, .13f), Point(1, .26f), Point(.39f, .91f)}, Color);
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
	const bool Pressed = Hovered && PC->IsInputKeyDown(EKeys::LeftMouseButton);
	const float Lift = Enabled ? (Pressed || (Id == LastClickedButton && Age >= 0 && Age < .14)
	    ? -1.5f : Hovered ? 1.25f : 0.f) * S : 0.f;
	const bool ReadyAction = Id == TEXT("Ready"), StartAction = Id == TEXT("Start");
	FLinearColor Active = Destructive ? TBPaper::Danger : Id == TEXT("Child") ? TBPaper::HumanBlue :
	    Id == TEXT("Toy") ? TBPaper::Accent : TBPaper::Teal;
	if (ReadyAction || StartAction)
	{
		const FLinearColor Fill = !Enabled ? TBPaper::Paper : StartAction ? TBPaper::Gold :
		    Selected ? TBPaper::Cream : TBPaper::Teal;
		PaperSign(Hovered ? FMath::Lerp(Fill, TBPaper::Paper, .16f) : Fill, X, Y - Lift, W, H);
		const bool ShowKey = Enabled && PC;
		const FString ActionLabel = ReadyAction ? Selected ? TBText::Literal(TEXT("Canvas.Unready")) : TBText::Literal(TEXT("Canvas.Ready")) :
		    Enabled ? TBText::Literal(TEXT("Canvas.Start")) : PC && !PC->HasAuthority() ? Label : TBText::String(TEXT("Canvas.Waiting"));
		const FString Key = ShowKey ? TB::KeyLabel(PC, ReadyAction ? TB::Keys::LobbyReady : TB::Keys::LobbyStart) : FString();
		float LabelW = 0, LabelH = 0, KeyW = 0, KeyH = 0;
		GetTextSize(ActionLabel, LabelW, LabelH, HeadingFont, S);
		if (ShowKey) GetTextSize(Key, KeyW, KeyH, HeadingFont, .8f * S);
		const float Chip = ShowKey ? FMath::Clamp(KeyW + 12 * S, 20 * S, W * .32f) : 0.f;
		const float Leading = ShowKey ? Chip + 7 * S : 0.f;
		const float Fit = FMath::Min(1.f, (W - 22 * S - Leading) / FMath::Max(LabelW, 1.f));
		const float Left = X + (W - Leading - LabelW * Fit) * .5f;
		if (ShowKey) DrawKeycap(Key, Left, Y + (H - 22 * S) * .5f - Lift, Chip, 22 * S);
		DrawText(ActionLabel, Enabled ? TBPaper::Ink : TBPaper::Muted, Left + Leading,
		    Y + H * .5f - Lift + (10 * S - LabelH * .5f) * Fit, HeadingFont, S * Fit);
	}
	else
	{
		const bool Primary = Id == TEXT("ChangeStage") || Id == TEXT("Resume") || Id == TEXT("RetryLobbyReturn") ||
		    Id == TEXT("DraftNextStage");
		const bool Secondary = Id == TEXT("CloseSettings") || Id == TEXT("OpenOptions");
		const bool Confirm = Id == TEXT("ConfirmKick") || Id == TEXT("ConfirmLeave");
		const FLinearColor Base = !Enabled ? TBPaper::Paper : Confirm ? FLinearColor::FromSRGBColor(FColor(243, 163, 136)) : Selected ? Active :
		    Primary ? TBPaper::Gold : Secondary ? TBPaper::Teal : TBPaper::Cream;
		const FLinearColor Fill = Hovered && !Pressed ? FMath::Lerp(Base, TBPaper::Paper, .16f) : Base;
		PaperSign(Fill, X, Y - Lift, W, H);
		float TextScale = FMath::Min(1.05f, H / (40 * S)) * S;
		if (H < 30 * S)
		{
			TextScale = FMath::Max(.75f * S, TextScale);
		}
		const bool Heading = H >= 30 * S;
		float TW = 0, TH = 0;
		GetTextSize(Label, TW, TH, Heading ? HeadingFont.Get() : PaperFont.Get(), TextScale);
		const float LeftInset = 12.f * S;
		const float TextWidth = W - LeftInset - 12 * S;
		const float Fit = FMath::Min(1.f, TextWidth / FMath::Max(1.f, TW));
		const float TextY = Y + (H - TH * Fit) * .5f + 10 * TextScale * Fit - Lift;
		const bool LightText = Selected && (Id == TEXT("Child") || Id == TEXT("Toy") || Destructive);
		DrawFittedText(Label, !Enabled ? TBPaper::Muted : LightText ? TBPaper::Paper :
		    Destructive && !Confirm ? TBPaper::Danger : TBPaper::Ink,
		    X + LeftInset, TextY, TextWidth, TextScale, Heading, true);
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

void ATBHUD::DrawSlicedArt(UTexture2D* Texture, float X, float Y, float W, float H, float Edge,
                          FVector2D UVEdge, FLinearColor Tint)
{
	Edge = FMath::Min(Edge, .5f * FMath::Min(W, H));
	const float PX[] = {X, X + Edge, X + W - Edge, X + W};
	const float PY[] = {Y, Y + Edge, Y + H - Edge, Y + H};
	const double U[] = {0, UVEdge.X, 1 - UVEdge.X, 1};
	const double V[] = {0, UVEdge.Y, 1 - UVEdge.Y, 1};
	for (int32 Row = 0; Row < 3; ++Row)
		for (int32 Col = 0; Col < 3; ++Col)
			DrawTexture(Texture, PX[Col], PY[Row], PX[Col + 1] - PX[Col], PY[Row + 1] - PY[Row],
			    U[Col], V[Row], U[Col + 1] - U[Col], V[Row + 1] - V[Row], Tint);
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
	const FVector2D UVEdge(.25, .25);
	DrawSlicedArt(Frame->Get(), X + 3 * S, Y + 5 * S, W, H, Edge, UVEdge, TBPaper::Shadow);
	DrawSlicedArt(Frame->Get(), X, Y, W, H, Edge, UVEdge);
}

void ATBHUD::PaperSign(FLinearColor Fill, float X, float Y, float W, float H)
{
	if (!Canvas || W <= 0.f || H <= 0.f) return;
	const float S = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	// The title's illustrated buttons are gently bowed paper labels. Keep all text live.
	static const TArray<FVector2D> Edge = []
	{
		TArray<FVector2D> Points;
		// Match the shallow, continuous outline used by the menu sign.
		FVector2D Start(.013, .063);
		Points.Add(Start);
		auto Curve = [&](FVector2D Control, FVector2D End, int32 Steps)
		{
			for (int32 Step = 1; Step <= Steps; ++Step)
			{
				const float T = float(Step) / Steps, U = 1.f - T;
				Points.Add(U * U * Start + 2 * U * T * Control + T * T * End);
			}
			Start = End;
		};
		Curve({.483, .125}, {.979, 0}, 12);
		Curve({.988, 0}, {.988, .047}, 4);
		Curve({.992, .46}, {.996, .859}, 2);
		Curve({.996, .891}, {.988, .891}, 4);
		Curve({.492, .984}, {.009, .906}, 12);
		Curve({0, .906}, {0, .875}, 4);
		Curve({.002, .49}, {.004, .110}, 2);
		Curve({.004, .063}, Points[0], 4);
		return Points;
	}();
	auto Layer = [&](FLinearColor Color, float LX, float LY, float LW, float LH, bool Shade = false)
	{
		TArray<FVector2D> Points;
		Points.Reserve(Edge.Num());
		for (const FVector2D& Point : Edge) Points.Emplace(LX + Point.X * LW, LY + Point.Y * LH);
		DrawSmoothPolygon(Points, Color, Shade);
	};
	const float Border = FMath::Min(2.f * S, FMath::Min(W, H) * .09f);
	Layer(TBPaper::Outline.CopyWithNewOpacity(.82f * Fill.A), X + S, Y + 2.5f * S, W, H);
	Layer(TBPaper::Outline.CopyWithNewOpacity(Fill.A), X, Y, W, H);
	const FLinearColor Stock = Fill == TBPaper::Teal ? FLinearColor::FromSRGBColor(FColor(128, 203, 188)) :
	    Fill == TBPaper::Cream ? FLinearColor::FromSRGBColor(FColor(250, 232, 194)) : Fill;
	Layer(Stock, X + Border, Y + Border, W - 2 * Border, H - 2 * Border, true);
}

// Canvas triangles do not inherit the scene's AA. A one-pixel coverage fringe keeps
// the authored curved edge sharp without stair steps or overlapping translucent corners.
// Callers supply an ordered outline whose centre sees every edge (rounded cards / bowed labels).
void ATBHUD::DrawSmoothPolygon(const TArray<FVector2D>& Points, FLinearColor Color, bool ShadePaper)
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
	// A small variation across the face follows the painted paper without adding a texture seam.
	double Top = 0, Height = 1;
	if (ShadePaper)
	{
		const FBox2D Bounds(Boundary.GetData(), Count);
		Top = Bounds.Min.Y;
		Height = FMath::Max(1.0, Bounds.GetSize().Y);
	}
	auto FaceColor = [&](double Y)
	{
		if (!ShadePaper) return Color;
		const float T = FMath::Clamp(float((Y - Top) / Height), 0.f, 1.f);
		return FMath::Lerp(FMath::Lerp(Color, TBPaper::Paper, .11f), FMath::Lerp(Color, TBPaper::Ink, .09f), T)
		    .CopyWithNewOpacity(Color.A);
	};
	const FLinearColor Middle = FaceColor(Center.Y);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const int32 Next = (Index + 1) % Count;
		const FLinearColor A = FaceColor(Inner[Index].Y), B = FaceColor(Inner[Next].Y);
		Add(Center, Inner[Index], Inner[Next], Middle, A, B);
		Add(Inner[Index], Outer[Index], Outer[Next], A, A.CopyWithNewOpacity(0), B.CopyWithNewOpacity(0));
		Add(Inner[Index], Outer[Next], Inner[Next], A, B.CopyWithNewOpacity(0), B);
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
	if (B > 0.f) Layer(Outline, X, Y, W, H, Radius * S);
	Layer(Fill, X + B, Y + B, W - 2 * B, H - 2 * B, FMath::Max(0.f, Radius * S - B));
}

void ATBHUD::DrawKeycap(const FString& Key, float X, float Y, float W, float H)
{
	const float S = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	const float Lip = FMath::Min(3.f * S, H * .12f);
	RoundedPanel(TBPaper::Muted, X, Y, W, H, 6, 1.8f);
	RoundedPanel(TBPaper::Paper, X, Y, W, H - Lip, 6, 1.8f);
	DrawFittedText(Key, TBPaper::Ink, X + 5 * S, Y + (H - Lip) * .5f - 6 * S, W - 10 * S, .95f * S, true, true);
}
