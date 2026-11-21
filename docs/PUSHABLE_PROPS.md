# 押して動く小物

`ATBGameState::ConfigurePushableProps` から `TBPushableProps::TryEnable` を呼び、既存配置を実行時に物理化する。レベルアセットの保存や再配置は不要。

- 対象レベルは `TB_StylizedHouse` と `TB_ToyWorldDemoExpanded`。共有のおもちゃ箱ルームは対象外。
- Houseでは缶9種類、シリアル箱3種類、タイマー、クッキージャー、ガラス瓶、洗面所のコップ、パンが対象。
- ToyWorldでは電池、ミニカー、トラック、木のおもちゃ、列車、トラクター、カセット、ゲームパッド、リモコン、絵の具、果物、水筒、糸巻きが対象。
- メッシュのフォルダーと名前を許可リストで照合する。家具、床、壁、線路、複数部品のキャラクターは対象外。半径方向の最大半サイズ35cm以下で、物理シミュレーション可能な簡易衝突形状を持つものだけを使う。
- 固定したい個体にはActorまたはStaticMeshComponentに `TBFixedProp` タグを付ける。Blueprintや他Actorに取り付けられた個体は変更しない。既存の `TBPushable` 個体にも同じ初期静止・衝突設定を適用し、元の質量override（Houseは2kg、Demoは1kg）は保つ。果物をweldしたボウルの実効質量には果物分も含まれる。`TBPushableConfigured` で初期化済みの個体を区別し、再設定で移動を止めない。
- 隣の小物と配置時から衝突形状が重なっている部分は、両方の初期めり込み解消速度を0にして横方向への押し出しを抑える。棚・床側の接触補正と通常の移動後の衝突解消は維持する。
- 開始時はボディ生成時の `bStartAwake` と衝突設定後のsleepを揃えて配置を保つ。人間・おもちゃが接触すると動き、サーバーから位置を同期する。おもちゃのカプセルにはBlockで応答し、通常接触は小さい力に制限する。同じフレームのSweepとImpactによる二重の押し出しを防ぐ。小物への自動の乗り上げは無効にする。
- クライアントの初期状態は一時フィールド `ReplicatedMovement.bRepPhysics` で受信済みか判定する。保存マップのsimulating設定だけでは受信済みと扱わず、受信済みのawake/sleepを初期静止処理で上書きしない。
- Houseの床下にある既存 `PropSupports/Cube_Support` は、見た目と同じBOX簡易形状を物理接触に使う（SimpleAndComplex）。薄い三角面から床下へ抜ける経路を防ぎ、可視メッシュ・材質・Naniteは変更しない。
- Houseの静的なゴミ箱3個は、空間を埋めてしまう4個の凸形状に代えて既存メッシュ表面の衝突を使う（ComplexAsSimple、両面）。床付近の瓶との不要な接触補正・揺れを抑える。元メッシュは4,105三角形で、geometry・材質・Nanite OFFは維持する。動く小物は引き続き簡易衝突を使う。
- 今回の追加は既存配置のHouseパン6個とDemo糸巻き4個。実行時allowlist対象はHouse62個、Demo56個（既存タグ付き小物は別）。既存のメッシュ、マテリアル、Nanite設定をそのまま使い、描画メッシュや衝突派生資産は増やさない。

## 検証

各ステージのStandaloneで `Automation RunTests ToonStory.Playroom.ExpandedProps`。実際の物理ボディ、移動、床との衝突、玩具カプセルのBlock、既存の押せるタグ、再設定時に動作をリセットしないことを確認する。`ToonStory.Interaction.ToyPush` は通常接触の弱い押し出し、同一フレームのA→B→A接触、明示的な押す操作、壁越しの抑止を確認する。`ToonStory.Playroom.PropStartupStability` は重なった2個の缶を起こし、開始時の横ずれ・静止時の速度と、押した後に移動できることを一時的な検証台で確認する。

実配置の開始直後も別に確認する。入力なしで位置・速度を記録し、棚上の缶・シリアル、床付近の瓶、果物皿、ベッド上の木のおもちゃ、梁上のカセットを押して、静止・落下後の床支持を調べる。孤立したテスト台の成功だけで配置由来の重なりを解決済みとしない。

同じステージをListen Serverとクライアント2個で起動し、3プロセスそれぞれで同じテストを実行すると、サーバーが動かした小物がクライアントでも同じテスト台へ移動・着地することを確認できる。テスト台と移動は一時的で、マップには保存しない。

各プロセスには別々の `-SessionId=<GUID>` を指定してAutomation同士の干渉を防ぐ。マップURLはuprojectの直後に置く。テストはGameStateのBeginPlay到着を待ってから小物を調べる。

ガラスの落下SEは `GlassDropLight/Medium/Heavy` の3段階すべて `glass_clink` を使用する。高さごとに音量が変わり、破砕音は鳴らさない。素材・ライセンスは `AUDIO.md` と `Content/Audio/Notices/CREDITS.txt` を参照。
