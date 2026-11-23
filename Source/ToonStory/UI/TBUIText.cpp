// Copyright (c) 2026 Mamodou. All Rights Reserved.
#include "TBUIText.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/StringTableRegistry.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Internationalization/Regex.h"

namespace
{
	const FName TableId(TEXT("ToyOrNot.UI"));
	FString Language;
	uint32 Revision = 0;
	FSimpleMulticastDelegate Changed;
	TMap<FString, TUniquePtr<FString>> LiteralCache;
	TMap<FString, FString> NoticeKeys;

	struct FLegacyNotice { const TCHAR* Source; const TCHAR* Key; };
	// Source tokens identify old server messages; editable display copy is in the CSV.
	const FLegacyNotice LegacyNotices[] = {
		{TEXT("近くの安全な場所へ移動しました。"), TEXT("Notice.UnstuckDone")},
		{TEXT("今の状態では解除できません。運搬・収納・停止・操作中は使えません。"), TEXT("Notice.UnstuckState")},
		{TEXT("スタック解除は30秒に一度使えます。"), TEXT("Notice.UnstuckCooldown")},
		{TEXT("移動を3秒ほど試してください。動けない状態を確認してから解除できます。"), TEXT("Notice.UnstuckTryMove")},
		{TEXT("3秒後に近くの安全な場所へ移動します。動くとキャンセルします。"), TEXT("Notice.UnstuckPending")},
		{TEXT("状態が変わったため、スタック解除をキャンセルしました。"), TEXT("Notice.UnstuckCancelled")},
		{TEXT("近くに安全な移動先が見つかりませんでした。"), TEXT("Notice.UnstuckNoSpot")},
		{TEXT("そのプレイヤーはすでに退出しています。"), TEXT("Notice.PlayerGone")},
		{TEXT("今はエモートできません。安全な場所で止まってから選んでください。"), TEXT("Notice.EmoteBlocked")},
		{TEXT("At least one child and one toy are required to start."), TEXT("Notice.NeedBothTeams")},
		{TEXT("試合ステージへ移動できませんでした。"), TEXT("Notice.StageTravelFailed")},
		{TEXT("NPCの移動経路を準備できませんでした。"), TEXT("Notice.NPCNavigationFailed")},
		{TEXT("Same room. Choose your role and ready up for another round."), TEXT("Notice.SameRoom")},
		{TEXT("No toy in reach."), TEXT("Notice.NoToy")},
		{TEXT("Lights on"), TEXT("Notice.LightsOn")},
		{TEXT("Lights off"), TEXT("Notice.LightsOff")},
		{TEXT("部屋が見つかりませんでした。"), TEXT("Notice.NoRooms")},
		{TEXT("ホストによって部屋から退出しました。"), TEXT("Notice.Kicked")},
		{TEXT("ホストが退出したため、部屋が解散しました。"), TEXT("Notice.RoomClosed")},
		{TEXT("ロビーマップの読み込みに失敗しました。"), TEXT("Notice.LobbyLoadFailed")},
		{TEXT("別のマップ移動が処理中です。"), TEXT("Notice.TravelBusy")},
		{TEXT("移動先が設定されていません。"), TEXT("Notice.MissingDestination")},
		{TEXT("移動先のマップが見つかりません。"), TEXT("Notice.MissingMap")},
		{TEXT("サーバーがマップ移動を開始できませんでした。"), TEXT("Notice.TravelStartFailed")},
		{TEXT("隠れる準備に必要な参加者を確認できませんでした。"), TEXT("Notice.HidePlayersMissing")},
		{TEXT("隠れる準備中に陣営の参加者がいなくなりました。"), TEXT("Notice.HideTeamLeft")},
		{TEXT("参加者またはNPCの開始状態を確認できませんでした。"), TEXT("Notice.StartPlayersMissing")},
		{TEXT("参加者のキャラクターを確認できませんでした。"), TEXT("Notice.StartCharacterMissing")},
		{TEXT("ロビー配置後の元の位置へ安全に戻せませんでした。"), TEXT("Notice.RestoreFailed")},
		{TEXT("ステージの入口設定が不足または重複しています。"), TEXT("Notice.EntranceSetup")},
		{TEXT("脱出コースを一つだけ登録してください。"), TEXT("Notice.EscapeSetup")},
		{TEXT("脱出コースの箱参照が違います。"), TEXT("Notice.EscapeBox")},
		{TEXT("No online session interface."), TEXT("Notice.NoOnline")},
		{TEXT("Existing session: TBLeave before hosting again."), TEXT("Notice.ExistingRoom")},
		{TEXT("Hosting requires an offline/server world and MaxPlayers >= 2."), TEXT("Notice.HostingRequirements")},
		{TEXT("CreateSession could not start. Check Steam login/App ID."), TEXT("Notice.CreateStartFailed")},
		{TEXT("Room created. Opening listen server..."), TEXT("Notice.RoomCreated")},
		{TEXT("Lobby map is not installed. Opening test arena."), TEXT("Notice.LobbyNotInstalled")},
		{TEXT("TBLeave before searching."), TEXT("Notice.LeaveBeforeFind")},
		{TEXT("FindSessions could not start."), TEXT("Notice.FindStartFailed")},
		{TEXT("Could not update room advertisement; retrying."), TEXT("Notice.AdvertiseRetry")},
		{TEXT("Invalid room index. TBFind first."), TEXT("Notice.InvalidRoom")},
		{TEXT("Room build mismatch. TBFind to refresh."), TEXT("Notice.BuildMismatch")},
		{TEXT("TBLeave before joining another room."), TEXT("Notice.LeaveBeforeJoin")},
		{TEXT("Room is closed or full. TBFind to refresh."), TEXT("Notice.RoomFull")},
		{TEXT("JoinSession could not start."), TEXT("Notice.JoinStartFailed")},
		{TEXT("Invite build mismatch."), TEXT("Notice.InviteMismatch")},
		{TEXT("Online operation still pending. Wait for its completion before leaving."), TEXT("Notice.OperationPending")},
		{TEXT("箱・最終到達領域・出口を設定してください。"), TEXT("Notice.StageSetup01")},
		{TEXT("最終到達領域は箱内室の床と天井の間に置いてください。"), TEXT("Notice.StageSetup02")},
		{TEXT("コース範囲の大きさまたは回転が無効です。"), TEXT("Notice.StageSetup03")},
		{TEXT("脱出先が無効です。"), TEXT("Notice.StageSetup04")},
		{TEXT("出口をコースと箱内室の外の安全な床へ置いてください。"), TEXT("Notice.StageSetup05")},
		{TEXT("最終領域・余白・支持面タグ/共通level/想定数の設定が無効です。"), TEXT("Notice.StageSetup06")},
		{TEXT("最終領域が玩具capsuleと安全余白に対して小さすぎます。"), TEXT("Notice.StageSetup07")},
		{TEXT("最終面タグは指定共通level内の有効な非物理Block支持componentだけへ付けてください。"), TEXT("Notice.StageSetup08")},
		{TEXT("タグ付き最終面は箱室内で最終領域の足元を覆う同位置の面に限定してください。"), TEXT("Notice.StageSetup09")},
		{TEXT("最終支持面タグが欠落または想定外に重複しています。"), TEXT("Notice.StageSetup10")},
		{TEXT("最終領域を指定された水平支持面の直上へ、立位capsule全体が収まる高さで配置してください。"), TEXT("Notice.StageSetup11")},
		{TEXT("許可する重複最終面の上端は同じ支持高さへ合わせてください。"), TEXT("Notice.StageSetup12")},
		{TEXT("複数のドアは同じ順序の開始位置を設定してください。"), TEXT("Notice.StageSetup13")},
		{TEXT("入口の足音間隔は0.1秒以上にしてください。"), TEXT("Notice.StageSetup14")},
		{TEXT("入口の人数分の開始位置が不足しています。"), TEXT("Notice.StageSetup15")},
		{TEXT("入口の開始位置が無効または重複しています。"), TEXT("Notice.StageSetup16")},
		{TEXT("入口カメラの位置が無効です。"), TEXT("Notice.StageSetup17")},
		{TEXT("入口の扉のmeshと閉・開transformを設定してください。"), TEXT("Notice.StageSetup18")},
		{TEXT("入口の開始位置に安全な床・頭上空間がありません。"), TEXT("Notice.StageSetup19")},
		{TEXT("入口の開始位置どうしが重なっています。"), TEXT("Notice.StageSetup20")},
		{TEXT("開始準備中に参加者が変わりました。"), TEXT("Notice.StageSetup21")},
		{TEXT("開始位置がありません。ステージの配置を確認してください。"), TEXT("Notice.StageSetup22")},
		{TEXT("開始配置中に参加者または開始位置が変わりました。"), TEXT("Notice.StageSetup23")},
		{TEXT("最終体格で入口の安全な床・頭上空間を確認できませんでした。"), TEXT("Notice.StageSetup24")},
		{TEXT("開始位置が塞がれています。ステージの配置を確認してください。"), TEXT("Notice.StageSetup25")},
		{TEXT("開始位置から外れて配置されました。ステージの配置を確認してください。"), TEXT("Notice.StageSetup26")},
		{TEXT("入口への配置後に安全な床・空間を確認できませんでした。"), TEXT("Notice.StageSetup27")},
		{TEXT("開始準備中に参加人数が変わりました。"), TEXT("Notice.StageSetup28")},
		{TEXT("NPCを配置できませんでした。"), TEXT("Notice.StageSetup29")},
		{TEXT("開始確定前に参加者が変わりました。"), TEXT("Notice.StageSetup30")},
		{TEXT("開始確定中に参加者が変わりました。"), TEXT("Notice.StageSetup31")},
		{TEXT("開始確定中に参加人数が変わりました。"), TEXT("Notice.StageSetup32")},
		{TEXT("開始確定前にNPCが変わりました。"), TEXT("Notice.StageSetup33")},
	};

