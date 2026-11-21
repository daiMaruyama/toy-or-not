// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "UI/TBOptionRow.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "TBPaperStyle.h"

void UTBOptionRow::Build(const FText& Label, UFont* HeadingFont, UFont* NumericFont, bool bBar)
{
	auto* Size = WidgetTree->ConstructWidget<USizeBox>();
	Size->SetHeightOverride(53);
	WidgetTree->RootWidget = Size;
	auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	Size->SetContent(Canvas);
	auto Place = [&](UWidget* Widget, float X, float Y, float W, float H, FAnchors Anchors = FAnchors(0, 0))
	{
		auto* Slot = Canvas->AddChildToCanvas(Widget);
		Slot->SetAnchors(Anchors);
		Slot->SetOffsets(FMargin(X, Y, W, H));
	};
	auto* Divider = WidgetTree->ConstructWidget<UBorder>();
	Divider->SetBrush(FSlateRoundedBoxBrush(FLinearColor::FromSRGBColor(FColor(190, 182, 156)), 0.f));
	Divider->SetVisibility(ESlateVisibility::HitTestInvisible);
	const FAnchors Stretch(0, 0, 1, 0);
	const FAnchors Right(1, 0);
	Place(Divider, 0, 52, 6, 1, Stretch);
	auto* Name = WidgetTree->ConstructWidget<UTextBlock>();
	Name->SetText(Label);
	Name->SetFont(FSlateFontInfo(HeadingFont, 11));
	Name->SetColorAndOpacity(TBPaper::Ink);
	Place(Name, 6, 15, bBar ? 180 : 264, 29);
	if (bBar)
	{
		const FLinearColor Edge = FLinearColor::FromSRGBColor(FColor(72, 43, 26));
		auto StepButton = [&](const TCHAR* Glyph, float X, FAnchors Anchors)
		{
			auto* Button = WidgetTree->ConstructWidget<UButton>();
			Button->SetStyle(TBPaper::PrintButtonStyle(TBPaper::PrintCream));
			auto* Text = WidgetTree->ConstructWidget<UTextBlock>();
			Text->SetFont(FSlateFontInfo(NumericFont, 14));
			Text->SetText(FText::AsCultureInvariant(Glyph));
			Text->SetColorAndOpacity(TBPaper::PrintInk);
			auto* Slot = CastChecked<UButtonSlot>(Button->SetContent(Text));
			Slot->SetHorizontalAlignment(HAlign_Center);
			Slot->SetVerticalAlignment(VAlign_Center);
			Slot->SetPadding(FMargin(0));
			Place(Button, X, 11, 30, 30, Anchors);
			return Button;
		};
		MinusButton = StepButton(TEXT("−"), 193, FAnchors(0, 0));
		PlusButton = StepButton(TEXT("+"), -36, Right);
		MinusButton->OnClicked.AddDynamic(this, &ThisClass::Decrease);
		PlusButton->OnClicked.AddDynamic(this, &ThisClass::Increase);
		TrackFill = WidgetTree->ConstructWidget<UProgressBar>();
		FProgressBarStyle TrackStyle;
		TrackStyle.SetBackgroundImage(FSlateRoundedBoxBrush(FLinearColor::FromSRGBColor(FColor(226, 199, 155)), 4.f, Edge, 1.f));
		TrackStyle.SetFillImage(FSlateRoundedBoxBrush(TBPaper::PrintSea, 3.f));
		TrackFill->SetWidgetStyle(TrackStyle);
		TrackFill->SetBarFillStyle(EProgressBarFillStyle::Scale);
		TrackFill->SetBorderPadding(FVector2D(1, 1));
		TrackFill->SetFillColorAndOpacity(FLinearColor::White);
		TrackFill->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(TrackFill, 235, 23, 111, 6, Stretch);

		Slider = WidgetTree->ConstructWidget<USlider>();
		Slider->MouseUsesStep = true;
		Slider->SetIndentHandle(false);
		FSliderStyle Style = Slider->GetWidgetStyle();
		Style.SetNormalBarImage(FSlateNoResource()).SetHoveredBarImage(FSlateNoResource());
		Style.SetNormalThumbImage(FSlateRoundedBoxBrush(TBPaper::PrintSea, 8.f, TBPaper::PrintInk, 1.f, FVector2D(14, 14)));
		Style.SetHoveredThumbImage(FSlateRoundedBoxBrush(TBPaper::Cream, 9.f, Edge, 2.f, FVector2D(20, 20)));
		Slider->SetWidgetStyle(Style);
		Slider->SetSliderHandleColor(FLinearColor::White);
		Slider->OnValueChanged.AddDynamic(this, &ThisClass::SliderMoved);
		Slider->OnMouseCaptureBegin.AddDynamic(this, &ThisClass::SliderGrabbed);
		Slider->OnMouseCaptureEnd.AddDynamic(this, &ThisClass::SliderReleased);
		Place(Slider, 225, 7, 101, 38, Stretch);

		ValueText = WidgetTree->ConstructWidget<UTextBlock>();
		ValueText->SetFont(FSlateFontInfo(HeadingFont, 10));
		ValueText->SetColorAndOpacity(TBPaper::Ink);
		ValueText->SetJustification(ETextJustify::Right);
		Place(ValueText, -104, 15, 59, 30, Right);
		return;
	}

	ChoiceFont = HeadingFont;
	Picker = WidgetTree->ConstructWidget<UComboBoxString>();
	const FLinearColor ChoiceEdge = FLinearColor::FromSRGBColor(FColor(86, 108, 96));
	const FSlateRoundedBoxBrush Face(FLinearColor::FromSRGBColor(FColor(255, 249, 233)), 5.f, ChoiceEdge, 1.5f);
	FButtonStyle Button = TBPaper::CompactButtonStyle();
	Button.SetNormal(Face).SetHovered(FSlateRoundedBoxBrush(TBPaper::Cream, 5.f, ChoiceEdge, 1.5f));
	Button.SetPressed(FSlateRoundedBoxBrush(TBPaper::Cream, 5.f, ChoiceEdge, 1.5f));
	Button.SetNormalPadding(FMargin(7, 2)).SetPressedPadding(FMargin(7, 3, 7, 1));
	FComboBoxStyle Style = Picker->GetWidgetStyle();
	FSlateBrush Arrow = Style.ComboButtonStyle.DownArrowImage;
	Arrow.TintColor = TBPaper::Ink;
	Style.ComboButtonStyle.SetButtonStyle(Button).SetDownArrowImage(Arrow);
	Style.ComboButtonStyle.SetMenuBorderBrush(Face).SetMenuBorderPadding(FMargin(3));
	Picker->SetWidgetStyle(Style);
	FTableRowStyle Rows = Picker->GetItemStyle();
	const FSlateRoundedBoxBrush Active(TBPaper::Cream, 3.f);
	Rows.SetEvenRowBackgroundBrush(FSlateNoResource()).SetOddRowBackgroundBrush(FSlateNoResource());
	Rows.SetEvenRowBackgroundHoveredBrush(Active).SetOddRowBackgroundHoveredBrush(Active);
	Rows.SetActiveHoveredBrush(Active).SetInactiveHoveredBrush(Active).SetActiveBrush(Active).SetInactiveBrush(Active);
	Picker->SetItemStyle(Rows);
	Picker->SetContentPadding(FMargin(9, 3));
	Picker->SetMaxListHeight(280);
	Picker->OnGenerateWidgetEvent.BindDynamic(this, &ThisClass::ChoiceText);
	Picker->OnSelectionChanged.AddDynamic(this, &ThisClass::SelectChoice);
	Place(Picker, 280, 8, 6, 36, Stretch);
}

