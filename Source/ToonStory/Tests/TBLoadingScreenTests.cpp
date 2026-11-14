// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "UI/Loading/STBLoadingScreen.h"

// 暗転が始まったら、おもちゃの周期は次の周へ進まない。既定は 見せる1.6 + 走る2.2 + 隠れる0.7 = 4.5 秒。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBLoadingScreenToyStop, "ToonStory.Review.LoadingScreenToyStop",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTBLoadingScreenToyStop::RunTest(const FString&)
{
	// 隠れている時間の終わりぎわで始まれば、その時刻で止まり、次の周に入らない。
	const double Hidden = STBLoadingScreen::ToyStopElapsed(4.45, 1.6, 2.2, .7);
	TestEqual(TEXT("暗転が隠れ際で始まれば、その時刻で止まる"), Hidden, 4.45);
	TestTrue(TEXT("次の周に入らない"), Hidden < 4.5);
	// 走っている途中で始まれば、その周の走り終わりまで動く。
	TestEqual(TEXT("走っている途中なら走り終わりで止まる"), STBLoadingScreen::ToyStopElapsed(2.0, 1.6, 2.2, .7), 3.8);
	TestEqual(TEXT("2周目でも同じ"), STBLoadingScreen::ToyStopElapsed(4.5 + 1.0, 1.6, 2.2, .7), 4.5 + 3.8);
	// 隠れ際が周の頭に近い側でも、その周から出ない。
	TestEqual(TEXT("隠れはじめの時刻で止まる"), STBLoadingScreen::ToyStopElapsed(3.9, 1.6, 2.2, .7), 3.9);
	// 隠れる間が0でも、次の周には入らない。
	TestTrue(TEXT("隠れる間が0でも次の周に入らない"), STBLoadingScreen::ToyStopElapsed(1.0, 1.6, 2.2, 0.) < 3.8);
	return true;
}
#endif
