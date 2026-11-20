// Copyright (c) 2026 Mamodou. All Rights Reserved.

#pragma once

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/ScaleBox.h"
#include "Components/TextBlock.h"

namespace TBPaper
{
	// Keep translated captions inside the printed edges, including after a live language change.
	inline void FitSignCaption(UWidgetTree& Tree, UButton& Button, UTextBlock& Caption)
	{
		auto* Fit = Tree.ConstructWidget<UScaleBox>();
		Fit->SetStretch(EStretch::ScaleToFit);
		Fit->SetStretchDirection(EStretchDirection::DownOnly);
		Caption.SetAutoWrapText(false);
		Fit->SetContent(&Caption);
		auto* Slot = CastChecked<UButtonSlot>(Button.SetContent(Fit));
		Slot->SetHorizontalAlignment(HAlign_Fill);
		Slot->SetVerticalAlignment(VAlign_Fill);
	}

	inline UTextBlock* SignCaption(UButton* Button)
	{
		return CastChecked<UTextBlock>(CastChecked<UScaleBox>(Button->GetContent())->GetContent());
	}
}
