// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "UI/TBOptionRow.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "TBPaperStyle.h"

namespace
{
	constexpr float TrackWidth = 336.f;
}

void UTBOptionRow::Build(const FText& Label, UFont* HeadingFont, UFont* NumericFont, bool bBar)
{
	auto* Size = WidgetTree->ConstructWidget<USizeBox>();
	Size->SetWidthOverride(696);
	Size->SetHeightOverride(53);
	WidgetTree->RootWidget = Size;
	auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	Size->SetContent(Canvas);
	auto Place = [&](UWidget* Widget, float X, float Y, float W, float H)
	{
		auto* Slot = Canvas->AddChildToCanvas(Widget);
		Slot->SetPosition(FVector2D(X, Y));
		Slot->SetSize(FVector2D(W, H));
	};
	auto* Divider = WidgetTree->ConstructWidget<UBorder>();
	Divider->SetBrush(FSlateRoundedBoxBrush(FLinearColor::FromSRGBColor(FColor(209, 179, 139)), 0.f));
	Divider->SetVisibility(ESlateVisibility::HitTestInvisible);
	Place(Divider, 0, 52, 696, 1);
	auto* Name = WidgetTree->ConstructWidget<UTextBlock>();
	Name->SetText(Label);
	Name->SetFont(FSlateFontInfo(HeadingFont, 11));
	Name->SetColorAndOpacity(TBPaper::Ink);
	Place(Name, 6, 15, 264, 29);
	if (bBar)
	{
		const FLinearColor Edge = FLinearColor::FromSRGBColor(FColor(72, 43, 26));
		auto* Track = WidgetTree->ConstructWidget<UBorder>();
		Track->SetBrush(FSlateRoundedBoxBrush(FLinearColor::FromSRGBColor(FColor(226, 199, 155)), 4.f, Edge, 1.f));
		Track->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Track, 280, 22, TrackWidth, 8);
		TrackFill = WidgetTree->ConstructWidget<UBorder>();
		TrackFill->SetBrush(FSlateRoundedBoxBrush(FLinearColor::FromSRGBColor(FColor(67, 152, 141)), 3.f));
		TrackFill->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(TrackFill, 281, 23, 0, 6);

		Slider = WidgetTree->ConstructWidget<USlider>();
		Slider->MouseUsesStep = true;
		Slider->SetIndentHandle(false);
		FSliderStyle Style = Slider->GetWidgetStyle();
		Style.SetNormalBarImage(FSlateNoResource()).SetHoveredBarImage(FSlateNoResource());
		Style.SetNormalThumbImage(FSlateRoundedBoxBrush(TBPaper::Gold, 9.f, Edge, 2.f, FVector2D(20, 20)));
		Style.SetHoveredThumbImage(FSlateRoundedBoxBrush(TBPaper::Cream, 9.f, Edge, 2.f, FVector2D(20, 20)));
		Slider->SetWidgetStyle(Style);
		Slider->SetSliderHandleColor(FLinearColor::White);
		Slider->OnValueChanged.AddDynamic(this, &ThisClass::SliderMoved);
		Slider->OnMouseCaptureBegin.AddDynamic(this, &ThisClass::SliderGrabbed);
		Slider->OnMouseCaptureEnd.AddDynamic(this, &ThisClass::SliderReleased);
		Place(Slider, 270, 7, TrackWidth + 20, 38);

		ValueText = WidgetTree->ConstructWidget<UTextBlock>();
		ValueText->SetFont(FSlateFontInfo(NumericFont, 14));
		ValueText->SetColorAndOpacity(TBPaper::Ink);
		ValueText->SetJustification(ETextJustify::Right);
		Place(ValueText, 630, 12, 61, 32);
		return;
	}

	ChoiceFont = HeadingFont;
	Picker = WidgetTree->ConstructWidget<UComboBoxString>();
	const FLinearColor ChoiceEdge = FLinearColor::FromSRGBColor(FColor(133, 100, 68));
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
	Place(Picker, 280, 8, 411, 36);
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
		CastChecked<UCanvasPanelSlot>(TrackFill->Slot)->SetSize(FVector2D((TrackWidth - 2.f) * FMath::Clamp(Fill(), 0.f, 1.f), 6));
	}
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
