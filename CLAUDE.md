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

## 学習 (Phase 7)
- 判断ログ: `LabelStore` (`<configDir>\learning\labels_<PC>.jsonl`, PC ごとの追記型, 読み込み時に全 PC 分をマージ)。保存/書き出し時に、確定したサイクルの特徴量 (48 次元) とタイプを記録。提示された候補と最終回答を並べて残す (採用/訂正)
- 候補提案: `Suggester` (z-score kNN, 確信度は較正済み)。エディタで未タイプのサイクルに候補を表示 (一覧の「候補」列、`A` で採用)。確信度 0.70 以上は「全て適用」で自動タイプ (未確認扱い, 1 回の Undo で戻せる)
- Inbox: 取り込み時に Suggester のスナップショットで自動タイプ付け。キューに 確信度 / 自動 / 要確認 を保存し、「確認が必要なものを確信度の低い順」に並べる (`InboxQueue::reviewOrder`)。フォルダ名のタイプ指定は人の判断として優先
- 検証: `--selftest-learn` (26 項目), `--selftest-inbox` (26 項目), `--report-learn` (`WT_APPDATA` 必須)

## 整音チェーン (Phase 7, `Core/Conditioning`)
- 2 段構成。**A 段 (録音全体、`conditionFile`)**: DC 除去 → ハム除去 → ローカット → L/R バランス補正 (Butterworth 2/4 次、単一パス = 遅延なし・長さ不変、先頭値でウォームアップしてクリック防止)。**B 段 (切り出し後のサイクルごと、`conditionCycle`)**: 正規化 (Peak dBFS / LUFS BS.1770 ゲート付き)。マイクロフェードは `exportCycles` の `fadeMs` (B 段の先頭)。UI は S3 (下記)
- 既定は全部オフ (`ConditionOptions{}` はビット単位で無変更、書き出しは従来と同一)。正規化は**リミッターを使わず**、天井 (`ceilingDb` 既定 -1) を超えないようゲインを下げる。`maxGainDb` (30) 以上は持ち上げない。-120dBFS 未満は無音扱い
- 16bit 書き出しの TPDF ディザは、整音が有効な時だけ。適用内容は WAV の INFO コメントに `cond=dc,lc30Hz/2,norm=-1.0dBFS,gain=+3.2dB` の形で記録。元の `AudioData` は変更しない
- **ハム除去** (`detectHum` + 狭帯域ノッチ, `HumMode` off/automatic/mains50/mains60): 録音のうち**エネルギーが最小の 1/5 のフレーム** (FFT 2^17、短ければ 2^14 まで下げる。約 0.35 秒未満は解析しない) の平均パワースペクトルで、50/60Hz の 1〜8 倍音それぞれが周辺 (±4〜40 bin の中央値) より 10dB 以上突出しているかを見る。automatic は「そのシステム固有の倍音 (300Hz など両方に共通のものは除く) が 2 本以上」で採用し、強制 (50/60 固定) は 1 本でよい。ノッチは**実測した周波数** (放物線補間) に Q=30 で掛けるので、50/60 から少しずれたハムにも効く。見つからなければ無変更 (ビット単位)。音程のある定常音 (モーター音など) を誤検出しうるので、検出結果 (`hum60Hz/4` など) は WAV コメントに残し、UI ではオフにできるようにする
- 単一パスのノッチは録音の頭で立ち上がりの過渡が出るので、先頭の約 10τ (τ = Q/πf ≈ 0.16〜0.19 秒) は時間を逆向きにも処理してクロスフェードでつなぐ
- **L/R バランス補正 (S4, `detectPanBalance`, `PanMode` off/automatic)**: 100Hz 以上 (ハム・振動・風を除く) の 50ms フレームごとの左右エネルギーを測り、「音のあるフレーム」(下位 10% の床の +12dB 超。変動のない音 = 雨・機械などは -60dBFS 超の全フレーム) の合計から 左−右 を出す。**補正するのは 0.5dB 以上 6dB 以下で、フレームごとの差の P25〜P75 の幅が 3dB 以内 (= 一定のずれ) の時だけ**。像が動く録音・意図的なパン・片チャンネル不良・短い/無音・モノラルは何もしない (ビット単位で無変更、理由は `PanOutcome`)。補正は録音全体に一定のゲインで、左右を差の半分ずつ逆向きに動かす (全体の音量を保つ)。SFX 標準プリセットに含む。書き出しコメントは `pan+1.8dB` / `pan=none`
- **整音パネルと A/B 試聴 (S3)**: トランスポートバー中ほどの「整音...」(`UI/ConditioningPanel`, モードレスのウィンドウ) と「B: 整音後で試聴」(キー **B**、`Action::ToggleConditioned`)。設定は共通 `preferences.json` の `conditioning` (`ConditionSettings`: プリセット オフ/SFX 標準/カスタム + オプション + `previewGain`)。読み込み時は未知の名前→オフ、数値は範囲内に丸める。プリセットと値が合わなければカスタム扱い
- 整音後の録音全体 (A 段) は `Core/ConditioningPreview` が**別スレッドで、B をオンにした時だけ**作る (設定変更から 0.3 秒待って作り直し、古い結果は捨てる。A 段が不要な設定ならコピーしない = 元の録音をそのまま使う)。ハム検出だけは軽いので常に別スレッドで実行しパネルに表示。作成中は原音が鳴り続け、できたら再生を止めずに差し替える (`PlaybackEngine::swapAudio`: 長さ・チャンネル数が違えば拒否)。「音量の補正も含める」がオンの時だけ、再生範囲の正規化ゲインを `previewGainFor` (書き出しと同じフェード+正規化) で求めて `setGain` (ブロック内でランプ)。60 秒を超える範囲は計算せず等倍
- 書き出しは現在の設定をそのまま使う (フォルダ選択のタイトルに要約、完了ダイアログにハム/ゲイン範囲/上限で止まった件数)。ツールバーに置くと Blend/Floor が潰れる (1400px 幅) ので、整音の 2 つのボタンはトランスポートバー側
- 画面確認: `--snapshot-conditioning <out.png>`
- 検証: `--selftest-condition` (83 項目: L/R バランスの測定・補正・各見送りケース・書き出し統合を含む。66 項目: 設定の保存/丸め、試聴ゲイン=書き出しゲイン、非同期生成、プレイヤーの差し替え、再起動後の設定 ほか。旧 43 項目: EBU Tech 3341 基準の -23 LUFS、フィルタ減衰量、天井/最大ゲイン、ハム検出と除去 (各倍音 -30dB 以上・音は 0.05dB 以内)、書き出し統合)。**注意**: `--selftest-learn` は前回のテストが残した状態に依存し、同じ `WT_APPDATA` で 2 回続けると失敗する。毎回新しいフォルダで実行すること

