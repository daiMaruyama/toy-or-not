# 試遊手順

ゲーム起動時はTitleから部屋を作成・検索・参加します。Title、退出、切断時の戻り方は [Titleと画面遷移](TITLE_FLOW.md) を参照してください。エディタの起動先はステージのままです。

ルールの数値は Notion の仕様を参照してください。このページのコマンド引数は動作確認用の例です。
制限時間切れ・全員収納で人間勝利、時間内の必要アイテム収集でおもちゃ勝利となります。
箱の救助は外側の操作位置で行い、完了すると仲間が箱外へ移動します。
救助の進捗はおもちゃ側だけに表示します。人間側には救助開始のメッセージ・進捗・警報音を通知しません。`tb.DebugHUD`でも人間側には救助の進捗を表示しません。

人間の収納操作は、おもちゃを持って箱の正面に近づき、箱を見ながらEを3秒間押し続けます。画面には箱の位置・距離、長押しの案内、経過秒数と進捗バーが表示されます。Eを離す、箱から離れる、箱への視線が45度の範囲を外れると進捗はリセットされますが、おもちゃは持ったままです。置きたい場合はQを押します。収納の受付は正面全体、救助は従来どおり外側の操作位置です。

ロビーでは設定を閉じるとWASD・マウスで移動と視点操作ができ、Escで設定を開閉します。エモートは1・2・3、隠しダンスは0です。設定・チャット操作中は移動できません。

試合中、おもちゃは正面の味方や小物に近づき、左クリック（ゲームパッドはRB）で1つ押せます。小物に歩いて触れただけでは動きません。テーブルの端から押し落とすと、床までの高さに応じた着地音を確認できます。

## ビルド

既定はLANです。Steam配布時だけ切り替え、作成後はLANへ戻します。切り替え時はEditorを終了し、変更後に再起動してください。

```powershell
.\Scripts\BuildEditor.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.8'
.\Scripts\PackageWindows.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.8'
```

## マップ

| マップ | 用途 |
| --- | --- |
| `TB_ToyWorldDemoExpanded` | Demoの家具付きステージ |
| `TB_StylizedHouse` | Houseの家具付きステージ |
| `Arena` | 検証用。床・壁・箱・アイテム・開始位置をPlay時にC++が生成する |

部屋を作る（Host room / `TBHost`）と、ホストが本番ステージ2つから1つを選び、参加者も同じマップへ入ります。
PIEは部屋を作らずエディタで開いているマップをそのまま使うため、本番ステージを試すときは `TB_ToyWorldDemoExpanded` か `TB_StylizedHouse` を開いてからPlayします。
マップに箱（TBBox）がなく、GameModeの `bBuildTestArena` が有効な場合だけ、検証用の床・箱・アイテムが生成されます。

本番ステージは保存済みの `.umap` をUEエディタで直接編集します。家具・壁・ドア・照明・ロビーカメラは配置済みのActorです。
エディタ起動時は `TB_ToyWorldDemoExpanded` を開きます。別ステージはContent BrowserのMapsから開いてください。
編集方法と維持する配置条件は [ステージ編集](STAGE_EDITING.md) を参照してください。Pythonによる生成・実行手順はありません。

## UE側で最初に用意するもの

`Content/Maps/Arena.umap` は作成済みです。Content BrowserのMapsからArenaを開いて、そのまま試遊できます。エディタ上では照明と空が表示され、床・壁・箱などはPlay後に生成されます。以下は作り直す場合の手順です。

エディタで作業を保存して終了し、`Scripts/BuildEditor.ps1`で通常ビルドしてください。新規クラス・削除クラス・衝突チャンネル・プラグインの変更があるため、再起動が必要です。

現在のContentには `ThirdPerson/Lvl_ThirdPerson`、`SandBox/Lvl_ChildRoom`、`Tests/MatchLoop/Lvl_MatchLoop` がありますが、既存レベルのGameMode Overrideや配置物は未検証です。最初は以下の空マップが確実です。

