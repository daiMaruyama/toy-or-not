// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "GamePlay/TBBoxEscapeCourse.h"
#include "GamePlay/TBBox.h"
#include "GamePlay/TBBoxLid.h"
#include "Core/TBGameMode.h"
#include "Core/TBGameState.h"
#include "Core/TBController.h"
#include "Core/TBPlayerState.h"
#include "Core/TBLobbySettings.h"
#include "Character/TBCharacter.h"
#include "Data/TBPresentationData.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/TriggerBox.h"
#include "Engine/TargetPoint.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"

// 脱出のゴールの瞬間は、ふたを大きく開いて頭突きの合図を出し、出口へ移すのを少し遅らせる。
// 遅らせている間に試合が終わる・抜ける・捕まり直す場合は、移さない。実際のコースのある家（House）で動かす。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBBoxEscapeLidTest, "ToonStory.Playroom.BoxEscapeLid",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBBoxEscapeLidTest::RunTest(const FString&)
{
	UWorld* World = nullptr;
	for (const auto& Context : GEngine->GetWorldContexts())
		if (Context.WorldType == EWorldType::Game) { World = Context.World(); break; }
	if (!World || World->GetNetMode() != NM_Standalone) return false;
	auto* Mode = World->GetAuthGameMode<ATBGameMode>();
	auto* State = World->GetGameState<ATBGameState>();
	auto* Host = Cast<ATBController>(World->GetFirstPlayerController());
	if (!Mode || !State || !Host || !State->Box || State->Phase != ETBPhase::Lobby || State->PlayerArray.Num() != 1 ||
	    World->GetGameInstance()->GetSubsystem<UTBLobbySettings>()->bDedicatedLobby || TBLobbySettings::ShouldTravelToStage(World)) return false;
	ATBBoxEscapeCourse* Course = nullptr;
	for (TActorIterator<ATBBoxEscapeCourse> It(World); It; ++It) Course = *It;
	auto* Lid = ATBBoxLid::Find(World);
	FString Error;
	if (!TestNotNull(TEXT("Authored course"), Course) || !TestNotNull(TEXT("Lid"), Lid) || !TestNotNull(TEXT("Lid ceiling"), Lid ? Lid->Ceiling.Get() : nullptr) ||
	    !TestTrue(TEXT("Authored course is valid"), Course->Validate(Error)))
	{ AddInfo(Error); return false; }
	const auto OriginalSettings = State->Settings;
	const auto Preference = Host->GetPlayerState<ATBPlayerState>()->Preference;
	auto& Feel = const_cast<UTBPresentationData&>(TB::PresentationData());
	const FTBLidFeel OriginalFeel = Feel.Lid;
	Feel.Lid = FTBLidFeel();
	auto* Guest = Cast<ATBController>(UGameplayStatics::CreatePlayer(World, -1, true));
	if (!Guest) { Feel.Lid = OriginalFeel; return false; }
	ATBController* Third = nullptr;
	auto* Box = State->Box.Get();
	int32 Bumps = 0, Opens = 0;
	const FDelegateHandle BumpHandle = Box->OnEscapeOpened.AddLambda([&Bumps] { ++Bumps; });
	const FDelegateHandle OpenHandle = Box->OnOpened.AddLambda([&Opens] { ++Opens; });
	ON_SCOPE_EXIT
	{
		if (IsValid(Box)) { Box->OnEscapeOpened.Remove(BumpHandle); Box->OnOpened.Remove(OpenHandle); }
		Feel.Lid = OriginalFeel;
		if (IsValid(World) && IsValid(State) && IsValid(Mode) && IsValid(Host))
		{
			if (State->Phase == ETBPhase::Playing) Mode->Finish(ETBWinner::None, TEXT("Escape lid fixture cleanup"), true);
			if (State->Phase == ETBPhase::Results || State->Phase == ETBPhase::Aborted) Mode->ReturnToLobby(Host);
		}
		if (IsValid(World) && IsValid(Guest)) UGameplayStatics::RemovePlayer(Guest, true);
		if (IsValid(World) && IsValid(Third)) UGameplayStatics::RemovePlayer(Third, true);
		if (IsValid(State)) State->Settings = OriginalSettings;
		if (IsValid(Host))
			if (auto* Info = Host->GetPlayerState<ATBPlayerState>(); IsValid(Info)) Info->Preference = Preference;
		if (IsValid(World) && IsValid(Mode) && IsValid(State)) Mode->SyncLobbyCounts();
	};
	Third = Cast<ATBController>(UGameplayStatics::CreatePlayer(World, -1, true));
	if (!TestNotNull(TEXT("Third player keeps one toy outside"), Third)) return false;
	Third->GetPlayerState<ATBPlayerState>()->Preference = ETBTeam::Toy;
	auto Rules = State->Settings;
	Rules.Humans = 1; Rules.Toys = 2; Rules.NPCHumans = Rules.NPCToys = 0; Rules.Duration = 120; Rules.RequiredItems = 1;
	Mode->SetRules(Host, Rules);
	Host->GetPlayerState<ATBPlayerState>()->Preference = ETBTeam::Human;
	Guest->GetPlayerState<ATBPlayerState>()->Preference = ETBTeam::Toy;

	auto Begin = [&](const TCHAR* Case) -> ATBCharacter*
	{
		const FString Stage(Case);
		if (State->Phase == ETBPhase::Playing) Mode->Finish(ETBWinner::None, TEXT("Escape lid next"), true);
		if (State->Phase == ETBPhase::Results || State->Phase == ETBPhase::Aborted) Mode->ReturnToLobby(Host);
		if (!TestEqual(Stage + TEXT(" returns to Lobby"), State->Phase, ETBPhase::Lobby)) return nullptr;
		Host->GetPlayerState<ATBPlayerState>()->bReady = Guest->GetPlayerState<ATBPlayerState>()->bReady = Third->GetPlayerState<ATBPlayerState>()->bReady = true;
		int32 StagingIndex = 0;
		for (auto* PC : {Host, Guest, Third})
			PC->GetPawn()->SetActorLocation(FVector(-1000 + 200 * StagingIndex++, -1000, 120), false, nullptr, ETeleportType::TeleportPhysics);
		Mode->StartRound(Host);
		Mode->AdvancePreparationClockForTest(0);
		if (!TestTrue(Stage + TEXT(" is playing"), State->Phase == ETBPhase::Playing && !State->IsPreparing() &&
		    State->EndTime > State->GetServerWorldTimeSeconds())) return nullptr;
		return Cast<ATBCharacter>(Guest->GetPawn());
	};
	auto Store = [&](const TCHAR* Label, ATBCharacter* Body)
	{
		return TestTrue(Label, IsValid(Body) && Body->TBPS() && Body->TBPS()->ToyState == ETBToyState::Free && Box->Store(Body));
	};

	// ゴールの真上と、その下（コースの中でゴールの高さに届かない位置）。
	const auto* GoalVolume = CastChecked<UBoxComponent>(Course->Goal->GetCollisionComponent());
	const FBox GoalBounds = GoalVolume->Bounds.GetBox();
	const FVector Center = GoalBounds.GetCenter();
	auto* Toy = Begin(TEXT("Delay case"));
	if (!Toy) return false;
	const float Half = Toy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TBEscapeLidSupport), false, Toy);
	if (!TestTrue(TEXT("Final support trace"), World->LineTraceSingleByChannel(Hit, FVector(Center.X, Center.Y, GoalBounds.Max.Z),
	    FVector(Center.X, Center.Y, GoalBounds.Min.Z - 4), ECC_GameTraceChannel1, Query))) return false;
	const FVector Low(Center.X, Center.Y, GoalBounds.Min.Z - 100);
	const FVector Finish(Center.X, Center.Y, Hit.ImpactPoint.Z + Course->EscapeHeightAboveFinalSupport + Half + .5);
	auto Place = [&](ATBCharacter* Body, FVector Position, float Delta)
	{
		Body->SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
		auto* Movement = Body->GetCharacterMovement();
		Movement->SetMovementMode(MOVE_Falling); Movement->Velocity = FVector::ZeroVector;
		Course->Tick(Delta);
	};
	// 閉じたふたの向きからの傾き（度）。
	const FQuat RestQuat = Lid->Ceiling->GetActorQuat();
	auto LidAngle = [&] { return FMath::RadiansToDegrees(RestQuat.AngularDistance(Lid->Ceiling->GetActorQuat())); };
	Lid->Tick(0.f);
	Lid->Tick(10.f);
	TestTrue(TEXT("Lid starts closed"), LidAngle() < .5f);
	const FVector ExitLocation = Course->ExitPoints[0]->GetActorLocation();

	// 1) ゴールの瞬間に頭突きの合図が出て、遅らせたあとで出口へ移る。救助・収納のふたの合図は出ない。
	if (!Store(TEXT("Toy is stored"), Toy)) return false;
	const int32 BumpsBefore = Bumps;
	Place(Toy, Low, 0.f);
	TestEqual(TEXT("Below the goal makes no bump"), Bumps, BumpsBefore);
	Place(Toy, Finish, 0.f);
	TestEqual(TEXT("The goal moment bumps the lid for everyone"), Bumps, BumpsBefore + 1);
	TestEqual(TEXT("The toy is still boxed right at the goal"), Toy->TBPS()->ToyState, ETBToyState::Boxed);
	Course->Tick(.1f);
	Course->Tick(.1f);
	TestEqual(TEXT("The toy is held until the delay passes"), Toy->TBPS()->ToyState, ETBToyState::Boxed);
	TestEqual(TEXT("Only one bump per escape"), Bumps, BumpsBefore + 1);
	Lid->Tick(.2f);
	TestTrue(TEXT("The escape opens the lid wider than rescue"), FMath::IsNearlyEqual(LidAngle(), Feel.Lid.EscapeOpenAngle, 1.5f));
	Box->OnOpened.Broadcast();
	const int32 OpensAfterTestSignal = Opens;
	Lid->Tick(.05f);
	TestTrue(TEXT("A rescue signal does not shrink the escape opening"), FMath::IsNearlyEqual(LidAngle(), Feel.Lid.EscapeOpenAngle, 1.5f));
	Course->Tick(.1f);
	TestEqual(TEXT("After the delay the toy leaves"), Toy->TBPS()->ToyState, ETBToyState::Free);
	TestTrue(TEXT("The toy reaches the safe exit"), Toy->GetActorLocation().Equals(ExitLocation, 1.f));
	TestEqual(TEXT("Leaving sends no extra escape bump"), Bumps, BumpsBefore + 1);
	TestEqual(TEXT("Leaving sends no ordinary lid signal"), Opens, OpensAfterTestSignal);
	Lid->Tick(10.f);
	TestTrue(TEXT("The lid closes again"), LidAngle() < .5f);
	Box->OnOpened.Broadcast();
	Lid->Tick(.4f);
	TestTrue(TEXT("Rescue and storage still open the lid by the old angle"), FMath::IsNearlyEqual(LidAngle(), Feel.Lid.OpenAngle, 1.5f));
	// 救助・収納の開きの途中で脱出に切り替わっても、切り替えた直後の実際の角度は跳ばない。
	Lid->Tick(10.f);
	for (const float OpenedFor : {.06f, .12f, .5f})
	{
		Box->OnOpened.Broadcast();
		Lid->Tick(OpenedFor);
		const float Before = LidAngle();
		Box->OnEscapeOpened.Broadcast();
		Lid->Tick(0.f);
		TestTrue(*FString::Printf(TEXT("Switching to the escape keeps the angle (%.2f -> %.2f)"), Before, LidAngle()),
		         FMath::IsNearlyEqual(LidAngle(), Before, .3f));
		Lid->Tick(10.f);
	}

	// 2) 遅らせている間に試合が終わったら、出口へ移さない。
	Toy = Begin(TEXT("Deadline case"));
	if (!Toy || !Store(TEXT("Deadline toy is stored"), Toy)) return false;
	Place(Toy, Low, 0.f);
	Place(Toy, Finish, 0.f);
	TestEqual(TEXT("Deadline toy is held"), Toy->TBPS()->ToyState, ETBToyState::Boxed);
	State->EndTime = State->GetServerWorldTimeSeconds() - .01;
	Mode->CheckWin();
	Course->Tick(.5f);
	TestEqual(TEXT("A match that ends during the delay does not release the toy"), Toy->TBPS()->ToyState, ETBToyState::Boxed);
	TestTrue(TEXT("The toy stays where it was"), Toy->GetActorLocation().Equals(Finish, 1.f));

	// 3) 遅らせている間に本人が抜けた（別の経路で助けられた）ら、出口へ移さない。
	Toy = Begin(TEXT("Rescued case"));
	if (!Toy || !Store(TEXT("Rescued toy is stored"), Toy)) return false;
	Place(Toy, Low, 0.f);
	Place(Toy, Finish, 0.f);
	TestEqual(TEXT("Rescued toy is held"), Toy->TBPS()->ToyState, ETBToyState::Boxed);
	Box->ReleasePrisoners();
	TestEqual(TEXT("The team rescue frees the toy during the delay"), Toy->TBPS()->ToyState, ETBToyState::Free);
	const FVector RescuedAt = Toy->GetActorLocation();
	Course->Tick(.5f);
	TestTrue(TEXT("A toy that already left is not moved to the exit"), Toy->GetActorLocation().Equals(RescuedAt, 1.f));
	TestEqual(TEXT("It stays free"), Toy->TBPS()->ToyState, ETBToyState::Free);

	// 4) 遅らせている間に捕まり直したら、前のキャプチャの脱出では移さない。
	Toy = Begin(TEXT("Recaptured case"));
	if (!Toy || !Store(TEXT("First capture is stored"), Toy)) return false;
	Place(Toy, Low, 0.f);
	Place(Toy, Finish, 0.f);
	const int32 BumpsBeforeRecapture = Bumps;
	Box->ReleasePrisoners();
	if (!Store(TEXT("Second capture is stored"), Toy)) return false;
	Course->Tick(.5f);
	TestEqual(TEXT("A recaptured toy stays boxed"), Toy->TBPS()->ToyState, ETBToyState::Boxed);
	TestEqual(TEXT("The old escape sends no further bump"), Bumps, BumpsBeforeRecapture);

	// 5) 出口が全部塞がって失敗したあとの再試行は、ゴールの高さにいる間だけ。箱の底へ戻ったあとに出口が空いても移さない。
	Toy = Begin(TEXT("Blocked exit case"));
	if (!Toy || !Store(TEXT("Blocked toy is stored"), Toy)) return false;
	TArray<UBoxComponent*> Blockers;
	TArray<AActor*> BlockerActors;
	ON_SCOPE_EXIT { for (auto* Actor : BlockerActors) if (IsValid(Actor)) Actor->Destroy(); };
	for (const auto& Exit : Course->ExitPoints)
	{
		auto* Blocker = World->SpawnActor<AActor>(); BlockerActors.Add(Blocker);
		auto* Collision = NewObject<UBoxComponent>(Blocker); Blocker->SetRootComponent(Collision);
		Collision->SetBoxExtent(FVector(100)); Collision->SetCollisionProfileName(TEXT("BlockAll")); Collision->RegisterComponent();
		Blocker->SetActorLocation(Exit->GetActorLocation());
		Blockers.Add(Collision);
	}
	const int32 BumpsBeforeBlocked = Bumps;
	Place(Toy, Low, 0.f);
	Place(Toy, Finish, 0.f);
	TestEqual(TEXT("The blocked goal still bumps once"), Bumps, BumpsBeforeBlocked + 1);
	Course->Tick(.3f);
	TestEqual(TEXT("Every exit blocked keeps the toy boxed"), Toy->TBPS()->ToyState, ETBToyState::Boxed);
	Place(Toy, Low, 0.f); // 箱の底へ着地した。
	for (auto* Collision : Blockers) Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Course->Tick(.1f);
	Course->Tick(.1f);
	TestEqual(TEXT("An exit that opens after landing on the bottom does not warp the toy"), Toy->TBPS()->ToyState, ETBToyState::Boxed);
	TestTrue(TEXT("The toy stays at the bottom"), Toy->GetActorLocation().Equals(Low, 1.f));
	Place(Toy, Finish, 0.1f);
	TestEqual(TEXT("Reaching the goal height again releases the toy"), Toy->TBPS()->ToyState, ETBToyState::Free);
	TestTrue(TEXT("It reaches the safe exit"), Toy->GetActorLocation().Equals(ExitLocation, 1.f));
	TestEqual(TEXT("The retry does not bump again"), Bumps, BumpsBeforeBlocked + 1);
	return !HasAnyErrors();
}
#endif
