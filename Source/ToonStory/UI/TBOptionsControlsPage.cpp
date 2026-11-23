// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "Core/TBGameUserSettings.h"
#include "Input/TBKeyLabels.h"
#include "TBOptionRow.h"
#include "TBOptionsMenu.h"
#include "TBOptionsPages.h"

void TBOptionsPages::BuildControls(UTBOptionsMenu& Menu, UTBGameUserSettings& Settings)
{
	auto* S = &Settings;
	auto* Sensitivity = Menu.AddRow(TBOptionsPage::Controls, TBText::Get(TEXT("Options.MouseSensitivity")), true);
	Sensitivity->Value = [S] { return FString::Printf(TEXT("%.1f"), S->GetMouseSensitivity()); };
	Sensitivity->Fill = [S]
	{
		const auto& Range = TB::ConfigData().Controls.MouseSensitivity;
		return Fraction(S->GetMouseSensitivity(), Range.Min, Range.Max);
	};
	const auto& SensitivityRange = TB::ConfigData().Controls.MouseSensitivity;
	BindSlider(*Sensitivity, SensitivityRange.Min, SensitivityRange.Max, SensitivityRange.Step,
	           [S](float Value) { S->SetMouseSensitivity(Value); }, nullptr, [S] { S->SaveSettings(); });
	auto* Invert = Menu.AddRow(TBOptionsPage::Controls, TBText::Get(TEXT("Options.InvertY")), false);
	Invert->Value = [S] { return OnOff(S->IsMouseYInverted()); };
	Invert->Choices = ToggleChoices;
	Invert->Select = [S](int32 Index)
	{
		S->SetMouseYInverted(Index != 0);
		S->SaveSettings();
	};

	const TCHAR* SectionKeys[] = {TEXT("Keys.Common"), TEXT("Keys.Human"), TEXT("Keys.Toy"), TEXT("Keys.Lobby"), TEXT("Keys.Results")};
	int32 SectionIndex = 0;
	for (const TB::FKeySection& Section : TB::KeySections())
	{
		Menu.AddHeading(TBOptionsPage::Controls, TBText::Get(SectionKeys[SectionIndex++]));
		for (const TB::FKeyRowInfo& Row : Section.Rows)
		{
			Menu.AddKeyRow(TBOptionsPage::Controls, TBText::Get(*(FString(TEXT("Keys.Action.")) + Row.Name.ToString())), Row.Name);
		}
	}
}