1. File > New Level > Empty Levelを作る。既存レベルの未保存作業は先に保存する。
2. Directional LightとSky Lightを1つずつ配置し、MobilityをMovableにする。Sky Lightは必要に応じてRecaptureする。
3. World SettingsのGameMode Overrideを `TBGameMode` にする。使用するPawnは `TBCharacter`。
4. `/Game/Maps/Arena`、つまり `Content/Maps/Arena.umap` に保存する。
5. ArenaのWorld SettingsでGameModeがTBGameModeであることを確認する。ゲーム起動時の既定マップはTitleなので、Arenaの検証では明示的にArenaを開く。

Arenaの床・壁・箱・検証用アイテム・定員分の開始位置はC++が生成します。床やPlayerStartを追加する必要はありません。キャラクターは設定された子ども／おもちゃを使い、素材がない場合だけCubeで代替します。

Contentはサブモジュールです。マップや素材はContent側で管理し、その後に親リポジトリの参照を更新してください。

## 1台でルールを確認する（Steamを使わないPIE）

エディタを終了し、プロジェクトルートのPowerShellで実行します。

```powershell
.\Scripts\SetOnlineMode.ps1 -Mode LAN
```

エディタを再起動して `TB_ToyWorldDemoExpanded` または `TB_StylizedHouse` を開き、Playの設定をNumber of Players=3、Net Mode=Play As Listen Server、New Editor Windowにします。単純な地形でルールを確認する場合はArenaを使います。PIEの接続を使うため、この段階ではTBHost/TBFindを呼びません。

F10でコンソールを開き、ホストで次を入力します。

```text
TBRules 1 2 60 5
TBPrefer 1
```

おもちゃ役のウィンドウで `TBPrefer 2`。全員R、ホストF5で開始。設定変更後はReadyが解除されるので、Rを押し直します。

- WASDで移動、マウスで視点操作、Spaceでジャンプ。人間の歩行は255 cm/秒、運搬中は170 cm/秒。おもちゃの歩行は50 cm/秒で、左Shiftを押している間は120 cm/秒。おもちゃのジャンプ初速は400 cm/秒で、標準重力では高さ約82 cm。凍結・運搬中はダッシュできません。
- 人間: Eで捕獲、箱の外の小さな操作マーカーでE長押しで収納、Qで落とす。
- おもちゃ: アイテムに接触し続けて取得、箱の操作マーカーでE長押しして救助。
- 時間切れを手早く試すなら次の試合で `TBRules 1 2 10 5`。
- 2人なら `TBRules 1 1 60 5`。ただし唯一のおもちゃを収納すると勝利が確定し、救助の検証はできません。
- 勝利後は勝ったチームが三人称で約10秒動けます。負けたチームは勝者を観戦し、その後は全員が自動でロビーに戻ります。
- 戻った後は全員のReadyが解除されます。役割希望・人数設定・ステージは保持されるので、必要なら変更して全員Ready→ホストStartで再戦します。
- 結果画面の「Leave room」は自分だけ退出します。ホストの「Close room」はサーバーを終了するため、再戦したい場合は押さないでください。

受入確認: 凍結中の落下、視線解除、取得進捗の停止/再開、おもちゃ同士の衝突、収納位置の重なり、救助時の箱外移動、残り0秒でHUMANS表示を確認してください。

## Steam App ID 5582500で別PCと接続

このリポジトリはOnlineSubsystemSteamとSteamSocketsを有効化済みです。SteamDevAppId は製品 ID `5582500` を使用します。接続テストには、このアプリへのアクセス権を持つ Steam アカウントを使用してください。

1. 2台以上のWindows PCと、それぞれ別のSteamアカウントを用意。救助とおもちゃ同士の衝突まで試すなら3台・3人で行う。
2. 各PCでSteamを起動してオンライン状態でログイン。エディタ内の複数窓によるSteam実接続テストは避け、Developmentパッケージを使う。
3. エディタを終了し、以下でSteamへ切り替えてパッケージする。

