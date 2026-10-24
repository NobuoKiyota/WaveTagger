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
- 検証: `node Tools/test_wav.js` (WAV の独立確認)。ブラウザでの録音〜アップロードの流れは、偽の音源と偽の Drive で確認済み。実機のマイク・実 Drive・画面オフ時の挙動は未確認
