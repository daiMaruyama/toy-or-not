// Copyright (c) 2026 Mamodou. All Rights Reserved.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Core/TBController.h"
#include "Core/TBGameMode.h"
#include "Core/TBGameState.h"
#include "Core/TBPlayerState.h"
#include "Core/TBLobbySettings.h"
#include "Data/TBRuleData.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "UI/TBHUD.h"
#include "UI/TBOptionsMenu.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBStageSettingsUITest, "ToonStory.UI.StageSettingsDraft",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FTBStageSettingsUITest::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	for (const auto& Context : GEngine->GetWorldContexts())
		if (Context.WorldType == EWorldType::Game && Context.World() && TBLobbySettings::IsLobby(Context.World()))
			World = Context.World();
	if (!TestNotNull(TEXT("Run in standalone TB_Lobby"), World)) return false;
	if (!TestEqual(TEXT("Isolated standalone process"), World->GetNetMode(), NM_Standalone)) return false;
	auto* PC = Cast<ATBController>(World->GetFirstPlayerController());
	auto* HUD = PC ? Cast<ATBHUD>(PC->GetHUD()) : nullptr;
	auto* State = World->GetGameState<ATBGameState>();
	auto* Mode = World->GetAuthGameMode<ATBGameMode>();
	if (!TestNotNull(TEXT("Host"), PC) || !TestNotNull(TEXT("Canvas HUD"), HUD) ||
	    !TestNotNull(TEXT("Match state"), State) || !TestNotNull(TEXT("Authority mode"), Mode)) return false;
	if (!TestEqual(TEXT("Lobby phase"), State->Phase, ETBPhase::Lobby) ||
	    !TestFalse(TEXT("No travel is pending"), State->bTravelPending)) return false;
	auto* Pending = World->GetGameInstance()->GetSubsystem<UTBLobbySettings>();
	auto* Player = PC->GetPlayerState<ATBPlayerState>();
	if (!TestNotNull(TEXT("Lobby handoff"), Pending) || !TestNotNull(TEXT("Player state"), Player)) return false;
	if (!TestTrue(TEXT("No handoff is in progress"), Pending->PendingMap.IsEmpty())) return false;
	const FTBSettings OriginalRules = State->Settings;
	const int32 OriginalStage = State->SelectedStage, OriginalPendingStage = Pending->SelectedStage;
	const FString OriginalName = State->StageName;
	const bool bStageSelected = Pending->bStageSelected, bSettingsOpen = HUD->IsLobbySettingsOpen();
	const bool bReady = Player->bReady;
	const ETBTeam Preference = Player->Preference;
	TWeakObjectPtr<UTBOptionsMenu> Options;
	ON_SCOPE_EXIT
	{
		if (Options.IsValid()) Options->Dismiss();
		HUD->NotifyHitBoxClick(TEXT("CancelStage"));
		State->Settings = OriginalRules;
		State->SelectedStage = OriginalStage;
		State->StageName = OriginalName;
		Pending->SelectedStage = OriginalPendingStage;
		Pending->bStageSelected = bStageSelected;
		Player->Preference = Preference;
		Player->bReady = bReady;
		State->ForceNetUpdate();
		// OpenSettings now snapshots the live rules immediately. Restore those
		// first so an originally open editor never retains the test's draft.
		HUD->NotifyHitBoxClick(bSettingsOpen ? TEXT("OpenSettings") : TEXT("CloseSettings"));
	};
	PC->ServerPreference(ETBTeam::None);
	PC->ServerSelectStage(TBLobbySettings::DemoStageId);
	FTBSettings Baseline = State->Settings;
	Baseline.Duration = TB::Rules().Match.MinDuration;
	Baseline.RequiredItems = 1;
	Baseline.NPCHumans = Baseline.NPCToys = 0;
	PC->ServerRules(Baseline);
	Baseline = State->Settings;
	Player->bReady = true;
	auto SameRules = [&](const TCHAR* Message, const FTBSettings& Expected)
	{
		return TestTrue(Message, State->Settings.Humans == Expected.Humans && State->Settings.Toys == Expected.Toys &&
		    State->Settings.NPCHumans == Expected.NPCHumans && State->Settings.NPCToys == Expected.NPCToys &&
		    State->Settings.Duration == Expected.Duration && State->Settings.RequiredItems == Expected.RequiredItems);
	};
	auto Edit = [&]()
	{
		HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
		HUD->NotifyHitBoxClick(TEXT("DraftNextStage"));
		HUD->NotifyHitBoxClick(TEXT("DraftMoreTime"));
		HUD->NotifyHitBoxClick(TEXT("DraftMoreItems"));
		HUD->NotifyHitBoxClick(TEXT("ToggleDraftNPCs"));
	};
	auto OpenOptions = [&]() -> UTBOptionsMenu*
	{
		HUD->NotifyHitBoxClick(TEXT("OpenOptions"));
		TArray<UUserWidget*> Found;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Found, UTBOptionsMenu::StaticClass(), true);
		if (!TestEqual(TEXT("Stage opens one shared options menu"), Found.Num(), 1)) return nullptr;
		Options = Cast<UTBOptionsMenu>(Found[0]);
		return Options.Get();
	};
	auto EditorClosed = [&](const TCHAR* Message)
	{
		return TestTrue(Message, !HUD->IsStagePickerOpen() && !HUD->IsLobbySettingsOpen());
	};
	HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
	TestTrue(TEXT("Game settings directly opens the stage and rules editor"),
	    HUD->IsLobbySettingsOpen() && HUD->IsStagePickerOpen());
	Edit();
	TestTrue(TEXT("Draft editor remains open while editing"), HUD->IsStagePickerOpen());
	SameRules(TEXT("Draft edits do not change replicated rules"), Baseline);
	TestEqual(TEXT("Draft stage does not change replicated selection"), State->SelectedStage, TBLobbySettings::DemoStageId);
	PC->TBReady();
	TestTrue(TEXT("Ready shortcut cannot change readiness while editing"), Player->bReady);
	HUD->NotifyHitBoxClick(TEXT("Ready"));
	TestTrue(TEXT("Ready button cannot change readiness while editing"), Player->bReady);
	PC->ToggleLeaveMenu();
	EditorClosed(TEXT("Escape cancels the draft and returns directly to the lobby"));
	TestFalse(TEXT("The closed editor no longer blocks lobby chat"), HUD->IsChatBlocked());
	HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
	HUD->NotifyHitBoxClick(TEXT("SaveStage"));
	EditorClosed(TEXT("Saving the reopened editor closes all settings panels"));
	SameRules(TEXT("Reopening after Escape starts from saved rules"), Baseline);
	TestEqual(TEXT("Escape discards the chosen stage"), State->SelectedStage, TBLobbySettings::DemoStageId);
	Edit();
	HUD->NotifyHitBoxClick(TEXT("CancelStage"));
	EditorClosed(TEXT("Cancel returns directly to the lobby"));
	SameRules(TEXT("Cancel leaves saved rules unchanged"), Baseline);
	HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
	HUD->NotifyHitBoxClick(TEXT("SaveStage"));
	SameRules(TEXT("Reopening after Cancel starts from saved rules"), Baseline);
	TestEqual(TEXT("Cancel discards the chosen stage"), State->SelectedStage, TBLobbySettings::DemoStageId);
	Edit();
	UTBOptionsMenu* Menu = OpenOptions();
	if (!TestNotNull(TEXT("Stage options menu"), Menu)) return false;
	for (const FName Blocked : {FName(TEXT("OpenOptions")), FName(TEXT("CancelStage")), FName(TEXT("SaveStage")),
	    FName(TEXT("DraftNextStage")), FName(TEXT("DraftMoreTime")), FName(TEXT("ToggleDraftNPCs")), FName(TEXT("DraftMoreNPCHumans"))})
		HUD->NotifyHitBoxClick(Blocked);
	TestTrue(TEXT("Options blocks underlying stage actions"), HUD->IsStagePickerOpen() && HUD->IsLobbySettingsOpen());
	SameRules(TEXT("Options cannot apply rules through a background click"), Baseline);
	TestEqual(TEXT("Options cannot apply a stage through a background click"), State->SelectedStage, TBLobbySettings::DemoStageId);
	{
		TArray<UUserWidget*> Found;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Found, UTBOptionsMenu::StaticClass(), true);
		TestEqual(TEXT("Repeated options entry does not stack menus"), Found.Num(), 1);
	}
	Menu->Close();
	TestFalse(TEXT("Options close removes the overlay"), Menu->IsInViewport());
	TestTrue(TEXT("Options close returns to the stage draft"), HUD->IsStagePickerOpen() && HUD->IsLobbySettingsOpen());
	Menu = OpenOptions();
	if (!TestNotNull(TEXT("Stage options reopens"), Menu)) return false;
	TestTrue(TEXT("Options consumes Escape"),
	    Menu->SendKeyDownForTest(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0)).IsEventHandled());
	TestFalse(TEXT("Options Escape removes only the overlay"), Menu->IsInViewport());
	TestTrue(TEXT("Options Escape preserves the stage draft"), HUD->IsStagePickerOpen() && HUD->IsLobbySettingsOpen());
	// Repeated entry actions must not overwrite in-progress edits. ChangeStage
	// remains a compatibility alias rather than an extra settings screen.
	HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
	HUD->NotifyHitBoxClick(TEXT("ChangeStage"));
	HUD->NotifyHitBoxClick(TEXT("SaveStage"));
	EditorClosed(TEXT("Save returns directly to the lobby"));
	TestEqual(TEXT("Save applies chosen stage"), State->SelectedStage, TBLobbySettings::HouseStageId);
	TestEqual(TEXT("Save updates travel handoff selection"), Pending->SelectedStage, TBLobbySettings::HouseStageId);
	TestEqual(TEXT("Save applies duration"), State->Settings.Duration,
	    FMath::Min(TB::Rules().Match.MaxDuration, Baseline.Duration + TB::Rules().Match.DurationStep));
	TestEqual(TEXT("Save applies collection target"), State->Settings.RequiredItems, 2);
	TestEqual(TEXT("Settings round trips preserve the draft human NPC count"), State->Settings.NPCHumans, 0);
	TestEqual(TEXT("Save applies toy NPC count"), State->Settings.NPCToys, 1);
	TestTrue(TEXT("Saving settings preserves player readiness"), Player->bReady);
	TestFalse(TEXT("Saving settings never starts travel"), State->bTravelPending);
	TestTrue(TEXT("Saving settings never starts a pending map load"), Pending->PendingMap.IsEmpty());
	const FTBSettings SavedRules = State->Settings;
	Edit();
	HUD->NotifyHitBoxClick(TEXT("CloseSettings"));
	EditorClosed(TEXT("CloseSettings discards the draft and closes both panels"));
	SameRules(TEXT("CloseSettings leaves saved rules unchanged"), SavedRules);
	TestEqual(TEXT("CloseSettings leaves the saved stage unchanged"), State->SelectedStage, TBLobbySettings::HouseStageId);
	HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
	HUD->NotifyHitBoxClick(TEXT("SaveStage"));
	SameRules(TEXT("Reopening after CloseSettings starts from saved rules"), SavedRules);
	TestEqual(TEXT("Closed draft stage was discarded"), State->SelectedStage, TBLobbySettings::HouseStageId);
	PC->ServerSelectStage(TBLobbySettings::DemoStageId);
	auto MaximumItems = State->Settings;
	MaximumItems.RequiredItems = TBLobbySettings::StageItemCount(TBLobbySettings::DemoStageId);
	PC->ServerRules(MaximumItems);
	HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
	HUD->NotifyHitBoxClick(TEXT("DraftNextStage"));
	HUD->NotifyHitBoxClick(TEXT("SaveStage"));
	TestEqual(TEXT("Smaller stage clamps collection target before saving"), State->Settings.RequiredItems,
	    TBLobbySettings::StageItemCount(TBLobbySettings::HouseStageId));
	EditorClosed(TEXT("Clamped save also returns directly to the lobby"));
	HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
	HUD->NotifyHitBoxClick(TEXT("Toy"));
	TestEqual(TEXT("Each player can explicitly choose toy"), Player->Preference, ETBTeam::Toy);
	HUD->NotifyHitBoxClick(TEXT("CancelStage"));
	TestEqual(TEXT("Cancelling host rule edits preserves the player's own choice"), Player->Preference, ETBTeam::Toy);
	HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
	HUD->NotifyHitBoxClick(TEXT("Child"));
	HUD->NotifyHitBoxClick(TEXT("SaveStage"));
	TestEqual(TEXT("Saving match rules preserves the player's human choice"), Player->Preference, ETBTeam::Human);
	HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
	HUD->NotifyHitBoxClick(TEXT("Either"));
	TestEqual(TEXT("Random clears the explicit team choice"), Player->Preference, ETBTeam::None);
	HUD->NotifyHitBoxClick(TEXT("CancelStage"));

	// NPC enablement is derived from the counts. Only the current draft keeps
	// the last positive pair, and hidden steppers must never enable NPCs.
	FTBSettings NoNPCs = State->Settings;
	NoNPCs.NPCHumans = NoNPCs.NPCToys = 0;
	PC->ServerRules(NoNPCs);
	HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
	for (const FName Hidden : {FName(TEXT("DraftMoreNPCHumans")), FName(TEXT("DraftMoreNPCToys")),
	    FName(TEXT("DraftFewerNPCHumans")), FName(TEXT("DraftFewerNPCToys"))}) HUD->NotifyHitBoxClick(Hidden);
	HUD->NotifyHitBoxClick(TEXT("SaveStage"));
	TestEqual(TEXT("Hidden human controls cannot enable NPCs"), State->Settings.NPCHumans, 0);
	TestEqual(TEXT("Hidden toy controls cannot enable NPCs"), State->Settings.NPCToys, 0);
	HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
	HUD->NotifyHitBoxClick(TEXT("ToggleDraftNPCs"));
	TestEqual(TEXT("Enabling NPCs does not update saved counts"), State->Settings.NPCToys, 0);
	HUD->NotifyHitBoxClick(TEXT("SaveStage"));
	TestEqual(TEXT("First enable starts with no human NPCs"), State->Settings.NPCHumans, 0);
	TestEqual(TEXT("First enable starts with one toy NPC"), State->Settings.NPCToys, 1);
	HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
	HUD->NotifyHitBoxClick(TEXT("DraftMoreNPCHumans"));
	HUD->NotifyHitBoxClick(TEXT("DraftMoreNPCToys"));
	HUD->NotifyHitBoxClick(TEXT("ToggleDraftNPCs"));
	HUD->NotifyHitBoxClick(TEXT("ToggleDraftNPCs"));
	HUD->NotifyHitBoxClick(TEXT("SaveStage"));
	TestEqual(TEXT("Off then on restores draft human NPCs"), State->Settings.NPCHumans, 1);
	TestEqual(TEXT("Off then on restores draft toy NPCs"), State->Settings.NPCToys, 2);
	HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
	HUD->NotifyHitBoxClick(TEXT("ToggleDraftNPCs"));
	HUD->NotifyHitBoxClick(TEXT("CancelStage"));
	TestEqual(TEXT("Cancelling NPC off retains saved humans"), State->Settings.NPCHumans, 1);
	TestEqual(TEXT("Cancelling NPC off retains saved toys"), State->Settings.NPCToys, 2);
	HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
	HUD->NotifyHitBoxClick(TEXT("ToggleDraftNPCs"));
	HUD->NotifyHitBoxClick(TEXT("DraftMoreNPCHumans"));
	HUD->NotifyHitBoxClick(TEXT("DraftMoreNPCToys"));
	HUD->NotifyHitBoxClick(TEXT("SaveStage"));
	TestEqual(TEXT("Saving NPC off clears humans"), State->Settings.NPCHumans, 0);
	TestEqual(TEXT("Saving NPC off clears toys"), State->Settings.NPCToys, 0);
	HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
	HUD->NotifyHitBoxClick(TEXT("ToggleDraftNPCs"));
	HUD->NotifyHitBoxClick(TEXT("DraftFewerNPCToys"));
	HUD->NotifyHitBoxClick(TEXT("DraftMoreNPCHumans"));
	HUD->NotifyHitBoxClick(TEXT("DraftMoreNPCToys"));
	HUD->NotifyHitBoxClick(TEXT("SaveStage"));
	TestEqual(TEXT("Reducing the last NPC to zero disables human controls"), State->Settings.NPCHumans, 0);
	TestEqual(TEXT("Reducing the last NPC to zero disables toy controls"), State->Settings.NPCToys, 0);
	HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
	HUD->NotifyHitBoxClick(TEXT("ToggleDraftNPCs"));
	HUD->NotifyHitBoxClick(TEXT("DraftMoreNPCHumans"));
	HUD->NotifyHitBoxClick(TEXT("DraftFewerNPCToys"));
	HUD->NotifyHitBoxClick(TEXT("DraftFewerNPCHumans"));
	HUD->NotifyHitBoxClick(TEXT("ToggleDraftNPCs"));
	HUD->NotifyHitBoxClick(TEXT("SaveStage"));
	TestEqual(TEXT("Re-enabling after zero restores the last positive human count"), State->Settings.NPCHumans, 1);
	TestEqual(TEXT("Re-enabling after zero restores the last positive toy count"), State->Settings.NPCToys, 0);
	FTBSettings FullNPCs = State->Settings;
	FullNPCs.NPCHumans = 0;
	int32 LobbyPlayers = 0;
	for (const APlayerState* Participant : State->PlayerArray)
	{
		const auto* Info = Cast<ATBPlayerState>(Participant);
		if (Info && !Info->IsABot()) ++LobbyPlayers;
	}
	FullNPCs.NPCToys = FMath::Max(0, State->PlayerLimit - LobbyPlayers);
	PC->ServerRules(FullNPCs);
	HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
	HUD->NotifyHitBoxClick(TEXT("DraftMoreNPCHumans"));
	HUD->NotifyHitBoxClick(TEXT("DraftMoreNPCToys"));
	HUD->NotifyHitBoxClick(TEXT("SaveStage"));
	TestEqual(TEXT("A stale increase click at capacity cannot add a human NPC"), State->Settings.NPCHumans, 0);
	TestEqual(TEXT("A stale increase click at capacity cannot replace toy NPCs"), State->Settings.NPCToys, FullNPCs.NPCToys);
	return true;
}
#endif
