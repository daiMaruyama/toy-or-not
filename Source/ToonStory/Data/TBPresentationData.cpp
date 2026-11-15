// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "Data/TBPresentationData.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "TBPresentationData"

EDataValidationResult UTBPresentationData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Valid = CombineDataValidationResults(Super::IsDataValid(Context), EDataValidationResult::Valid);
	auto Fail = [&](const FText& Message)
	{
		Context.AddError(Message);
		Valid = EDataValidationResult::Invalid;
	};
	// 決着を見せてから覆いはじめるので、覆いきる時刻より後に決着を見せ終えると、吹き出しが出ないまま結果の画面になる。
	if (Result.DecisiveSeconds >= Result.CoverSeconds)
	{
		Fail(LOCTEXT("ResultDecisiveAfterCover", "リザルトの「決着を見せる時間」は「覆いきる時刻」より短くしてください。"));
	}
	// 暗くしはじめる・ふたが閉まりはじめるのが決着を見せている間に入ると、決着の瞬間が隠れる。
	else if (Result.DarkenSeconds > Result.CoverSeconds - Result.DecisiveSeconds ||
	         Result.LidSlamSeconds > Result.CoverSeconds - Result.DecisiveSeconds)
	{
		Fail(LOCTEXT("ResultCoverTooShort",
		             "リザルトの「暗くしはじめる時間」と「ふたが閉まる時間」は、「覆いきる時刻」から「決着を見せる時間」を引いた秒以内にしてください。"));
	}
	return Valid;
}

#undef LOCTEXT_NAMESPACE
#endif
