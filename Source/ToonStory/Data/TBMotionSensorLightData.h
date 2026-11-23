#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TBMotionSensorLightData.generated.h"

UCLASS(BlueprintType)
class TOONSTORY_API UTBMotionSensorLightData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="人感センサーライト", meta=(DisplayName="検知・照明範囲", ClampMin="1", Units="cm"))
	float Radius = 250.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="人感センサーライト", meta=(DisplayName="検知後の点灯時間", ClampMin="0.1", Units="s"))
	float LightSeconds = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="人感センサーライト", meta=(DisplayName="設置から起動までの時間", ClampMin="0", Units="s"))
	float ArmSeconds = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="人感センサーライト", meta=(DisplayName="明るさ", ClampMin="0"))
	float Intensity = 1000.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="人感センサーライト", meta=(DisplayName="操作距離", ClampMin="1", Units="cm"))
	float InteractionDistance = 200.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="人感センサーライト", meta=(DisplayName="照らされた場所を明所として扱う"))
	bool bOverridesDarkness = true;
};