## ★ / NG / 属性 (Phase 7)
- データ層は最初から全部ある (`Cycle` の `rating` 0〜5 / `ng` / `attributes` → project・WAV の `wtag`・学習ログ `LabelStore`。NG は `Suggester` が学習から外す)。画面は サイクル一覧の `属性` `★` `NG` 列
- 操作: **Ctrl+0..5** = ★ (0 で外す)、**N** = NG の切り替え (選択中が全部 NG なら解除、そうでなければ全部 NG)、**E** = 属性を編集。数字キー単独は今までどおりタイプ。キーは選択中の行、無ければカーソル位置のサイクルに効く (`targetRows`)。一覧では星をクリック (同じ星でもう一度クリックすると解除)、NG セルをクリック、属性セルをダブルクリック。複数行を選んでいればまとめて適用 (1 回の Undo)
- 属性 (`Core/Attributes`, `UI/AttributeEditor`): 空白区切りの `key=value` (空白を含む値は `"..."`、キーの無い語も可)。入力補助は 既定の key (strength / speed / material / tool / mic / distance) と代表値 + 学習ログとこの録音で使った語 (使用回数順)。何も強制しない。同じ key を追加すると置き換え、値を空にすると削除
- **NG のサイクルは書き出さない** (件数を完了ダイアログに出す。WAV のマーカー/プロジェクトには NG のまま残る)。書き出す WAV の INFO コメントに `rating=4; attrs=strength=strong ...` を追記 (ある時だけ)。波形上の NG は薄く `NG` 付きで表示
- 検証: `--selftest-rating` (36 項目)。画面確認: `--snapshot-attributes <out.png>` (属性エディタ)、`WT_DEMO_RATING=1 ... --snapshot <in.wav> <out.png>` (一覧にデモの ★/NG/属性)

