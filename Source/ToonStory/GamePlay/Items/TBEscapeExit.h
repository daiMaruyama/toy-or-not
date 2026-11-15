#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TBEscapeExit.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/** レベル担当者が車両の出口に設置する領域。 */
UCLASS(Blueprintable)
class TOONSTORY_API ATBEscapeExit : public AActor
{
	GENERATED_BODY()
public:
	ATBEscapeExit();
	virtual void Tick(float DeltaSeconds) override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="脱出")
	TObjectPtr<UBoxComponent> ExitArea;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="脱出")
	TObjectPtr<UStaticMeshComponent> Marker;
	/** 脱出の決着で、車が横切るのを映すカメラ。門を抜けた先の脇に置き、来る車へ向ける */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="脱出")
	TObjectPtr<USceneComponent> ResultCamera;
	/** リザルトで脱出したおもちゃを並べる列の中心。床の高さに置き、向いた方へ顔を向ける */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="脱出")
	TObjectPtr<USceneComponent> Lineup;
	/** 車がカメラの前を横切りきる位置（門の通過方向の線上） */
	FVector ResultPassPoint() const;
	/** 列のIndex番目（Count人中）の立ち位置 */
	FTransform LineupSpot(int32 Index, int32 Count) const;
	/** リザルトで車を止めておく位置。列の後ろ、門の手前 */
	FTransform ParkSpot() const;
};
