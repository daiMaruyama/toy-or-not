// Copyright (c) 2026 Mamodou. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/TBTypes.h"
#include "TBPickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class ATBCharacter;
class ATBPickup;

/** 接触による取得を確定し、取得済み状態を同期する。 */
UCLASS(Config = Game)
class TOONSTORY_API ATBPickup : public AActor
{
	GENERATED_BODY()
public:
	ATBPickup();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	// 保存済みマップと検証用Arenaで同じ収集物を使用する。
	UPROPERTY(Config, EditDefaultsOnly, Category = "Appearance")
	TSoftObjectPtr<UStaticMesh> PickupMesh;
	// Depth-tested rim: identify collectibles without revealing them through walls.
	UPROPERTY(Config, EditDefaultsOnly, Category = "Appearance")
	TSoftObjectPtr<UMaterialInterface> PickupRimMaterial;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<USphereComponent> Contact;
	// 回収にかかる秒数はマスターデータ（TB::Rules().Pickup）にある。
	bool Touches(const ATBCharacter* C) const;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(ReplicatedUsing = OnRep_Taken, BlueprintReadOnly)
	bool bTaken = false;
	UFUNCTION()
	void OnRep_Taken();
	bool Take(ATBCharacter* Who);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	void RefreshAppearance();
	void UpdateLocalHighlight();
	float HighlightWeight(const ATBCharacter* Observer, const FVector& Eye, const FVector& Forward) const;
	void ApplyHighlight(float Weight);
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RimMaterial;
	FLinearColor RimColor = FLinearColor::White;
	float AppliedHighlight = -1.f;
	friend class FTBBatteryHighlightTest;
};
