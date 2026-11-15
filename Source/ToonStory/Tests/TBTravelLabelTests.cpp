// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Audio/TBAudioSubsystem.h"
#include "UI/Loading/TBLoadingScreenSubsystem.h"

// ロード画面の文字の向きは、移動の行き先で決まる。行き先がわからないときだけGameStateの値を使う。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBTravelLabel, "ToonStory.Review.TravelLabel",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTBTravelLabel::RunTest(const FString&)
{
	using L = UTBLoadingScreenSubsystem;
	TestEqual(TEXT("オプション付きの URL"), L::MapNameFromURL(TEXT("/Game/Maps/TB_StylizedHouse?listen")), FString(TEXT("TB_StylizedHouse")));
	TestEqual(TEXT("ホスト付きの URL"), L::MapNameFromURL(TEXT("192.168.0.2:7777/Game/Maps/TB_Lobby")), FString(TEXT("TB_Lobby")));
	TestTrue(TEXT("マップのない URL は空"), L::MapNameFromURL(TEXT("192.168.0.2:7777")).IsEmpty());
	TestTrue(TEXT("空の URL は空"), L::MapNameFromURL(FString()).IsEmpty());
	TestTrue(TEXT("オプションだけの URL は空"), L::MapNameFromURL(TEXT("?listen")).IsEmpty());
	TestTrue(TEXT("末尾が / の URL は空"), L::MapNameFromURL(TEXT("/Game/Maps/")).IsEmpty());
	TestTrue(TEXT("試合へ向かうなら、GameState が古くてもロビーへ戻るにならない"), !L::IsReturningLabel(TEXT("TB_StylizedHouse"), true));
	TestTrue(TEXT("ロビーへ戻るなら、GameState が古くても試合へ向かうにならない"), L::IsReturningLabel(TEXT("TB_Lobby"), false));
	TestTrue(TEXT("行き先がなければ GameState の値（戻る）"), L::IsReturningLabel(FString(), true));
	TestTrue(TEXT("行き先がなければ GameState の値（向かう）"), !L::IsReturningLabel(FString(), false));
	// 文字を書き直すか。
	TestTrue(TEXT("新しく作ったときは書く"), L::ShouldRewriteLabel(true, false, -1, true));
	TestTrue(TEXT("付け直しで記録を引き継げば、向きが変わったとき書き直す"), L::ShouldRewriteLabel(false, false, 0, true));
	TestTrue(TEXT("向きが同じなら書き直さない"), !L::ShouldRewriteLabel(false, false, 1, true));
	TestTrue(TEXT("読み直しのあとの記録-1は書き直さない"), !L::ShouldRewriteLabel(false, false, -1, true));
	TestTrue(TEXT("読み直し中は書かない"), !L::ShouldRewriteLabel(true, true, -1, true));
	return true;
}

// ロビーの入室音。最初の1回は鳴らし、そのワールドで最初に人数を数えてからしばらくの増減は鳴らさない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBLobbyCountSound, "ToonStory.Review.LobbyCountSound",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTBLobbyCountSound::RunTest(const FString&)
{
	using A = UTBAudioSubsystem;
	using S = A::ELobbyCountSound;
	TestEqual(TEXT("最初に入ったときは鳴らす"), A::LobbyCountSound(1, -1, true, .1, 1.5f), S::Join);
	TestEqual(TEXT("最初でなければ鳴らさない"), A::LobbyCountSound(1, -1, false, 5., 1.5f), S::None);
	TestEqual(TEXT("最初に数えてから1.5秒以内の増加は鳴らさない"), A::LobbyCountSound(2, 1, false, 1.4, 1.5f), S::None);
	TestEqual(TEXT("最初に数えてから1.5秒以内の減少も鳴らさない"), A::LobbyCountSound(1, 2, false, .5, 1.5f), S::None);
	TestEqual(TEXT("最初に数えて1.5秒たてば増加を鳴らす"), A::LobbyCountSound(2, 1, false, 1.5, 1.5f), S::Join);
	// ワールドが変わってから長くたっていても、判断は最初に数えてからの秒数で決まる（渡す値が数え始めからの秒数）。
	TestEqual(TEXT("ワールドが変わって2秒たっていても、数えて0.3秒なら鳴らさない"), A::LobbyCountSound(3, 2, false, .3, 1.5f), S::None);
	TestEqual(TEXT("そろったあとに入れば鳴らす"), A::LobbyCountSound(3, 2, false, 3., 1.5f), S::Join);
	TestEqual(TEXT("そろったあとに出れば鳴らす"), A::LobbyCountSound(2, 3, false, 3., 1.5f), S::Leave);
	TestEqual(TEXT("人数が同じなら鳴らさない"), A::LobbyCountSound(2, 2, false, 3., 1.5f), S::None);
	TestEqual(TEXT("人数0は鳴らさない"), A::LobbyCountSound(0, 2, false, 3., 1.5f), S::None);
	return true;
}
#endif
