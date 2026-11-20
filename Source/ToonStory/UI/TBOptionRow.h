// Copyright (c) 2026 Mamodou. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/SlateEnums.h"
#include "TBOptionRow.generated.h"

class UBorder;
class UComboBoxString;
class UFont;
class USlider;
class UTextBlock;

/** A settings row with a stepped slider or a localized list of choices. */
UCLASS()
class TOONSTORY_API UTBOptionRow : public UUserWidget
{
	GENERATED_BODY()
public:
	void Build(const FText& Label, UFont* HeadingFont, UFont* NumericFont, bool bBar);
	void Refresh();
	TFunction<FString()> Value;
	TFunction<TArray<FString>()> Choices;
	TFunction<void(int32 Index)> Select;
	bool IsChoiceOpen() const;
	// 0〜1。スライダーがあるときだけ使う。
	TFunction<float()> Fill;
	// スライダーを動かしている途中（0〜1）。すぐ反映してよいものだけ反映し、保存はしない。
	TFunction<void(float Fraction)> Slide;
	// スライダーから手を離したとき。ここで保存する。
	TFunction<void()> Released;
	// スライダーの目盛りの幅（0〜1 の割合）。0 なら目盛りなし。
	float SliderStep = 0.f;
	// 値を変えたあと。ほかの行の表示も変わることがある。
	TFunction<void()> Changed;

private:
	UFUNCTION()
	UWidget* ChoiceText(FString Choice);
	UFUNCTION()
	void SelectChoice(FString Choice, ESelectInfo::Type Source);
	UFUNCTION()
	void SliderMoved(float Fraction);
	UFUNCTION()
	void SliderGrabbed();
	UFUNCTION()
	void SliderReleased();
	void RefreshText();
	bool bDragging = false;
	bool bRefreshing = false;
	UPROPERTY(Transient)
	TObjectPtr<UComboBoxString> Picker;
	UPROPERTY(Transient)
	TObjectPtr<UFont> ChoiceFont;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> TrackFill;
	UPROPERTY(Transient)
	TObjectPtr<USlider> Slider;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ValueText;
};
