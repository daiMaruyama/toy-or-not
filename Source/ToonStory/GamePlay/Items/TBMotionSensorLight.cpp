#include "GamePlay/Items/TBMotionSensorLight.h"
#include "Character/TBCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TBGameHelpers.h"
#include "Core/TBGameState.h"
#include "Core/TBPlayerState.h"
#include "Data/TBMotionSensorLightData.h"
#include "Data/TBRuleData.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GamePlay/Items/TBDecoyToy.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FName PickUpAction(TEXT("PickUp"));
	constexpr float ScanInterval = .1f;
}

const FName ATBMotionSensorLight::PutDownAction(TEXT("PutDown"));

ATBMotionSensorLight::ATBMotionSensorLight()
{
	SetReplicateMovement(true);
	DisplayName = NSLOCTEXT("TBItems", "MotionSensorLight", "人感センサーライト");
	FTBItemAction PickUp;
	PickUp.Id = PickUpAction;
	PickUp.Label = NSLOCTEXT("TBItems", "SensorPickUp", "持つ");
	PickUp.AllowedTeams = ETBInteractionTeams::HumanOnly;
	FTBItemAction PutDown = PickUp;
	PutDown.Id = PutDownAction;
	PutDown.Label = NSLOCTEXT("TBItems", "SensorPutDown", "置く");
	PutDown.bRequiresLineOfSight = false;
	Actions = {PickUp, PutDown};

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Mesh->SetStaticMesh(Cylinder.Object);
	Mesh->SetRelativeLocation(FVector(0, 0, 4));
	Mesh->SetRelativeScale3D(FVector(.22f, .22f, .08f));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InteractionPoint->SetRelativeLocation(FVector(0, 0, 9));

	PointLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("SensorLight"));
	PointLight->SetupAttachment(Root);
	PointLight->SetRelativeLocation(FVector(0, 0, 12));
	PointLight->SetMobility(EComponentMobility::Movable);
	PointLight->SetVisibility(false);
	SensorData = TSoftObjectPtr<UTBMotionSensorLightData>(
	    FSoftObjectPath(TEXT("/Game/Data/DA_MotionSensorLight.DA_MotionSensorLight")));
}

const UTBMotionSensorLightData& ATBMotionSensorLight::Data() const
{
	return LoadedData ? *LoadedData : *GetDefault<UTBMotionSensorLightData>();
}

void ATBMotionSensorLight::BeginPlay()
{
	Super::BeginPlay();
	LoadedData = SensorData.LoadSynchronous();
	for (auto& Action : Actions) Action.MaxDistance = FMath::Max(1.f, Data().InteractionDistance);
	PointLight->SetAttenuationRadius(FMath::Max(1.f, Data().Radius));
	PointLight->SetIntensity(FMath::Max(0.f, Data().Intensity));
	OnRep_Lit();
	if (HasAuthority())
	{
		InitialPlacement = LastPlacement = GetActorTransform();
		ArmedAt = GetWorld()->GetTimeSeconds() + FMath::Max(0.f, Data().ArmSeconds);
		GetWorldTimerManager().SetTimer(ScanTimer, this, &ATBMotionSensorLight::Scan, ScanInterval, true);
	}
}

void ATBMotionSensorLight::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(ScanTimer);
	if (auto* Human = Holder.Get(); Human && Human->CarriedSensorLight == this)
	{
		Human->CarriedSensorLight = nullptr;
	}
	Super::EndPlay(Reason);
}

bool ATBMotionSensorLight::CanPerformAction_Implementation(const FTBInteractionContext& Context) const
{
	const auto* Human = Context.InstigatorCharacter.Get();
	if (!Human || !Human->IsHuman() || !TB::Playing(GetWorld())) return false;
	if (Context.ActionId == PutDownAction) return Human->CarriedSensorLight == this;
	return Context.ActionId == PickUpAction && !GetAttachParentActor() &&
	    !Human->CarriedSensorLight && !Human->HasCarriedToy();
}

bool ATBMotionSensorLight::PerformAction_Implementation(const FTBInteractionContext& Context)
{
	auto* Human = Context.InstigatorCharacter.Get();
	if (Context.ActionId == PutDownAction) return PutDown(Human);
	if (!AttachToComponent(Human->CarryAnchor, FAttachmentTransformRules::SnapToTargetNotIncludingScale)) return false;
	Holder = Human;
	Human->CarriedSensorLight = this;
	SetLit(false);
	Human->ForceNetUpdate();
	ForceNetUpdate();
	return true;
}

