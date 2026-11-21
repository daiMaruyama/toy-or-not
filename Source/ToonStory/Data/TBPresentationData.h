// Copyright (c) 2026 Mamodou. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TBPresentationData.generated.h"

USTRUCT(BlueprintType)
struct FTBLidFeel
{
	GENERATED_BODY()
	/** 開くときに天井（蓋）を持ち上げる角度 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "箱のふた",
	          meta = (DisplayName = "開く角度", Units = "Degrees", ClampMin = "0", ClampMax = "80"))
	float OpenAngle = 60.f;
	/** 開いた隙間から差し込む光の、いちばん明るいときの強さ */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "箱のふた", meta = (DisplayName = "光の強さ", ClampMin = "0"))
	float PeakIntensity = 30000.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "箱のふた",
	          meta = (DisplayName = "開くのにかかる時間", Units = "s", ClampMin = "0.01"))
	float OpenSeconds = .25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "箱のふた",
	          meta = (DisplayName = "開いたままの時間", Units = "s", ClampMin = "0"))
	float HoldSeconds = .9f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "箱のふた",
	          meta = (DisplayName = "閉じるのにかかる時間", Units = "s", ClampMin = "0.01"))
	float CloseSeconds = .8f;
	/** 開いた瞬間だけ部屋全体を明るくする量（露出の補正） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "箱のふた", meta = (DisplayName = "部屋が明るくなる量", ClampMin = "0"))
	float FlashExposure = 2.5f;
	/** 光が引いてから開いた蓋が見えるよう、蓋より早く戻す */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "箱のふた",
	          meta = (DisplayName = "部屋の明るさが戻るまで", Units = "s", ClampMin = "0.01"))
	float FlashSeconds = .35f;
};

/** 決着からリザルトの画面までの演出。秒は決着の瞬間から数える */
USTRUCT(BlueprintType)
struct FTBResultFeel
{
	GENERATED_BODY()
	/** 決着の瞬間を、そのままの画で見せる時間 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "リザルト",
	          meta = (DisplayName = "決着を見せる時間", Units = "s", ClampMin = "0"))
	float DecisiveSeconds = .8f;
	/** 車・ふたで画面が覆われきる時刻。ここで配置とカメラを切り替える */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "リザルト",
	          meta = (DisplayName = "覆いきる時刻", Units = "s", ClampMin = "0.1"))
	float CoverSeconds = 3.2f;
	/** 覆いが抜けて結果の画面が見えるまで */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "リザルト",
	          meta = (DisplayName = "覆いが抜けるまで", Units = "s", ClampMin = "0.05"))
	float RevealSeconds = .55f;
	/** 車とふたで、覆いきる直前に暗くしはじめる時間（カメラの前を横切る車の隙間を隠す） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "リザルト",
	          meta = (DisplayName = "暗くしはじめる時間", Units = "s", ClampMin = "0"))
	float DarkenSeconds = .15f;
	/** ふたが閉まりきるまで（覆いきる時刻で閉まりきる） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "リザルト",
	          meta = (DisplayName = "ふたが閉まる時間", Units = "s", ClampMin = "0.05"))
	float LidSlamSeconds = .35f;
	// Retain the former timing property for existing assets and Blueprint references.
	UPROPERTY(BlueprintReadOnly, Category = "リザルト", meta = (DeprecatedProperty, DeprecationMessage = "Result speech bubbles have been retired."))
	float MomLineSeconds = 2.5f;
};

USTRUCT(BlueprintType)
struct FTBTelevisionFeel
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "テレビ", meta = (DisplayName = "映像の解像度"))
	FIntPoint CaptureResolution = FIntPoint(512, 288);
	/** 1秒に何回、映像を撮り直すか */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "テレビ",
	          meta = (DisplayName = "映像の更新回数（毎秒）", ClampMin = "1", ClampMax = "30"))
	float CaptureRate = 15.f;
	/** チャンネルを変えたときに砂嵐を挟む時間 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "テレビ",
	          meta = (DisplayName = "切り替えの砂嵐", Units = "s", ClampMin = "0", ClampMax = "2"))
	float NoiseSeconds = .4f;
	/** 箱に入れられた瞬間に見る位置の、画面中心からの高さ */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "テレビ",
	          meta = (DisplayName = "入ったときに見上げる高さ", Units = "cm"))
	float ArrivalLookAbove = 100.f;
};

USTRUCT(BlueprintType)
struct FTBRemoteFeel
{
	GENERATED_BODY()
	/** 立ち位置の揺れや乗り直しで、同じ操作が続けて起きないようにする */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "リモコン",
	          meta = (DisplayName = "ボタンを続けて押せるまで", Units = "s", ClampMin = "0"))
	float PressCooldown = .5f;
};

/** 箱のふた・部屋の光、テレビ、リモコンの演出 */
UCLASS(BlueprintType)
class TOONSTORY_API UTBPresentationData : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "箱のふた", meta = (ShowOnlyInnerProperties))
	FTBLidFeel Lid;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "テレビ", meta = (ShowOnlyInnerProperties))
	FTBTelevisionFeel Television;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "リモコン", meta = (ShowOnlyInnerProperties))
	FTBRemoteFeel Remote;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "リザルト", meta = (ShowOnlyInnerProperties))
	FTBResultFeel Result;

#if WITH_EDITOR
	// 1つずつの範囲は ClampMin で守り、ここでは値どうしの関係だけを確かめる。
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

namespace TB
{
	// UTBDataSettings で選んだ演出のデータ。未設定なら C++ の初期値。
	TOONSTORY_API const UTBPresentationData& PresentationData();
}
