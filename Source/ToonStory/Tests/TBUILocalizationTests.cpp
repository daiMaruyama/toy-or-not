// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Core/TBController.h"
#include "Core/TBGameUserSettings.h"
#include "Core/TBSession.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/StringTableCore.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UI/TBOptionsMenu.h"
#include "UI/TBOptionsPages.h"
#include "UI/TBUIText.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	// Compare format arguments, including their multiplicity; translators may reorder named arguments.
	TArray<FString> FormatTokens(const FString& Text)
	{
		TArray<FString> Tokens;
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			if (Text[Index] == TEXT('{'))
			{
				const int32 End = Text.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 1);
				if (End != INDEX_NONE)
				{
					Tokens.Add(Text.Mid(Index, End - Index + 1));
					Index = End;
				}
			}
			else if (Text[Index] == TEXT('%') && Index + 1 < Text.Len())
			{
				if (Text[Index + 1] == TEXT('%'))
				{
					++Index;
					continue;
				}
				int32 End = Index + 1;
				while (End < Text.Len() && FString(TEXT("-+ #0.123456789hlz")).Contains(FString::Chr(Text[End]))) ++End;
				if (End < Text.Len() && FString(TEXT("diufsgc")).Contains(FString::Chr(Text[End])))
				{
					Tokens.Add(Text.Mid(Index, End - Index + 1));
					Index = End;
				}
			}
		}
		Tokens.Sort();
		return Tokens;
	}

	bool ReadCatalog(const TCHAR* Language, TMap<FString, FString>& Out)
	{
		auto Table = MakeShared<FStringTable, ESPMode::ThreadSafe>();
		const FString Path = FPaths::ProjectContentDir() / TEXT("Localization/UI") / (FString(Language) + TEXT(".csv"));
		if (!Table->ImportStringsFromCSVFile(Path)) return false;
		Table->EnumerateSourceStrings([&Out](const FString& Key, const FString& Value)
		{
			Out.Add(Key, Value);
			return true;
		});
		return true;
	}

	ATBController* LocalizationController()
	{
		for (const auto& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Game && Context.World())
				return Cast<ATBController>(Context.World()->GetFirstPlayerController());
		}
		return nullptr;
	}

	UTextBlock* FindText(UTBOptionsMenu& Menu, const FString& Value)
	{
		UTextBlock* Found = nullptr;
		Menu.WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (auto* Text = Cast<UTextBlock>(Widget); Text && Text->GetText().ToString() == Value) Found = Text;
		});
		return Found;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBUILocalizationCatalogTest, "ToonStory.UI.Localization.Catalog",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBUILocalizationCatalogTest::RunTest(const FString& Parameters)
{
	TMap<FString, FString> Japanese, English;
	if (!TestTrue(TEXT("Japanese CSV imports through UE StringTable"), ReadCatalog(TEXT("ja"), Japanese)) ||
		!TestTrue(TEXT("English CSV imports through UE StringTable"), ReadCatalog(TEXT("en"), English))) return false;
	TestTrue(TEXT("Catalog is populated"), Japanese.Num() > 0);
	TestEqual(TEXT("Both languages have the same number of keys"), English.Num(), Japanese.Num());
	for (const auto& Pair : Japanese)
	{
		const FString* Translation = English.Find(Pair.Key);
		if (!TestNotNull(*FString::Printf(TEXT("English entry: %s"), *Pair.Key), Translation)) continue;
		TestFalse(*FString::Printf(TEXT("Nonempty Japanese entry: %s"), *Pair.Key), Pair.Value.IsEmpty());
		TestFalse(*FString::Printf(TEXT("Nonempty English entry: %s"), *Pair.Key), Translation->IsEmpty());
		TestTrue(*FString::Printf(TEXT("Format arguments agree: %s"), *Pair.Key),
			FormatTokens(Pair.Value) == FormatTokens(*Translation));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBUILocalizationSwitchTest, "ToonStory.UI.Localization.Switch",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBUILocalizationSwitchTest::RunTest(const FString& Parameters)
{
	const FString OriginalLanguage = TBText::GetLanguage();
	const FString OriginalCulture = FInternationalization::Get().GetCurrentCulture()->GetName();
	ON_SCOPE_EXIT
	{
		TBText::SetLanguage(OriginalLanguage);
		FInternationalization::Get().SetCurrentCulture(OriginalCulture);
	};
	if (!TestTrue(TEXT("Japanese activates"), TBText::SetLanguage(TEXT("ja")))) return false;
	const FText HeldTitle = TBText::Get(TEXT("Options.Title"));
	const FText HeldFormat = FText::Format(TBText::Get(TEXT("Keys.Waiting")), FText::FromString(TEXT("TEST ACTION")));
	TestEqual(TEXT("Initial Japanese title"), HeldTitle.ToString(), FString(TEXT("オプション")));
	if (!TestTrue(TEXT("English activates"), TBText::SetLanguage(TEXT("en")))) return false;
	TestEqual(TEXT("A retained FText changes to English"), HeldTitle.ToString(), FString(TEXT("Options")));
	TestTrue(TEXT("A retained formatted FText changes to English"), HeldFormat.ToString().StartsWith(TEXT("Press a key")));
	TestTrue(TEXT("Formatting preserves its argument"), HeldFormat.ToString().Contains(TEXT("TEST ACTION")));
	TestEqual(TEXT("A fixed legacy notice uses the English catalog"),
		TBText::TranslateNotice(TEXT("近くの安全な場所へ移動しました。")), FString(TEXT("Moved to a nearby safe spot.")));
	TestEqual(TEXT("A translated join notice preserves the player name"),
		TBText::TranslateNotice(TEXT("Aliceがあそびにきた！")), FString(TEXT("Alice came to play!")));
	TestEqual(TEXT("A translated room notice preserves the count"),
		TBText::TranslateNotice(TEXT("部屋が3個見つかりました。")), FString(TEXT("Found 3 rooms.")));
	TestTrue(TEXT("A prefix-only error preserves its technical details"),
		TBText::TranslateNotice(TEXT("マップの読み込みに失敗しました。\nTEST_DETAIL")).Contains(TEXT("TEST_DETAIL")));
	TestFalse(TEXT("A prefix-only error translates its explanation"),
		TBText::TranslateNotice(TEXT("マップの読み込みに失敗しました。\nTEST_DETAIL")).StartsWith(TEXT("マップ")));
	TestEqual(TEXT("Unrecognized player-authored text is unchanged"),
		TBText::TranslateNotice(TEXT("これはプレイヤーの文章です")), FString(TEXT("これはプレイヤーの文章です")));
	const uint32 Revision = TBText::GetRevision();
	TestFalse(TEXT("Unknown language is rejected"), TBText::SetLanguage(TEXT("unsupported-test-language")));
	TestEqual(TEXT("Rejected language preserves active language"), TBText::GetLanguage(), FString(TEXT("en")));
	TestEqual(TEXT("Rejected language preserves revision"), TBText::GetRevision(), Revision);
	TestEqual(TEXT("Rejected language preserves retained text"), HeldTitle.ToString(), FString(TEXT("Options")));
	TestTrue(TEXT("Japanese activates again"), TBText::SetLanguage(TEXT("ja")));
	TestEqual(TEXT("The same retained FText changes back"), HeldTitle.ToString(), FString(TEXT("オプション")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBUILocalizationRoomsTest, "ToonStory.UI.Localization.Rooms",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBUILocalizationRoomsTest::RunTest(const FString& Parameters)
{
	const FString OriginalLanguage = TBText::GetLanguage();
	const FString OriginalCulture = FInternationalization::Get().GetCurrentCulture()->GetName();
	ON_SCOPE_EXIT
	{
		TBText::SetLanguage(OriginalLanguage);
		FInternationalization::Get().SetCurrentCulture(OriginalCulture);
	};
	// Only detached result metadata is used; no subsystem initialization, search, join or settings save.
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>());
	TStrongObjectPtr<UTBSession> Session(NewObject<UTBSession>(Instance.Get()));
	const FString HostName = TEXT("Alice | open 9/9 | ping {0} 日本語");
	FOnlineSessionSearchResult First;
	First.Session.OwningUserName = HostName;
	First.Session.NumOpenPublicConnections = 3;
	First.Session.SessionSettings.NumPublicConnections = 8;
	First.PingInMs = 42;
	auto Second = First;
	Second.Session.NumOpenPublicConnections = 1;
	Second.PingInMs = MAX_QUERY_PING;
	Session->Results = {First, Second};
	Session->Rooms = {TEXT("Old opaque display value"), TEXT("Another stale display value")};
	if (!TestTrue(TEXT("Japanese room labels activate"), TBText::SetLanguage(TEXT("ja")))) return false;
	const FText HeldLabel = Session->GetRoomLabel(0);
	TestEqual(TEXT("Japanese metadata is composed without parsing the host name"), HeldLabel.ToString(),
		HostName + TEXT(" | 空き 3/8 | 通信 42 ms"));
	TestEqual(TEXT("Second result keeps its own metadata even with the same host name"), Session->GetRoomLabel(1).ToString(),
		HostName + TEXT(" | 空き 1/8 | 通信 不明"));
	TestEqual(TEXT("Negative room index has a localized fallback"), Session->GetRoomLabel(-1).ToString(),
		TBText::String(TEXT("Rooms.Unavailable")));
	TestEqual(TEXT("Past-end room index has a localized fallback"), Session->GetRoomLabel(2).ToString(),
		TBText::String(TEXT("Rooms.Unavailable")));
	if (!TestTrue(TEXT("English room labels activate"), TBText::SetLanguage(TEXT("en")))) return false;
	TestEqual(TEXT("Existing formatted room text follows language changes"), HeldLabel.ToString(),
		HostName + TEXT(" | Open 3/8 | Ping 42 ms"));
	TestEqual(TEXT("Refreshing the same result needs no search or network operation"), Session->GetRoomLabel(0).ToString(), HeldLabel.ToString());
	TestEqual(TEXT("Unknown ping is localized"), Session->GetRoomLabel(1).ToString(),
		HostName + TEXT(" | Open 1/8 | Ping Unknown"));
	TestEqual(TEXT("Formatting preserves raw player text"), Session->Results[0].Session.OwningUserName, HostName);
	TestEqual(TEXT("Formatting preserves result order"), Session->Results[1].Session.NumOpenPublicConnections, 1);
	TestEqual(TEXT("Formatting does not reinterpret legacy display copy"), Session->Rooms[0], FString(TEXT("Old opaque display value")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBUILocalizationSettingsTest, "ToonStory.UI.Localization.Settings",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBUILocalizationSettingsTest::RunTest(const FString& Parameters)
{
	// Never call CommitLanguage/SaveSettings: test an independent settings object and compare the real save bytes.
	const bool HadSave = FPaths::FileExists(GGameUserSettingsIni);
	TArray<uint8> BeforeSave;
	if (HadSave && !TestTrue(TEXT("Existing user save can be read"), FFileHelper::LoadFileToArray(BeforeSave, *GGameUserSettingsIni))) return false;
	const auto* Actual = UTBGameUserSettings::Get();
	const FString ActualLanguage = Actual ? Actual->GetLanguage() : FString();
	auto* Settings = NewObject<UTBGameUserSettings>(GetTransientPackage());
	Settings->SetToDefaults();
	TestEqual(TEXT("Default language is Japanese"), Settings->GetLanguage(), FString(TEXT("ja")));
	Settings->SetLanguage(TEXT("en"));
	TestEqual(TEXT("English preference is accepted"), Settings->GetLanguage(), FString(TEXT("en")));
	Settings->SetLanguage(TEXT("unsupported-test-language"));
	TestEqual(TEXT("Invalid stored preference normalizes to Japanese"), Settings->GetLanguage(), FString(TEXT("ja")));
	if (Actual) TestEqual(TEXT("Actual user preference is unchanged"), Actual->GetLanguage(), ActualLanguage);
	TestEqual(TEXT("Test does not create or remove the real user save"), FPaths::FileExists(GGameUserSettingsIni), HadSave);
	if (HadSave)
	{
		TArray<uint8> AfterSave;
		TestTrue(TEXT("User save is still readable"), FFileHelper::LoadFileToArray(AfterSave, *GGameUserSettingsIni));
		TestTrue(TEXT("User save bytes are unchanged"), AfterSave == BeforeSave);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBUILocalizationOptionsTest, "ToonStory.UI.Localization.Options",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBUILocalizationOptionsTest::RunTest(const FString& Parameters)
{
	const FString OriginalLanguage = TBText::GetLanguage();
	const FString OriginalCulture = FInternationalization::Get().GetCurrentCulture()->GetName();
	ON_SCOPE_EXIT
	{
		TBText::SetLanguage(OriginalLanguage);
		FInternationalization::Get().SetCurrentCulture(OriginalCulture);
	};
	if (!TestTrue(TEXT("Japanese activates before creating options"), TBText::SetLanguage(TEXT("ja")))) return false;
	auto* PC = LocalizationController();
	if (!TestNotNull(TEXT("Game controller"), PC)) return false;
	auto* Menu = CreateWidget<UTBOptionsMenu>(PC);
	if (!TestNotNull(TEXT("Options menu"), Menu)) return false;
	ON_SCOPE_EXIT { Menu->Dismiss(); };
	TestEqual(TEXT("Existing audio tab index is preserved"), int32(TBOptionsPage::Audio), 0);
	TestEqual(TEXT("Existing display tab index is preserved"), int32(TBOptionsPage::Display), 1);
	TestEqual(TEXT("Existing controls tab index is preserved"), int32(TBOptionsPage::Controls), 2);
	TestEqual(TEXT("Language is the fourth tab"), int32(TBOptionsPage::Language), 3);
	Menu->ShowPage(TBOptionsPage::Language);
	TestEqual(TEXT("Language page can be selected"), Menu->GetPage(), 3);
	TestNotNull(TEXT("Japanese language tab is visibly selected"), FindText(*Menu, TEXT("●  言語")));
	UTextBlock* ExistingTitle = FindText(*Menu, TEXT("オプション"));
	if (!TestNotNull(TEXT("Existing title text block"), ExistingTitle)) return false;
	TestTrue(TEXT("Switches while the menu already exists"), TBText::SetLanguage(TEXT("en")));
	TestEqual(TEXT("Same title widget now has English text"), ExistingTitle->GetText().ToString(), FString(TEXT("Options")));
	TestEqual(TEXT("Language page remains selected"), Menu->GetPage(), 3);
	TestNotNull(TEXT("English language tab is visibly selected"), FindText(*Menu, TEXT("●  Language")));
	TestNotNull(TEXT("English choice is marked as active"), FindText(*Menu, TEXT("●  English")));
	TestNotNull(TEXT("Japanese remains available in its native script"), FindText(*Menu, TEXT("日本語")));
	TestTrue(TEXT("Switches back with the same menu"), TBText::SetLanguage(TEXT("ja")));
	TestEqual(TEXT("Same title widget returns to Japanese"), ExistingTitle->GetText().ToString(), FString(TEXT("オプション")));
	return true;
}
#endif
