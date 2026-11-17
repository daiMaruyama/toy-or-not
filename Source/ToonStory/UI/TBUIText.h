// Copyright (c) 2026 Mamodou. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

/** Editable UI copy lives in Content/Localization/UI/<language>.csv. */
namespace TBText
{
	TOONSTORY_API FText Get(const TCHAR* Key);
	TOONSTORY_API FString String(const TCHAR* Key);
	// For existing Canvas interfaces; storage remains valid across language switches.
	TOONSTORY_API const TCHAR* Literal(const TCHAR* Key);
	TOONSTORY_API bool SetLanguage(const FString& Code);
	TOONSTORY_API FString GetLanguage();
	TOONSTORY_API uint32 GetRevision();
	TOONSTORY_API FSimpleMulticastDelegate& OnLanguageChanged();
	// Localize legacy server notices at their display boundary; player-authored text is never passed here.
	TOONSTORY_API FString TranslateNotice(const FString& Source);
}
