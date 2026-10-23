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
- Category / Type は `categories.csv` / `types.csv` (UTF-8 BOM, Excel 可)。初回に内蔵シードを書き出し、以後は CSV が正本