## データセット書き出し (Phase 7, `Core/Dataset`)
- ツールバーの `Export ▾` メニュー: ライブラリへ書き出し (Ctrl+E) / **データセットに追加 (Ctrl+Shift+E)** / データセットの場所を変更。場所は PC ごと (`settings.json` の `datasetDir`、初回に選択。別のフォルダを選べば別のデータセット)
- 形: `<folder>/audio/<出力名>.wav` (24bit、整音設定を適用) + `metadata.jsonl` (1 行 1 クリップ) + `dataset.json` (形式の説明)。行 (schema 1): `id`, `audio`, `original{file,start,end}`, `name`, `category`, `type`, `caption_en`, `caption_ja`, `tags_en`, `tags_ja`, `attributes{key:値}`, `free_tags[]`, `rating`, `ng`, `duration`, `sampleRate`, `channels`, `bits`, `conditioning`, `exportedAt`, `app` (更新時は `updatedAt`)
- `id` = 録音のファイル名 (大小無視) + **音声の長さ (サンプル数)** + 範囲から作る 128bit (FNV-1a ×2)。ファイルサイズは入れない (WAV にマーカーを書くとサイズが変わり、同じクリップが二重に入ってしまうため)。サイズ入りの旧 ID の行は `datasetLegacyClipId` で見つけて ID を新しいものに置き換える (ただし旧い行のうち、すでにマーカーを書いてサイズが変わったファイルのものは照合できず、一度だけ重複する)。**同じ id が既にあれば音声は書き直さず、メタデータ (★・属性・タイプ・キャプション・NG) だけ更新**。名前が別クリップと衝突したら末尾に `_<id の先頭 8 文字>`。何も上書き・削除しない (`metadata.jsonl` は一時ファイル経由で置き換え、読めない行・他者の行はそのまま保持)
- **NG**: 新しいクリップは追加しない。すでに入っているものが後から NG になったら行に `"ng": true` を付けるだけ (音声は残す。学習側が `ng` で絞る)。NG を外せば戻る
- キャプションは翻訳辞書ができるまで規則で作る (`Open (TOOLS), strength strong, ...`)。日本語はカタログの日本語列 (Category / Type) を使い、属性の語は原文のまま
- **Raw フォルダ全体から** (`Export ▾ → Raw フォルダ全体をデータセットに追加...`, `addRawFolderToDataset`): Raw 以下の `*.wav` (サブフォルダも) の `wtag` チャンクを読み (音声は読まずヘッダだけ)、**タイプが付いていて人が確認したサイクル**を追加する。Inbox が自動でタイプを付けただけで未確認 (`confirmed=false`) のサイクルと未分類は入れない (件数だけ報告)。`wtag` の無い WAV は数えるだけ。**音声は、まだデータセットに無いクリップがある録音だけ読む** (`DatasetAudioProvider`): すべて入っている録音はメタデータ (★・属性・タイプ・訳・NG) の更新だけなので 2 回目以降は速い。確認ダイアログ → 進捗バー + 中止 (ファイルの区切りで止まる) → 完了ダイアログ。Raw のファイルは変更しない。日本語名は `JapaneseNames` (カタログのコピー) から、辞書もコピーを渡す。`wtag` に `category` / `prefix` も読むようにした (`WtagData`)
- 検証: `--selftest-dataset` (39 項目: 一括追加・2 回目は音声を読まない・★の更新・マーカー書き込み後も重複しない・旧 ID の行・中止 を含む)
- **注意 (ビルド)**: `juce::String` の `+` を 1 つの式に 30 個ほどつなぐと、VS 2019 のコンパイラが `C1060 ヒープの領域を使い果たしました` で落ちる。メッセージ文は `+=` で短い文に分けて作ること
- **修正**: Type の日本語シード (`Catalog.cpp` の `seedTypes`) が `const char*` のまま `String` になり Latin-1 として読まれて文字化けしていた (新しく作った `types.csv` / `catalog.xlsx` の Japanese 列)。`String::fromUTF8` に修正済み。**修正前に作られた設定フォルダの Types シートは文字化けしたまま**なので、Excel で開いて確認すること (カテゴリは元から正常)