bool ATBMotionSensorLight::PutDown(ATBCharacter* Human)
{
	if (!HasAuthority() || !Human || Holder.Get() != Human) return false;

	const FVector From = Human->GetPawnViewLocation();
	const FVector Forward = FRotator(0, Human->GetControlRotation().Yaw, 0).Vector();
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TBSensorPlacement), false, Human);
	Query.AddIgnoredActor(this);
	const FTransform Upright(FRotator::ZeroRotator, FVector::ZeroVector, GetActorScale3D());
	const FBoxSphereBounds Bounds = Mesh->CalcBounds(Mesh->GetRelativeTransform() * Upright);
	const FVector Extent = Bounds.BoxExtent.ComponentMax(FVector(1));
	const auto Shape = FCollisionShape::MakeBox(Extent);
	const double FeetZ = Human->GetActorLocation().Z - Human->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	for (const double Distance : {static_cast<double>(TB::Rules().Aim.DropDistance), 0.0})
	{
		const FVector Start = From + Forward * Distance + FVector(Bounds.Origin.X, Bounds.Origin.Y, 0);
		FHitResult Hit;
		if (!From.Equals(Start) &&
		    GetWorld()->SweepSingleByChannel(Hit, From, Start, FQuat::Identity, ECC_Visibility, Shape, Query)) continue;
		const FVector End(Start.X, Start.Y, FeetZ - 100.f);
		if (!GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility, Shape, Query) ||
		    Hit.bStartPenetrating || Hit.ImpactNormal.Z < .9f) continue;
		const FVector Position = Hit.Location - Bounds.Origin + FVector(0, 0, .5f);
		FinishPlacement(FTransform(FRotator::ZeroRotator, Position, GetActorScale3D()));
		return true;
	}
	return false;
}

void ATBMotionSensorLight::FinishPlacement(const FTransform& Placement)
{
	if (auto* Human = Holder.Get(); Human && Human->CarriedSensorLight == this)
	{
		Human->CarriedSensorLight = nullptr;
		Human->ForceNetUpdate();
	}
	Holder.Reset();
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetActorTransform(Placement, false, nullptr, ETeleportType::TeleportPhysics);
	LastPlacement = Placement;
	ArmedAt = GetWorld()->GetTimeSeconds() + FMath::Max(0.f, Data().ArmSeconds);
	LitUntil = 0;
	SetLit(false);
	ForceNetUpdate();
}

void ATBMotionSensorLight::ReturnToPlacement()
{
	if (HasAuthority()) FinishPlacement(LastPlacement);
}

void ATBMotionSensorLight::ResetPlacement()
{
	if (HasAuthority()) FinishPlacement(InitialPlacement);
}

bool ATBMotionSensorLight::Reaches(const FVector& Position, const AActor* Target) const
{
	const FVector Origin = PointLight->GetComponentLocation();
	return FVector::DistSquared(Origin, Position) <= FMath::Square(FMath::Max(1.f, Data().Radius)) &&
	    TB::ClearPath(this, Target, Origin, Position);
}

bool ATBMotionSensorLight::Illuminates(const FVector& Position, const AActor* Target) const
{
	return bLit && Data().bOverridesDarkness && Data().Intensity > 0.f && Reaches(Position, Target);
}

void ATBMotionSensorLight::Scan()
{
	const auto* State = TB::GS(GetWorld());
	if (!State || State->Phase != ETBPhase::Playing || State->IsPreparing() || Holder.IsValid())
	{
		LitUntil = 0;
		SetLit(false);
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < ArmedAt) return;
	bool bDetected = false;
	for (TActorIterator<ATBCharacter> It(GetWorld()); It; ++It)
	{
		const auto* Player = It->TBPS();
		if (Player && Player->Team == ETBTeam::Toy && Player->ToyState == ETBToyState::Free &&
		    !It->GetVelocity().IsNearlyZero() && Reaches(It->GetActorLocation(), *It))
		{
			bDetected = true;
			break;
		}
	}
	if (!bDetected)
	{
		for (TActorIterator<ATBDecoyToy> It(GetWorld()); It; ++It)
		{
			if (It->bWalking && Reaches(It->GetInteractionLocation(), *It))
			{
				bDetected = true;
				break;
			}
		}
	}
	if (bDetected) LitUntil = Now + FMath::Max(.1f, Data().LightSeconds);
	SetLit(Now < LitUntil);
}

void ATBMotionSensorLight::SetLit(bool bOn)
{
	if (bLit == bOn) return;
	bLit = bOn;
	OnRep_Lit();
	ForceNetUpdate();
}

void ATBMotionSensorLight::OnRep_Lit()
{
	PointLight->SetVisibility(bLit);
}

void ATBMotionSensorLight::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const
{
	Super::GetLifetimeReplicatedProps(Out);
	DOREPLIFETIME(ATBMotionSensorLight, bLit);
}
