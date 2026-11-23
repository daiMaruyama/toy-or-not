#include "TBMomArrivalZone.h"
#include "Character/TBCharacter.h"
#include "Core/TBGameHelpers.h"
#include "Core/TBGameMode.h"
#include "Core/TBGameState.h"
#include "Core/TBPlayerState.h"
#include "Data/TBRuleData.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ATBMomArrivalZone::ATBMomArrivalZone()
{
	bReplicates=true;
	bAlwaysRelevant=true;
	SetNetUpdateFrequency(10);
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Marker=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderCube"));
	Marker->SetupAttachment(RootComponent);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Marker->SetStaticMesh(Cube.Object);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Plain(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	NeutralBoundaryMaterial=Plain.Object;
	BoundaryMaterial=Plain.Object;
	Marker->SetMaterial(0,Plain.Object);
	Marker->SetRelativeLocation(FVector(0,0,12));
	Marker->SetRelativeScale3D(FVector(.24));
	Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Marker->SetHiddenInGame(true);
	Marker->SetCastShadow(false);
	Boundary=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("DashedBoundary"));
	Boundary->SetupAttachment(RootComponent);
	Boundary->SetStaticMesh(Cube.Object);
	Boundary->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Boundary->SetCastShadow(false);
}

void ATBMomArrivalZone::BuildBoundary(float Radius)
{
	DisplayRadius=Radius;
	Boundary->ClearInstances();
	// Keep the serialized component/property names for placed Blueprint compatibility.
	// Only the old emergency-red material adopts the new look; authored custom materials still work.
	const bool UsePaper=!BoundaryMaterial || BoundaryMaterial==NeutralBoundaryMaterial ||
	    BoundaryMaterial->GetPathName()==TEXT("/Game/Events/Mom/M_MomBoundary.M_MomBoundary");
	if (UsePaper && NeutralBoundaryMaterial)
	{
		if (!BoundaryTint) BoundaryTint=UMaterialInstanceDynamic::Create(NeutralBoundaryMaterial,this);
		BoundaryTint->SetVectorParameterValue(TEXT("Color"),FLinearColor(.78f,.64f,.38f));
		float Roughness=0;
		if (NeutralBoundaryMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Roughness")),Roughness))
			BoundaryTint->SetScalarParameterValue(TEXT("Roughness"),.9f);
		Boundary->SetMaterial(0,BoundaryTint);
	}
	else if (BoundaryMaterial) Boundary->SetMaterial(0,BoundaryMaterial);
	// 描画用の寸法。判定は同じ半径の円柱で、Actorの回転・拡大率には依存しない。
	Boundary->SetUsingAbsoluteRotation(true);
	Boundary->SetUsingAbsoluteScale(true);
	// A thin, matte paper ribbon lies just inside the actual standing radius.
	// Slightly overlapping tangent pieces form a smooth continuous ring, without warning-style gaps.
	constexpr int32 Segments=96;
	const float Width=FMath::Min(3.f,Radius*.08f);
	const float RingRadius=Radius-Width*.5f;
	const float Length=2*PI*RingRadius/Segments*1.04f;
	for (int32 I=0;I<Segments;++I)
	{
		const float Angle=2*PI*I/Segments;
		const FVector Position(RingRadius*FMath::Cos(Angle),RingRadius*FMath::Sin(Angle),1);
		Boundary->AddInstance(FTransform(FRotator(0,FMath::RadiansToDegrees(Angle)+90,0),Position,
		    FVector(Length/100,Width/100,.002)));
	}
}

void ATBMomArrivalZone::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (TB::Rules().Events.Mom.IsValid()) BuildBoundary(TB::Rules().Events.Mom.Radius);
}

void ATBMomArrivalZone::BeginPlay()
{
	Super::BeginPlay();
	// Existing placed instances may still serialize the old visible placeholder defaults.
	Marker->SetHiddenInGame(true);
	Marker->SetCastShadow(false);
	// Rebuild even if a placed instance serialized the old radius and 48-dash instance data.
	DisplayRadius=-1;
	OnRep_State();
}

void ATBMomArrivalZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATBMomArrivalZone,State);
}

void ATBMomArrivalZone::OnRep_State()
{
	if (GetNetMode()==NM_DedicatedServer) return;
	if (!FMath::IsNearlyEqual(DisplayRadius,State.Radius)) BuildBoundary(State.Radius);
	Boundary->SetVisibility(State.bActive);
}

ATBMomArrivalZone* ATBMomArrivalZone::FindActive(UWorld* World)
{
	if (!World) return nullptr;
	for (TActorIterator<ATBMomArrivalZone> It(World);It;++It)
		if (!It->IsActorBeingDestroyed() && It->State.bActive) return *It;
	return nullptr;
}

bool ATBMomArrivalZone::StartEvent()
{
	const auto* Match=TB::GS(GetWorld());
	const auto& Rules=TB::Rules().Events.Mom;
	if (!HasAuthority() || !bEnabled || !TB::Playing(GetWorld()) || !Match || Match->IsPreparing() ||
	    !Rules.IsValid() || !HasHumanPlayer(GetWorld()) || FindActive(GetWorld())) return false;
	Round=Match->HidePreparation.Round;
	State=FTBMomArrivalState();
	State.bActive=true;
	State.Remaining=Rules.ArrivalSeconds;
	State.RequiredHold=Rules.PersuadeSeconds;
	State.Radius=Rules.Radius;
	State.Height=Rules.Height;
	OnRep_State();
	ForceNetUpdate();
	return true;
}

void ATBMomArrivalZone::ResetEvent()
{
	if (!HasAuthority()) return;
	State=FTBMomArrivalState();
	OnRep_State();
	ForceNetUpdate();
}

bool ATBMomArrivalZone::ContainsHuman(const ATBCharacter& Human) const
{
	if (!Human.IsHuman() || !Human.GetController() || Human.IsActorBeingDestroyed()) return false;
	const FVector Feet=Human.GetActorLocation()-FVector(0,0,Human.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const FVector Offset=Feet-GetActorLocation();
	return Offset.SizeSquared2D()<=FMath::Square(State.Radius) && Offset.Z>=-15 && Offset.Z<=State.Height &&
	    TB::ClearPath(this,&Human,GetActorLocation()+FVector(0,0,60),Feet+FVector(0,0,60));
}

bool ATBMomArrivalZone::HasHumanPlayer(UWorld* World)
{
	if (!World) return false;
	for (TActorIterator<ATBCharacter> It(World);It;++It)
	{
		const auto* Controller = It->GetController();
		const auto* Player = It->TBPS();
		if (!It->IsActorBeingDestroyed() && It->IsHuman() && Controller &&
		    !Controller->IsActorBeingDestroyed() && Controller->IsPlayerController() &&
		    Player && !Player->IsABot() && !Player->IsInactive()) return true;
	}
	return false;
}

void ATBMomArrivalZone::Advance(float DeltaSeconds)
{
	if (!HasAuthority() || !State.bActive) return;
	const auto* Match=TB::GS(GetWorld());
	if (!TB::Playing(GetWorld()) || !Match || Match->IsPreparing() || Match->HidePreparation.Round!=Round ||
	    !HasHumanPlayer(GetWorld()))
	{
		ResetEvent();
		return;
	}
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds<=0) return;
	State.bOccupied=false;
	for (TActorIterator<ATBCharacter> It(GetWorld());It;++It)
		if (ContainsHuman(**It)) { State.bOccupied=true; break; }
	if (State.bOccupied)
	{
		State.Held=FMath::Min(State.RequiredHold,State.Held+DeltaSeconds);
		if (State.Held>=State.RequiredHold) ResetEvent();
	}
	else
	{
		State.Held=0;
		State.Remaining=FMath::Max(0.f,State.Remaining-DeltaSeconds);
		if (State.Remaining<=0)
		{
			ResetEvent();
			if (auto* Mode=GetWorld()->GetAuthGameMode<ATBGameMode>())
				Mode->Finish(ETBWinner::Toys,TEXT("ママが来た！ 説得が間に合わなかったため人間の負け！"));
		}
	}
}
