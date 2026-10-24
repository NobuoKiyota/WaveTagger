# カテゴリ・Type追加記録: カーテン / 小物 / ベルツリー

- **実施日**: 2026-10-24
- **対象ファイル**:
  - `D:\マイドライブ\WaveTagger\config\catalog.xlsx` (バックアップ: `catalog.xlsx.bak-20261024-122148`)
  - `D:\マイドライブ\WaveTagger\config\categories.csv`
  - `D:\マイドライブ\WaveTagger\config\types.csv`

---

## 1. 追加内容

ご指定のカテゴリおよびTypeを、パレット（先頭9枠）で最優先に選択できるようマトリクスの先頭（TYPE01〜）から割り当て、`Types` シートの `Japanese` 列にも日本語名を完全登録しました。

### ① カーテン (`CURTAINS`, Prefix: `CRTN`)
- **日本語名**: カーテン
- **マトリクスType (パレット優先順)**:
  1. `Open` (開く) ★ご指定
  2. `Close` (閉じる) ★ご指定
  3. `Rail` (レール音)
  4. `Rustle` (布擦れ)
  5. `Swish` (シャーッと引く)
  6. `Shake` (はたく・揺らす)
  7. `Hook` (フック・留め具)
  8. `Tie` (タッセルで留める)
  9. `Flap` (はためく)
  10. `Drop` (垂らす)

### ② 小物 (`PROPS`, Prefix: `PROP`)
- **日本語名**: 小物
- **マトリクスType (パレット優先順)**:
  1. `Rattle` (カタカタ) ★ご指定
  2. `PickUp` (持ち上げる)
  3. `Put` (置く)
  4. `Drop` (落とす)
  5. `Slide` (滑らせる)
  6. `Shake` (振る)
  7. `Tap` (小突く)
  8. `Scrape` (擦る)
  9. `Sort` (探る・選別)
  10. `Clatter` (カチャカチャ)

### ③ ベルツリー (`BELL_TREE`, Prefix: `BLTR`)
- **日本語名**: ベルツリー
- **マトリクスType (パレット優先順)**:
  1. `Long` (長い) ★ご指定
  2. `Short` (短い) ★ご指定
  3. `Up` (上昇) ★ご指定
  4. `Down` (下降) ★ご指定
  5. `Fast` (高速)
  6. `Slow` (低速)
  7. `Hit` (単音・打撃)
  8. `Shimmer` (揺らぎ・トレモロ)
  9. `Swell` (クレッシェンド)
  10. `Damp` (消音・ミュート)

---

## 2. システム検証

- **カテゴリ総数**: 109 → **112カテゴリ**
- **Types シート行数**: 1,109 → **1,139行**
- **WaveTagger 検証**: `--selftest-catalog-load`, `--selftest-matrix`, `--selftest-catalog` の全10項目すべて PASS。
