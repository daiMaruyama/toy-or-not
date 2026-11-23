#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Core/TBController.h"
#include "Core/TBGameMode.h"
#include "Core/TBGameState.h"
#include "Core/TBPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBLobbyCountsTest, "ToonStory.Lobby.Counts",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FTBLobbyCountsTest::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	for (const auto& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Game && Context.World()->GetAuthGameMode<ATBGameMode>())
		{
			World = Context.World();
			break;
		}
	}
	if (!TestNotNull(TEXT("Standalone game world"), World)) return false;
	if (!TestEqual(TEXT("Isolated standalone test"), World->GetNetMode(), NM_Standalone)) return false;
	auto* Mode = World->GetAuthGameMode<ATBGameMode>();
	auto* State = World->GetGameState<ATBGameState>();
	auto* Host = Cast<ATBController>(World->GetFirstPlayerController());
	if (!TestNotNull(TEXT("State"), State) || !TestNotNull(TEXT("Host"), Host)) return false;
	auto Check = [&](int32 Humans, int32 Toys)
	{
		TestEqual(TEXT("Children count"), State->Settings.Humans, Humans);
		TestEqual(TEXT("Toy count"), State->Settings.Toys, Toys);
		TestEqual(TEXT("Total matches connected players"), State->Settings.Humans + State->Settings.Toys,
		          State->PlayerArray.Num());
	};
	Check(1, 0);
	Host->GetPlayerState<ATBPlayerState>()->bReady = true;
	Mode->StartRound(Host);
	TestEqual(TEXT("Cannot start alone"), State->Phase, ETBPhase::Lobby);
	auto* Second = UGameplayStatics::CreatePlayer(World, -1, true);
	if (!TestNotNull(TEXT("Second player"), Second)) return false;
	Check(1, 1);
	TestTrue(TEXT("Joining preserves ready"), Host->GetPlayerState<ATBPlayerState>()->bReady);
	auto* Third = UGameplayStatics::CreatePlayer(World, -1, true);
	if (!TestNotNull(TEXT("Third player"), Third))
	{
		UGameplayStatics::RemovePlayer(Second, true);
		return false;
	}
	Check(1, 2);
	auto Rules = State->Settings;
	Rules.Humans = 2;
	Mode->SetRules(Host, Rules);
	Check(1, 2); // Host quotas no longer override individual choices or automatic teams.
	TestTrue(TEXT("Ignoring host team counts preserves ready"), Host->GetPlayerState<ATBPlayerState>()->bReady);
	Rules = State->Settings;
	Rules.Duration = Rules.Duration == 300.f ? 360.f : 300.f;
	Mode->SetRules(Host, Rules);
	TestEqual(TEXT("Time change is applied"), State->Settings.Duration, Rules.Duration);
	TestTrue(TEXT("Changing time preserves ready"), Host->GetPlayerState<ATBPlayerState>()->bReady);
	Rules = State->Settings;
	Rules.Toys = 2;
	Mode->SetRules(Host, Rules);
	Check(1, 2);
	Rules = State->Settings;
	Rules.Toys = 1;
	Mode->SetRules(Host, Rules);
	Check(1, 2);
	Rules = State->Settings;
	Rules.Humans = 1;
	Mode->SetRules(Host, Rules);
	Check(1, 2);
	Rules = State->Settings;
	Rules.Toys = 3;
	Mode->SetRules(Host, Rules);
	Check(1, 2); // An old client cannot empty a team through a host quota.
	Rules.Humans = 3;
	Rules.Toys = 3;
	Mode->SetRules(Host, Rules);
	Check(1, 2); // Derived counts ignore stale or mismatched host quotas.
	Rules = State->Settings;
	Rules.Humans = 2;
	Mode->SetRules(Host, Rules);
	Check(1, 2);
	UGameplayStatics::RemovePlayer(Third, true);
	Check(1, 1); // Children decrease only when needed to retain a toy.
	UGameplayStatics::RemovePlayer(Second, true);
	Check(1, 0);
	Second = UGameplayStatics::CreatePlayer(World, -1, true);
	if (!TestNotNull(TEXT("Rejoining player"), Second)) return false;
	Check(1, 1);
	Mode->Finish(ETBWinner::Toys, TEXT("test"));
	UGameplayStatics::RemovePlayer(Second, true);
	Mode->ReturnToLobby(Host);
	Check(1, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBLobbyAutomaticTeamsTest, "ToonStory.Lobby.AutomaticTeams",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBLobbyAutomaticTeamsTest::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	for (const auto& Context : GEngine->GetWorldContexts())
		if (Context.WorldType == EWorldType::Game && Context.World() && Context.World()->GetAuthGameMode<ATBGameMode>()) World = Context.World();
	if (!TestNotNull(TEXT("Standalone lobby world"), World) ||
		!TestEqual(TEXT("No network participants"), World->GetNetMode(), NM_Standalone)) return false;
	auto* Mode = World->GetAuthGameMode<ATBGameMode>();
	auto* State = World->GetGameState<ATBGameState>();
	auto* Host = Cast<ATBController>(World->GetFirstPlayerController());
	auto* HostInfo = Host ? Host->GetPlayerState<ATBPlayerState>() : nullptr;
	if (!State || !HostInfo || State->Phase != ETBPhase::Lobby || State->bTravelPending || State->PlayerArray.Num() != 1) return false;
	const FTBSettings OriginalRules = State->Settings;
	const ETBTeam OriginalPreference = HostInfo->Preference;
	const FString OriginalNotice = Host->Notice;
	TArray<ATBPlayerState*> Guests;
	ON_SCOPE_EXIT
	{
		for (auto* Guest : Guests)
			if (IsValid(Guest)) { State->RemovePlayerState(Guest); Guest->Destroy(); }
		HostInfo->Preference = OriginalPreference;
		State->Settings = OriginalRules;
		Mode->SyncLobbyCounts();
		Host->Notice = OriginalNotice;
	};
	FActorSpawnParameters Spawn;
	Spawn.ObjectFlags |= RF_Transient;
	for (int Index = 0; Index < 7; ++Index)
	{
		auto* Guest = World->SpawnActor<ATBPlayerState>(Spawn);
		if (!TestNotNull(TEXT("Temporary participant state"), Guest)) return false;
		Guests.Add(Guest);
		State->AddPlayerState(Guest);
	}
	State->Settings.NPCHumans = State->Settings.NPCToys = 0;
	auto ChooseAll = [&](ETBTeam Team)
	{
		for (auto* Guest : Guests) if (IsValid(Guest)) Guest->Preference = Team;
		Host->ServerPreference(Team);
	};
	ChooseAll(ETBTeam::None);
	TestEqual(TEXT("Eight random players derive two humans"), State->Settings.Humans, 2);
	TestEqual(TEXT("Eight random players derive six toys"), State->Settings.Toys, 6);
	auto Rules = State->Settings;
	Rules.Humans = MAX_int32; Rules.Toys = MIN_int32;
	Rules.Duration = Rules.Duration == 300.f ? 360.f : 300.f;
	Mode->SetRules(Host, Rules);
	TestEqual(TEXT("Host-provided team counts are ignored"), State->Settings.Humans, 2);
	TestEqual(TEXT("Unrelated match setting still applies with stale team fields"), State->Settings.Duration, Rules.Duration);
	AddExpectedError(TEXT("reason=team/player counts"), EAutomationExpectedErrorFlags::Contains, 2);
	for (ETBTeam Team : {ETBTeam::Human, ETBTeam::Toy})
	{
		ChooseAll(Team);
		TestEqual(TEXT("All explicit choices are preserved"), Team == ETBTeam::Human ? State->Settings.Humans : State->Settings.Toys, 8);
		Mode->StartRound(Host);
		TestEqual(TEXT("One-sided explicit teams cannot start"), State->Phase, ETBPhase::Lobby);
		TestEqual(TEXT("Missing-team guidance remains"), Host->Notice, FString(TEXT("At least one child and one toy are required to start.")));
	}
	ChooseAll(ETBTeam::Human);
	Guests[0]->Preference = ETBTeam::Toy;
	Host->ServerPreference(ETBTeam::None);
	TestEqual(TEXT("Explicit choices outrank the standard ratio"), State->Settings.Humans, 6);
	TestEqual(TEXT("Random player fills the toy side"), State->Settings.Toys, 2);
	ChooseAll(ETBTeam::Toy);
	Host->ServerPreference(ETBTeam::None);
	TestEqual(TEXT("Random player fills the missing human side"), State->Settings.Humans, 1);
	ChooseAll(ETBTeam::None);
	TestEqual(TEXT("Returning to random discards the previous team quota"), State->Settings.Humans, 2);
	for (int Index = Guests.Num() - 1; Index >= 3; --Index)
	{
		State->RemovePlayerState(Guests[Index]);
		Guests[Index]->Destroy();
	}
	Rules = State->Settings;
	Rules.NPCHumans = 2; Rules.NPCToys = 2;
	Mode->SetRules(Host, Rules);
	TestEqual(TEXT("Fixed NPC humans are respected"), State->Settings.NPCHumans, 2);
	TestEqual(TEXT("Fixed NPC toys are respected"), State->Settings.NPCToys, 2);
	TestEqual(TEXT("Random players account for already-filled NPC teams"), State->Settings.Humans, 0);
	Host->ServerPreference(ETBTeam::Human);
	TestEqual(TEXT("Explicit choice still wins when NPCs fill both teams"), State->Settings.Humans, 1);
	return true;
}
#endif
