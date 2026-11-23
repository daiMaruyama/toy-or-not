#include "TBDummyToy.h"
#include "Character/TBCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Core/TBLobbySettings.h"
#include "Engine/World.h"
#include "EngineUtils.h"

void ATBDummyToy::PrepareStageDolls(UWorld* World)
{
	const int32 Stage = TBLobbySettings::StageIndex(World);
	if (Stage != TBLobbySettings::DemoStageId && Stage != TBLobbySettings::HouseStageId) return;

	TArray<ATBDummyToy*> Dolls;
	for (TActorIterator<ATBDummyToy> It(World); It; ++It) Dolls.Add(*It);
	if (Stage == TBLobbySettings::HouseStageId && Dolls.IsEmpty())
	{
		// Floor spots by the lounge seating and the room rugs in the original House map.
		struct FFloorSpot { float X, Y, FacingYaw; };
		const FFloorSpot Spots[] = {{-68, 346, 35}, {-712, 68, 120},
		                            {-325, -215, 60}, {415, -285, -130}};
		for (const FFloorSpot& Spot : Spots)
		{
			const FVector Above(Spot.X, Spot.Y, 53);
			FHitResult Floor;
			if (!World->LineTraceSingleByChannel(Floor, Above, Above - FVector(0, 0, 65), ECC_WorldStatic) ||
			    Floor.ImpactNormal.Z < .9f) continue;
			const FVector RootPosition = Floor.ImpactPoint + FVector(0, 0, 13);
			if (World->OverlapBlockingTestByChannel(RootPosition, FQuat::Identity, ECC_WorldStatic,
			                                        FCollisionShape::MakeBox(FVector(13, 13, 11)))) continue;
			if (auto* Doll = World->SpawnActor<ATBDummyToy>(RootPosition, FRotator(0, Spot.FacingYaw, 0)))
				Dolls.Add(Doll);
		}
	}
	// Alternate through the existing layout, regardless of the randomly selected visual class.
	Dolls.Sort([](const ATBDummyToy& A, const ATBDummyToy& B) { return A.GetActorLocation().X < B.GetActorLocation().X; });
	for (int32 I = 0; I < Dolls.Num(); ++I)
	{
		Dolls[I]->bLyingAtRest = I % 2 == 1;
		Dolls[I]->OnRep_Placement();
	}
}

void ATBDummyToy::OnRep_Placement()
{
	bRestPoseDirty = true;
	SettleFrames = 12;
	SetActorTickEnabled(true);
}

void ATBDummyToy::Place(const FVector& Position, ATBBox* Box)
{
	if (!HasAuthority())
	{
		return;
	}
	auto* Previous = Holder.Get();
	Holder = nullptr;
	StoredBox = Box;
	bFalling = true;
	FallSpeed = 0;
	OnRep_Holder();
	SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
	if (Previous)
	{
		Previous->CarriedDummy = nullptr;
		Previous->ClearHeldInteraction();
		Previous->ApplyState();
		Previous->ForceNetUpdate();
	}
	ForceNetUpdate();
}

void ATBDummyToy::Fall(float DeltaSeconds)
{
	FallSpeed = FMath::Min(FallSpeed + 980.f * DeltaSeconds, 4000.f);
	const FVector Start = Collision->GetComponentLocation();
	const FVector End = Start - FVector(0, 0, FallSpeed * DeltaSeconds);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DecoyFall), false, this);
	FHitResult Hit;
	if (GetWorld()->SweepSingleByChannel(
	        Hit, Start, End, FQuat::Identity, ECC_GameTraceChannel1,
	        FCollisionShape::MakeCapsule(Collision->GetScaledCapsuleRadius(), Collision->GetScaledCapsuleHalfHeight()),
	        Params))
	{
		SetActorLocation(Hit.Location - Collision->GetRelativeLocation());
		bFalling = false;
		FallSpeed = 0;
		OnRep_Placement();
		ForceNetUpdate();
	}
	else
	{
		SetActorLocation(GetActorLocation() + End - Start);
	}
}