## 翻訳辞書 (Phase 7, `Core/Dictionary`, `UI/DictionaryWindow`)
- 属性の語 (key / 値 / 単語) の日英を対応づけ、データセットの英語・日本語キャプションとタグをそろえる。`Export ▾ → 翻訳辞書...` で開く (一覧 / 絞り込み / 追加・更新 / ユーザーの訳の削除 / 「この録音の未訳語...」)
- 中身: 内蔵語彙 (約 120 語: 属性の key・代表値・素材・道具・マイクなど) + **設定フォルダ (共通) の `dictionary.csv`** (`English,Japanese,Note`、UTF-8 BOM、Excel で編集可、Shift-JIS の CSV も読める)。ユーザーの訳が内蔵より優先。同じ英語に複数の日本語があれば 英→日 は最初の行、日→英 はどの行からでも引ける。保存は一時ファイル経由 (Excel が開いている等で失敗しても元は壊れない)、ファイルが変われば自動で読み直す (`reloadIfChanged`)、設定フォルダの移動でコピーされる
- 照合は語全体の完全一致 (前後の空白・大小・全角英数は無視)。**語の一部からの推測はしない** (`cardboard box` を分解しない。足りなければ未訳として知らせる)。数字だけ・数字で始まる量 (`3m`, `10cm`, `-3.5dB`) は言語に関係ないので対象外 (`isNeutral`)。かな/漢字を含めば日本語 (`isJapanese`)
- データセット (`addToDataset(..., &dictionary)`): `caption_en/ja`・`tags_en/ja` の属性の語をそれぞれの言語にそろえる。`attributes` は打ったまま + `attributes_en` / `attributes_ja`。訳が無い語は両側とも原文のままで、行の `untranslated` と `DatasetResult::untranslated` に残る。**訳を足してからもう一度追加すると、音声はそのままでキャプションだけ更新される**。書き出しジョブには辞書のスナップショット (コピー) を渡す (ウィンドウでの編集と競合しない)
- `Core/Csv` (`parseCsv` / `toCsvLine` / `readCsvRows` / `csvBytes`): `Catalog.cpp` から切り出して共用 (動作は同じ)
- 検証: `--selftest-dictionary` (34 項目)。画面確認: `--snapshot-dictionary <out.png>`

## 書き出しのフォルダと番号
- 書き出し先は `<Category>/<Prefix>[/<Type>]` (`exportSubFolder`)。Prefix が Category と大文字小文字違いで同じなら Prefix の階層は作らない。`Export ▾` →「Type ごとのフォルダに分けて書き出す」で Type 階層を追加 (PC ごとの設定 `exportTypeFolders`)
- 書き出し先で番号が使用済みなら、空いている次の番号にする (`renumberForDestination`。上書きしない、同じ書き出しの中の順序は保つ)。「続きから」はライブラリ索引、こちらは書き出し先フォルダの実在を見る
- 検証: `--selftest-export` (9 項目)

## Category x Type マトリクス (catalog.xlsx)
- Categories シート: `Category, Japanese, Note, Prefix, TYPE01..TYPE10` (Category の右に Type を書く。10 個埋める必要も、重複も可)。日本語は Types シート (`Category, Type, Japanese, Note`) に書く
- マトリクスの Type は列順に採用 (空欄・重複は無視)。その後に Types シートの Category 別の行が続く。日本語は Types シートから引く (その Category の行 → 共通 `*` や他の Category の行の順)
- マトリクスにあって Types シートに無い Type は、起動/再読込時に Types シートへ 1 行追加される (日本語は空欄で、記入待ち)
- 検証: `--selftest-matrix` (10 項目)

