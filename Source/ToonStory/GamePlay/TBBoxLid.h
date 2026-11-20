// Copyright (c) 2026 Mamodou. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TBBoxLid.generated.h"

class APostProcessVolume;
class UMaterialInstanceDynamic;
class USpotLightComponent;

/** ふたの蝶番がある縁。反対側の縁が持ち上がる */
UENUM(BlueprintType)
enum class ETBLidHingeSide : uint8
{
	PositiveY UMETA(DisplayName = "+Y側"),
	NegativeY UMETA(DisplayName = "-Y側"),
};

/** 箱の蓋。誰かが入れられる・助け出されるたびに、天井が光って開いたように見せる。各PCで再生する。 */
UCLASS()
class TOONSTORY_API ATBBoxLid : public AActor
{
	GENERATED_BODY()
public:
	ATBBoxLid();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, Category = "Lid")
	TObjectPtr<USpotLightComponent> Light;
	// 蓋にあたる天井。開き具合に合わせてマテリアルの Glow を上げる。
	UPROPERTY(EditInstanceOnly, Category = "Lid")
	TObjectPtr<AActor> Ceiling;
	// 開いた瞬間だけ部屋全体を明るくする。
	UPROPERTY(EditInstanceOnly, Category = "Lid")
	TObjectPtr<APostProcessVolume> RoomVolume;
	// 蝶番を付ける縁（ワールドのY方向）。反対側が持ち上がる。脱出のゴールの真上を大きく持ち上げるなら、ゴールから遠い側にする。
	UPROPERTY(EditInstanceOnly, Category = "Lid", meta = (DisplayName = "蝶番の側"))
	ETBLidHingeSide HingeSide = ETBLidHingeSide::PositiveY;
	// 開く角度・光の強さ・時間は、救助・収納も脱出もマスターデータ（TB::PresentationData().Lid）にある。

	/** リザルトの演出で開き具合を決める。負なら手放して閉じたふたに戻す */
	void HoldForResult(float Alpha);
	/** 箱の部屋の床から、ふたを見上げるカメラの位置。天井が無ければfalse */
	bool ResultShot(FTransform& Out) const;
	static ATBBoxLid* Find(const UWorld* World);

private:
	// 一回の開閉の大きさと時間。救助・収納と脱出で値が違う。
	struct FPlay
	{
		float Angle = 0.f;
		float PeakIntensity = 0.f;
		float OpenSeconds = .25f;
		float HoldSeconds = 0.f;
		float CloseSeconds = .8f;
		float FlashExposure = 0.f;
		float FlashSeconds = .35f;
		bool bEscape = false;
	};
	// 蝶番のあるY方向の符号（+Yなら1、-Yなら-1）。
	float HingeSign() const;
	static FPlay StandardPlay();
	static FPlay EscapePlay();
	void Start(const FPlay& Next);
	// 脱出の開きは、新しい合図から EscapeReleaseDelay の間は閉じ始めない。足りないときだけ、開いたままの時間を延ばす。
	void ExtendEscapeHold();
	FPlay Play;
	bool bResultHeld = false;
	bool bBoundToBox = false;
	float Elapsed = -1.f;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CeilingMaterial;
	FTransform CeilingRest;
	// 閉じた天井の範囲。リザルトのカメラは、決着の瞬間にふたが開いていてもこの範囲から決める。
	FVector ClosedOrigin = FVector::ZeroVector;
	FVector ClosedExtent = FVector::ZeroVector;
	FVector Hinge = FVector::ZeroVector;
	float OpenSign = 1.f;
	float RestExposure = 0.f;
	bool bRestExposureOverride = false;
	void Open();
	void OpenForEscape();
	void SetOpen(float Alpha);
	void SetFlash(float Alpha);
};
