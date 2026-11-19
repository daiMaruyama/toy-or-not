#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Data/TBDataSettings.h"
#include "Data/TBPresentationData.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBPresentationDataTest, "ToonStory.Data.Presentation",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FTBPresentationDataTest::RunTest(const FString& Parameters)
{
	// C++ の初期値は、移す前に実際に効いていた値と同じ。蓋の角度と光は、箱の部屋に置いた蓋の上書き（60°・30000）を引き継ぐ。
	const auto* Defaults = GetDefault<UTBPresentationData>();
	TestEqual(TEXT("Lid open angle"), Defaults->Lid.OpenAngle, 60.f);
	TestEqual(TEXT("Lid light"), Defaults->Lid.PeakIntensity, 30000.f);
	TestEqual(TEXT("Lid timing"), FVector(Defaults->Lid.OpenSeconds, Defaults->Lid.HoldSeconds, Defaults->Lid.CloseSeconds),
	          FVector(.25f, .9f, .8f));
	TestEqual(TEXT("Room flash"), FVector2D(Defaults->Lid.FlashExposure, Defaults->Lid.FlashSeconds), FVector2D(2.5f, .35f));
	// 脱出は救助・収納より大きく・長く・勢いよく開き、出口へ移すのを0.25秒遅らせる。
	TestEqual(TEXT("Escape lid angle and light"), FVector2D(Defaults->Lid.EscapeOpenAngle, Defaults->Lid.EscapePeakIntensity),
	          FVector2D(75.f, 80000.f));
	TestEqual(TEXT("Escape lid timing"), FVector2D(Defaults->Lid.EscapeOpenSeconds, Defaults->Lid.EscapeHoldSeconds), FVector2D(.12f, 1.6f));
	TestEqual(TEXT("Escape room flash"), Defaults->Lid.EscapeFlashExposure, 4.f);
	TestEqual(TEXT("Escape release delay"), Defaults->Lid.EscapeReleaseDelay, .25f);
#if WITH_EDITOR
	{
		auto Check = [&](const TCHAR* What, TFunctionRef<void(FTBLidFeel&)> Edit, EDataValidationResult Expected)
		{
			auto* Data = NewObject<UTBPresentationData>();
			Edit(Data->Lid);
			FDataValidationContext Context;
			TestEqual(What, Data->IsDataValid(Context), Expected);
		};
		Check(TEXT("Defaults are valid"), [](FTBLidFeel&) {}, EDataValidationResult::Valid);
		Check(TEXT("Escape angle smaller than rescue is rejected"), [](FTBLidFeel& Lid) { Lid.EscapeOpenAngle = Lid.OpenAngle - 1.f; },
		      EDataValidationResult::Invalid);
		Check(TEXT("Escape hold shorter than rescue is rejected"), [](FTBLidFeel& Lid) { Lid.EscapeHoldSeconds = Lid.HoldSeconds - .1f; },
		      EDataValidationResult::Invalid);
		Check(TEXT("Release delay beyond the open lid is rejected"),
		      [](FTBLidFeel& Lid) { Lid.EscapeReleaseDelay = Lid.EscapeOpenSeconds + Lid.EscapeHoldSeconds + .1f; },
		      EDataValidationResult::Invalid);
		Check(TEXT("Zero release delay is allowed"), [](FTBLidFeel& Lid) { Lid.EscapeReleaseDelay = 0.f; }, EDataValidationResult::Valid);
	}
#endif
	TestEqual(TEXT("TV resolution"), Defaults->Television.CaptureResolution, FIntPoint(512, 288));
	TestEqual(TEXT("TV capture rate"), Defaults->Television.CaptureRate, 15.f);
	TestEqual(TEXT("TV channel static"), Defaults->Television.NoiseSeconds, .12f);
	TestEqual(TEXT("Remote cooldown"), Defaults->Remote.PressCooldown, .22f);

	// プロジェクト設定でアセットを選んでいれば、初期値ではなくそのアセットを使う。
	const auto& Selected = GetDefault<UTBDataSettings>()->PresentationData;
	if (!Selected.IsNull())
	{
		TestTrue(TEXT("Selected presentation asset is used"), &TB::PresentationData() == Selected.LoadSynchronous());
	}
	return true;
}
#endif
