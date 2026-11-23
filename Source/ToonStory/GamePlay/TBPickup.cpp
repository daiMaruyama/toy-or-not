// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "TBPickup.h"
#include "Audio/TBAudioEvents.h"
#include "Core/TBPlayerState.h"
#include "Core/TBGameState.h"
#include "Core/TBGameMode.h"
#include "Character/TBCharacter.h"
#include "Core/TBGameHelpers.h"
#include "Data/TBPresentationData.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ATBPickup::ATBPickup()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.bAllowTickOnDedicatedServer = false;
	PrimaryActorTick.TickInterval = .05f;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	Contact = CreateDefaultSubobject<USphereComponent>(TEXT("Contact"));
	SetRootComponent(Contact);
	Contact->SetSphereRadius(30.f);
	Contact->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Contact->SetCollisionResponseToAllChannels(ECR_Ignore);
	Contact->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Contact->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Overlap);
	Contact->SetGenerateOverlapEvents(true);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Contact);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		Mesh->SetStaticMesh(Sphere.Object);
	}
	Mesh->SetRelativeScale3D(FVector(.35));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// The translucent rim overlay uses the standard mesh pass, including Nanite source assets.
	Mesh->bDisallowNanite = true;
}

void ATBPickup::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshAppearance();
}

void ATBPickup::BeginPlay()
{
	Super::BeginPlay();
	RefreshAppearance();
	OnRep_Taken();
}

void ATBPickup::RefreshAppearance()
{
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (auto* Material = PickupRimMaterial.LoadSynchronous())
		{
			if (!RimMaterial || RimMaterial->Parent != Material)
			{
				RimMaterial = UMaterialInstanceDynamic::Create(Material, this);
				Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("RimColor")), RimColor);
				AppliedHighlight = -1.f;
			}
		}
		ApplyHighlight(0.f);
	}
	if (auto* Visual = PickupMesh.LoadSynchronous())
	{
		Mesh->SetStaticMesh(Visual);
		const FBoxSphereBounds Bounds = Visual->GetBounds();
		const float Scale = 16.f / FMath::Max(1.f, float(Bounds.BoxExtent.Z * 2));
		Mesh->SetRelativeScale3D(FVector(Scale));
		Mesh->SetRelativeLocation(FVector(0, 0, -15.f - (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale));
	}
}

void ATBPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateLocalHighlight();
}

void ATBPickup::UpdateLocalHighlight()
{
	const auto* GI = GetGameInstance();
	const auto* PC = GI ? GI->GetFirstLocalPlayerController(GetWorld()) : nullptr;
	const auto* Observer = PC ? Cast<ATBCharacter>(PC->GetPawn()) : nullptr;
	float Weight = 0.f;
	if (PC && PC->IsLocalController() && Observer && PC->GetViewTarget() == Observer)
	{
		FVector Eye;
		FRotator Rotation;
		PC->GetPlayerViewPoint(Eye, Rotation);
		Weight = HighlightWeight(Observer, Eye, Rotation.Vector());
	}
	ApplyHighlight(Weight);
}

float ATBPickup::HighlightWeight(const ATBCharacter* Observer, const FVector& Eye, const FVector& Forward) const
{
	const auto* Player = Observer ? Observer->TBPS() : nullptr;
	if (bTaken || IsHidden() || !TB::Playing(GetWorld()) || !Player || Player->Team != ETBTeam::Toy ||
	    Player->ToyState != ETBToyState::Free || Observer->EscapeVehicle || Eye.ContainsNaN() || Forward.ContainsNaN()) return 0.f;
	const auto& Feel = TB::PresentationData().Pickup;
	if (!FMath::IsFinite(Feel.HighlightDistance) || !FMath::IsFinite(Feel.HighlightHalfAngle) ||
	    !FMath::IsFinite(Feel.HighlightStrength) || Feel.HighlightDistance <= 0.f || Feel.HighlightStrength <= 0.f) return 0.f;
	const FVector Target = Mesh->Bounds.Origin;
	const FVector Delta = Target - Eye;
	const double Distance = Delta.Size();
	if (Distance <= UE_SMALL_NUMBER || Distance >= Feel.HighlightDistance) return 0.f;
	const float HalfAngle = FMath::Clamp(Feel.HighlightHalfAngle, 1.f, 80.f);
	const float Alignment = FVector::DotProduct(Forward.GetSafeNormal(), Delta / Distance);
	const float Outer = FMath::Cos(FMath::DegreesToRadians(HalfAngle));
	const float Inner = FMath::Cos(FMath::DegreesToRadians(HalfAngle * .65f));
	if (Alignment <= Outer || !TB::ClearPath(Observer, this, Eye, Target)) return 0.f;
	const float RangeFade = 1.f - FMath::SmoothStep(Feel.HighlightDistance * .8f, Feel.HighlightDistance, float(Distance));
	return FMath::Clamp(Feel.HighlightStrength, 0.f, 1.f) * FMath::SmoothStep(Outer, Inner, Alignment) * RangeFade;
}

void ATBPickup::ApplyHighlight(float Weight)
{
	const bool bWasVisible = AppliedHighlight > 0.f;
	const bool bVisible = Weight > 0.f;
	if (!RimMaterial || (bVisible == bWasVisible && FMath::IsNearlyEqual(Weight, AppliedHighlight, .001f))) return;
	RimMaterial->SetVectorParameterValue(TEXT("RimColor"), RimColor * Weight);
	if (bVisible != bWasVisible)
		Mesh->SetOverlayMaterial(bVisible ? RimMaterial.Get() : nullptr);
	AppliedHighlight = Weight;
}

bool ATBPickup::Touches(const ATBCharacter* ToyCharacter) const
{
	return !bTaken && ToyCharacter && Contact->IsOverlappingActor(ToyCharacter) &&
	       TB::ClearPath(ToyCharacter, this, ToyCharacter->GetPawnViewLocation(), GetActorLocation());
}

void ATBPickup::OnRep_Taken()
{
	SetActorHiddenInGame(bTaken);
	Contact->SetCollisionEnabled(bTaken ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
	SetActorTickEnabled(!bTaken && GetNetMode() != NM_DedicatedServer);
	if (bTaken) ApplyHighlight(0.f);
}

// 接触と進捗を再確認する。取得済みフラグを先に立てて二重取得を防ぐ。
bool ATBPickup::Take(ATBCharacter* ToyCharacter)
{
	if (!HasAuthority() || bTaken || !ToyCharacter || !ToyCharacter->CanAct() || ToyCharacter->IsHuman())
	{
		return false;
	}
	if (ToyCharacter->ContactItem != this || ToyCharacter->ItemProgress < 1.f || !Touches(ToyCharacter))
	{
		return false;
	}
	auto* MatchInfo = TB::GS(GetWorld());
	if (!MatchInfo || !ToyCharacter->TBPS())
	{
		return false;
	}
	bTaken = true;
	OnRep_Taken();
	ForceNetUpdate();
	++MatchInfo->Collected;
	++ToyCharacter->TBPS()->Items;
	if (MatchInfo->Settings.RequiredItems - MatchInfo->Collected == 1)
		UTBAudioEvents::Emit(this, ETBSoundEvent::BatteryLast, ToyCharacter->TBPS(), GetActorLocation());
	UTBAudioEvents::Emit(this, ETBSoundEvent::Pickup, ToyCharacter->TBPS(), GetActorLocation());
	if (auto* GameMode = GetWorld()->GetAuthGameMode<ATBGameMode>())
	{
		GameMode->CheckWin();
	}
	return true;
}

void ATBPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATBPickup, bTaken);
}
