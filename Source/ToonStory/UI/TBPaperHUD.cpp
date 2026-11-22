// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "TBHUD.h"
#include "TBPaperStyle.h"
#include "TBOptionsMenu.h"
#include "TBUIText.h"
#include "Core/TBLobbySettings.h"
#include "Core/TBPlayerState.h"
#include "Core/TBGameState.h"
#include "Core/TBController.h"
#include "Input/TBKeyLabels.h"
#include "Core/TBGameHelpers.h"
#include "Character/TBCharacter.h"
#include "Data/TBRuleData.h"
#include "Data/TBPresentationData.h"
#include "GamePlay/TBBox.h"
#include "GamePlay/Events/TBMomArrivalZone.h"
#include "GamePlay/Items/TBEscapeExit.h"
#include "GamePlay/Items/TBEscapeVehicle.h"
#include "GamePlay/TBRuleMath.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "GlobalRenderResources.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Misc/PackageName.h"

static TAutoConsoleVariable<int32> CVarTBDebugHUD(TEXT("tb.DebugHUD"), 0, TEXT("Show detailed match diagnostics."));

// Each escape stage has one car and one gate. Keep weak references so driving only updates
// the projected position, and a streamed-out or replaced actor is discovered again.
template <typename T>
static T* ResolveEscapeGuideActor(UWorld* World, TWeakObjectPtr<T>& Cached)
{
	if (Cached.IsValid() && Cached->GetWorld() == World) return Cached.Get();
	TActorIterator<T> It(World);
	Cached = It ? *It : nullptr;
	return Cached.Get();
}

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

void ATBHUD::DrawRoomPanel(float X, float Y, float W, float H, float Unit, FLinearColor Fill)
{
	using namespace TBPaper;
	DrawRect(PrintInk, X + 3 * Unit, Y + 4 * Unit, W, H);
	DrawRect(PrintInk, X, Y, W, H);
	DrawRect(Fill, X + 2 * Unit, Y + 2 * Unit, W - 4 * Unit, H - 4 * Unit);
}

void ATBHUD::DrawRoomButton(FName Id, const FString& Caption, float X, float Y, float W, float H, float Unit,
                           FLinearColor Fill, bool Enabled, bool Interactive, float TextScale)
{
	using namespace TBPaper;
	const auto* PC = GetOwningPlayerController();
	float MX = 0, MY = 0;
	const bool Over = Enabled && Interactive && PC && PC->GetMousePosition(MX, MY) &&
	    MX >= X && MX < X + W && MY >= Y && MY < Y + H;
	const float Press = Over && PC->IsInputKeyDown(EKeys::LeftMouseButton) ? Unit * 2 : 0.f;
	const FLinearColor Face = !Enabled ? FLinearColor::FromSRGBColor(FColor(226, 215, 189)) : Over ? PrintAqua : Fill;
	const bool Dark = Fill == PrintSea && !Over && Enabled;
	DrawRoomPanel(X + Press, Y + Press, W - Press, H - Press, Unit * .82f, Face);
	float TW = 0, TH = 0;
	GetTextSize(Caption, TW, TH, HeadingFont, TextScale * Unit);
	const float Fit = FMath::Min3(1.f, (W - 12 * Unit) / FMath::Max(1.f, TW), (H - 4 * Unit) / FMath::Max(1.f, TH));
	const float FS = TextScale * Unit * Fit;
	DrawText(Caption, !Enabled ? PrintQuiet : Dark ? PrintCream : PrintInk, X + (W - TW * Fit + Press) * .5f,
	    Y + (H - TH * Fit + Press) * .5f + 10 * FS, HeadingFont, FS);
	if (Enabled && Interactive) AddHitBox({X, Y}, {W, H}, Id, true);
}