## スマホ録音 (ブラウザ版 WaveTagger Rec, `docs/`)
- GitHub Pages (`/docs`) で公開する PWA。スマホのブラウザで 48kHz/24bit WAV を録音し、停止すると Google Drive の指定フォルダ (Inbox が監視しているもの) へ自動アップロード。設定手順は `docs/SETUP.md`
- `wav.js` (24bit WAV 書き出し) / `worklet.js` (AudioWorklet で録音) / `app.js` (録音・IndexedDB の送信待ち・Google サインイン (GIS トークン)・Drive 再開可能アップロード)。クライアント ID と保存先フォルダは端末の localStorage (リポジトリには秘密を置かない)
- **録音フォーマット切り替え**: ステレオ/モノラル、48/44.1 kHz、24/16 bit (端末の localStorage `wtrec.format`、録音中は固定)。ステレオはまず `channelCount: {exact: 2}` で要求し、`OverconstrainedError` なら `{ideal: 2}` で取り直す。**ステレオかモノラルかは、ブラウザの報告 (`getSettings().channelCount`: 無いことがある) ではなく、実際に届いた音のチャンネル数で決める** (ワークレットを `channelCountMode: 'max'` で作り、最初のブロックの `input.length` を見てから `explicit` に固定。以前は報告が無いと 1 とみなして、2 チャンネル届く端末をモノラル扱いしていた)。1 チャンネルしか届かなければモノラル。2 チャンネル届いても左右が (1 秒以上の有音で) サンプル単位で同じなら「左右がまったく同じ音」と表示 (偽のステレオ)。判定の根拠 (マイク名・ブラウザの報告・最大・届いた音・要求) を `#diag` に表示。モノラルは 1 チャンネルのノードに繋いでブラウザに混ぜさせる。16 bit は TPDF ディザ (`wav.js` の `encodeWav`、同じ録音なら同じファイル)。保存した録音に `format` を持たせ、一覧に表示
- **入力レベルの確認 (録音前)**: 「入力レベルを確認」で、マイクを開いてメーターだけ動かす (録音はしない)。録音ボタンを押すとそのマイクをそのまま使う (開き直さない)。メーターをタップでピーク・CLIP・Integrated をリセット。**LUFS** は `docs/loudness.js` (BS.1770、Momentary 0.4 秒 / Short-term 3 秒 / Integrated ゲート付き。アプリ本体の C++ と同じ K 特性) を録音中・確認中ずっと更新。メーターは**ステレオなら L/R の 2 本** (各自にピークホールド)、モノラルなら 1 本 (M)。本数は実際に得られたチャンネル数で決まる (ステレオを選んでもマイクが 1 チャンネルなら 1 本)。左右別のピーク (dBFS) の数字も表示。LUFS は左右を合わせた値
- 検証: `node Tools/test_wav.js` (WAV の独立確認。16 bit・ディザを含む)、`node Tools/test_loudness.js` (EBU の -23 LUFS 基準、モノラル、ゲート、窓、チャンクサイズ非依存)。ブラウザでの録音〜アップロードの流れは、偽の音源と偽の Drive で確認済み。フォーマット切り替え・レベル確認・LUFS・保存された WAV のヘッダは、偽のマイク (1 kHz -20 dBFS のサイン波) でブラウザ上で確認済み。実機のマイク・実 Drive・画面オフ時の挙動は未確認
- **画面の向きボタン** (右上、自動→横→縦→自動、`wtrec.orientation`): `screen.orientation.lock()`。拒否されたら全画面にしてからもう一度ロック (Android Chrome は全画面が要ることがある)、全画面を抜けると自動に戻る、iPhone は不可 (案内を表示)。保存した向きは最初のタップで掛け直す。**横画面レイアウト** (`@media (orientation: landscape) and (max-height: 560px)`): L のメーターを左端、R を右端に縦長で固定表示 (目盛りは内側)、中央にラウドネス・時間・ボタン。メーターの棒は CSS カスタムプロパティ (`--lvl` / `--pk` / `--full` を `.meter` に設定) で、縦横どちらのレイアウトも同じスクリプトで動く
- ブラウザでの確認 (偽のマイクを差し替えて実画面を操作): 報告なしで 2 チャンネル届く場合がステレオ扱いになること、左右同じ音の検出、「ちょうど 2」を断られた場合の取り直し、ステレオ/モノラルの WAV のヘッダとピーク、780×360 の横画面レイアウト、390×800 の縦画面、向きボタンの 4 つの経路 (通常のロック / 全画面が要る場合 / 戻る操作 / 固定不可)。**実機の Galaxy などでの確認は未実施**
- **画面はタブ (録音 / 履歴 / 設定)**: 録音タブは `body` を `100dvh` の縦 flex にして、スコープ (`.scopebox`) が余りの高さを取り、**スクロールしない**。他のタブは中でスクロール。横画面は 2 列 (`.recgrid`: 左 = ピーク・LUFS・スコープ・相関、右 = 時間・情報・ボタン)、L/R メーターは画面の両端。フォーマット・入力の診断・Drive 設定は設定タブ。設定が空なら最初に設定タブ。設定入りリンク `#cid=<ID>&f=<フォルダ>` は 10/24 から実装済み (SETUP.md に説明)
- **ステレオスコープ** (`docs/scope.js`、ステレオの時だけ表示): **Polar Sample** (半円。真上 = L=R、±45° = 片チャンネルのみ、±90° = 逆位相。半径は dB: 外周 0 dBFS、中心 -54 dB。負の極性は上半分に折り返す) と **Lissajous** (縦 = (L+R)/2、横 = (R-L)/2、直近ピークで自動ゲイン、最大 +35 dB 程度)。スコープをタップで切替 (`wtrec.scope` に保存)。**相関** = 約 0.5 秒の指数窓の L/R 相関 (+1 = 同一、無音は `--`、片側だけなら 0)。描画は新しいブロックが来た時だけ (2048 フレームごと、1 ブロック約 320 点、残像は半透明の塗りで)、`requestAnimationFrame` はマイクが開いている間だけ。検証: `node Tools/test_scope.js` (数式と相関)。画面はブラウザで偽のステレオ入力 (L = ノイズ、R = 0.7 L + ノイズ → 相関 +0.8) を流して確認 (縦 390×760・横 780×360 でスクロールなし、モノラルでスコープと相関が消える)。**実機は未確認**
- テスト時の注意: ブラウザの別タブが同じ `wtrec` の IndexedDB を開いたままだと、`deleteDatabase` が保留になり、その後の `indexedDB.open` が固まって「保存中...」のままになる (アプリの不具合ではない)。テストの後始末では古いタブを先に閉じる
- **録音停止でサインイン画面に飛ばない**: 以前は停止のたびにトークンが無いと GIS のポップアップを開いていた。今は、トークンが有効なら送信、無効なら「送信待ち」で保存して終わり。送信待ちがあってトークンが無い時だけ `#connectBar` (「Google に接続して送信」) を出し、押した時に 1 回だけ `requestAccessToken({prompt: ''})`。30 秒ごとに `showAuth` で期限切れを反映。完全なバックグラウンド化は純ブラウザでは不可 (リフレッシュトークンが無い。`prompt: 'none'` は未検証)。やるなら Apps Script の受け口 (秘密キー付き) かネイティブアプリ
- **無音の録音は保存しない**: `onBlock` で NaN/Infinity を 0 にして数え (`WavEnc.sanitize`、`#diag` に個数)、録音中の最大ピークが 1e-6 以下なら保存せず「録音が無音でした」と表示。2026-10-25 17:18 に Drive へ届いた全サンプル 0 の WAV (2ch/16bit) の原因は未特定 (入力が NaN だった仮説、再現できず)
- **向きの初期値は横** (`orient = 'landscape'`、最初のタップで `screen.orientation.lock`、ユーザーが選ぶまで保存しない・失敗しても黙る)。**設定ボタン**: 横画面では設定欄が下に出て反応が無いように見えたため、開く時に `scrollIntoView`。モノラルの横画面で R メーターが残るのを `.mrow.hidden{display:none}` で修正
- ブラウザでの確認 (偽のマイク/偽の Drive): 接続帯→1 回だけトークン要求→送信済み、トークン有効時は要求なしで送信、無音は保存されない、モノラル横画面の R 非表示。**実機のスマホでは未確認**。端末の Firefox は `channelCount` 最大 1 を返す (ブラウザ側の制限。Chrome で要確認)
- PWA のキャッシュは `sw.js` の `wtrec-v5` (ファイルを増やしたら `FILES` に足して、版を上げる)。サービスワーカーは `cache: 'no-cache'` でブラウザの HTTP キャッシュも飛ばす。`index.html` のスクリプトは `?v=5` 付き (直したら版を上げる。古い `app.js` が新しい `index.html` と組み合わさってスコープが動かない事故があった)