UWidget* UTBOptionRow::ChoiceText(FString Choice)
{
	auto* Text = WidgetTree->ConstructWidget<UTextBlock>();
	Text->SetText(FText::FromString(Choice));
	Text->SetColorAndOpacity(TBPaper::Ink);
	Text->SetFont(FSlateFontInfo(ChoiceFont, 11));
	Text->SetMargin(FMargin(5, 3));
	Text->SetJustification(ETextJustify::Center);
	return Text;
}

bool UTBOptionRow::IsChoiceOpen() const
{
	return Picker && Picker->IsOpen();
}

void UTBOptionRow::SelectChoice(FString Choice, ESelectInfo::Type)
{
	// Slate also reports a controller's Accept as Direct. Suppress only our own refreshes.
	if (bRefreshing || !Choices || !Select || !Value || Choice == Value()) return;
	const int32 Index = Choices().IndexOfByKey(Choice);
	if (Index != INDEX_NONE)
	{
		Select(Index);
		if (Changed) Changed();
	}
}

void UTBOptionRow::Refresh()
{
	TGuardValue<bool> Guard(bRefreshing, true);
	if (Picker && Choices && Value)
	{
		const FString Current = Value();
		TArray<FString> Items = Choices();
		// Keep a custom or unsupported current value readable without making it a new selectable setting.
		Items.AddUnique(Current);
		bool bSame = Picker->GetOptionCount() == Items.Num();
		for (int32 I = 0; bSame && I < Items.Num(); ++I) bSame = Picker->GetOptionAtIndex(I) == Items[I];
		if (!bSame)
		{
			Picker->ClearOptions();
			for (const FString& Item : Items) Picker->AddOption(Item);
		}
		Picker->SetIsEnabled(Items.Num() > 1);
		if (Picker->GetSelectedOption() != Current) Picker->SetSelectedOption(Current);
	}
	if (Slider && Fill && !bDragging)
	{
		Slider->SetStepSize(SliderStep > 0.f ? SliderStep : .01f);
		Slider->SetValue(FMath::Clamp(Fill(), 0.f, 1.f));
	}
	RefreshText();
}

void UTBOptionRow::RefreshText()
{
	if (ValueText && Value) ValueText->SetText(FText::FromString(Value()));
	if (TrackFill && Fill)
	{
		const float Fraction = FMath::Clamp(Fill(), 0.f, 1.f);
		MinusButton->SetIsEnabled(Fraction > KINDA_SMALL_NUMBER);
		PlusButton->SetIsEnabled(Fraction < 1.f - KINDA_SMALL_NUMBER);
		TrackFill->SetPercent(Fraction);
	}
}

void UTBOptionRow::Decrease()
{
	if (Fill) SliderMoved(FMath::Clamp(Fill() - SliderStep, 0.f, 1.f));
}

void UTBOptionRow::Increase()
{
	if (Fill) SliderMoved(FMath::Clamp(Fill() + SliderStep, 0.f, 1.f));
}

void UTBOptionRow::SliderMoved(float Fraction)
{
	if (bRefreshing)
	{
		return;
	}
	if (Slide)
	{
		Slide(Fraction);
	}
	RefreshText();
	// キー操作など、つまんでいないときの変化は、その場で確定する。
	if (!bDragging)
	{
		SliderReleased();
	}
}

void UTBOptionRow::SliderGrabbed()
{
	bDragging = true;
}

void UTBOptionRow::SliderReleased()
{
	bDragging = false;
	if (Released)
	{
		Released();
	}
	if (Changed)
	{
		Changed();
	}
	Refresh();
}