void ATBHUD::DrawLobby()
{
	using namespace TBPaper;
	auto* PC = Cast<ATBController>(GetOwningPlayerController());
	const auto* State = TB::GS(GetWorld());
	if (!PC || !State) return;

	const float S = FMath::Min(Canvas->ClipX / 1440.f, Canvas->ClipY / 810.f);
	const float X = (Canvas->ClipX - 1440 * S) * .5f, Y = (Canvas->ClipY - 810 * S) * .5f;
	const bool Interactive = !bPlayersOpen && !bConfirmingLeave && !(Options.IsValid() && Options->IsInViewport());
	const bool Available = !State->bTravelPending;
	const bool StageOpen = bLobbySettingsOpen && bStagePickerOpen;
	const bool Host = PC->HasAuthority() && PC->IsLocalController() && Available;
	const auto* Local = PC->GetPlayerState<ATBPlayerState>();
	auto Rect = [&](float PX, float PY, float W, float H, FLinearColor Fill)
	{ DrawRect(Fill, X + PX * S, Y + PY * S, W * S, H * S); };
	auto Frame = [&](float PX, float PY, float W, float H, FLinearColor Fill)
	{ DrawRoomPanel(X + PX * S, Y + PY * S, W * S, H * S, S, Fill); };
	auto Dots = [&](float PX, float PY, float W, float H, FLinearColor Dot)
	{
		if (const auto* Tile = ArtTextures.Find(TEXT("RoomHalftone")); Tile && *Tile)
			DrawTexture(Tile->Get(), X + PX * S, Y + PY * S, W * S, H * S, 0, 0, W / 6.f, H / 6.f, Dot);
	};
	auto Label = [&](const FString& Copy, float PX, float CY, float W, float FontSize,
	                 FLinearColor Color = PrintInk, bool Center = false)
	{
		float TW = 0, TH = 0;
		GetTextSize(Copy, TW, TH, HeadingFont, FontSize * S);
		const float Fit = FMath::Min(1.f, W * S / FMath::Max(1.f, TW));
		DrawText(Copy, Color, X + PX * S + (Center ? (W * S - TW * Fit) * .5f : 0.f),
		    Y + CY * S + (10 * FontSize * S - TH * .5f) * Fit, HeadingFont, FontSize * S * Fit);
	};
	auto Button = [&](FName Id, const FString& Copy, float PX, float PY, float W, float H,
	                  FLinearColor Fill = PrintCream, bool Enabled = true, float FontSize = .92f)
	{ DrawRoomButton(Id, Copy, X + PX * S, Y + PY * S, W * S, H * S, S, Fill, Enabled, Interactive, FontSize); };
	auto Number = [](const TCHAR* Key, int32 Count)
	{ return FText::Format(TBText::Get(Key), FText::AsNumber(Count)).ToString(); };
	auto Duration = [&](float Seconds)
	{
		const int32 Value = FMath::RoundToInt(Seconds);
		return Value % 60 == 0 ? Number(TEXT("Stage.Minutes"), Value / 60) : Number(TEXT("Stage.Seconds"), Value);
	};
	auto Preview = [&](bool House, float PX, float PY, float W, float H, float FS)
	{
		const auto* Entry = ArtTextures.Find(House ? TEXT("StagePreview_House") : TEXT("StagePreview_Demo"));
		Rect(PX, PY, W, H, PrintCream);
		const float PW = W - 4, PH = H - 4;
		if (Entry && *Entry && (*Entry)->GetSizeY() > 0)
		{
			const float Aspect = float((*Entry)->GetSizeX()) / (*Entry)->GetSizeY();
			const float U = FMath::Min(1.f, PW / PH / Aspect), V = FMath::Min(1.f, Aspect * PH / PW);
			DrawTexture(Entry->Get(), X + (PX + 2) * S, Y + (PY + 2) * S, PW * S, PH * S,
			    (1 - U) * .5f, (1 - V) * .5f, U, V);
		}
		const FString Name = TBText::String(House ? TEXT("Stage.House") : TEXT("Stage.Demo"));
		float TW = 0, TH = 0;
		GetTextSize(Name, TW, TH, HeadingFont, FS);
		const float NameW = FMath::Min(W - 4.f, TW + 24.f), NameH = TH + 9.f;
		Rect(PX + 2, PY + H - NameH - 2, NameW, NameH, PrintInk.CopyWithNewOpacity(.94f));
		Label(Name, PX + 12, PY + H - NameH * .5f - 2, NameW - 18, FS, PrintCream);
	};

	if (StageOpen)
		DrawRect(FLinearColor::FromSRGBColor(FColor(30, 62, 58, 217)), 0, 0, Canvas->ClipX, Canvas->ClipY);
	const float HeadingW = StageOpen ? 296.f : 174.f;
	DrawSmoothPolygon({{X + 29 * S, Y + 18 * S}, {X + (29 + HeadingW) * S, Y + 18 * S},
	                   {X + (19 + HeadingW) * S, Y + 72 * S}, {X + 29 * S, Y + 72 * S}}, PrintSea);
	Dots(31, 19, HeadingW - 13, 51, PrintInk.CopyWithNewOpacity(.65f));
	Label(TBText::String(StageOpen ? TEXT("Stage.Title") : TEXT("Keys.Lobby")), 45, 44, HeadingW - 32, 1.9f, PrintCream);
	Button(TEXT("OpenOptions"), TBText::String(TEXT("Stage.Options")), 1099, 26, 72, 39, PrintCream, Available);
	Button(TEXT("Players"), TBText::String(TEXT("Lobby.Players")), 1185, 26, 89, 39, PrintCream, Available);
	Button(TEXT("Leave"), TBText::String(TEXT("Stage.Leave")), 1288, 26, 122, 39, PrintCream, Available);

	if (!StageOpen)
	{
		int32 Ready = 0, Players = 0;
		for (APlayerState* Player : State->PlayerArray)
		{
			const auto* Member = Cast<ATBPlayerState>(Player);
			if (!Member || Member->IsABot()) continue;
			Ready += Member->bReady ? 1 : 0;
			++Players;
		}
		if (Local)
		{
			const FString Name = Local->GetPlayerName();
			float TW = 0, TH = 0;
			GetTextSize(Name, TW, TH, HeadingFont, .85f);
			const float NameW = FMath::Clamp(TW + 14, 44.f, 215.f);
			Frame(29, 90, 68 + NameW, 32, PrintCream);
			Rect(36, 95, 54, 22, PrintAqua);
			Label(TBText::String(TEXT("Root.Common.You")), 38, 106, 50, .72f, PrintInk, true);
			Label(Name, 96, 106, NameW - 4, .85f);
		}

		Frame(1058, 106, 352, 464, PrintSea);
		Dots(1061, 109, 346, 458, PrintInk.CopyWithNewOpacity(.6f));
		Rect(1077, 121, 4, 26, PrintHoney);
		Label(TBText::String(TEXT("Stage.Stage")), 1091, 134, 286, 1.22f, PrintCream);
		Preview(State->SelectedStage == TBLobbySettings::HouseStageId, 1077, 156, 315, 268, .98f);
		auto Fact = [&](const TCHAR* Key, const FString& Value, float CY)
		{
			Label(TBText::String(Key), 1078, CY, 225, .84f, PrintCream);
			float TW = 0, TH = 0;
			GetTextSize(Value, TW, TH, HeadingFont, .95f);
			const float Width = FMath::Min(103.f, TW + 1);
			Label(Value, 1392 - Width, CY, Width, .95f, PrintCream);
		};
		Fact(TEXT("Stage.Time"), Duration(State->Settings.Duration), 445);
		Fact(TEXT("Stage.Batteries"), Number(TEXT("Stage.Items"), State->Settings.RequiredItems), 467);
		const int32 NPCs = State->Settings.NPCHumans + State->Settings.NPCToys;
		Fact(TEXT("Lobby.NPC"), NPCs ? FString::FromInt(NPCs) : TBText::String(TEXT("Lobby.None")), 489);
		Button(TEXT("OpenSettings"), TBText::String(TEXT("Lobby.StageRules")), 1077, 516, 315, 39, PrintHoney, Available);

		Frame(1058, 584, 352, 197, PrintStock);
		Rect(1061, 587, 95, 191, FLinearColor::FromSRGBColor(FColor(224, 200, 145)));
		Label(TBText::String(TEXT("Keys.Action.Lobby_Ready")), 1068, 668, 58, .95f, PrintInk, true);
		RoundedPanel(Local && Local->bReady ? PrintAqua : PrintStock, X + 1126 * S, Y + 662 * S, 11 * S, 11 * S, 6, 1.4f, PrintQuiet);
		Label(FText::Format(TBText::Get(TEXT("Stage.RoomPlayers")), Ready, Players).ToString(), 1064, 699, 88, 1.18f, PrintInk, true);
		Button(TEXT("Ready"), TBText::String(Local && Local->bReady ? TEXT("Canvas.Unready") : TEXT("Canvas.Ready")),
		    1168, 596, 228, 81, Local && Local->bReady ? PrintCream : PrintSea, Available && Local);
		const bool Teams = State->Settings.Humans + State->Settings.NPCHumans >= 1 && State->Settings.Toys + State->Settings.NPCToys >= 1;
		const bool AllReady = Players > 0 && Ready == Players && State->Settings.Humans + State->Settings.Toys == Players;
		Button(TEXT("Start"), TBText::String(PC->HasAuthority() ? TEXT("Canvas.Start") : TEXT("Canvas.WaitHost")),
		    1168, 687, 228, 80, PrintHoney, Host && Teams && AllReady);
		// Preserve feedback from failed starts or session operations without obscuring the play area.
		if (!PC->Notice.IsEmpty())
		{
			const FString Notice = TBText::TranslateNotice(PC->Notice);
			Rect(29, 137, 412, 34, PrintInk.CopyWithNewOpacity(.87f));
			Label(Notice, 38, 154, 392, .77f, PrintCream);
		}
		return;
	}

	if (DraftStage == INDEX_NONE || !PC->HasAuthority()) BeginStageDraft();
	NormalizeStageDraft();
	const bool House = DraftStage == TBLobbySettings::HouseStageId;
	const int32 NPCCount = StageDraft.NPCHumans + StageDraft.NPCToys;
	const bool CanAddNPC = DraftPlayerCount + NPCCount < State->PlayerLimit;
	Frame(30, 96, 534, 634, PrintStock);
	const FLinearColor Rule = FLinearColor::FromSRGBColor(FColor(184, 165, 132, 168));
	Rect(48, 99, 1, 627, FLinearColor::FromSRGBColor(FColor(202, 130, 107)));
	constexpr float FirstRule = 111, Pitch = 58.2f;
	for (float LineY = FirstRule; LineY < 729; LineY += Pitch) Rect(33, LineY, 528, 1.1f, Rule);
	auto RowY = [](int32 Row) { return FirstRule + (Row + .5f) * Pitch; };
	auto Stepper = [&](const TCHAR* NameKey, int32 Row, const FString& Value, FName Less, FName More, bool LessOK, bool MoreOK, float LabelInset = 0.f)
	{
		const float CY = RowY(Row);
		Label(TBText::String(NameKey), 62 + LabelInset, CY, 296 - LabelInset, .98f);
		Button(Less, TEXT("−"), 389, CY - 16, 34, 32, PrintCream, Host && LessOK, 1.08f);
		Label(Value, 428, CY, 68, 1.24f, PrintInk, true);
		Button(More, TEXT("+"), 502, CY - 16, 36, 32, PrintCream, Host && MoreOK, 1.08f);
	};
	Label(TBText::String(TEXT("Lobby.Rules")), 62, RowY(0), 430, 1.18f);
	Stepper(TEXT("Stage.Time"), 1, Duration(StageDraft.Duration), TEXT("DraftLessTime"), TEXT("DraftMoreTime"),
	    StageDraft.Duration > TB::Rules().Match.MinDuration, StageDraft.Duration < TB::Rules().Match.MaxDuration);
	Stepper(TEXT("Stage.Batteries"), 2, Number(TEXT("Stage.Items"), StageDraft.RequiredItems), TEXT("DraftFewerItems"), TEXT("DraftMoreItems"),
	    StageDraft.RequiredItems > 1, StageDraft.RequiredItems < TBLobbySettings::StageItemCount(DraftStage));
	Label(TBText::String(TEXT("Canvas.ChooseTeam")), 62, RowY(3), 430, 1.18f);
	const ETBTeam Preferred = Local ? Local->Preference : ETBTeam::None;
	auto TeamButton = [&](FName Id, const TCHAR* Key, float PX, ETBTeam Team)
	{ Button(Id, TBText::String(Key), PX, RowY(4) - 19, 151, 38, Team == Preferred ? PrintAqua : PrintCream, Available && Local); };
	TeamButton(TEXT("Child"), TEXT("Stage.Human"), 62, ETBTeam::Human);
	TeamButton(TEXT("Toy"), TEXT("Stage.Toy"), 225, ETBTeam::Toy);
	TeamButton(TEXT("Either"), TEXT("Canvas.Either"), 387, ETBTeam::None);
	Label(TBText::String(TEXT("Stage.NPCs")), 62, RowY(5), 240, 1.03f);
	Button(TEXT("ToggleDraftNPCs"), TBText::String(NPCCount ? TEXT("Lobby.NPCOn") : TEXT("Lobby.NPCOff")),
	    457, RowY(5) - 19, 81, 38, NPCCount ? PrintAqua : PrintCream, Host && (NPCCount || CanAddNPC), 1.f);
	if (NPCCount)
	{
		Stepper(TEXT("Stage.Human"), 6, FString::FromInt(StageDraft.NPCHumans), TEXT("DraftFewerNPCHumans"), TEXT("DraftMoreNPCHumans"), StageDraft.NPCHumans > 0, CanAddNPC, 16.f);
		Stepper(TEXT("Stage.Toy"), 7, FString::FromInt(StageDraft.NPCToys), TEXT("DraftFewerNPCToys"), TEXT("DraftMoreNPCToys"), StageDraft.NPCToys > 0, CanAddNPC, 16.f);
	}
	else if (!CanAddNPC) Label(TBText::String(TEXT("Stage.NPCFullHint")), 62, RowY(6), 470, .85f, PrintQuiet);

	Frame(586, 96, 824, 634, PrintSea);
	Dots(589, 99, 818, 628, PrintAqua.CopyWithNewOpacity(.13f));
	Frame(900, 120, 196, 37, PrintHoney);
	Label(TBText::String(TEXT("Stage.ChooseStage")), 907, 139, 182, 1.12f, PrintInk, true);
	Preview(House, 613, 169, 770, 494, 1.4f);
	Button(TEXT("DraftPreviousStage"), TEXT("‹"), 614, 676, 33, 32, PrintCream, Host, 1.16f);
	Button(TEXT("DraftNextStage"), TEXT("›"), 1350, 676, 33, 32, PrintCream, Host, 1.16f);
	Label(House ? TEXT("02 / 02") : TEXT("01 / 02"), 909, 692, 178, 1.f, PrintCream, true);

	if (!StageDraftNotice.IsNone()) Label(TBText::String(*StageDraftNotice.ToString()), 40, 765, 930, .84f, PrintCream);
	if (Host) Button(TEXT("CancelStage"), TBText::String(TEXT("Stage.Cancel")), 1085, 747, 154, 39);
	Button(Host ? TEXT("SaveStage") : TEXT("CancelStage"), TBText::String(Host ? TEXT("Stage.SaveAction") : TEXT("Keys.Close")),
	    1254, 747, 156, 39, PrintHoney);
}

