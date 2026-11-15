#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/TBTestMatch.h"
#include "Character/TBCharacter.h"
#include "Core/TBController.h"
#include "Core/TBGameMode.h"
#include "Core/TBGameState.h"
#include "Core/TBPlayerState.h"
#include "Data/TBPresentationData.h"
#include "GamePlay/Items/TBEscapeExit.h"
#include "GamePlay/Items/TBEscapeVehicle.h"
#include "GamePlay/TBBoxLid.h"
#include "Camera/CameraActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

// 決着からリザルトの画面まで。車で脱出したら車の演出と並べ直し、人間の勝ちはふた（箱の部屋が無ければママ）、それ以外はママ。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBResultPresentationTest, "ToonStory.Results.Presentation",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBResultPresentationTest::RunTest(const FString&)
{
	UWorld* World = nullptr;
	for (const auto& Context : GEngine->GetWorldContexts())
		if (Context.WorldType == EWorldType::Game) World = Context.World();
	if (!World || World->GetNetMode() != NM_Standalone) return false;
	auto* Mode = World->GetAuthGameMode<ATBGameMode>();
	auto* State = World->GetGameState<ATBGameState>();
	auto* PC = Cast<ATBController>(World->GetFirstPlayerController());
	auto* Toy = PC ? PC->GetPawn<ATBCharacter>() : nullptr;
	if (!TestNotNull(TEXT("Mode"), Mode) || !TestNotNull(TEXT("State"), State) || !TestNotNull(TEXT("Toy"), Toy) || !Toy->TBPS())
		return false;
	TBTest::FPlayingScope Playing(*State, World->GetTimeSeconds() + 600);
	auto* Info = Toy->TBPS();
	const auto OldTeam = Info->Team;
	const auto OldToyState = Info->ToyState;
	const auto OldTransform = Toy->GetActorTransform();
	const auto OldPhase = ETBPhase::Lobby;
	const auto OldWinner = State->Winner;
	const auto OldReason = State->Reason;
	const auto& Feel = TB::PresentationData().Result;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Exit = World->SpawnActor<ATBEscapeExit>(FVector(0, 0, 30000), FRotator::ZeroRotator, Spawn);
	auto* Vehicle = World->SpawnActor<ATBEscapeVehicle>(FVector(0, 0, 29920), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Exit"), Exit) || !TestNotNull(TEXT("Vehicle"), Vehicle)) return false;
	ON_SCOPE_EXIT
	{
		Vehicle->ResetRound();
		Vehicle->Destroy();
		Exit->Destroy();
		Mode->ClearResultPresentation();
		State->bEscapePhase = false;
		State->bVehicleEscapeResult = false;
		State->EscapedPlayers.Reset();
		State->Winner = OldWinner;
		State->Reason = OldReason;
		State->Phase = OldPhase;
		PC->UpdateResultView();
		PC->SetViewTarget(Toy);
		Info->Team = OldTeam;
		Info->ToyState = OldToyState;
		Toy->ApplyState();
		Toy->SetActorTransform(OldTransform);
	};
	Info->Team = ETBTeam::Toy;
	Info->ToyState = ETBToyState::Free;
	Toy->ApplyState();
	Toy->SetActorLocation(FVector(300, 0, 30000));

	// 車で脱出：車が横切るカメラを送り、覆いきるまで乗ったまま。
	State->bEscapePhase = true;
	if (!TestTrue(TEXT("Toy boards"), Vehicle->Board(Toy))) return false;
	State->EscapedPlayers.Add(Info);
	State->bVehicleEscapeResult = true;
	Mode->Finish(ETBWinner::Toys, TEXT("test"));
	TestEqual(TEXT("Car style"), State->ResultStyle, ETBResultStyle::Car);
	TestTrue(TEXT("Shot is the exit camera"), State->ResultShot.GetLocation().Equals(Exit->ResultCamera->GetComponentLocation(), 1.f));
	TestTrue(TEXT("Result time grows by the presentation"),
	         FMath::IsNearlyEqual(State->EndTime - State->ResultStartTime, double(Feel.CoverSeconds + Feel.RevealSeconds + 10.f), .01));
	TestTrue(TEXT("Still in the car before the cover"), Toy->EscapeVehicle == Vehicle);
	TestFalse(TEXT("Result screen waits for the cover"), State->IsResultRevealed());

	// 決着の後は出口のカメラ、覆いきったら自分（勝者）に切り替える。
	State->ResultStartTime = State->GetServerWorldTimeSeconds() - (Feel.DecisiveSeconds + .1f);
	PC->UpdateResultView();
	const auto* Shot = Cast<ACameraActor>(PC->GetViewTarget());
	TestTrue(TEXT("Shot camera is used after the decisive moment"),
	         Shot && Shot->GetActorLocation().Equals(State->ResultShot.GetLocation(), 1.f));
	Mode->PlaceResults();
	TestNull(TEXT("Escaped toy leaves the car"), Toy->EscapeVehicle.Get());
	TestFalse(TEXT("Escaped toy is visible"), Toy->IsHidden());
	TestTrue(TEXT("Car is empty"), Vehicle->Passengers.IsEmpty());
	TestTrue(TEXT("Escaped toy stands in the lineup"),
	         FVector::Dist2D(Toy->GetActorLocation(), Exit->LineupSpot(0, 1).GetLocation()) < 100.f);
	TestTrue(TEXT("Car is parked behind the lineup"), FVector::Dist2D(Vehicle->GetActorLocation(), Exit->ParkSpot().GetLocation()) < 10.f);
	State->ResultStartTime = State->GetServerWorldTimeSeconds() - (Feel.CoverSeconds + .1f);
	TestTrue(TEXT("Result screen after the cover"), State->IsResultRevealed());
	PC->UpdateResultView();
	TestTrue(TEXT("Winner sees their own toy"), PC->GetViewTarget() == Toy);
	TestTrue(TEXT("Escaped toy celebrates"), Toy->IsCelebrating());

	// 車を使わずにおもちゃが勝ったら、ママの吹き出し。
	Playing.Hold(World->GetTimeSeconds() + 600);
	State->bVehicleEscapeResult = false;
	State->EscapedPlayers.Reset();
	Mode->Finish(ETBWinner::Toys, TEXT("test"));
	TestEqual(TEXT("Toy win without the car uses Mom"), State->ResultStyle, ETBResultStyle::Mom);
	State->ResultStartTime = State->GetServerWorldTimeSeconds() - (Feel.DecisiveSeconds + .1f);
	PC->UpdateResultView();
	TestNull(TEXT("No shot camera for Mom"), Cast<ACameraActor>(PC->GetViewTarget()));

	// 人間の勝ちは、箱の部屋のふたがあればふた、無ければママ。
	Playing.Hold(World->GetTimeSeconds() + 600);
	Mode->Finish(ETBWinner::Humans, TEXT("test"));
	FTransform LidShot;
	const auto* Lid = ATBBoxLid::Find(World);
	const bool bLid = Lid && Lid->ResultShot(LidShot);
	AddInfo(FString::Printf(TEXT("Box lid in this map: %d"), bLid));
	TestEqual(TEXT("Human win uses the lid when the box room exists"), State->ResultStyle, bLid ? ETBResultStyle::Lid : ETBResultStyle::Mom);

	// ロビーに戻ると演出は残らない。
	Mode->ClearResultPresentation();
	TestEqual(TEXT("Cleared style"), State->ResultStyle, ETBResultStyle::None);
	return true;
}
#endif