## 新規 Category と Type の追加 (`Core/CategoryTypes`, `UI/NewCategoryDialog`)
- 「Category を追加」(カテゴリ選択の下) は、名前と日本語に加えて **Type の案 (最大 10 個、English / 日本語、編集可)** を決めてから書き込む。案の出し方は 3 つ: (1) **似たカテゴリからコピー** (`rankSimilarCategories`: 名前の語・語頭の一致と日本語の包含/共通 2 文字で並べ、最良を自動で選ぶ。一致が無ければ共通の Type。一覧から手で選べる)、(2) **AI に聞く** (質問文をクリップボードへコピー → Gemini/Claude などに貼る → 返事をコピーして「AI の返事を読み込む」)、(3) 手入力 (↑ で並べ替え)。**API キーも通信も使わない**
- 質問文 (`buildAiPrompt`): カテゴリ名・日本語、条件 (1 語の PascalCase、よく出る順、最初の 9 個がキー 1〜9、既存の綴りを使う)、似たカテゴリの Type の例、既存の Type (よく使われる順 60 個)、JSON の出力形式。返事の読み取り (`parseAiReply`): JSON ({"types":[{"en","ja"}]} / 配列 / `"Hit=叩く"` などの文字列)、コードブロックや前後の文があっても可、`1. Hit - 叩く` のような行も可。**英語の文 (「no idea」「Sure, here are…」) は Type として読まない** (単語 1 つ、または単語+日本語の行だけ)
- 整形 (`tidyProposals`): English は英数字の PascalCase 1 語・24 文字以内 (日本語や記号は除く)、**カタログにすでにある Type は同じ綴り**にそろえ、日本語が空ならカタログから補う、重複・空欄は除く、最大 10 個
- 書き込み (`Catalog::addCategoryWithTypes`): Categories シートに 1 行 (`TYPE01`〜 に English) → Types シートに日本語つきで 1 Type 1 行。Category 行が先なので途中で失敗しても Type は全部載る (足りない Types の行は次回の読み込みで補われる)。既存の Category は変更しない。古い形式 (TYPE 列の無い) のブックでも Types の行で拾える
- **修正**: `Catalog::getTypes ("*", false)` は空を返す (共通 Type は**空のカテゴリ**で引く)。`JapaneseNames::from` がこれを使っていたため、Raw 一括追加で共通 Type (Hit など) の日本語が引けていなかった → 修正済み
- 検証: `--selftest-newcategory` (40 項目)。画面確認: `--snapshot-newcategory <out.png>`

