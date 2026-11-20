// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "UI/TBKeyRow.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "Input/TBKeyLabels.h"
#include "TBPaperStyle.h"
#include "TBUIText.h"

void UTBKeyRow::Build(const FText& Label, FName InMappingName, UFont* Font, UFont* HeadingFont)
{
	MappingName = InMappingName;
	auto* Size = WidgetTree->ConstructWidget<USizeBox>();
	Size->SetWidthOverride(696);
	Size->SetHeightOverride(44);
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
	Divider->SetBrush(FSlateRoundedBoxBrush(FLinearColor::FromSRGBColor(FColor(209, 179, 139)), 0.f));
	Divider->SetVisibility(ESlateVisibility::HitTestInvisible);
	Place(Divider, 0, 43, 696, 1);
	Place(Text(Label, 12), 6, 10, 344, 29);

	ChangeButton = WidgetTree->ConstructWidget<UButton>();
	auto KeyStyle = TBPaper::CompactButtonStyle();
	KeyStyle.SetNormal(FSlateRoundedBoxBrush(FLinearColor::FromSRGBColor(FColor(255, 250, 240)), 5.f,
	    FLinearColor::FromSRGBColor(FColor(133, 100, 68)), 1.5f));
	ChangeButton->SetStyle(KeyStyle);
	KeyText = Text(FText::GetEmpty(), 11);
	KeyText->SetFont(FSlateFontInfo(HeadingFont, 11));
	KeyText->SetJustification(ETextJustify::Center);
	auto* KeySlot = CastChecked<UButtonSlot>(ChangeButton->SetContent(KeyText));
	KeySlot->SetPadding(FMargin(4, 0));
	Place(ChangeButton, 366, 5, 237, 33);
	ChangeButton->OnClicked.AddDynamic(this, &ThisClass::Clicked);

	ResetButton = WidgetTree->ConstructWidget<UButton>();
	auto ResetStyle = KeyStyle;
	ResetStyle.SetNormal(FSlateNoResource()).SetDisabled(FSlateNoResource());
	ResetButton->SetStyle(ResetStyle);
	auto* ResetText = Text(TBText::Get(TEXT("Keys.Reset")), 11);
	ResetText->SetJustification(ETextJustify::Center);
	auto* ResetSlot = CastChecked<UButtonSlot>(ResetButton->SetContent(ResetText));
	ResetSlot->SetPadding(FMargin(3, 0));
	Place(ResetButton, 609, 5, 82, 33);
	ResetButton->OnClicked.AddDynamic(this, &ThisClass::ResetClicked);
}

void UTBKeyRow::Refresh()
{
	TB::FKeyboardKey Binding;
	const bool bMapped = TB::FindKeyboardKey(GetOwningPlayer(), MappingName, Binding);
	KeyText->SetText(FText::FromString(bMapped ? TB::FormatKey(Binding.Key) : TB::KeyLabel(GetOwningPlayer(), MappingName)));
	ResetButton->SetIsEnabled(bMapped && Binding.Key != Binding.DefaultKey);
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
