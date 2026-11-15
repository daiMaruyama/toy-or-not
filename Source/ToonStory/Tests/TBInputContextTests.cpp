#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/TBTestMatch.h"
#include "Core/TBController.h"
#include "Core/TBGameState.h"
#include "Input/TBInputSettings.h"
#include "Core/TBPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBInputContextTest, "ToonStory.Input.RoleContexts",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBInputContextTest::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	for (const auto& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Game) World = Context.World();
	}
	auto* PC = World ? Cast<ATBController>(World->GetFirstPlayerController()) : nullptr;
	auto* Info = PC ? PC->GetPlayerState<ATBPlayerState>() : nullptr;
	auto* Input = PC ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr;
	auto* State = World ? World->GetGameState<ATBGameState>() : nullptr;
	if (!TestNotNull(TEXT("Standalone game"), Info) || !TestNotNull(TEXT("Input subsystem"), Input) ||
	    !TestNotNull(TEXT("Game state"), State))
		return false;

	auto* Settings = GetMutableDefault<UTBInputSettings>();
	const auto SavedCommon = Settings->CommonContext;
	const auto SavedHuman = Settings->HumanContext;
	const auto SavedToy = Settings->ToyContext;
	const auto SavedLobby = Settings->LobbyContext;
	const auto SavedResults = Settings->ResultsContext;
	const auto SavedWinner = State->Winner;
	const ETBTeam SavedTeam = Info->Team;
	const ETBPhase SavedPhase = State->Phase;
	ON_SCOPE_EXIT
	{
		Settings->CommonContext = SavedCommon;
		Settings->HumanContext = SavedHuman;
		Settings->ToyContext = SavedToy;
		Settings->LobbyContext = SavedLobby;
		Settings->ResultsContext = SavedResults;
		State->Winner = SavedWinner;
		Info->Team = SavedTeam;
		State->Phase = SavedPhase;
		PC->SyncInputContexts();
	};

	auto* Common = NewObject<UInputMappingContext>(GetTransientPackage());
	auto* Human = NewObject<UInputMappingContext>(GetTransientPackage());
	auto* Toy = NewObject<UInputMappingContext>(GetTransientPackage());
	auto* Lobby = NewObject<UInputMappingContext>(GetTransientPackage());
	auto* Results = NewObject<UInputMappingContext>(GetTransientPackage());
	Common->AddToRoot();
	Human->AddToRoot();
	Toy->AddToRoot();
	Lobby->AddToRoot();
	Results->AddToRoot();
	ON_SCOPE_EXIT
	{
		Common->RemoveFromRoot();
		Human->RemoveFromRoot();
		Toy->RemoveFromRoot();
		Lobby->RemoveFromRoot();
		Results->RemoveFromRoot();
	};

	Settings->CommonContext = nullptr;
	Settings->HumanContext = nullptr;
	Settings->ToyContext = nullptr;
	Settings->LobbyContext = nullptr;
	Settings->ResultsContext = nullptr;
	Info->Team = ETBTeam::Human;
	PC->SyncInputContexts();
	TestFalse(TEXT("Empty settings attach nothing"), Input->HasMappingContext(Common) || Input->HasMappingContext(Human));

	Settings->CommonContext = Common;
	Settings->HumanContext = Human;
	Settings->ToyContext = Toy;
	Settings->LobbyContext = Lobby;
	Settings->ResultsContext = Results;
	State->Phase = ETBPhase::Lobby;
	PC->SyncInputContexts();
	TestTrue(TEXT("Human: common"), Input->HasMappingContext(Common));
	TestTrue(TEXT("Human: human"), Input->HasMappingContext(Human));
	TestFalse(TEXT("Human: not toy"), Input->HasMappingContext(Toy));

	Info->Team = ETBTeam::Toy;
	PC->SyncInputContexts();
	TestTrue(TEXT("Toy: common"), Input->HasMappingContext(Common));
	TestFalse(TEXT("Toy: not human"), Input->HasMappingContext(Human));
	TestTrue(TEXT("Toy: toy"), Input->HasMappingContext(Toy));

	Info->Team = ETBTeam::None;
	PC->SyncInputContexts();
	TestTrue(TEXT("No role: common"), Input->HasMappingContext(Common));
	TestFalse(TEXT("No role: no role context"), Input->HasMappingContext(Human) || Input->HasMappingContext(Toy));

	// ロビーのIMCは、ロビーの間だけ付く
	TestTrue(TEXT("Lobby phase: lobby context"), Input->HasMappingContext(Lobby));
	TBTest::FPlayingScope Playing(*State);
	PC->SyncInputContexts();
	TestFalse(TEXT("Playing: lobby context is removed"), Input->HasMappingContext(Lobby));
	TestTrue(TEXT("Playing: common stays"), Input->HasMappingContext(Common));
	State->Phase = ETBPhase::Lobby;
	PC->SyncInputContexts();
	TestTrue(TEXT("Back to lobby: lobby context is attached again"), Input->HasMappingContext(Lobby));

	// リザルトで負けた人は、陣営のキーの代わりに観戦のキー。勝った人は陣営のキーのまま。
	TestFalse(TEXT("Lobby: no results context"), Input->HasMappingContext(Results));
	Info->Team = ETBTeam::Human;
	State->Phase = ETBPhase::Results;
	State->Winner = ETBWinner::Toys;
	PC->SyncInputContexts();
	TestTrue(TEXT("Results loser: results context"), Input->HasMappingContext(Results));
	TestFalse(TEXT("Results loser: no human context"), Input->HasMappingContext(Human));
	TestTrue(TEXT("Results loser: common stays"), Input->HasMappingContext(Common));
	State->Winner = ETBWinner::Humans;
	PC->SyncInputContexts();
	TestFalse(TEXT("Results winner: no results context"), Input->HasMappingContext(Results));
	TestTrue(TEXT("Results winner: human context"), Input->HasMappingContext(Human));
	State->Winner = SavedWinner;
	State->Phase = ETBPhase::Lobby;
	PC->SyncInputContexts();

	// 外部から外されても、次のSyncで付き直る
	Input->RemoveMappingContext(Common);
	TestFalse(TEXT("Removed: common"), Input->HasMappingContext(Common));
	PC->SyncInputContexts();
	TestTrue(TEXT("Re-attached: common"), Input->HasMappingContext(Common));
	return true;
}
#endif
