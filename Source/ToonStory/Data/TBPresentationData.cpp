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
	// 脱出の開き方は救助・収納より小さくしない。出口へ移す瞬間もふたが開いたままにする。
	if (Lid.EscapeOpenAngle < Lid.OpenAngle || Lid.EscapeHoldSeconds < Lid.HoldSeconds)
	{
		Fail(LOCTEXT("LidEscapeSmaller", "箱のふたの「脱出で開く角度」と「脱出で開いたままの時間」は、救助・収納の値以上にしてください。"));
	}
	if (Lid.EscapeReleaseDelay > Lid.EscapeOpenSeconds + Lid.EscapeHoldSeconds)
	{
		Fail(LOCTEXT("LidEscapeDelayTooLong", "箱のふたの「脱出で出口へ移すまで」は、脱出で開く時間と開いたままの時間の合計以内にしてください。"));
	}
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
