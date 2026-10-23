# WaveTagger

効果音収録素材のトリミング/タイプ分け/リネーム/整音デスクトップアプリ。全体設計・フェーズは `DESIGN.md` を参照。

- C++20 / JUCE 8 (`C:\JUCE`, add_subdirectory) / CMake / VS 2022 x64
- ビルド: `.\build.ps1` (Release) / `.\build.ps1 -Config Debug` / `-Run` で起動
- 出力: `bin\WaveTagger.exe` (ビルドツリーは `%LOCALAPPDATA%\WaveTagger\build`。E: 上だと juceaide の生成ファイル書き込みがハングするため)
- 描画確認: `WaveTagger.exe --snapshot <in.wav> <out.png>` で読込→スナップショットPNG保存→終了
- 構成: `Source/Core` (音声データ・ピーク階層・スペクトログラム・再生) / `Source/UI` (各コンポーネント)
- 日本語文字列リテラルは `WT_U8("...")` (Source/Util/Text.h) を使う
- 旧ライブラリ `G:\SE` は読み取り専用 (Cubase 参照のため絶対に移動・リネームしない)。新ライブラリは `G:\SFX_Library`

## テスト時の注意 (重要)
- 実設定 `%APPDATA%\WaveTagger` と Category/Type CSV (`G:\SFX_Library\_config`) には触れない。自動確認は必ず環境変数 `WT_APPDATA=<テスト用フォルダ>` を付けて実行する (付けないと `--snapshot*` / `--selftest*` は終了コード 2 で拒否する)
- `--snapshot-picker <out.png>` (Category アコーディオン) / `--snapshot-input <out.png>` (入力設定。`WT_DEMO_TAB=1` でショートカット側) / `--selftest-catalog` (CSV の読み書き)
- Category / Type の正本は `catalog.xlsx` (シート Categories / Types)。`categories.csv` / `types.csv` (UTF-8 BOM) はそこから自動生成される派生物。CSV を直接編集した場合は xlsx に取り込む (古い xlsx は `.bak-<時刻>` で保存)。Excel の「CSV」(Shift-JIS) も読める。アプリからの追加は xlsx の該当シートに 1 行追記するだけ (他のシート・書式は保持)
- 検証: `--selftest-catalog` (20 項目) / `--selftest-catalog-load` (`WT_APPDATA/config/catalog.xlsx` を読んで内容を表示。openpyxl 製 xlsx の照合用)

