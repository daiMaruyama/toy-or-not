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
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
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
	// 時間は、データアセットの値から決める（開く時間などを長くしても、全開・閉じきった状態で測れるように）。
	const FTBLidFeel& L = Feel.Lid;
	const float RescueFull = L.OpenSeconds + L.HoldSeconds * .5f;
	const float EscapeFull = L.EscapeOpenSeconds + L.EscapeHoldSeconds * .5f;
	const float CloseAll = FMath::Max(L.OpenSeconds + L.HoldSeconds, L.EscapeOpenSeconds + L.EscapeHoldSeconds) + L.CloseSeconds + 1.f;
	const float Delay = L.EscapeReleaseDelay;
	const float PastDelay = Delay * 2.f + .1f;
	Lid->Tick(0.f);
	Lid->Tick(CloseAll);
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
	Course->Tick(Delay * .4f);
	Course->Tick(Delay * .4f);
	TestEqual(TEXT("The toy is held until the delay passes"), Toy->TBPS()->ToyState, ETBToyState::Boxed);
	TestEqual(TEXT("Only one bump per escape"), Bumps, BumpsBefore + 1);
	Lid->Tick(EscapeFull);
	TestTrue(TEXT("The escape opens the lid wider than rescue"), FMath::IsNearlyEqual(LidAngle(), Feel.Lid.EscapeOpenAngle, 1.5f));
	Box->OnOpened.Broadcast();
	const int32 OpensAfterTestSignal = Opens;
	Lid->Tick(L.EscapeHoldSeconds * .1f);
	TestTrue(TEXT("A rescue signal does not shrink the escape opening"), FMath::IsNearlyEqual(LidAngle(), Feel.Lid.EscapeOpenAngle, 1.5f));
	Course->Tick(Delay * .4f);
	TestEqual(TEXT("After the delay the toy leaves"), Toy->TBPS()->ToyState, ETBToyState::Free);
	TestTrue(TEXT("The toy reaches the safe exit"), Toy->GetActorLocation().Equals(ExitLocation, 1.f));
	TestEqual(TEXT("Leaving sends no extra escape bump"), Bumps, BumpsBefore + 1);
	TestEqual(TEXT("Leaving sends no ordinary lid signal"), Opens, OpensAfterTestSignal);
	Lid->Tick(CloseAll);
	TestTrue(TEXT("The lid closes again"), LidAngle() < .5f);
	Box->OnOpened.Broadcast();
	Lid->Tick(RescueFull);
	TestTrue(TEXT("Rescue and storage still open the lid by the old angle"), FMath::IsNearlyEqual(LidAngle(), Feel.Lid.OpenAngle, 1.5f));
	// 救助・収納の開きの途中で脱出に切り替わっても、切り替えた直後の実際の角度は跳ばない。
	Lid->Tick(CloseAll);
	for (const float OpenedFor : {L.OpenSeconds * .25f, L.OpenSeconds * .5f, RescueFull})
	{
		Box->OnOpened.Broadcast();
		Lid->Tick(OpenedFor);
		const float Before = LidAngle();
		Box->OnEscapeOpened.Broadcast();
		Lid->Tick(0.f);
		TestTrue(*FString::Printf(TEXT("Switching to the escape keeps the angle (%.2f -> %.2f)"), Before, LidAngle()),
		         FMath::IsNearlyEqual(LidAngle(), Before, .3f));
		Lid->Tick(CloseAll);
	}

	// 2) 遅らせている間に試合が終わったら、出口へ移さない。
	Toy = Begin(TEXT("Deadline case"));
	if (!Toy || !Store(TEXT("Deadline toy is stored"), Toy)) return false;
	Place(Toy, Low, 0.f);
	Place(Toy, Finish, 0.f);
	TestEqual(TEXT("Deadline toy is held"), Toy->TBPS()->ToyState, ETBToyState::Boxed);
	State->EndTime = State->GetServerWorldTimeSeconds() - .01;
	Mode->CheckWin();
	Course->Tick(PastDelay);
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
	Course->Tick(PastDelay);
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
	Course->Tick(PastDelay);
	TestEqual(TEXT("A recaptured toy stays boxed"), Toy->TBPS()->ToyState, ETBToyState::Boxed);
	TestEqual(TEXT("The old escape sends no further bump"), Bumps, BumpsBeforeRecapture);

	// 5) 出口が全部塞がって失敗したあと、ゴールの高さに居続けても頭突きは出ない。箱の底へ戻ったあとに出口が空いても移さない。
	//    もう一度ゴールの高さに届いたときだけ、頭突きとふたの開きをもう一度出して、遅らせたあとで移す。
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
	Course->Tick(PastDelay);
	TestEqual(TEXT("Every exit blocked keeps the toy boxed"), Toy->TBPS()->ToyState, ETBToyState::Boxed);
	Course->Tick(PastDelay);
	Course->Tick(PastDelay);
	TestEqual(TEXT("Staying at the goal height after a failure does not bump every frame"), Bumps, BumpsBeforeBlocked + 1);
	Place(Toy, Low, 0.f); // 箱の底へ着地した。
	for (auto* Collision : Blockers) Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Course->Tick(.1f);
	Course->Tick(.1f);
	TestEqual(TEXT("An exit that opens after landing on the bottom does not warp the toy"), Toy->TBPS()->ToyState, ETBToyState::Boxed);
	TestTrue(TEXT("The toy stays at the bottom"), Toy->GetActorLocation().Equals(Low, 1.f));
	TestEqual(TEXT("Waiting at the bottom sends no bump"), Bumps, BumpsBeforeBlocked + 1);
	Place(Toy, Finish, 0.f);
	TestEqual(TEXT("Reaching the goal height again bumps the lid again"), Bumps, BumpsBeforeBlocked + 2);
	TestEqual(TEXT("The retry also waits for the delay"), Toy->TBPS()->ToyState, ETBToyState::Boxed);
	Course->Tick(PastDelay);
	TestEqual(TEXT("After the delay the retry releases the toy"), Toy->TBPS()->ToyState, ETBToyState::Free);
	TestTrue(TEXT("It reaches the safe exit"), Toy->GetActorLocation().Equals(ExitLocation, 1.f));
	TestEqual(TEXT("The release sends no extra bump"), Bumps, BumpsBeforeBlocked + 2);
	// リザルトの演出でふたを持っている間に、遅れて届いた脱出の合図は、手放したあとに開き直さない。
	Lid->HoldForResult(.5f);
	Box->OnEscapeOpened.Broadcast();
	Lid->Tick(EscapeFull);
	Lid->HoldForResult(-1.f);
	Lid->Tick(EscapeFull);
	TestTrue(TEXT("A late escape signal during the result hold does not reopen the lid after release"), LidAngle() < .5f);

	// 全開のところへ2人目の脱出の合図が来て開き直しても、移すまで開いたままにする。普通の値では延ばさず、決まった時間で閉じはじめる。
	Lid->Tick(CloseAll);
	Box->OnEscapeOpened.Broadcast();
	Lid->Tick(L.EscapeOpenSeconds + L.EscapeHoldSeconds - .05f);
	Box->OnEscapeOpened.Broadcast();
	Lid->Tick(L.EscapeHoldSeconds - .05f);
	TestTrue(TEXT("With normal data the reopened lid is still open just before the hold ends"), FMath::IsNearlyEqual(LidAngle(), L.EscapeOpenAngle, 1.5f));
	Lid->Tick(.25f);
	TestTrue(TEXT("With normal data the hold is not extended and the lid starts closing"), LidAngle() < L.EscapeOpenAngle - 5.f);
	// データを変な値（開いたままの時間より遅らせる秒が長い）にしても、2人目の合図から移すまで開いている。
	Lid->Tick(CloseAll);
	Feel.Lid.EscapeHoldSeconds = .1f;
	Feel.Lid.EscapeReleaseDelay = .5f;
	Box->OnEscapeOpened.Broadcast();
	Lid->Tick(Feel.Lid.EscapeOpenSeconds + .05f);
	Box->OnEscapeOpened.Broadcast();
	Lid->Tick(Feel.Lid.EscapeReleaseDelay - .05f);
	TestTrue(TEXT("With odd data the lid stays open until the release delay after the second signal"),
	         FMath::IsNearlyEqual(LidAngle(), Feel.Lid.EscapeOpenAngle, 1.5f));
	Lid->Tick(CloseAll);
	return !HasAnyErrors();
}