void ATBHUD::DrawResults(const ATBGameState& MatchState)
{
	const float S = FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 800.f);
	const float X = Canvas->ClipX * .5f - 340 * S;
	const auto* Viewer = Cast<ATBController>(GetOwningPlayerController());
	if (MatchState.Phase == ETBPhase::Results && (MatchState.Winner == ETBWinner::None ||
	    !(Viewer ? Viewer->IsResultShown() : MatchState.IsResultRevealed()))) return;
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
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	const float U = FMath::Min(W / 1600.f, H / 900.f);
	const float ActionW = 222 * U, ActionH = 40 * U;
	const float ActionX = (W - ActionW) * .5f, ActionY = H * .878f - ActionH;
	TGuardValue<bool> SubtitleText(bDrawingMatchSubtitles, true);
	const FLinearColor Ink = FLinearColor::FromSRGBColor(FColor(255, 249, 223));
	const FLinearColor Gold = FLinearColor::FromSRGBColor(FColor(255, 220, 136));
	const FLinearColor Mint = FLinearColor::FromSRGBColor(FColor(176, 228, 216));
	const FLinearColor Alert = FLinearColor::FromSRGBColor(FColor(255, 164, 138));
	const auto* Player = Controller.GetPlayerState<ATBPlayerState>();
	if (Controller.ShouldHidePreparationView())
	{
		if (!Controller.IsShowingEntranceApproach()) DrawRect(FLinearColor::Black, 0, 0, W, H);
		const FString Wait = Player && Player->Team == ETBTeam::Human && MatchState.HidePreparation.bValid && MatchState.HidePreparation.bHiding
		    ? FString::Format(TBText::Literal(TEXT("Canvas.WaitHide")), {FMath::CeilToInt(MatchState.HideRemaining())})
		    : TBText::Literal(TEXT("Canvas.Preparing"));
		if (Controller.IsShowingEntranceApproach())
		{
			DrawSubtitleShade(W * .5f - 230 * S, H - 92 * S, 460 * S, 44 * S);
			DrawFittedText(FString::Format(TBText::Literal(TEXT("Canvas.EntranceSeconds")), {FMath::CeilToInt(MatchState.HideRemaining())}),
			    Ink, W * .5f - 218 * S, H - 76 * S, 436 * S, S, true, true);
		}
		else DrawText(Wait, TBPaper::Cream, W * .5f - 220 * S, H * .5f, PaperFont, 1.3f * S);
		return;
	}
	DrawGazeEffects(Controller);
	float BannerY = 128 * S;
	auto Banner = [&](const FString& Label, FLinearColor Color)
	{
		const float TextScale = .9f * S;
		float TW = 0, TH = 0;
		GetTextSize(Label, TW, TH, HeadingFont, TextScale);
		const float Width = FMath::Min(520 * S, TW + 38 * S);
		const float Left = (W - Width) * .5f;
		DrawSubtitleShade(Left, BannerY, Width, 34 * S);
		DrawFittedText(Label, Color, Left + 12 * S, BannerY + 11 * S,
		    Width - 24 * S, TextScale, true, true);
		BannerY += 42 * S;
	};
	if (MatchState.IsPreparing())
		Banner(FString::Format(TBText::Literal(TEXT("Canvas.HideSeconds")), {FMath::CeilToInt(MatchState.HideRemaining())}), Gold);
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
	if (LocalCharacter && ((LocalCharacter->ContactItem && !LocalCharacter->IsGazeFrozen()) ||
	    (Child && LocalCharacter->StoreStarted >= 0 && LocalCharacter->HasCarriedToy())))
		GuidePanels.Emplace(FVector2D(ActionX - 10 * U, ActionY - 8 * U),
		    FVector2D(ActionX + ActionW + 6 * U, ActionY + ActionH + 4 * U));
	auto DirectionCue = [&](const FVector& Destination, const TCHAR* LabelKey, FLinearColor Color,
	    float EdgeLane, bool bReached, const FString& Detail = FString())
	{
		if (!LocalCharacter || MatchState.IsPreparing() || Destination.ContainsNaN()) return;
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
		const FString Label = TBText::String(LabelKey);
		float LabelW = 0, LabelH = 0;
		GetTextSize(Label, LabelW, LabelH, HeadingFont, .81f * U);
		const float Width = Detail.IsEmpty() ? FMath::Clamp(LabelW + (bAnchored ? 20 : 41) * U, 51 * U, 126 * U) : 166 * U;
		const float Height = (Detail.IsEmpty() ? 33.f : 46.f) * U;
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
		DrawSubtitleShade(X, Y, Width, Height, bAnchored ? ESubtitleShade::Center : ESubtitleShade::Uniform);
		float TextX = X + 8 * U, TextWidth = Width - 16 * U;
		if (!bAnchored && !bReached)
		{
			const FVector2D Center(X + (bLeft ? 13 * U : Width - 13 * U), Y + Height * .5f);
			const float Sign = bLeft ? -1.f : 1.f;
			const FVector2D Tip = Center + FVector2D(4 * Sign, 0) * U;
			Stroke(Center + FVector2D(-2 * Sign, -5) * U, Tip, 3 * U, Color);
			Stroke(Tip, Center + FVector2D(-2 * Sign, 5) * U, 3 * U, Color);
			if (bLeft) TextX += 17 * U;
			TextWidth -= 17 * U;
		}
		DrawFittedText(Label, Ink, TextX, Y + (Detail.IsEmpty() ? 15 : 11) * U,
		    TextWidth, .81f * U, true, true);
		if (!Detail.IsEmpty())
			DrawFittedText(Detail, Ink, X + 7 * U, Y + 31 * U, Width - 14 * U, .73f * U, false, true);
	};
	if (const auto* Mom = ATBMomArrivalZone::FindActive(GetWorld()); Mom && !MatchState.IsPreparing())
	{
		const auto& Event = Mom->State;
		const FLinearColor Warning = Alert;
		Banner(FString::Format(TBText::Literal(TEXT("Canvas.MomSeconds")), {FMath::CeilToInt(Event.Remaining)}), Warning);
		if (Child)
		{
			if (Event.bOccupied)
			{
				GuidePanels.Emplace(FVector2D(W * .5f - 235 * S, H * .60f),
				    FVector2D(W * .5f + 235 * S, H * .60f + 100 * S));
				const float Left = W * .5f - 150 * S, Top = H * .60f;
				DrawSubtitleShade(Left, Top, 300 * S, 70 * S);
				DrawFittedText(FString::Format(TBText::Literal(TEXT("Canvas.Persuading")), {FString::Printf(TEXT("%.1f"), Event.Held), FString::Printf(TEXT("%.1f"), Event.RequiredHold)}),
				    Ink, Left + 14 * S, Top + 12 * S, 272 * S, .85f * S, true);
				DrawProgress(Event.Held / FMath::Max(.1f, Event.RequiredHold), Left + 14 * S, Top + 33 * S, 272 * S, 4 * S, Gold);
				DrawFittedText(TBText::Literal(TEXT("Canvas.PersuadePause")), Ink, Left + 14 * S, Top + 52 * S, 272 * S, .72f * S);
			}
			if (LocalCharacter)
				DirectionCue(Mom->GetActorLocation() + FVector(0, 0, 60), TEXT("Canvas.MomDestination"),
				    Warning, .36f, Mom->ContainsHuman(*LocalCharacter));
		}
	}
	// Use a short, persistent objective. World feedback and emotes never displace it.
	const TCHAR* Objective = MatchState.bEscapePhase ? TEXT("Canvas.StopEscape") : TEXT("Canvas.HumanObjective");
	if (!Child)
	{
		if (Player && Player->ToyState == ETBToyState::Boxed)
			Objective = TEXT("Canvas.BoxClimb");
		else if (MatchState.IsPreparing())
			Objective = TEXT("Canvas.HideObjective");
		else if (MatchState.bEscapePhase)
			Objective = LocalCharacter && IsValid(LocalCharacter->EscapeVehicle)
			    ? TEXT("Canvas.ExitObjective") : TEXT("Canvas.VehicleObjective");
		else
			Objective = TEXT("Canvas.ToyObjective");
	}
	const FString Goal = TBText::String(Objective);
	float GoalTextW = 0, GoalTextH = 0;
	GetTextSize(Goal, GoalTextW, GoalTextH, HeadingFont, 1.04f * U);
	const float GoalX = 28 * U, GoalY = H * .032f;
	const float GoalW = FMath::Clamp(GoalTextW + 32 * U, 200 * U, 384 * U);
	const FLinearColor TeamColor = Child ? Mint : FLinearColor::FromSRGBColor(FColor(255, 209, 136));
	DrawSubtitleShade(GoalX - 8 * U, GoalY - 3 * U, GoalW + 16 * U, 53 * U, ESubtitleShade::Uniform);
	DrawFittedText(TBText::String(Child ? TEXT("Canvas.Human") : TEXT("Canvas.Toy")), TeamColor,
	    GoalX + 12 * U, GoalY + 9 * U, GoalW - 28 * U, .73f * U, true);
	DrawFittedText(Goal, Ink, GoalX + 12 * U, GoalY + 30 * U, GoalW - 26 * U, 1.04f * U, true);

	const int32 Seconds = FMath::Max(0, FMath::CeilToInt(MatchState.Remaining()));
	const bool Urgent = Seconds <= 10;
	const FString Clock = FString::Printf(TEXT("%02d:%02d"), Seconds / 60, Seconds % 60);
	const float ClockX = W * .5f - 55 * U, ClockY = H * .027f;
	DrawSubtitleShade(ClockX - 17 * U, ClockY - 5 * U, 144 * U, 44 * U);
	DrawArt(TEXT("Icon_Hourglass"), ClockX + 5 * U, ClockY + 6 * U, 18 * U, 21 * U);
	float ClockW = 0, ClockH = 0;
	GetTextSize(Clock, ClockW, ClockH, NumericFont, 1.62f * U);
	const float Fit = FMath::Min(1.f, 86 * U / FMath::Max(1.f, ClockW));
	DrawText(Clock, Urgent ? Alert : Ink, ClockX + 30 * U, ClockY + 16 * U + (16.2f * U - ClockH * .5f) * Fit,
	    NumericFont, 1.62f * U * Fit);

	// Size the narrow counter to its widest live row (including custom NPC counts).
	const int32 ToyTotal = MatchState.Settings.Toys + MatchState.Settings.NPCToys;
	const float CountScale = 1.18f * U, TotalScale = .68f * U;
	auto TallyWidth = [&](int32 Current, int32 Total)
	{
		float CurrentW = 0, TotalW = 0, TextH = 0;
		GetTextSize(FString::FromInt(Current), CurrentW, TextH, NumericFont, CountScale);
		GetTextSize(FString::Printf(TEXT("/ %d"), Total), TotalW, TextH, PaperFont, TotalScale);
		return 43 * U + CurrentW + TotalW;
	};
	const float CountW = FMath::Max(TallyWidth(MatchState.Collected, MatchState.Settings.RequiredItems),
	    TallyWidth(MatchState.BoxedCount, ToyTotal));
	const float CountX = W - 27 * U - CountW, CountY = H * .032f;
	auto Tally = [&](FName Icon, int32 Current, int32 Total, float RowY)
	{
		DrawArt(Icon, CountX + 3 * U, RowY, 23 * U, 23 * U);
		const FString Value = FString::FromInt(Current);
		float ValueWidth = 0, ValueHeight = 0;
		GetTextSize(Value, ValueWidth, ValueHeight, NumericFont, CountScale);
		const float ValueX = CountX + 32 * U;
		DrawText(Value, Ink, ValueX, RowY + 12 * U, NumericFont, CountScale);
		const float TotalX = ValueX + ValueWidth + 5 * U;
		DrawFittedText(FString::Printf(TEXT("/ %d"), Total), FLinearColor::FromSRGBColor(FColor(227, 224, 200)), TotalX, RowY + 18 * U,
		    CountX + CountW - TotalX, TotalScale);
	};
	DrawSubtitleShade(CountX - 12 * U, CountY - 4 * U, CountW + 22 * U, 65 * U, ESubtitleShade::Uniform);
	Tally(TEXT("Icon_BatteryPickup"), MatchState.Collected, MatchState.Settings.RequiredItems, CountY + 2 * U);
	Tally(MatchState.BoxedCount > 0 ? TEXT("Icon_ToyBoxFull") : TEXT("Icon_ToyBoxEmpty"), MatchState.BoxedCount,
	    ToyTotal, CountY + 33 * U);
	auto InteractionProgress = [&](const TCHAR* LabelKey, float Progress)
	{
		DrawSubtitleShade(ActionX - 23 * U, ActionY - 6 * U, ActionW + 46 * U, ActionH + 12 * U);
		DrawFittedText(TBText::String(LabelKey), Ink, ActionX + 12 * U, ActionY + 12 * U, ActionW - 24 * U, .83f * U, true);
		DrawProgress(Progress, ActionX + 12 * U, ActionY + 29 * U, ActionW - 24 * U, 4 * U, Gold);
	};
	if (LocalCharacter && LocalCharacter->ContactItem && !LocalCharacter->IsGazeFrozen())
		InteractionProgress(TEXT("Canvas.Collecting"), LocalCharacter->ItemProgress);
	if (Player && Player->Team == ETBTeam::Toy && MatchState.Box && MatchState.Box->Rescuers > 0)
	{
		DrawSubtitleShade(W * .5f - 150 * S, BannerY, 300 * S, 54 * S);
		DrawFittedText(FString::Format(TBText::Literal(TEXT("Canvas.Rescuing")), {FMath::RoundToInt(MatchState.Box->RescueProgress * 100)}), Ink,
		    W * .5f - 134 * S, BannerY + 14 * S, 268 * S, 1.05f * S, false, true);
		DrawProgress(MatchState.Box->RescueProgress, W * .5f - 130 * S, BannerY + 38 * S, 260 * S, 4 * S, Mint);
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
		DirectionCue(BoxLocation + FVector(0, 0, 60), TEXT("Canvas.BoxDestination"), Mint,
		    .52f, NearBox, Prompt);
		if (Storing) InteractionProgress(TEXT("Canvas.StoreProgress"), Elapsed / Duration);
	}
	if (MatchState.bEscapePhase && !MatchState.IsPreparing() && Player && Player->Team == ETBTeam::Toy &&
	    Player->ToyState == ETBToyState::Free && LocalCharacter && !LocalCharacter->Carrier)
	{
		if (IsValid(LocalCharacter->EscapeVehicle))
		{
			const auto* Exit = ResolveEscapeGuideActor(GetWorld(), EscapeGuideExit);
			if (Exit && IsValid(Exit->ExitArea))
				DirectionCue(Exit->ExitArea->GetComponentLocation(), TEXT("Canvas.ExitDestination"),
				    Gold, .52f, false);
		}
		else
		{
			const auto* Car = ResolveEscapeGuideActor(GetWorld(), EscapeGuideVehicle);
			if (Car && IsValid(Car->BoardingArea))
				DirectionCue(Car->BoardingArea->GetComponentLocation(), TEXT("Canvas.VehicleDestination"),
				    Gold, .52f, false);
		}
	}
	if (Controller.bStuckHint)
	{
		DrawFittedText(TBText::Literal(TEXT("Canvas.StuckHelp")), Gold, 330 * S, H * .65f, W - 360 * S, S);
	}
	if (!Controller.Notice.IsEmpty())
	{
		const FString Label = TBText::TranslateNotice(Controller.Notice);
		float TW = 0, TH = 0;
		GetTextSize(Label, TW, TH, PaperFont, .84f * S);
		const float Width = FMath::Min(424 * S, TW + 30 * S);
		const float X = W - Width - 20 * S, Y = H - 72 * S;
		DrawSubtitleShade(X, Y, Width, 38 * S, ESubtitleShade::Uniform);
		DrawFittedText(Label, Ink, X + 12 * S, Y + 14 * S, Width - 24 * S, .84f * S);
	}
	DrawRect(TBPaper::PrintInk, W * .5f - 3 * S, H * .5f - 1 * S, 6 * S, 2 * S);
	DrawRect(TBPaper::PrintInk, W * .5f - 1 * S, H * .5f - 3 * S, 2 * S, 6 * S);
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
	// Feather the background behind centered subtitles such as the clock and action progress.
	constexpr int32 MaskSize = 64;
	TArray64<uint8> MaskPixels;
	MaskPixels.SetNumUninitialized(MaskSize * MaskSize * 4);
	for (int32 Y = 0; Y < MaskSize; ++Y)
	{
		for (int32 X = 0; X < MaskSize; ++X)
		{
			const FVector2D Position(2.f * X / (MaskSize - 1) - 1.f, 2.f * Y / (MaskSize - 1) - 1.f);
			const float Opacity = 1.f - FMath::SmoothStep(.2f, 1.f, float(Position.Size()));
			const int32 Pixel = 4 * (Y * MaskSize + X);
			MaskPixels[Pixel] = MaskPixels[Pixel + 1] = MaskPixels[Pixel + 2] = 255;
			MaskPixels[Pixel + 3] = FMath::RoundToInt(255 * Opacity);
		}
	}
	MatchSubtitleMask = UTexture2D::CreateTransient(MaskSize, MaskSize, PF_B8G8R8A8, NAME_None, MaskPixels);
	if (MatchSubtitleMask)
	{
		MatchSubtitleMask->Filter = TF_Bilinear;
		MatchSubtitleMask->AddressX = MatchSubtitleMask->AddressY = TA_Clamp;
		MatchSubtitleMask->UpdateResource();
	}
	// A single filtered tile draws the print grain at any screen size without per-dot draw calls.
	TArray64<uint8> Dot;
	constexpr int32 Side = 16;
	Dot.SetNumUninitialized(Side * Side * 4);
	for (int32 V = 0; V < Side; ++V)
		for (int32 U = 0; U < Side; ++U)
		{
			const int32 Offset = (V * Side + U) * 4;
			Dot[Offset] = Dot[Offset + 1] = Dot[Offset + 2] = 255;
			const float Radius = FVector2D(U - 7.5f, V - 7.5f).Size();
			Dot[Offset + 3] = FMath::RoundToInt(255.f * FMath::Clamp(2.5f - Radius, 0.f, 1.f));
		}
	if (UTexture2D* Tile = UTexture2D::CreateTransient(Side, Side, PF_B8G8R8A8, NAME_None, Dot))
	{
		Tile->Filter = TF_Bilinear;
		Tile->UpdateResource();
		ArtTextures.Add(TEXT("RoomHalftone"), Tile);
	}
	PaperTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/PaperClub/Panel.Panel"));
	const TCHAR* Names[] = {TEXT("Icon_Stage"), TEXT("Icon_Settings"), TEXT("Icon_PlayerRatio"),
	    TEXT("Icon_Npc"), TEXT("Icon_Hourglass"), TEXT("Icon_Crown"), TEXT("Icon_Check"),
	    TEXT("Icon_Battery"), TEXT("Icon_BatteryPickup"), TEXT("Icon_ToyBoxEmpty"), TEXT("Icon_ToyBoxFull"),
	    TEXT("Tape_Cream"), TEXT("Tag_Toy"), TEXT("Tag_Human"),
	    TEXT("Tag_Any"), TEXT("Burst_Yellow"), TEXT("Burst_Teal"), TEXT("TitleLogo"),
	    TEXT("PauseReset"), TEXT("PauseResume"), TEXT("PausePlayers"),
	    TEXT("Button_Gear"), TEXT("Button_Leave"), TEXT("Mark_TriangleBrown"), TEXT("Mark_TriangleYellow"), TEXT("Label_You"),
	    TEXT("Frame_SquareBrown"), TEXT("Frame_SquareLavender"), TEXT("Gauge_Green"), TEXT("Gauge_Orange"), TEXT("Gauge_Gray"),
	    TEXT("StagePreview_Demo"), TEXT("StagePreview_House")};
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
	if (bDrawingMatchSubtitles)
	{
		const float Offset = FMath::Max(1.f, FMath::Min(Canvas->ClipX / 1600.f, Canvas->ClipY / 900.f));
		Item.EnableShadow(FLinearColor(.006f, .012f, .01f, .95f), FVector2D(Offset, Offset));
	}
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
	DrawRect(FLinearColor::FromSRGBColor(FColor(227, 230, 204, 122)), X, Y, W, H);
	const float Fill = FMath::Clamp(Value, 0.f, 1.f) * W;
	if (Fill > 0.f) DrawRect(Color, X, Y, Fill, H);
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

void ATBHUD::DrawSubtitleShade(float X, float Y, float W, float H, ESubtitleShade Shade)
{
	if (W <= 0.f || H <= 0.f) return;
	if (Shade == ESubtitleShade::Uniform)
		DrawRect(FLinearColor::FromSRGBColor(FColor(16, 35, 32, 66)), X, Y, W, H);
	else if (MatchSubtitleMask)
		DrawTexture(MatchSubtitleMask, X, Y, W, H, 0.f, 0.f, 1.f, 1.f, FLinearColor::FromSRGBColor(FColor(16, 35, 32, 198)));
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
