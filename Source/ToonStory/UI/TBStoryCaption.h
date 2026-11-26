// Copyright (c) 2026 Mamodou. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/Actor.h"
#include "TBStoryCaption.generated.h"

class UTextBlock;
class UWidgetAnimation;

/** 寸劇の字幕の番。LS_TitleStoryの文字列トラックが、出す台詞のキー（UIのCSVのキー）をこの値に打つ。空なら字幕を出さない。 */
UCLASS()
class TOONSTORY_API ATBStoryCaptionCue : public AActor
{
	GENERATED_BODY()
public:
	ATBStoryCaptionCue();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "字幕")
	FString CaptionKey;
};

/**
 * 寸劇の字幕とスキップの案内。見た目と配置はWBP_StoryCaptionで作る。
 * WBPにCaptionとSkipHintという名前のTextBlockを置く。ここでは文言を入れるだけ。
 */
UCLASS(Abstract)
class TOONSTORY_API UTBStoryCaption : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetSkipHintShown(bool bShown);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;

	UPROPERTY(Transient, meta = (BindWidget))
	TObjectPtr<UTextBlock> Caption;
	UPROPERTY(Transient, meta = (BindWidget))
	TObjectPtr<UTextBlock> SkipHint;
	// あれば、スキップの案内を出すたびに先頭から再生する。透明度や時間はWBPのアニメーションで決める。
	UPROPERTY(Transient, meta = (BindWidgetAnimOptional))
	TObjectPtr<UWidgetAnimation> SkipHintFadeIn;

private:
	void Refresh();
	TWeakObjectPtr<ATBStoryCaptionCue> Cue;
	FString ShownKey;
	uint32 ShownRevision = MAX_uint32;
};
