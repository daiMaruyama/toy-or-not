#pragma once

#include "CoreMinimal.h"
#include "GamePlay/Items/TBItemBase.h"
#include "TBEscapeVehicle.generated.h"

class ATBCharacter;
class UBoxComponent;
class UTBEscapeVehicleData;
class UTBEscapeVehicleMovement;

/** 仮の脱出車両。状態の決定と移動はサーバーのみで行う。リザルトの演出の走りだけは、参加側も同じ式で動かす。 */
UCLASS(Blueprintable)
class TOONSTORY_API ATBEscapeVehicle : public ATBItemBase
{
	GENERATED_BODY()
public:
	ATBEscapeVehicle();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	bool Board(ATBCharacter* Toy);
	bool Leave(ATBCharacter* Toy);
	// 同じマップで次の試合を始めるとき、置いた位置に戻して空にする。
	void ResetRound();
	void SetDrivingInput(ATBCharacter* Toy, FVector2D Input);
	/** リザルトの演出で、決着の時刻からCover秒かけてToへ走らせる。操作は受け付けない */
	void StartResultDrive(const FVector& To, double StartTime, float CoverSeconds);
	/** 演出を終えて、乗っている子を降ろしてWhereに止める */
	void ParkForResults(const FTransform& Where);
	/** リザルトの演出で走っている途中（まだ止めていない） */
	bool IsResultDriving() const { return ResultStart >= 0; }
	const UTBEscapeVehicleData& Data() const;
	bool IsDriver(const ATBCharacter* Toy) const;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="脱出車両")
	TObjectPtr<UBoxComponent> Collision;
	UPROPERTY(VisibleAnywhere, Category="脱出車両")
	TObjectPtr<UBoxComponent> BoardingArea;
	UPROPERTY(VisibleAnywhere, Category="脱出車両")
	TObjectPtr<UTBEscapeVehicleMovement> VehicleMovement;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="脱出車両")
	TArray<TObjectPtr<ATBCharacter>> Passengers;
	UPROPERTY(EditAnywhere, Category="脱出車両")
	TSoftObjectPtr<UTBEscapeVehicleData> VehicleData;
protected:
	virtual bool CanPerformAction_Implementation(const FTBInteractionContext& Context) const override;
	virtual bool PerformAction_Implementation(const FTBInteractionContext& Context) override;
private:
	UPROPERTY(Transient)
	TObjectPtr<UTBEscapeVehicleData> LoadedData;
	FVector2D DriveInput = FVector2D::ZeroVector;
	double LastInputAt = 0;
	FTransform StartTransform;
	// The shot and the decorative lamps share an origin at the parked mesh's center.
	FVector ParkedMeshLocation;
	FVector ShotCenter;
	UPROPERTY()
	TObjectPtr<class UMaterialInterface> LensMaterial;
	UPROPERTY()
	TObjectPtr<class UStaticMesh> LensPlane;
	UPROPERTY()
	TObjectPtr<class USoundBase> IgnitionSound;
	UPROPERTY()
	TObjectPtr<class USoundBase> ToyIgnitionSound;
	UPROPERTY(Transient)
	TObjectPtr<class USceneComponent> LampRig;
	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> LensGlow;
	UPROPERTY(Transient)
	TArray<TObjectPtr<class USpotLightComponent>> Headlights;
	UPROPERTY(Transient)
	TObjectPtr<class UAudioComponent> MotorAudio;
	void StartIgnitionEffects(float Elapsed, USoundBase* Sound);
	void UpdateIgnitionEffects(const class ATBGameState* Match);
	void StopIgnitionEffects();
	// リザルトの演出の走り。開始時刻が負なら走っていない。参加側も位置の同期を待たず、サーバー時刻からこの線の上の位置を出す。
	UPROPERTY(Replicated)
	FVector ResultFrom = FVector::ZeroVector;
	UPROPERTY(Replicated)
	FVector ResultTo = FVector::ZeroVector;
	UPROPERTY(Replicated)
	double ResultStart = -1;
	UPROPERTY(Replicated)
	float ResultCover = 0.f;
	void ReleasePassengers();
	UFUNCTION()
	void OnBoardingOverlap(UPrimitiveComponent* Component, AActor* Other, UPrimitiveComponent* OtherComponent,
	                       int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);
};