## Android ネイティブ (`android/`)
- 目的: スマホの内蔵マイクをブラウザ経由ではなく `AudioRecord` で開き、**本物のステレオ**で録る。ブラウザ (Chrome/Firefox) は通話・音声用の経路でマイクを渡すため、左右が同じ音 (モノラルの複製) になる (Chrome でも「左右がまったく同じ音です」を確認)
- **設計案は `android/DESIGN.md`** (未実装。WebView で `docs/` の画面をそのまま使い、マイクの取り込み・WAV・Drive 送信・認可だけ Kotlin。2026-10-26 にレビューを反映: 中身は 16bit 精度、画面オフ中の計算はネイティブ、回転で作り直さない、エフェクト無効化、橋のセキュリティ、モノラルは片方のマイク、48kHz 固定、1 時間/2GB で分割、測定の段階 M0 を追加)。決めてもらうこと: 送信後のコピーを残す日数、OAuth の公開状態 (テストだと同意が約 7 日で切れる)、横画面固定
- **S21 (SC-51B, Android 15) の実測 (2026-10-25)**: マイクは 2 個 (`builtin_mic_1` 下端寄り / `builtin_mic_2` 上端寄り、約 14.25cm 間隔)。入力元 `MIC` / `CAMCORDER` は左右が別の音、`UNPROCESSED` / `VOICE_RECOGNITION` / `VOICE_COMMUNICATION` は下端のマイク 1 個で左右が完全に同一。WAV 3 本 (`D:\マイドライブ\pcm録音\2026-10-25_18-3*_{MIC,CAMCORDER}.wav`) の分析で、左右の時間差が ±19〜23 サンプル (0.40〜0.48ms) に張り付く = マイク間隔 (0.416ms) と一致、左端/右端を叩くと符号が入れ替わる → **2 個のマイクによる AB ステレオ**。相関は 0.1〜0.3 (AB 方式なので低い)。`CAMCORDER` は叩いた音で 0 dBFS まで振れる。`MIC` / `CAMCORDER` では `routedDevice` / `activeMicrophones` が取れない。**float32 で受け取っても中身は 16bit 精度** (99% のサンプルで 24bit の下位 8 ビットが 0)。未確定: どちらのチャンネルが左か、端末側の処理 (自動音量調整など) の有無
- 分析: `python Tools/analyse_stereo.py <wav>...` (24bit ステレオ。ピーク・相関・0.5 秒ごとの左右の時間差とレベル差・帯域ごとのコヒーレンス)
- **ブラウザ版への宿題**: `wav.js` の 16bit 書き出しは TPDF ディザを掛けるが、ブラウザ経由の音も中身が 16bit 精度なら、ディザはノイズを足すだけ (要確認)
- **`android/StereoTest`** (検証用、Kotlin、依存なし、minSdk 29 / target 34、v0.2): (1) 入力元 MIC / CAMCORDER / UNPROCESSED / VOICE_RECOGNITION / VOICE_COMMUNICATION を 6 秒ずつステレオ 48kHz (float32、だめなら int16) で録り、経路・チャンネル数・使われているマイク・ピーク・相関・最大差・0.25 秒ごとの L-R レベル差と判定を表示。(2) 「ステレオ録音」画面 (`RecordActivity`): MIC / CAMCORDER の左右のメーター (ピークホールド)、左右の入れ替え、24bit ステレオ WAV を `Download/WaveTagger/` に保存 (MediaStore、ヘッダーは最後に書く)
- ビルド: `cd android\StereoTest; $env:JAVA_HOME='C:\Program Files\Java\jdk-20'; .\gradlew.bat assembleDebug` (Gradle 8.6 / AGP 8.3.2 / Kotlin 1.9.22 は `%USERPROFILE%\.gradle` にある。Android Studio 同梱の JDK 25 は Gradle 8.6 が使えない)。`local.properties` (sdk.dir) は Git に入れない。出力 `app\build\outputs\apk\debug\app-debug.apk`、コピーを `android\WTStereoTest.apk` (Git に入れない)。スマホへは `D:\マイドライブ\pcm録音\WTStereoTest.apk` に置いて Drive アプリから入れた (Inbox が拾わないよう、試験後は消す)
- デバッグ署名鍵 (`%USERPROFILE%\.android\debug.keystore`) の SHA-1: `F4:9D:90:D2:DB:51:82:0E:7B:57:B7:97:98:B9:88:99:7C:A3:D9:61` (Android 用 OAuth クライアントを作る時に使う)。エミュレーター: Android 34 google_apis x86_64 のイメージあり (AVD は未作成)

