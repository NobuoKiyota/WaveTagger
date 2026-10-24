# WaveTagger 音響カタログ・マトリクス・Typesシート大幅拡充 実施記録

- **実施日**: 2026-10-24
- **対象ファイル**:
  - `D:\マイドライブ\WaveTagger\config\catalog.xlsx` (バックアップ: `catalog.xlsx.bak-20261024-105742`)
  - `D:\マイドライブ\WaveTagger\config\categories.csv`
  - `D:\マイドライブ\WaveTagger\config\types.csv`

---

## 1. 実施概要

Google スプレッドシート (`catalog.xlsx`) の `Categories` シートおよび `Types` シートについて、以下の仕様と要件に基づき大規模な追加・充実を実施しました。

1. **Category x Type マトリクスの全カテゴリ展開**:
   - `Categories` シートの `TYPE01` 〜 `TYPE10` 列に、各音響カテゴリごとに現場で最も頻出するアクション・状態を割り当てました（パレットで即座に使用可能な先頭9枠＋予備1枠）。
2. **音響カテゴリの大規模追加**:
   - 既存の73カテゴリに加え、UCS (Universal Category System v8.2) やゲームオーディオ（Unity / Unreal Engine / Wwise / CRIWARE）および映像音響で不可欠な36カテゴリを新規追加。
   - **合計109カテゴリ** に拡大。
3. **Types シートの Japanese（日本語）列の完全入力**:
   - マトリクスに登録された全 Type に対し、各カテゴリ文脈に応じた適切な日本語名称を `Types` シートの `Japanese` 列に完全に記入（1,109行）。
   - 既存のTypes（TOOLS, PAPER, METAL, WOOD, DOORS 等106行）は100%保持・継承。

---

## 2. カテゴリ・Typeの拡充詳細

### 主要カテゴリ分類とマトリクス設計例

| 大分類 | カテゴリ例 | 主なマトリクスType (TYPE01〜TYPE09) |
| :--- | :--- | :--- |
| **乗り物・交通** | `AIRCRAFT`, `BOATS`, `BICYCLES`, `SHIPS`, `TRAINS`, `VEHICLES`, `ENGINES`, `ACCIDENT` | Prop, Jet, Flyby, Takeoff, Landing, Engine, Horn, Brake, Skid |
| **環境・天候** | `AMBIENCE`, `WEATHER`, `RAIN`, `WIND`, `WATER`, `UNDERWATER`, `ICE`, `SNOW`, `FIRE`, `GEOPHYSICAL`, `DISASTER` | RoomTone, City, Nature, Thunder, Heavy, Drip, Gust, Splash, Crack |
| **生体・動物** | `ANIMALS`, `BIRDS`, `INSECTS`, `REPTILES`, `CREATURES`, `MONSTERS`, `ZOMBIE`, `HUMAN`, `VOICES`, `CROWDS` | Bark, Growl, Flap, Buzz, Hiss, Roar, Stomp, Breath, Shout, Applause |
| **アクション・戦闘** | `FEET`, `FOOTSTEPS`, `FIGHT`, `GORE`, `IMPACTS`, `GUNS`, `BULLETS`, `WEAPONS`, `PROJECTILES`, `EXPLOSIONS`, `SWOOSHES`, `WHOOSHES` | Step, Concrete, Punch, Flesh, Heavy, Shot, Cock, Whiz, Swing, Blast |
| **マテリアル・物性** | `CERAMICS`, `CHAINS`, `CLOTH`, `DIRT`, `GLASS`, `LEATHER`, `METAL`, `PAPER`, `PLASTIC`, `ROCKS`, `ROPE`, `RUBBER`, `SAND`, `WOOD` | Hit, Break, Rattle, Rustle, Dig, Creak, Tear, Click, Squeak, Knock |
| **機械・道具・オフィス** | `DOORS`, `DRAWERS`, `LOCKS`, `SWITCHES`, `GEARS`, `MECHANICAL`, `GIZMOS`, `CLOCKS`, `BELLS`, `HORNS`, `ALARMS`, `SIRENS`, `BEEPS`, `TOOLS`, `POWER_TOOLS`, `MACHINES`, `MOTORS`, `INDUSTRIAL`, `VENTILATION`, `OFFICE`, `COMPUTERS`, `COMMUNICATIONS`, `RADIO`, `ELECTRONICS`, `ELECTRICITY` | Open, Close, Slide, TurnKey, Toggle, Ratchet, Tick, Ring, Siren, Drill |
| **SF・ファンタジー** | `MAGIC`, `SCIFI`, `LASERS`, `PORTALS`, `ROBOTS` | Cast, Beam, Element, Shield, Warp, Scanner, Blast, Open, Servo, Transform |
| **UI・メディア・抽象** | `UI`, `GAMES`, `CARTOON`, `TOYS`, `SPORTS`, `FOOD`, `LIQUID`, `CHEMICALS`, `DESIGNED`, `DESTRUCTION`, `EVOLVING`, `NOISE`, `SUB_BASS`, `SYNTH`, `TEXTURES`, `MUSIC`, `MUSICAL`, `INSTRUMENTS`, `WHISTLES`, `ARCHIVE` | Click, Confirm, Coin, Boing, Crunch, Pour, Hit, Riser, Drop, Arp |

---

## 3. アプリケーション読み込み検証

環境変数 `WT_APPDATA` を指定した独立テスト環境で検証を実施：

1. **`--selftest-catalog-load`**:
   - `categories=109`
   - 全カテゴリおよびType（日本語マッピング含む）が正常認識されることを確認。
2. **`--selftest-matrix`**:
   - マトリクス列順の採用、空欄・重複無視、Types シートからの日本語引き出し、すべて合格（Exit code: 0）。
3. **`--selftest-catalog`**:
   - 全項目合格（Exit code: 0）。

---

## 4. 管理スクリプト

今後のメンテナンスや追加用スクリプトを `Tools/` に配備しました：
- `Tools/build_catalog.py`: 全カテゴリ・Type・日本語辞書の定義
- `Tools/update_catalog.py`: `catalog.xlsx` へのマトリクスおよびTypesシート書き込み
- `Tools/export_csvs.py`: `catalog.xlsx` から `categories.csv` / `types.csv` へのUTF-8 BOM同期出力