## 設定の置き場所 (複数 PC 前提)
- 共通 (Google Drive ミラー `D:\マイドライブ\WaveTagger\config` が既定。`Catalog::sharedDirectory()`): `catalog.xlsx` / CSV / `preferences.json` (キー・ホイール・色・命名・Category 使用回数)
- PC ごと (`%APPDATA%\WaveTagger\settings.json`): ライブラリの場所 (`libraryRoots`)、書き出し先、`configDir`、移行ダイアログの辞退フラグ。サイクル情報 (`projects\`) も PC ごと (WAV のパスが鍵)
- `preferences.json` が読めない時 (Drive 同期中など) は上書きしない。保存の初回に `.bak-<時刻>` を 1 つ作り、`.bak-*` は新しい 5 つだけ残す。Drive の競合コピー (`catalog (1).xlsx` 等) は警告表示
- 設定フォルダの変更 (`設定` ダイアログの「設定フォルダ」タブ): コピーのみ・上書き/削除なし。移動先にすでに設定があればそれを採用
- 検証: `--selftest-prefs` (設定の分割・上書き防止・フォルダ移動 12 項目)、`--selftest-catalog` (26 項目)。`WT_DEMO_TAB=0/1/2` で `--snapshot-input` のタブを選べる

## WAV へのマーカー埋め込み (`Markers ▾` メニュー / Ctrl+S)
- `Core/WavChunks`: `cue ` + `LIST/adtl` (labl, ltxt = RX 等でリージョンとして見える) + 独自 `wtag` チャンク (JSON, schema 1。タイプ名・番号・出力名・savedAt)。既存の他チャンク (fmt/data/bext/iXML/smpl/INFO...) は**バイト単位でそのまま**コピー。1MB ごとのストリーム処理
- 書き込みは隣に一時ファイルを作り、fmt と data を元と照合してから置き換える (失敗しても既存ファイルは壊れない)。RF64 / 4GB 超 / 壊れたファイルは拒否
- 開く時は WAV 内の `wtag` と PC のプロジェクト (`%APPDATA%\WaveTagger\projects`) の**新しい方**を採用 (`preferEmbedded`)。タイプは**名前**でパレットに対応付け、無ければ空き枠に追加
- Raw フォルダの外の WAV は「Raw にコピーして書き込む(推奨) / 元に書き込む / キャンセル」を確認。Raw 内の WAV は直接更新。Raw の場所は PC ごと (`settings.json` の `rawDir`、初回に選択)
- 検証: `--selftest-wav` (22 項目: 音声サンプル完全一致・他チャンク保持・拒否ケース・名前での対応付けなど)。`riff_check.py` 相当の独立確認は Python の `wave` / 手書き RIFF パーサで行った

## Category 連動 / Inbox / 区分し直し (Phase 4・5)
- Category を選ぶと Prefix と Type 9 枠がその Category のものに切り替わる (`CategoryProfiles`: 最後に使った設定を覚える。初回は CSV の `Prefix` 列 → Title case、Type は候補の先頭 9 件)。打ったサイクルがある時だけ確認ダイアログ。`preferences.json` (共通) に保存
- **Inbox** (`Core/InboxProcessor`, `UI/InboxWindow`, ツールバー `Inbox`): 受信フォルダを 2 秒ごとに監視し、サイズ/更新時刻が 3 回連続で変わらないファイルだけ処理 → Raw/<年>/ にコピー (内容一致を検証、wav 以外は 24bit WAV に変換) → 自動分割 → cue + wtag を Raw のコピーに埋め込み → 元ファイルを `_Done/` へ移動。フォルダ名がラベル (`TOOLS/Open/x.wav` = Category TOOLS, Type Open)。失敗したファイルはそのまま残し再試行しない。**初めて監視を ON にする時、フォルダに既にある録音は「すべて取り込む/新しい録音だけ」を選ぶ** (`inbox_ignored.json` に保存し再起動後も有効)。キューは `inbox_queue.json` (PC ごと)
- **区分し直し** (`Core/AudioFeatures`, `FeatureStore`, `Clustering`, `ReclassDb`, `HardLinks`, `UI/OrganizerWindow`, `Library` ボタンのメニュー): 先頭 4 秒から 48 次元の特徴量 → k-means でグループ化 → グループごとに Category/Type を割り当て → `reclass.json` (設定フォルダ。共有) に記録。**元ファイルは一切変更しない**。整理ビューは NTFS ハードリンク (`<Category>/<Type>/<Category>_<Type>NN.ext`)、同一ドライブ必須、作成したリンクだけを manifest で取り消し可能。特徴量は `features.bin` (PC ごと、追記型で再開可能)
- 検証: `--selftest-inbox` (22) / `--selftest-watcher` (9, 実スレッド) / `--selftest-organize` (28) / `--selftest-prefs` (21)。画面確認: `--snapshot-inbox` / `--snapshot-organizer`
- `JUCE_MODAL_LOOPS_PERMITTED=1` は自己テストがメッセージループを手動で回すためだけに有効化
- JSON の `getArray()` は、一時的な `var` に対して呼ぶとダングリングポインタになる (`readJson(f).getArray()` は不可。いったん変数に受ける)

## 綴り揺れ・重複のチェック (Organizer の「チェック」タブ, `Core/Checks`)
- **綴り揺れ** (`findSimilarNames`, 索引の名前のみ): 1 文字違い (隣り合う文字の入れ替えも 1 回) の名前を、多く使われている綴りを中心にまとめる。**番号・英字ラベルのシリーズ (SynthA-D, Eliot 1-7 V, Piano a-o) はタイプミスではないので除外**。感度は Strict ⊂ Normal ⊂ Loose (Normal は 6 文字以上で 1 回、10 文字以上で 2 回)。並びは「多い綴りに対して少数派が極端に少ない組」が上 = タイプミスらしい順。「別の名前として扱う」は `spelling_ignore.json` (設定フォルダ)
- **完全一致の重複** (`findExactDuplicates`): 索引全体から サイズ → 先頭/末尾 64KB → 全体のハッシュ の順に絞る。ハッシュは `hashes.bin` (PC ごと, 追記型) に保存し再開可能、中止可
- **似た音の重複** (`findNearDuplicates`): 選んだフォルダ内で、長さと特徴量がほぼ同じ (形式・ビット深度・前後の無音だけの違い)
- 実ライブラリ (`G:\SE` 約 9 万件 / 名前 37,273 種) での目安: Normal 約 4 秒、Loose 約 23 秒。サイズが同じファイルは 9,679 組 / 53,096 件 (段階 1 の読み込み約 6.6GB)。`--report-checks` で読み取り専用の報告を出せる (`WT_APPDATA` 必須)
- 旧ライブラリ (Cubase 参照) のファイルは、ここでは名前変更も削除もしない。一覧・Excel 書き出し・場所を開く、だけ
- 検証: `--selftest-checks` (19 項目)