## Android ネイティブ録音アプリ `android/WaveTaggerRec` (M1・M2)
- 画面は `docs/` を Gradle (`syncWeb`) で assets にコピーして WebView (`WebViewAssetLoader`) で表示。`docs/native.js` が `WTNativeBridge` の有無でネイティブ版を判定 (ブラウザでは何もしない)。ネイティブ時は `app.js` がマイクの開閉・録音・履歴をアプリに任せる (ブラウザの枝は残してある)
- `Capture` (AudioRecord MIC/CAMCORDER/TEST、float32 ステレオ 48kHz、エフェクト無効化) → `Recorder` (画面に依存せず WAV 書き込み) → 画面へはブロックを ArrayBuffer で送る (画面が裏の間は送らない)。録音中は `CaptureService` (前面サービス + 部分ウェイクロック)
- **左右の割り当て (S21 実測 2026-10-26)**: スマホを横にして、チャンネル 0 = 下端のマイク、1 = 上端。`ROTATION_90` (上端が左) で入れ替え、`ROTATION_270` では入れ替えない (270° は物理からの推定で未測定)。録音中は割り当てを変えない。設定 `swap` で手動反転可
- WAV (`WavWriter`): 録音中は `<日時>.wav.part`、停止で `.wav` に改名。ヘッダー (INFO コメント付き、固定長) を 1 秒ごとに更新して `force`。24bit は ×2^23、16bit は ×2^15 で、16bit 格子に乗った信号 (端末のマイク) はそのまま (無損失)、そうでない信号の 16bit だけ TPDF ディザ。1 時間または 2GB で `_2`, `_3` に分割。最大ピーク 1e-6 以下は保存しない。NaN は 0 にして数える
- 回復: 起動時に `.wav.part` を実サイズから直して `_recovered.wav` として履歴へ。`Recordings` = `<name>.wav` + `<name>.json` (秒数・形式・状態 wait/done)。送信済みは `keepDays` (既定 7) 日後に削除、未送信は決して削除しない
- **左右の自動調整** (設定タブ「左右の自動調整」, `calibrate()` in `docs/app.js`): 左端→右端を叩いて L/R の差から向きを判定、逆なら `WTNative.flipSwap()` → 回転ごとの補正 `flip90`/`flip270` (SharedPreferences)。実効 swap = (ROTATION_90) XOR flipN。S21 で片方の向きが逆と判明し補正済み (どちらの向きかは未記録)
- **Downloads コピー** (`Downloads.kt`): 録音を `Download/WaveTagger/` に MediaStore で複製 (`copyDownloads` 既定 true)
- **M3 Drive 送信**: `GoogleAuth` (Play services `AuthorizationClient`、スコープ `drive`、同意後は背景でもサイレントにトークン更新) / `DriveUploader` (再開可能アップロード 8MiB チャンク、401→Auth、5xx→Retry、404→Fatal) / `UploadWorker` (WorkManager、ネットワーク必須、同意が要る時は通知)。送信済みは Drive 確認後だけ `markSent`。設定タブにフォルダ (URL/ID) と「Google に接続」。**必要な準備: Cloud Console で Android 用 OAuth クライアント (パッケージ `jp.wavetagger.rec` + デバッグ署名 SHA-1)**。テスト 17 項目 (`DriveUploaderTest` は MockWebServer)。実 Google/Drive での確認は未実施
- 未実装: ネイティブ側の Integrated LUFS (画面が前の間は JS が計算)、入力元/左右反転の設定画面、通知の停止ボタン、ページ再生成時の状態復元
- 検証: `cd android\WaveTaggerRec; $env:JAVA_HOME='C:\Users\kiyot\.gradle\jdks\eclipse_adoptium-17-amd64-windows.2'; .\gradlew.bat --no-daemon testDebugUnitTest assembleDebug` (12 項目: ヘッダー・サンプル完全一致・16bit 無損失/ディザ・回復・分割・無音・保持期間)。エミュレーター (Pixel_7a) で録音→停止→強制終了→回復まで確認。実機の画面オフ録音は未確認