	bool LoadCatalog(const FString& Code, FStringTable& Table)
	{
		const FString Path = FPaths::ProjectContentDir() / TEXT("Localization/UI") / (Code + TEXT(".csv"));
		FString CSV;
		if (!FFileHelper::LoadFileToString(CSV, *Path)) return false;
		// Quoted cells preserve literal newlines. Keep the UI copy the same after
		// editing a translation on Windows or on a system using LF.
		CSV.ReplaceInline(TEXT("\r\n"), TEXT("\n"), ESearchCase::CaseSensitive);
		CSV.ReplaceCharInline(TEXT('\r'), TEXT('\n'));
		return Table.ImportStringsFromCSVString(CSV, *Path);
	}
}

bool TBText::SetLanguage(const FString& Code)
{
	if (Code != TEXT("ja") && Code != TEXT("en")) return false;
	if (Language == Code) return true;
	// Validate into a temporary table first: a missing/broken file must not clear the active UI.
	FStringTableRef Imported = MakeShared<FStringTable, ESPMode::ThreadSafe>();
	if (!LoadCatalog(Code, *Imported)) return false;
	if (!FInternationalization::Get().SetCurrentCulture(Code)) return false;
	auto& Registry = FStringTableRegistry::Get();
	auto Active = Registry.FindMutableStringTable(TableId);
	if (!Active)
	{
		Registry.Internal_NewLocTable(TableId, TEXT("ToyOrNot.UI"));
		Active = Registry.FindMutableStringTable(TableId);
	}
	if (!Active) return false;
	Active->ClearSourceStrings();
	Imported->EnumerateSourceStrings([&](const FString& Key, const FString& Value)
	{
		#if WITH_EDITORONLY_DATA
		Active->SetSourceString(Key, Value, TEXT(""));
		#else
		Active->SetSourceString(Key, Value);
		#endif
		return true;
	});
	// Missing translations fall back to Japanese, while retaining stable keys.
	FStringTableRef Japanese = MakeShared<FStringTable, ESPMode::ThreadSafe>();
	if (LoadCatalog(TEXT("ja"), *Japanese))
	{
		Japanese->EnumerateSourceStrings([&](const FString& Key, const FString& Value)
		{
			FString Existing;
			if (!Active->GetSourceString(Key, Existing))
			{
				#if WITH_EDITORONLY_DATA
				Active->SetSourceString(Key, Value, TEXT(""));
				#else
				Active->SetSourceString(Key, Value);
				#endif
			}
			NoticeKeys.Add(Value, Key);
			return true;
		});
	}
	Language = Code;
	FTextLocalizationManager::Get().RefreshResources();
	++Revision;
	Changed.Broadcast();
	return true;
}

