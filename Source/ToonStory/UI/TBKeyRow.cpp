// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "UI/TBKeyRow.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "Input/TBKeyLabels.h"
#include "TBPaperStyle.h"
#include "TBUIText.h"

void UTBKeyRow::Build(const FText& Label, FName InMappingName, UFont* Font, UTexture2D* Paper)
{
	MappingName = InMappingName;
	auto* Size = WidgetTree->ConstructWidget<USizeBox>();
	Size->SetWidthOverride(800);
	Size->SetHeightOverride(50);
	WidgetTree->RootWidget = Size;
	auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	Size->SetContent(Canvas);
	auto Place = [&](UWidget* Widget, float X, float Y, float W, float H)
	{
		auto* Slot = Canvas->AddChildToCanvas(Widget);
		Slot->SetPosition(FVector2D(X, Y));
		Slot->SetSize(FVector2D(W, H));
	};
	auto Text = [&](const FText& String, int32 FontSize)
	{
		auto* Block = WidgetTree->ConstructWidget<UTextBlock>();
		Block->SetText(String);
		Block->SetFont(FSlateFontInfo(Font, FontSize));
		Block->SetColorAndOpacity(TBPaper::Ink);
		Block->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		Block->SetToolTipText(String);
		return Block;
	};
	auto* Divider = WidgetTree->ConstructWidget<UBorder>();
	Divider->SetBrushColor(TBPaper::Outline.CopyWithNewOpacity(.28f));
	Divider->SetVisibility(ESlateVisibility::HitTestInvisible);
	Place(Divider, 8, 49, 784, 1);
	auto* KeyPlate = WidgetTree->ConstructWidget<UBorder>();
	KeyPlate->SetBrush(FSlateRoundedBoxBrush(TBPaper::Paper, 8.f, TBPaper::Outline, 2.5f));
	KeyPlate->SetVisibility(ESlateVisibility::HitTestInvisible);
	Place(KeyPlate, 240, 4, 360, 42);
	Place(Text(Label, 19), 8, 10, 224, 36);
	KeyText = Text(FText::GetEmpty(), 19);
	KeyText->SetJustification(ETextJustify::Center);
	Place(KeyText, 240, 10, 360, 36);
	auto* ResetButton = WidgetTree->ConstructWidget<UButton>();
	ResetButton->SetStyle(TBPaper::CompactButtonStyle(true));
	ResetButton->SetContent(Text(TBText::Get(TEXT("Keys.Reset")), 16));
	ResetButton->SetToolTipText(FText::Format(TBText::Get(TEXT("Keys.ResetTooltip")), Label));
	Place(ResetButton, 612, 3, 84, 44);
	ResetButton->OnClicked.AddDynamic(this, &ThisClass::ResetClicked);
	ChangeButton = WidgetTree->ConstructWidget<UButton>();
	ChangeButton->SetStyle(TBPaper::CompactButtonStyle());
	ChangeButton->SetContent(Text(TBText::Get(TEXT("Keys.Change")), 16));
	ChangeButton->SetToolTipText(FText::Format(TBText::Get(TEXT("Keys.ChangeTooltip")), Label));
	Place(ChangeButton, 716, 3, 84, 44);
	ChangeButton->OnClicked.AddDynamic(this, &ThisClass::Clicked);
}

void UTBKeyRow::Refresh()
{
	KeyText->SetText(FText::FromString(TB::KeyLabel(GetOwningPlayer(), MappingName)));
	KeyText->SetToolTipText(KeyText->GetText());
}

void UTBKeyRow::FocusButton()
{
	if (ChangeButton)
	{
		ChangeButton->SetKeyboardFocus();
	}
}

FString UTBKeyRow::GetKeyText() const
{
	return KeyText ? KeyText->GetText().ToString() : FString();
}

void UTBKeyRow::ResetClicked()
{
	if (Reset)
	{
		Reset();
	}
}

void UTBKeyRow::Clicked()
{
	if (Pressed)
	{
		Pressed();
	}
}
