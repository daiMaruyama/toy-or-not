// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "Input/TBKeyPrompt.h"
#include "Input/TBKeyLabels.h"
#include "UI/TBUIText.h"

namespace
{
	TB::FKeyPrompt Make(TB::EKeyPromptKind Kind, const FString& Text = FString())
	{
		TB::FKeyPrompt Prompt;
		Prompt.Kind = Kind;
		Prompt.Text = Text;
		return Prompt;
	}
}

FString TB::KeyRowName(FName Name)
{
	for (const FKeySection& Section : KeySections())
	{
		for (const FKeyRowInfo& Row : Section.Rows)
		{
			if (Row.Name == Name)
			{
				return FText::Format(TBText::Get(TEXT("Keys.RowName")), FText::FromString(Section.Heading), FText::FromString(Row.Label)).ToString();
			}
		}
	}
	return Name.ToString();
}

FString TB::WaitingText(FName Row)
{
	return FText::Format(TBText::Get(TEXT("Keys.Waiting")), FText::FromString(KeyRowName(Row))).ToString();
}

TB::FKeyPrompt TB::PromptForRemap(const FRemapResult& Result, const FKey& Key)
{
	switch (Result.Result)
	{
	case ERemapResult::OK:
		return Make(EKeyPromptKind::Done);
	case ERemapResult::Unusable:
		return Make(EKeyPromptKind::Retry, TBText::String(TEXT("Keys.UnusableRetry")));
	case ERemapResult::Conflict:
		if (Result.Conflicting.Num() == 1)
		{
			return Make(EKeyPromptKind::AskSwap, FText::Format(TBText::Get(TEXT("Keys.Conflict")), FText::FromString(FormatKey(Key)), FText::FromString(KeyRowName(Result.Conflicting[0]))).ToString());
		}
		{
			// 重なっている相手を並べる。
			FString Names;
			for (const FName& Other : Result.Conflicting)
			{
				Names += FText::Format(TBText::Get(TEXT("Keys.QuotedAction")), FText::FromString(KeyRowName(Other))).ToString();
			}
			return Make(EKeyPromptKind::Notice, TBText::String(TEXT("Keys.MultipleConflicts")) + Names);
		}
	default:
		return Make(EKeyPromptKind::Notice, TBText::String(TEXT("Keys.ChangeFailed")));
	}
}

TB::FKeyPrompt TB::PromptForReset(const FRemapResult& Result, const FKey& DefaultKey)
{
	if (Result.Result != ERemapResult::Conflict)
	{
		return PromptForRemap(Result, DefaultKey);
	}
	if (Result.Conflicting.Num() == 1)
	{
		return Make(EKeyPromptKind::AskSwap, FText::Format(TBText::Get(TEXT("Keys.ResetConflict")), FText::FromString(FormatKey(DefaultKey)), FText::FromString(KeyRowName(Result.Conflicting[0]))).ToString());
	}
	FString Names;
	for (const FName& Other : Result.Conflicting)
	{
		Names += FText::Format(TBText::Get(TEXT("Keys.QuotedAction")), FText::FromString(KeyRowName(Other))).ToString();
	}
	return Make(EKeyPromptKind::Notice, TBText::String(TEXT("Keys.ResetMultipleConflicts")) + Names);
}

TB::FKeyPrompt TB::PromptForSwap(const FRemapResult& Result)
{
	switch (Result.Result)
	{
	case ERemapResult::OK:
		return Make(EKeyPromptKind::Done);
	case ERemapResult::NoPartner:
		return Make(EKeyPromptKind::Redo);
	case ERemapResult::Conflict:
		return Make(EKeyPromptKind::Notice, TBText::String(TEXT("Keys.SwapConflict")));
	case ERemapResult::Unusable:
		return Make(EKeyPromptKind::Notice, TBText::String(TEXT("Keys.Unusable")));
	default:
		return Make(EKeyPromptKind::Notice, TBText::String(TEXT("Keys.ChangeFailed")));
	}
}