FText TBText::Get(const TCHAR* Key)
{
	if (Language.IsEmpty()) SetLanguage(TEXT("ja"));
	return FText::FromStringTable(TableId, Key);
}

FString TBText::String(const TCHAR* Key) { return Get(Key).ToString(); }

const TCHAR* TBText::Literal(const TCHAR* Key)
{
	if (Language.IsEmpty()) SetLanguage(TEXT("ja"));
	const FString CacheKey = Language + TEXT(":") + Key;
	if (!LiteralCache.Contains(CacheKey)) LiteralCache.Add(CacheKey, MakeUnique<FString>(String(Key)));
	return **LiteralCache.FindChecked(CacheKey);
}

FString TBText::GetLanguage() { return Language.IsEmpty() ? TEXT("ja") : Language; }
uint32 TBText::GetRevision() { return Revision; }
FSimpleMulticastDelegate& TBText::OnLanguageChanged() { return Changed; }

FString TBText::TranslateNotice(const FString& Source)
{
	if (Language.IsEmpty()) SetLanguage(TEXT("ja"));
	for (const auto& Entry : LegacyNotices)
		if (Source == Entry.Source) return String(Entry.Key);
	struct FNoticePattern
	{
		const TCHAR* Prefix;
		const TCHAR* Suffix;
		const TCHAR* Key;
		bool bNumber = false;
		bool bNestedNotice = false;
	};
	// Only server-owned message shapes are recognized. Captured names/key labels
	// are arguments, never translated or interpreted as format strings.
	static const FNoticePattern Patterns[] = {
		{TEXT(""), TEXT("があそびにきた！"), TEXT("Notice.ChatJoined")},
		{TEXT(""), TEXT("はホストにおうちへ帰された"), TEXT("Notice.ChatKicked")},
		{TEXT(""), TEXT("がかえっていった"), TEXT("Notice.ChatLeft")},
		{TEXT(""), TEXT(" を部屋から退出させました。"), TEXT("Notice.KickedPlayer")},
		{TEXT("部屋が"), TEXT("個見つかりました。"), TEXT("Notice.RoomsFound"), true},
		{TEXT("タイトルの読み込みが"), TEXT("秒以内に完了しませんでした。"), TEXT("Notice.TitleTimeout"), true},
		{TEXT("接続が切れました。通信状況を確認してください。\n"), TEXT(""), TEXT("Notice.NetworkFailure"), false, true},
		{TEXT("マップの読み込みに失敗しました。\n"), TEXT(""), TEXT("Notice.MapFailure"), false, true},
		{TEXT("タイトルへ戻れませんでした。部屋を出る操作を再試行してください。\n"), TEXT(""), TEXT("Notice.TitleFailure"), false, true},
		{TEXT(""), TEXT(" 準備をやり直してください。"), TEXT("Notice.RetryPreparation"), false, true},
		{TEXT(""), TEXT(" 元の位置へ安全に戻せませんでした。ロビーへ戻ってください。"), TEXT("Notice.ReturnLobby"), false, true},
		{TEXT("Every player must press "), TEXT(" to be ready."), TEXT("Notice.ReadyRequired")},
	};
	for (const auto& Pattern : Patterns)
	{
		const int32 PrefixLength = FCString::Strlen(Pattern.Prefix), SuffixLength = FCString::Strlen(Pattern.Suffix);
		if ((PrefixLength > 0 && !Source.StartsWith(Pattern.Prefix)) ||
		    (SuffixLength > 0 && !Source.EndsWith(Pattern.Suffix)) ||
		    Source.Len() < PrefixLength + SuffixLength) continue;
		FString Value = Source.Mid(PrefixLength, Source.Len() - PrefixLength - SuffixLength);
		if (Pattern.bNumber && (Value.IsEmpty() || !Value.IsNumeric())) continue;
		if (Pattern.bNestedNotice) Value = TranslateNotice(Value);
		return FString::Format(Literal(Pattern.Key), {Value});
	}
	static const FRegexPattern MapPattern(TEXT("^Map requires exactly one box and enough items \\(boxes ([0-9]+), items ([0-9]+)/([0-9]+)\\)\\.$"));
	FRegexMatcher MapMatcher(MapPattern, Source);
	if (MapMatcher.FindNext())
		return FString::Format(Literal(TEXT("Notice.InvalidMap")),
		    {MapMatcher.GetCaptureGroup(1), MapMatcher.GetCaptureGroup(2), MapMatcher.GetCaptureGroup(3)});
	const FString* Key = NoticeKeys.Find(Source);
	return Key ? String(**Key) : Source;
}
