#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Character/TBCharacter.h"
#include "Character/TBCharacterGeometry.h"
#include "GamePlay/TBHumanEntrance.h"
#include "Core/TBGameMode.h"
#include "Core/TBGameState.h"
#include "Core/TBPlayerState.h"
#include "GamePlay/Items/TBEscapeVehicle.h"
#include "GamePlay/Items/TBEscapeVehicleMovement.h"
#include "GamePlay/Items/TBEscapeExit.h"
#include "Tests/TBTestMatch.h"
#include "AIController.h"
#include "Components/BoxComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBEscapeVehicleTest, "ToonStory.Items.EscapeVehicle",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBEscapeVehicleTest::RunTest(const FString&)
{
	UWorld* World = nullptr;
	for (const auto& Context : GEngine->GetWorldContexts())
		if (Context.WorldType == EWorldType::Game && Context.World() && Context.World()->GetGameState<ATBGameState>())
		{ World = Context.World(); break; }
	if (!World || World->GetNetMode() != NM_Standalone) return false;
	if (World->GetMapName().Contains(TEXT("Arena")))
	{
		TestTrue(TEXT("Test arena has a vehicle"), bool(TActorIterator<ATBEscapeVehicle>(World)));
		TestTrue(TEXT("Test arena has an exit"), bool(TActorIterator<ATBEscapeExit>(World)));
	}
	auto* Match = World->GetGameState<ATBGameState>();
	auto* Mode = World->GetAuthGameMode<ATBGameMode>();
	auto* PC = World->GetFirstPlayerController();
	auto* Toy = PC ? Cast<ATBCharacter>(PC->GetPawn()) : nullptr;
	if (!TestNotNull(TEXT("Mode"), Mode) || !TestNotNull(TEXT("Toy"), Toy) || !TestNotNull(TEXT("Player"), Toy->TBPS())) return false;
	auto* Info = Toy->TBPS();
	const auto OldTeam = Info->Team;
	const auto OldToyState = Info->ToyState;
	const bool OldFrozen = Info->bFrozen, OldEscape = Match->bEscapePhase;
	const bool OldResult = Match->bVehicleEscapeResult;
	const auto OldIgnition = Match->Ignition;
	const auto OldEscaped = Match->EscapedPlayers;
	const ETBWinner OldWinner = Match->Winner;
	const FString OldReason = Match->Reason;
	const FVector OldLocation = Toy->GetActorLocation();
	// Run the scenario near map startup regardless of test order; restore the world clock on exit.
	constexpr double StartTime = 2.0;
	TGuardValue<double> TestClock(World->TimeSeconds, StartTime);
	TBTest::FPlayingScope Playing(*Match, Match->GetServerWorldTimeSeconds() + 5.0);
	Info->Team = ETBTeam::Toy; Info->ToyState = ETBToyState::Free; Info->bFrozen = false;
	Match->bEscapePhase = false;
	Match->Ignition = {};
	FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Car = World->SpawnActor<ATBEscapeVehicle>(FVector(10000,10000,10000), FRotator::ZeroRotator, Params);
	ATBEscapeExit* Exit = nullptr;
	APlayerController* Guest = nullptr;
	ON_SCOPE_EXIT {
		if (IsValid(Exit)) Exit->Destroy();
		if (IsValid(Car)) Car->Destroy();
		if (IsValid(Guest)) UGameplayStatics::RemovePlayer(Guest, true);
		Info->Team = OldTeam; Info->ToyState = OldToyState; Info->bFrozen = OldFrozen;
		Match->EscapedPlayers = OldEscaped;
		Match->bEscapePhase = OldEscape; Match->bVehicleEscapeResult = OldResult;
		Match->Ignition = OldIgnition;
		Match->Winner = OldWinner; Match->Reason = OldReason;
		Toy->SetActorLocation(OldLocation, false, nullptr, ETeleportType::TeleportPhysics);
		Toy->ApplyState();
	};
	if (!TestNotNull(TEXT("Vehicle"), Car)) return false;
	TestFalse(TEXT("Cannot board before escape phase"), Car->Board(Toy));
	TestTrue(TEXT("Escape phase starts"), Mode->StartEscapePhase());
	TestTrue(TEXT("At least 30 seconds remain"), Match->Remaining() >= 29.9f);
	TestFalse(TEXT("Phase cannot start twice"), Mode->StartEscapePhase());
	TestTrue(TEXT("The car shot runs before boarding opens"), Match->IsIgniting() && !Match->bEscapePhase);
	TestTrue(TEXT("Toy is held during the shot"), Toy->IsMovementLocked() && !Toy->CanAct());
	Info->Team = ETBTeam::Human;
	TestTrue(TEXT("Human is held during the shot"), Toy->IsMovementLocked() && !Toy->CanAct());
	Info->Team = ETBTeam::Toy;
	TestFalse(TEXT("Cannot board during ignition"), Car->Board(Toy));
	auto* ShotCar = Match->Ignition.Vehicle.Get();
	if (!TestNotNull(TEXT("Ignition vehicle"), ShotCar)) return false;
	World->TimeSeconds = StartTime + 34. / 24.;
	ShotCar->Tick(1.f / 24.f);
	TInlineComponentArray<USpotLightComponent*> Lights(ShotCar);
	if (!TestEqual(TEXT("Both headlights exist before ignition"), Lights.Num(), 2)) return false;
	const FTransform LeftMount = Lights[0]->GetComponentTransform().GetRelativeTransform(ShotCar->Mesh->GetComponentTransform());
	TestTrue(TEXT("Headlights start dark"), FMath::IsNearlyZero(Lights[0]->Intensity));
	for (const double Frame : {35., 37., 72., 80.})
	{
		World->TimeSeconds = StartTime + Frame / 24.;
		ShotCar->Tick(1.f / 24.f);
		TestTrue(TEXT("Light comes up while fixed to the moving body"), Lights[0]->Intensity > 0.f &&
			Lights[0]->IsAttachedTo(ShotCar->Mesh) && LeftMount.Equals(
				Lights[0]->GetComponentTransform().GetRelativeTransform(ShotCar->Mesh->GetComponentTransform()), .001));
	}
	constexpr double EndOfShot = StartTime + FTBEscapeIgnition::Duration;
	constexpr float LastFrame = 1.f / 64.f;
	World->TimeSeconds = EndOfShot - LastFrame;
	Mode->Tick(LastFrame);
	TestTrue(TEXT("Ignition still holds the toy on the last frame"), Match->IsIgniting() && Toy->IsMovementLocked());
	TestFalse(TEXT("Cannot board just before ignition ends"), Car->Board(Toy));
	TestTrue(TEXT("Escape clock stays frozen throughout ignition"), FMath::IsNearlyEqual(Match->Remaining(), 30.f, .05f));
	World->TimeSeconds = EndOfShot;
	Mode->Tick(LastFrame);
	if (!TestTrue(TEXT("Ignition ends and opens boarding at the deadline"), !Match->IsIgniting() && Match->bEscapePhase)) return false;
	ShotCar->Tick(LastFrame);
	TestFalse(TEXT("Movement resumes after ignition"), Toy->IsMovementLocked());
	TestTrue(TEXT("Escape clock resumes with the full minimum"), FMath::IsNearlyEqual(Match->Remaining(), 30.f, .05f));
	TestTrue(TEXT("Toy boards"), Car->Board(Toy));
	TestTrue(TEXT("First passenger drives"), Car->IsDriver(Toy));
	TestFalse(TEXT("Passenger is protected from gaze"), Toy->CanBeFrozenBy(Toy));
	Guest = UGameplayStatics::CreatePlayer(World, -1, true);
	ATBCharacter* Passenger = Guest ? Cast<ATBCharacter>(Guest->GetPawn()) : nullptr;
	if (!TestNotNull(TEXT("Second passenger"), Passenger) || !TestNotNull(TEXT("Second player state"), Passenger->TBPS())) return false;
	Passenger->TBPS()->Team = ETBTeam::Toy;
	Passenger->TBPS()->ToyState = ETBToyState::Free;
	Passenger->TBPS()->bFrozen = false;
	TestTrue(TEXT("Second toy boards"), Car->Board(Passenger));
	TestFalse(TEXT("Second passenger is not the driver"), Car->IsDriver(Passenger));
	// NPC は運転も降車もしないので、乗せると席をふさいでしまう。
	auto* Bot = World->SpawnActorDeferred<AAIController>(AAIController::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("Bot controller"), Bot)) return false;
	Bot->bWantsPlayerState = true;
	Bot->FinishSpawning(FTransform::Identity);
	auto* BotToy = World->SpawnActor<ATBCharacter>(Toy->GetClass(), FVector(10000, 10300, 10000), FRotator::ZeroRotator, Params);
	ON_SCOPE_EXIT { if (IsValid(BotToy)) BotToy->Destroy(); if (IsValid(Bot)) Bot->Destroy(); };
	if (!TestNotNull(TEXT("Bot toy"), BotToy)) return false;
	Bot->Possess(BotToy);
	if (!TestNotNull(TEXT("Bot player state"), BotToy->TBPS())) return false;
	BotToy->TBPS()->Team = ETBTeam::Toy;
	BotToy->TBPS()->ToyState = ETBToyState::Free;
	TestFalse(TEXT("NPC toys do not board"), Car->Board(BotToy));
	const FVector Before = Car->GetActorLocation();
	Car->SetDrivingInput(Toy, FVector2D(0, 1));
	Car->Tick(.1f);
	TestTrue(TEXT("Driver advances the vehicle"), Car->GetActorLocation().X > Before.X);
	TestTrue(TEXT("Passenger leaves"), Car->Leave(Toy));
	TestNull(TEXT("Vehicle reference cleared"), Toy->EscapeVehicle.Get());
	TestTrue(TEXT("Driving passes to the next passenger"), Car->IsDriver(Passenger));
	Exit = World->SpawnActor<ATBEscapeExit>(Car->GetActorLocation(), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("Exit"), Exit)) return false;
	Exit->ExitArea->SetBoxExtent(FVector(20, 130, 80));
	Exit->OnConstruction(Exit->GetActorTransform());
	const FBox GateBounds = Exit->Marker->GetStaticMesh()->GetBoundingBox().TransformBy(Exit->Marker->GetRelativeTransform());
	TestTrue(TEXT("Gate follows resized exit width"), FMath::IsNearlyEqual(GateBounds.GetSize().Y, 260., .01));
	TestTrue(TEXT("Gate bottom follows resized exit floor"), FMath::IsNearlyEqual(GateBounds.Min.Z, -80., .01));
	TestTrue(TEXT("Gate stays centred on the exit plane"), FMath::IsNearlyZero(GateBounds.GetCenter().X, .01) && FMath::IsNearlyZero(GateBounds.GetCenter().Y, .01));
	Car->Collision->UpdateOverlaps();
	Exit->ExitArea->UpdateOverlaps();
	TestTrue(TEXT("Vehicle overlaps exit"), Exit->ExitArea->IsOverlappingActor(Car));
	Exit->Tick(.01f);
	TestTrue(TEXT("Passenger is recorded as escaped"), Match->EscapedInVehicle(Passenger->TBPS()));
	TestFalse(TEXT("Toy left behind is not recorded as escaped"), Match->EscapedInVehicle(Info));
	TestTrue(TEXT("Vehicle escape result is recorded"), Match->bVehicleEscapeResult);
	TestEqual(TEXT("Reaching exit ends the match for toys"), Match->Winner, ETBWinner::Toys);
	// 同じマップで次の試合をするときは、置いた位置に戻って空になる。
	Car->ResetRound();
	TestTrue(TEXT("Vehicle returns to where it was placed"), Car->GetActorLocation().Equals(FVector(10000, 10000, 10000), 1.f));
	TestTrue(TEXT("Vehicle is empty after a reset"), Car->Passengers.IsEmpty());
	TestNull(TEXT("Reset releases the passenger"), Passenger->EscapeVehicle.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBEscapeVehicleGroundTest, "ToonStory.Items.EscapeVehicleGround",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBEscapeVehicleGroundTest::RunTest(const FString&)
{
	UWorld* World = nullptr;
	for (const auto& Context : GEngine->GetWorldContexts())
		if (Context.WorldType == EWorldType::Game && Context.World()) { World = Context.World(); break; }
	if (!World || World->GetNetMode() != NM_Standalone) return false;
	FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Base(20000, 20000, 10000);
	TArray<AActor*> Fixtures;
	ON_SCOPE_EXIT { for (AActor* Actor : Fixtures) if (IsValid(Actor)) Actor->Destroy(); };
	auto MakeBox = [&](const FVector& Center, const FVector& Extent)
	{
		auto* Actor = World->SpawnActor<AActor>(Center, FRotator::ZeroRotator, Params);
		if (!Actor) return static_cast<AActor*>(nullptr);
		Fixtures.Add(Actor);
		auto* Box = NewObject<UBoxComponent>(Actor); Actor->SetRootComponent(Box);
		Box->SetBoxExtent(Extent); Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(ECC_WorldStatic); Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent(); Actor->SetActorLocation(Center);
		return Actor;
	};
	if (!TestNotNull(TEXT("Floor"), MakeBox(Base + FVector(0,0,-5), FVector(400,200,5)))) return false;
	if (!TestNotNull(TEXT("Carpet"), MakeBox(Base + FVector(150,0,2), FVector(30,150,2)))) return false;
	if (!TestNotNull(TEXT("Wall"), MakeBox(Base + FVector(300,0,60), FVector(5,150,60)))) return false;
	if (!TestNotNull(TEXT("Lower floor"), MakeBox(Base + FVector(700,0,-110), FVector(250,200,10)))) return false;
	auto* Car = World->SpawnActor<ATBEscapeVehicle>(Base + FVector(0,0,19.5), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("Vehicle"), Car)) return false;
	Fixtures.Add(Car);
	for (int32 I=0; I<30; ++I) Car->VehicleMovement->Advance(FVector::ZeroVector, FRotator::ZeroRotator, .016f);
	TestTrue(TEXT("Overlapping floor resolves with clearance"), Car->GetActorLocation().Z > Base.Z + 20.f);
	TestTrue(TEXT("Ground contact is retained at 60fps"), Car->VehicleMovement->IsGrounded());
	TestTrue(TEXT("Floor clearance remains close to character walking clearance"), Car->GetActorLocation().Z < Base.Z + 24.f);
	const float Before = Car->GetActorLocation().X;
	for (int32 I=0; I<45; ++I) Car->VehicleMovement->Advance(FVector(5,0,0), FRotator::ZeroRotator, .016f);
	TestTrue(TEXT("Vehicle continues after landing and climbs a small step"), Car->GetActorLocation().X > Before + 150.f);
	for (int32 I=0; I<40; ++I) Car->VehicleMovement->Advance(FVector(5,0,0), FRotator::ZeroRotator, .016f);
	TestTrue(TEXT("Vehicle does not climb through a high wall"), Car->GetActorLocation().X < Base.X + 245.f);
	const float BeforeSlide = Car->GetActorLocation().Y;
	for (int32 I=0; I<20; ++I) Car->VehicleMovement->Advance(FVector(5,2,0), FRotator::ZeroRotator, .016f);
	TestTrue(TEXT("Vehicle slides along a wall"), Car->GetActorLocation().Y > BeforeSlide + 20.f);
	Car->SetActorLocation(Base + FVector(460,0,22), false, nullptr, ETeleportType::TeleportPhysics);
	const float EdgeHeight = Car->GetActorLocation().Z;
	for (int32 I=0; I<45; ++I) Car->VehicleMovement->Advance(FVector(5,0,0), FRotator::ZeroRotator, .016f);
	TestTrue(TEXT("Vehicle falls after leaving a ledge"), Car->GetActorLocation().Z < EdgeHeight - 10.f);
	TestTrue(TEXT("Vehicle lands on the lower floor"), Car->VehicleMovement->IsGrounded());
	const float LandedX = Car->GetActorLocation().X;
	for (int32 I=0; I<8; ++I) Car->VehicleMovement->Advance(FVector(5,0,0), FRotator::ZeroRotator, .016f);
	TestTrue(TEXT("Vehicle keeps moving after landing"), Car->GetActorLocation().X > LandedX + 30.f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBParkedVehicleTest, "ToonStory.Items.ParkedVehicle",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBParkedVehicleTest::RunTest(const FString&)
{
	UWorld* World = nullptr;
	for (const auto& C : GEngine->GetWorldContexts()) if (C.WorldType == EWorldType::Game) World = C.World();
	if (!World || World->GetNetMode() != NM_Standalone) return false;
	TActorIterator<ATBEscapeVehicle> It(World);
	auto* Car = It ? *It : nullptr;
	auto* PC = World->GetFirstPlayerController();
	auto* Toy = PC ? PC->GetPawn<ATBCharacter>() : nullptr;
	auto* Match = World->GetGameState<ATBGameState>();
	if (!Car || !Toy || !Toy->TBPS() || !Match) return false;
	TBTest::FPlayingScope Playing(*Match);
	const auto OldTeam = Toy->TBPS()->Team;
	const auto OldToyState = Toy->TBPS()->ToyState;
	const auto OldTransform = Toy->GetActorTransform();
	const auto OldAim = PC->GetControlRotation();
	const bool WasEscape = Match->bEscapePhase, WasFrozen = Toy->TBPS()->bFrozen;
	ON_SCOPE_EXIT {
		Match->bEscapePhase = WasEscape;
		Car->ResetRound();
		Toy->TBPS()->Team = OldTeam; Toy->TBPS()->ToyState = OldToyState; Toy->TBPS()->bFrozen = WasFrozen;
		Toy->SetActorTransform(OldTransform, false, nullptr, ETeleportType::TeleportPhysics);
		Toy->ApplyState();
		PC->SetControlRotation(OldAim);
	};
	Match->bEscapePhase = true;
	Toy->TBPS()->Team = ETBTeam::Toy;
	Toy->TBPS()->ToyState = ETBToyState::Free;
	const FRotator BoardingAim(-18.f, Car->GetActorRotation().Yaw - 27.f, 0.f);
	PC->SetControlRotation(BoardingAim);
	if (!TestTrue(TEXT("Board the authored car"), Car->Board(Toy))) return false;
	TestTrue(TEXT("Boarding preserves the player's look direction"), PC->GetControlRotation().Equals(BoardingAim, .01f));
	FMinimalViewInfo View;
	Toy->CalcCamera(.016f, View);
	const FVector Start = Car->GetActorLocation();
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TBParkedDrivingView), false, Car);
	Query.AddIgnoredActor(Toy);
	TestFalse(TEXT("Boarding camera clears furniture and the floor"), World->OverlapBlockingTestByChannel(
	    View.Location, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(1.f), Query));
	TestFalse(TEXT("Boarding camera clears the visible bodywork"), Car->Mesh->Bounds.GetBox().IsInside(View.Location));
	TestFalse(TEXT("Furniture does not hide the car from the boarding camera"), World->LineTraceTestByChannel(
		View.Location, Car->Mesh->Bounds.Origin, ECC_Camera, Query));
	// The whole parked car should fit a 16:9 gameplay view with room around its silhouette.
	const FBox LocalBody = Car->Mesh->GetStaticMesh()->GetBoundingBox();
	const double HorizontalTangent = FMath::Tan(FMath::DegreesToRadians(View.FOV * .5f));
	bool bBodyInFrame = true;
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector Point(Corner & 1 ? LocalBody.Max.X : LocalBody.Min.X, Corner & 2 ? LocalBody.Max.Y : LocalBody.Min.Y,
			Corner & 4 ? LocalBody.Max.Z : LocalBody.Min.Z);
		const FVector Ray = View.Rotation.UnrotateVector(Car->Mesh->GetComponentTransform().TransformPosition(Point) - View.Location);
		bBodyInFrame &= Ray.X > View.PerspectiveNearClipPlane && FMath::Abs(Ray.Y) < .9 * Ray.X * HorizontalTangent &&
			FMath::Abs(Ray.Z) < .9 * Ray.X * HorizontalTangent * 9. / 16.;
	}
	TestTrue(TEXT("Third-person boarding view frames the entire car"), bBodyInFrame);
	AddInfo(FString::Printf(TEXT("Boarding view: %s, %s, FOV %.2f"), *View.Location.ToString(), *View.Rotation.ToString(), View.FOV));
	for (int32 Frame = 0; Frame < 35; ++Frame)
	{
		Toy->ServerVehicleInput(FVector2D(0, 1));
		Car->Tick(1.f / 60.f);
	}
	AddInfo(FString::Printf(TEXT("Parked %s, camera %s, driven %s"), *Start.ToString(), *View.Location.ToString(), *Car->GetActorLocation().ToString()));
	TestTrue(TEXT("Driver can pull forward out of the parking spot"), FVector::DotProduct(Car->GetActorLocation() - Start, Car->GetActorForwardVector()) > 10.);
	Car->SetActorLocation(FVector(20000, 20000, 10000), false, nullptr, ETeleportType::TeleportPhysics);
	const FRotator FreeLook(19.f, -64.f, 0.f);
	PC->SetControlRotation(FreeLook);
	Toy->CalcCamera(.016f, View);
	const FVector ExpectedEye = Car->GetActorLocation() + FVector::UpVector * 45. - FreeLook.Vector() * 250.;
	TestTrue(TEXT("Open driving view follows the player's yaw and pitch"),
		View.Rotation.Equals(FreeLook, .01f) && View.Location.Equals(ExpectedEye, .1));
	Car->AddActorWorldRotation(FRotator(0, 75, 0));
	Toy->CalcCamera(.016f, View);
	TestTrue(TEXT("Steering does not rotate the player's view"),
		View.Rotation.Equals(FreeLook, .01f) && View.Location.Equals(ExpectedEye, .1));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBEscapeEntranceClearanceTest, "ToonStory.Items.EscapeEntranceClearance",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBEscapeEntranceClearanceTest::RunTest(const FString&)
{
	UWorld* World = nullptr;
	for (const auto& Context : GEngine->GetWorldContexts())
		if (Context.WorldType == EWorldType::Game) World = Context.World();
	if (!TestNotNull(TEXT("Game world"), World)) return false;
	auto* Entrance = ATBHumanEntrance::Find(World);
	if (!TestNotNull(TEXT("Authored human entrance"), Entrance)) return false;
	ATBEscapeExit* Exit = nullptr;
	for (TActorIterator<ATBEscapeExit> It(World); It; ++It) Exit = *It;
	if (!TestNotNull(TEXT("Authored escape gate"), Exit)) return false;
	TestEqual(TEXT("Gate decoration never blocks characters or vehicles"), Exit->Marker->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestEqual(TEXT("Gate trigger only overlaps pawns"), Exit->ExitArea->GetCollisionResponseToChannel(ECC_Pawn), ECR_Overlap);
	const FBox Visual = Exit->Marker->GetStaticMesh()->GetBoundingBox().TransformBy(Exit->Marker->GetComponentTransform());
	const FBox Clearance = Visual.ExpandBy(FVector(TBCharacterGeometry::HumanRadius, TBCharacterGeometry::HumanRadius, TBCharacterGeometry::HumanHalfHeight));
	for (int32 Index = 0; Index < FMath::Min(2, Entrance->DoorSlots.Num()); ++Index)
	{
		const FVector Start = Entrance->DoorSlots[Index]->GetActorLocation();
		FVector End = Entrance->GetActorLocation(); End.Z = Start.Z;
		TestFalse(TEXT("Gate visual leaves the human entrance route clear"), FMath::LineBoxIntersection(Clearance, Start, End, End - Start));
	}
	return !HasAnyErrors();
}
#endif