```powershell
.\Scripts\SetOnlineMode.ps1 -Mode Steam
.\Scripts\PackageWindows.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.8'
.\Scripts\SetOnlineMode.ps1 -Mode LAN
```

LANへ戻しても、作成済みのSteamパッケージの設定は変わりません。

4. `Builds/Windows` 以下のパッケージ全体を各PCへコピー。exeだけをコピーしない。
5. スクリプトはDevelopmentテスト用 `steam_appid.txt` を実行ファイルのフォルダーへ配置する。内容は設定ファイルの `SteamDevAppId` から取得する（現在5582500）。手動パッケージの場合は必要に応じて同じファイルを置く。Steamへの製品アップロードには含めない。
6. 全員がパッケージのToonStory.exeを起動。ホストはF10で `TBHost`、参加者は `TBFind`、結果が表示されたら `TBJoin 0`（目的の部屋の番号）を実行。
7. 接続後にホストが `TBRules 1 1 60 5`（2人）または `TBRules 1 2 60 5`（3人）を設定。全員R、ホストF5。
8. 再戦はホストの「Return together to lobby」または `TBLobby`。全員同じ接続のままロビーに戻り、Readyして再開始する。部屋を退出・終了したい場合だけ `TBLeave` を使う。

3人接続の回帰確認では、2人目が参加した後も試合を開始せず、3人目が改めてFindを実行して同じ部屋を発見・参加できることを確認する。試合中は検索・参加を拒否し、ホストが再戦ロビーへ戻した後は空席への検索・参加を再開する。Steamでは`bAllowJoinInProgress`がメンバー変更時のロビー参加可否にも使われるため、待機中はtrue、試合中はfalseに設定する。サーバーのPreLoginでも試合開始後の参加を拒否する。

部屋一覧の所有者名を確認してください。コードは独自の識別子で結果を絞りますが、部屋検索・Steam招待・異なる回線での接続は実機検証が必要です。

接続できない場合は `-log` を付けて起動し、`LogOnline`・`STEAM`・`SteamSockets`と `[ToyBoxSession]` を確認します。Steamログイン、同じビルド/モード、対象マップの保存とCook、Windows Firewallの許可を確認してください。LANに切り替えた場合は再パッケージが必要です。


## 設定と切断時の確認

- 定員は `Config/DefaultGame.ini` の `[/Script/Engine.GameSession] MaxPlayers` を参照します。変更後は再起動してください。
- 救助の加速割合は `TBBox` の `AdditionalRescuerBonus` で調整できます。自動生成する箱を調整する場合は `TBBox` のBlueprintを作成し、使用するGameModeの `BoxClass` に指定します。
- 試合開始後は部屋の募集・招待を閉じます。開始前に取得した検索結果からの参加もサーバーが拒否します。
- 非ホストが退出しても両陣営が残っていれば継続します。最後の人間が退出するとおもちゃ勝利、最後のおもちゃが退出すると人間勝利です。結果後も部屋は自動解散しません。
- ホストが終了すると参加者は通信失敗後に退出します。ホスト移譲・途中参加・再接続には対応していません。
- 同じ部屋での再戦は、通常の試合終了・中断結果からホストが実行できます。収集物、収納・運搬・凍結、取得進捗、救助、勝敗・時間をリセットします。ロビー復帰後は部屋検索・招待による参加も再開します。
- `r.DefaultFeature.AutoExposure=False` は、Arenaで視線や遮蔽物による凍結を確認するとき、視点方向による明るさの変化を抑えるための設定です。全マップに適用されるため、本番マップの照明調整時に見直します。

実機での確認は [受入テスト](ACCEPTANCE_TESTS.md) に沿って行ってください。

## 再戦処理の確認記録（2026-10-04）

- `ToonStory.Match.Rematch`：両ステージで3回連続の開始→終了→ロビー復帰を確認。収集済み・収納・凍結・運搬と中断結果を含む。
- 同一PCのLANホスト／クライアントで、再接続なしのロビー復帰と2試合目の開始・終了を確認。
- 参加側の既定DirectX 12起動では再戦操作前のロビー読み込み中にRendererのアクセス違反が1回発生。参加側だけ `-dx11` を付けた検証では完了。プロジェクトの描画設定は変更していない。
- 別PC間のSteam接続、再戦ロビーの第三者による検索・招待参加は未検証。

