# 引継ぎメモ (2026-10-24 夕 更新)

別の PC で続きをするためのメモ。設計・フェーズは `DESIGN.md`、使い方・仕様・検証コマンドは `CLAUDE.md`。

## 現在地
- Phase 1〜5 実装済み。Phase 7 は、学習 / 整音チェーン (S1〜S4) / ★・NG・属性 / データセット書き出し / 翻訳辞書 まで実装済み。
- 今日 (10/24) 追加したもの (すべて push 済み。最新 `1723144`):
  - 書き出し: 番号の衝突回避 (書き出し先を見て空き番号)、Type 別フォルダ (`Export ▾` のトグル)、Prefix が Category と同じ語なら階層を作らない
  - catalog.xlsx: Category×Type マトリクス (TYPE01〜10)、日本語は Types シート、足りない Type は自動で Types シートに追記。Type のドロップダウン/メニューに日本語を表示
  - Inbox: Raw のファイルを「置き換え」でなく新規作成の移動で作る (ウイルスバスターが置き換えの瞬間に反応し、アプリが落ちたため)。`--report-import` (WT_IMPORT_FILE の録音を診断用に取り込む)
  - **スマホ録音 (ブラウザ版 WaveTagger Rec, `docs/`)**: GitHub Pages で公開。48kHz/24bit WAV を録音 → 停止で Drive の `_Inbox` へ自動アップロード。**実機 (Android) で録音〜Drive 送信まで成功を確認済み**。設定リンク `#cid=<クライアントID>&f=<フォルダID>` でスマホの設定を自動入力
- 未着手: Phase 6 (音声スレート、whisper.cpp)、Drive API 連携 (同期待ちなしの取得)、専用スマホアプリ、Cubase 参照解析、モーフィング/生成。
- 自己テスト 14 種 (`--selftest-` の catalog / prefs / wav / inbox / watcher / organize / checks / learn / condition / rating / dataset / dictionary / export / matrix) + `node Tools/test_wav.js`。**実行は毎回、新しい `WT_APPDATA` で** (`--selftest-learn` は同じフォルダで 2 回続けると失敗する)。

## 続きの PC で最初にやること
1. `git pull`。
2. ビルド環境: JUCE が `C:\JUCE` にあること。無ければ `cmake -S . -B %LOCALAPPDATA%\WaveTagger\build -DJUCE_DIR=<JUCE のパス>` で一度 configure してから `.\build.ps1`。2026-10-24 の PC では `G:\MCW_Effects\JUCE` (8.0.12) を使い、VS は 2019 (MSVC 19.29) でビルドが通った。
3. 設定フォルダ (`Catalog::defaultDirectory()`): Drive ミラー `D:\マイドライブ\WaveTagger\config` が既定。「設定…」→「設定フォルダ」タブで確認。ライブラリの場所・Raw・データセットの場所は PC ごと (`%APPDATA%\WaveTagger\settings.json`) なので、初回に選び直す。
4. 注意: `juce::String` の `+` を 1 つの式に 30 個ほどつなぐと VS 2019 が `C1060 ヒープの領域を使い果たしました` で落ちる。メッセージ文は `+=` で短い文に分けること。

## 次にやること (候補)
1. **PC 側の Inbox で、スマホの録音が取り込まれるかの確認** (受信フォルダ=`D:\マイドライブ\pcm録音\_Inbox`、Raw=`G:\SFX_Library\Raw`)。最新の exe (`bin\WaveTagger.exe`) で。取り込みの速さ・分割の精度・候補の当たりを見る。うまくいかない録音は `WT_IMPORT_FILE=<wav> WT_APPDATA=<テスト用> WaveTagger.exe --report-import` で原因を切り分けられる
2. **学習の改善**: (a) マーカー区間を修正して保存すると、古い区間のデータが学習ログに残る (取り除く処理が無い)、(b) ★を候補の重みに使っていない (記録はしている)、(c) Raw の確認済みサイクルを一括で学習ログに取り込む機能が無い (データセットの一括追加は学習ログには入らない)
3. Raw の場所は PC ごと (`settings.json` の `rawDir`)。他 PC と共有するなら Drive 側に置く案があるが、容量に注意。WAV 自体にマーカー・★・NG・属性は入っているので、ファイルを渡せば情報も移る
4. スマホ連携の次の段階: Drive API (同期待ちなし・ラベル付き取得) → 専用アプリ。時機を見て

## スマホ録音アプリの設定 (家でやり直す時)
- Google Cloud: プロジェクト `Politics-News-Asset` に OAuth クライアント (ウェブ アプリケーション、名前 `WaveTaggerRec`、JavaScript 生成元 `https://nobuokiyota.github.io`) を作成済み。クライアント ID は秘密ではない (リポジトリには置かず、設定リンクで渡す)。同意画面は「外部/テスト」で、テストユーザーに自分のアカウントを登録
- GitHub Pages: Settings → Pages → `main` / `/docs`。公開 URL `https://nobuokiyota.github.io/WaveTagger/`。手順の全文は `docs/SETUP.md`
- 接続は約 1 時間で切れる (テストモードの制限ではなく、トークンの有効期限)。切れていると停止時にログインの小窓が出る。画面は録音中点けたままにする
- 直った不具合の記録: 新しいクライアントは反映まで数時間かかることがあり、その間スマホで `401 invalid_client` が出た。PC の Chrome では先に通った

## 宿題 (決まっていないこと)
- **Category×Type の中身づくり**: マトリクスは実装済み。73 カテゴリ分の Type 案と日本語は、Gemini 側で xlsx に書き進める (`ai_archives/` に記録あり)。編集中は WaveTagger を閉じておく (同じ xlsx に書くため)
- **ASIO**: 現状は Windows Audio (Focusrite、48 kHz、480 サンプル) で足りている。ASIO には Steinberg の SDK が別に要る。遅延や音切れが実際に問題になったら検討。
- **アイコン**: `Resources/icon.*` と `CMakeLists.txt` の `ICON_BIG` / `ICON_SMALL`。ビルドしてエクスプローラー等で表示を確認する。

## 未確認のこと (自動テストでは確かめていない)
- 実録音でのハム検出・L/R バランス補正の精度、聴感 (A/B 試聴でのクリックの有無など)。しきい値は合成信号で決めた値。
- 画面の実操作: 一覧の星クリック・属性セルのダブルクリック、属性エディタ・翻訳辞書のウィンドウ、`Export ▾` メニュー、Raw 全体の一括追加の進捗バーと中止。
- 実 Raw フォルダ (数百ファイル) での一括追加の所要時間。
- Android の実機で、録音中に画面を消した時の挙動 (ブラウザがマイクを止める可能性)。48kHz/24bit が端末で実際に得られているか (画面の表示を確認)。
- データセット ID を「ファイルサイズ」から「音声の長さ」に変えた (`4f6aea3`)。**変更前に作ったデータセットで、すでにマーカーを書いたファイルの行は、一度だけ重複する**。重複 (同じ元ファイル・同じ範囲の行) がないか確認する。

## 判断済みのこと
- データセットの NG は、新しいものは追加せず、すでにあるものは `"ng": true` の印だけ付ける (音声は消さない)。
- Inbox が自動でタイプを付けただけの未確認サイクルは、データセットに入れない。
- 整音は録音を変更せず、書き出す WAV にだけ掛ける。既定は全部オフ。
