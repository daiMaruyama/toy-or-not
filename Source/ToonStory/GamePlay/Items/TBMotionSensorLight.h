#pragma once

#include "CoreMinimal.h"
#include "GamePlay/Items/TBItemBase.h"
#include "TBMotionSensorLight.generated.h"

class UPointLightComponent;
class UTBMotionSensorLightData;
class ATBCharacter;

UCLASS(Blueprintable, meta=(DisplayName="人感センサーライト"))
class TOONSTORY_API ATBMotionSensorLight : public ATBItemBase
{
	GENERATED_BODY()

public:
	ATBMotionSensorLight();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	bool Illuminates(const FVector& Position, const AActor* Target) const;
	bool PutDown(ATBCharacter* Human);
	void ReturnToPlacement();
	void ResetPlacement();
	static const FName PutDownAction;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="人感センサーライト")
	TObjectPtr<UPointLightComponent> PointLight;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="人感センサーライト")
	TSoftObjectPtr<UTBMotionSensorLightData> SensorData;

protected:
	virtual bool CanPerformAction_Implementation(const FTBInteractionContext& Context) const override;
	virtual bool PerformAction_Implementation(const FTBInteractionContext& Context) override;

private:
	const UTBMotionSensorLightData& Data() const;
	bool Reaches(const FVector& Position, const AActor* Target) const;
	void FinishPlacement(const FTransform& Placement);
	void Scan();
	void SetLit(bool bOn);

	UFUNCTION()
	void OnRep_Lit();
	UPROPERTY(ReplicatedUsing=OnRep_Lit)
	bool bLit = false;
	UPROPERTY(Transient)
	TObjectPtr<UTBMotionSensorLightData> LoadedData;
	TWeakObjectPtr<ATBCharacter> Holder;
	FTransform InitialPlacement;
	FTransform LastPlacement;
	FTimerHandle ScanTimer;
	double ArmedAt = 0;
	double LitUntil = 0;
};