## エモートと倒れる操作（2026-11-18）

- 別のエモートを選ぶと再生中でも切り替わり、自然終了後は同じエモートもすぐ再開できます。同じエモートの再生中の連打は再始動しません。移動・捕獲などによる中断は従来どおり優先します。
- ブリキのロボットは、0の隠しダンスで頭が4周し、左右の腕が逆方向に6周ずつ回ります。1の手振りは片腕を外に開き、2の応援は両腕を外側に開いてから前に振り上げます。4のびっくりは両手を外側から頭の横まで上げます。
- テディベアは1で肩の縫い目を支点に右手を振り、4では両手を外側に開いて上げます。
- おもちゃは Ctrl（設定の「倒れる（押している間）」）を押している間だけ倒れ、離すと立ち上がります。TinRobot・ArmyMan・Nutcracker は仰向け、TeddyBear はうつ伏せ、Duck・Dinosaur・Car は横倒しになります。Nutcracker は背中側へ出た銃があるため、足と銃が床へ接するよう仰向けから15度だけ傾けます。倒れている間は移動・ジャンプ・エモートを行えません。凍結していなければ視点操作は可能です。人間の Ctrl は従来どおり押している間のしゃがみです。
- Demo と Stylized House では、拾えるおとり人形に立ち姿と寝姿が混ざります。拾い上げと箱への収納では通常の持ち姿勢に戻ります。
- 倒れる姿勢は表示だけを変更し、捕獲判定に使うカプセルの大きさは変えません。凍結中は倒れた姿勢を保ちますが、キーを離した要求は保持し、解凍後すぐに立ち上がります。メニューや入力フォーカスの切替でも保持を解除します。拾われた時・陣営変更時・リザルト移行時には姿勢を解除します。
- 接地は倒れ始めの静止ポーズから実頂点を一度だけ計算し、同じ姿勢の間はキャッシュします。Duck / TinRobot / TeddyBear / Dinosaur / ReconSoldier / Nutcracker の本体6メッシュは LOD0 の CPU Access を保持してください。毛の追従レイヤーは計算に含めません。静的メッシュの車は従来の形状境界を使います。
- 回帰確認: `ToonStory.NormalAnimation.Emotes`、`ToonStory.NormalAnimation.ToyFall` と入力登録・陣営別キー・キー設定のテスト。ネットワークの遅延あり確認は別途行います。

## 室内の影と収集電池（2026-11-18）

`DefaultScalability.ini` の ShadowQuality 2/3 は、局所ライトの VSM を1段細かくし、移動するライトの解像度低下も抑える。ページ数は4096/8192に合わせる。鏡・部屋の配置、露出、ライトの色や強さは変えない。品質ごとにVRAM負荷が増えるため、対象GPUで確認する。仕様: https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine

収集用 `TBPickup` は、おもちゃ本人のカメラで見ているときだけ `PickupRimMaterial` の発光縁取りを使う。演出DAの `Pickup` で視野の半角25度・距離600cm・強さ0.28を調整でき、範囲の端では滑らかに弱くなる。人間・運ばれているおもちゃ・別カメラでの観戦には表示しない。壁で遮られた場合、視野外、取得済みでは消す。回収や進捗の判定は変更しない。

表示側だけ毎秒20回、角度と距離に入った電池の遮蔽を調べる。取得後は更新を停止し、縁取りが不要なら追加描画も外す。元の電池メッシュや背景の素材は変更しない。縁取りを描く収集個体だけ通常メッシュ描画を使用し、ステージの小物のNanite設定は維持する。素材は既存 `/Game/Items/Battery` の AlwaysCook に含まれる。回帰確認は `ToonStory.Items.BatteryHighlight` と実際のおもちゃ視点で行う。
