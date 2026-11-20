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
		return Block;
	};
	auto* Divider = WidgetTree->ConstructWidget<UBorder>();
	Divider->SetBrushColor(TBPaper::Outline.CopyWithNewOpacity(.28f));
	Divider->SetVisibility(ESlateVisibility::HitTestInvisible);
	Place(Divider, 8, 49, 784, 1);
	auto* KeyPlate = WidgetTree->ConstructWidget<UBorder>();
	KeyPlate->SetBrush(TBPaper::SlicedArtwork(TBPaper::Art(TEXT("Frame_StageSelect")),
	    FVector2D(196, 42), FMargin(.08f, .29f)));
	KeyPlate->SetVisibility(ESlateVisibility::HitTestInvisible);
	Place(KeyPlate, 240, 4, 360, 42);
	Place(Text(Label, 19), 8, 6, 224, 36);
	KeyText = Text(FText::GetEmpty(), 19);
	KeyText->SetJustification(ETextJustify::Center);
	Place(KeyText, 240, 6, 360, 36);
	auto* ResetButton = WidgetTree->ConstructWidget<UButton>();
	const FSlateBrush ButtonPaper = TBPaper::ImageBrush(Paper);
	ResetButton->SetStyle(TBPaper::ArtButtonStyle(ButtonPaper));
	ResetButton->SetContent(Text(TBText::Get(TEXT("Keys.Reset")), 16));
	Place(ResetButton, 612, 3, 84, 44);
	ResetButton->OnClicked.AddDynamic(this, &ThisClass::ResetClicked);
	ChangeButton = WidgetTree->ConstructWidget<UButton>();
	ChangeButton->SetStyle(TBPaper::ArtButtonStyle(ButtonPaper, TBPaper::Gold));
	ChangeButton->SetContent(Text(TBText::Get(TEXT("Keys.Change")), 16));
	Place(ChangeButton, 716, 3, 84, 44);
	ChangeButton->OnClicked.AddDynamic(this, &ThisClass::Clicked);
}

void UTBKeyRow::Refresh()
{
	KeyText->SetText(FText::FromString(TB::KeyLabel(GetOwningPlayer(), MappingName)));
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
