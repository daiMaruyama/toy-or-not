#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Character/TBCharacter.h"
#include "Core/TBController.h"
#include "Core/TBGameState.h"
#include "Core/TBPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

// リザルトで負けた人の観戦。最初は番号の小さい勝者、右クリックで次へ、選んだ相手を保つ、抜けたら選び直す、NPCは候補にしない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBResultViewTest, "ToonStory.Results.View",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBResultViewTest::RunTest(const FString&)
{
	UWorld* World = nullptr;
	for (const auto& Context : GEngine->GetWorldContexts())
		if (Context.WorldType == EWorldType::Game) World = Context.World();
	if (!World || World->GetNetMode() != NM_Standalone) return false;
	auto* State = World->GetGameState<ATBGameState>();
	auto* PC = Cast<ATBController>(World->GetFirstPlayerController());
	auto* Mine = PC ? PC->GetPawn<ATBCharacter>() : nullptr;
	if (!TestNotNull(TEXT("State"), State) || !TestNotNull(TEXT("Local pawn"), Mine) || !Mine->TBPS()) return false;
	auto* Info = Mine->TBPS();
	const auto OldTeam = Info->Team;
	const auto OldPhase = State->Phase;
	const auto OldWinner = State->Winner;
	const auto OldTarget = PC->GetViewTarget();

	struct FWinner
	{
		ATBController* Controller = nullptr;
		ATBCharacter* Body = nullptr;
		ATBPlayerState* Player = nullptr;
	};
	TArray<FWinner> Winners;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto AddToy = [&](int32 Id, bool bBot)
	{
		FWinner Made;
		Made.Controller = World->SpawnActor<ATBController>();
		Made.Body = World->SpawnActor<ATBCharacter>(FVector(Id * 300.f, 0, 10000), FRotator::ZeroRotator, Spawn);
		Made.Player = Made.Controller->GetPlayerState<ATBPlayerState>();
		if (!Made.Player)
		{
			Made.Player = World->SpawnActor<ATBPlayerState>();
			Made.Controller->PlayerState = Made.Player;
			State->AddPlayerState(Made.Player);
		}
		Made.Player->Team = ETBTeam::Toy;
		Made.Player->ToyState = ETBToyState::Free;
		Made.Player->SetPlayerId(Id);
		Made.Player->SetPlayerName(FString::Printf(TEXT("Winner%d"), Id));
		Made.Player->SetIsABot(bBot);
		Made.Controller->Possess(Made.Body);
		Made.Body->ApplyState();
		Winners.Add(Made);
		return Made;
	};
	auto Remove = [&](const FWinner Gone)
	{
		Gone.Controller->UnPossess();
		Gone.Body->Destroy();
		State->RemovePlayerState(Gone.Player);
		Gone.Player->Destroy();
		Gone.Controller->Destroy();
		Winners.RemoveAll([&Gone](const FWinner& Other) { return Other.Player == Gone.Player; });
	};
	ON_SCOPE_EXIT
	{
		while (!Winners.IsEmpty()) Remove(Winners.Last());
		Info->Team = OldTeam;
		Mine->ApplyState();
		State->Phase = OldPhase;
		State->Winner = OldWinner;
		PC->UpdateResultView();
		PC->SetViewTarget(OldTarget);
		PC->SyncInputContexts();
	};

	// 自分はにんげんで、おもちゃが勝った。
	Info->Team = ETBTeam::Human;
	Mine->ApplyState();
	const FWinner Second = AddToy(902, false);
	const FWinner First = AddToy(901, false);
	AddToy(900, true);
	State->Phase = ETBPhase::Results;
	State->Winner = ETBWinner::Toys;
	TestTrue(TEXT("Loser spectates"), PC->IsSpectatingResults());
	const auto Candidates = PC->ResultViewCandidates();
	if (!TestEqual(TEXT("NPC winner is not a candidate"), Candidates.Num(), 2)) return false;
	TestTrue(TEXT("Candidates are ordered by id"), Candidates[0] == First.Body && Candidates[1] == Second.Body);

	PC->UpdateResultView();
	TestTrue(TEXT("First view is the smallest id"), PC->GetResultViewCharacter() == First.Body);
	TestTrue(TEXT("Camera follows the chosen winner"), PC->GetViewTarget() == First.Body);
	PC->ViewNextResultPlayer();
	TestTrue(TEXT("Next goes to the second winner"), PC->GetResultViewCharacter() == Second.Body);
	PC->ViewNextResultPlayer();
	TestTrue(TEXT("Next wraps to the first"), PC->GetResultViewCharacter() == First.Body);
	PC->ViewNextResultPlayer();

	// 番号の小さい勝者が増えても、選んだ相手を保つ。
	const FWinner Early = AddToy(899, false);
	PC->UpdateResultView();
	TestTrue(TEXT("Choice is kept when a new winner appears"), PC->GetResultViewCharacter() == Second.Body);

	// 選んだ相手が抜けたら、最初の勝者に選び直す。
	Remove(Second);
	PC->UpdateResultView();
	TestTrue(TEXT("Leaving winner falls back to the first"), PC->GetResultViewCharacter() == Early.Body);

	// プレイヤーの勝者がいなければ、NPCの勝者を映す。切り替えはしない。
	Remove(Early);
	Remove(First);
	PC->UpdateResultView();
	TestEqual(TEXT("No player candidates"), PC->ResultViewCandidates().Num(), 0);
	TestNull(TEXT("Nobody is chosen"), PC->GetResultViewCharacter());
	TestTrue(TEXT("NPC winner is shown"), PC->GetViewTarget() == Winners[0].Body);
	PC->ViewNextResultPlayer();
	TestTrue(TEXT("Next does nothing without candidates"), PC->GetViewTarget() == Winners[0].Body);

	// 自分が勝ったときは観戦しない。
	State->Winner = ETBWinner::Humans;
	TestFalse(TEXT("Winner does not spectate"), PC->IsSpectatingResults());
	TestEqual(TEXT("Winner has no candidates"), PC->ResultViewCandidates().Num(), 0);
	return true;
}
#endif