// 蝶番の側。既定の+Yは今までと同じ動き、-Yは反対側（ゴール側）が大きく持ち上がり、リザルトのカメラも持ち上がる側を向く。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBBoxLidHingeTest, "ToonStory.Playroom.BoxLidHinge",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBBoxLidHingeTest::RunTest(const FString&)
{
	UWorld* World = nullptr;
	for (const auto& Context : GEngine->GetWorldContexts())
		if (Context.WorldType == EWorldType::Game) { World = Context.World(); break; }
	auto* State = World ? World->GetGameState<ATBGameState>() : nullptr;
	if (!State || !State->Box) return false;
	auto* Box = State->Box.Get();
	auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Cube mesh"), Cube)) return false;
	// 本物のデータ（開く角度が0でも正しい設定）に頼らないよう、決まった正の角度（初期値の60度）で確かめる。
	auto& Feel = const_cast<UTBPresentationData&>(TB::PresentationData());
	const FTBLidFeel OriginalFeel = Feel.Lid;
	Feel.Lid = FTBLidFeel();
	TArray<AActor*> Spawned;
	ON_SCOPE_EXIT { Feel.Lid = OriginalFeel; for (auto* Actor : Spawned) if (IsValid(Actor)) Actor->Destroy(); };
	// 本物と同じ大きさ（800x600、厚さ2）の天井を、離れた場所に置く。ゴールは本物と同じく、中心から(+245,+240)。
	struct FTrial { ATBBoxLid* Lid = nullptr; AStaticMeshActor* Ceiling = nullptr; FVector Center; };
	auto Make = [&](ETBLidHingeSide Side, FVector Center)
	{
		FTrial Trial; Trial.Center = Center;
		Trial.Ceiling = World->SpawnActor<AStaticMeshActor>(Center, FRotator::ZeroRotator); Spawned.Add(Trial.Ceiling);
		Trial.Ceiling->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Trial.Ceiling->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Trial.Ceiling->SetActorScale3D(FVector(8, 6, .02));
		Trial.Lid = World->SpawnActorDeferred<ATBBoxLid>(ATBBoxLid::StaticClass(), FTransform(Center));
		Spawned.Add(Trial.Lid);
		Trial.Lid->Ceiling = Trial.Ceiling;
		Trial.Lid->HingeSide = Side;
		Trial.Lid->FinishSpawning(FTransform(Center));
		return Trial;
	};
	// 高さは本物の部屋の天井と同じにする。ResultShot の床は本物の落下エリアから決まるので、高さが違うとカメラがほぼ真上を向いて、向きの検査が意味を持たない。
	constexpr double RoomCeilingZ = 319.;
	FTrial Positive = Make(ETBLidHingeSide::PositiveY, FVector(60000, 0, RoomCeilingZ));
	FTrial Negative = Make(ETBLidHingeSide::NegativeY, FVector(60000, 3000, RoomCeilingZ));
	for (auto* Lid : {Positive.Lid, Negative.Lid}) Lid->Tick(0.f); // 箱の合図へつなぐ。
	const float Angle = TB::PresentationData().Lid.OpenAngle;
	Box->OnOpened.Broadcast();
	// 全開になってから測る。時間はデータアセットの値から決める。
	const auto& RealLid = TB::PresentationData().Lid;
	const float FullyOpen = RealLid.OpenSeconds + RealLid.HoldSeconds * .5f;
	const float ClosedAll = RealLid.OpenSeconds + RealLid.HoldSeconds + RealLid.CloseSeconds + 1.f;
	for (auto* Lid : {Positive.Lid, Negative.Lid}) Lid->Tick(FullyOpen);
	// ゴールの真下にあたる天井の面が、閉じていたときからどれだけ上がったか。
	auto Rise = [&](const FTrial& Trial)
	{
		const FVector Goal = Trial.Center + FVector(245, 240, -1);
		const FTransform Rest(FRotator::ZeroRotator, Trial.Center, FVector(8, 6, .02));
		return float(Trial.Ceiling->GetActorTransform().TransformPosition(Rest.InverseTransformPosition(Goal)).Z - Goal.Z);
	};
	const float Lever = FMath::Sin(FMath::DegreesToRadians(Angle));
	// 今までと同じ（+Y）: 蝶番までゴールから約60cm なので、ほとんど上がらない。
	TestTrue(*FString::Printf(TEXT("+Y hinge lifts the goal side only about 60cm*sin (%.1f)"), Rise(Positive)),
	         FMath::IsNearlyEqual(Rise(Positive), 60.f * Lever, 6.f));
	// -Y: 蝶番までゴールから約540cm なので、大きく上がる。
	TestTrue(*FString::Printf(TEXT("-Y hinge lifts the goal side about 540cm*sin (%.1f)"), Rise(Negative)),
	         FMath::IsNearlyEqual(Rise(Negative), 540.f * Lever, 6.f));
	TestTrue(TEXT("-Y lifts the goal far more than +Y"), Rise(Negative) > Rise(Positive) * 5.f);
	// 開く角度は蝶番の側によらず同じ。
	for (const FTrial* Trial : {&Positive, &Negative})
	{
		const FQuat Closed = FQuat::Identity;
		TestTrue(TEXT("Open angle is the same on both sides"),
		         FMath::IsNearlyEqual(FMath::RadiansToDegrees(Closed.AngularDistance(Trial->Ceiling->GetActorQuat())), Angle, 1.f));
	}
	// 持ち上がる縁は、蝶番の反対側。
	{
		FVector Origin, Extent;
		Positive.Ceiling->GetActorBounds(false, Origin, Extent);
		TestTrue(TEXT("+Y hinge keeps the +Y edge low"), Origin.Z > Positive.Center.Z);
	}
	// リザルトのカメラは、蝶番の側から持ち上がる側を向く。
	for (auto* Lid : {Positive.Lid, Negative.Lid}) Lid->Tick(ClosedAll);
	FTransform ShotPositive, ShotNegative;
	if (TestTrue(TEXT("Both lids give a result shot"), Positive.Lid->ResultShot(ShotPositive) && Negative.Lid->ResultShot(ShotNegative)))
	{
		// Eye と Look の Y の差は Extent.Y*0.8（240cm）、X の差は 200cm。高さの差が約300cm なので、向きの Y は約±0.5 になる。
		const float ForwardPositive = ShotPositive.GetRotation().GetForwardVector().Y;
		const float ForwardNegative = ShotNegative.GetRotation().GetForwardVector().Y;
		TestTrue(*FString::Printf(TEXT("+Y hinge: the camera looks toward -Y, the lifted side (%.2f)"), ForwardPositive), ForwardPositive < -.3f);
		TestTrue(*FString::Printf(TEXT("-Y hinge: the camera looks toward +Y, the lifted side (%.2f)"), ForwardNegative), ForwardNegative > .3f);
		TestTrue(TEXT("+Y hinge: the camera stands on the +Y side"), ShotPositive.GetLocation().Y > Positive.Center.Y);
		TestTrue(TEXT("-Y hinge: the camera stands on the -Y side"), ShotNegative.GetLocation().Y < Negative.Center.Y);
	}
	return !HasAnyErrors();
}
#endif
