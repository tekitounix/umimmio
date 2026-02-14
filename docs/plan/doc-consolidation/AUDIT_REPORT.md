# ドキュメント統廃合計画 — 監査レポート

**監査日:** 2026-02-14
**監査方法:** 5チーム並列監査（各チームがソースファイルと照合）
**対象:** CAT_A〜CAT_G, DOCUMENT_INVENTORY, CONSOLIDATION_PLAN

---

## 総合サマリー

| カテゴリ | 正確性スコア | ファイル存在 | 行数精度 | 統廃合判定 | 重大問題 |
|---------|------------|-----------|---------|----------|---------|
| CAT_A | 75% | 100% (14/14) | 60% | 妥当 | 2件 |
| CAT_B | 65% | 100% (53/53) | 30% | 一部誤り | 3件 |
| CAT_C | 92% | 100% (14/14) | 95% | 妥当 | 0件 |
| CAT_D | 96% | 100% (19/19) | 95% | 妥当 | 0件 |
| CAT_E | 100% | 100% (58/58) | 100% | 妥当 | 0件 |
| CAT_F | 98.5% | 100% | 95% | 妥当 | 0件 |
| CAT_G | 82% | 100% | 65% | 妥当 | 0件 |
| INVENTORY | 72% | 90% | — | — | 3件 |
| PLAN | 72% | 95% | — | Phase整合OK | 1件 |

**全体平均: 83.6%**

---

## 1. 重大問題一覧（要修正: 9件）

### URGENT（即座に修正が必要: 4件）

| # | 対象 | 問題 | 影響 |
|---|------|------|------|
| U1 | CAT_A | CLAUDE.md の参照パス `docs/refs/specs/ARCHITECTURE.md` が壊れている。実際は `docs/refs/ARCHITECTURE.md` | ドキュメント参照不能 |
| U2 | CAT_B | archive/umi-kernel/ 内の ARCHITECTURE.md, BOOT_SEQUENCE.md, IMPLEMENTATION_PLAN.md は「完全同一」ではなく**異なるバージョン**。「削除」判定は誤り | 履歴消失のリスク |
| U3 | CAT_B | plan.md (L120-125) が旧文書 docs/umi-kernel/*.md を直接参照。archive移動後にリンク切れ | 統廃合実行時の障害 |
| U4 | INVENTORY | CAT_A〜CAT_G (docs/plan/) が DOCUMENT_INVENTORY に未記載 | インベントリの完全性欠如 |

### HIGH（優先度高: 5件）

| # | 対象 | 問題 | 影響 |
|---|------|------|------|
| H1 | CAT_A | CONCEPTS.md, NOMENCLATURE.md が `lib/umiusb/` を参照。実在しない（正しくは `lib/umi/usb/`） | コードパス参照の陳腐化 |
| H2 | CAT_B | README.md (35行) と index.md (105行) は「ほぼ同一」と判定されているが、実際は**相補的**。統合判定は撤回すべき | 不要な統合作業 |
| H3 | CAT_B/INVENTORY | archive/umi-kernel/ の完全同一ファイル数が不一致（PLAN: 5件 vs INVENTORY: 4件） | 整合性不良 |
| H4 | CAT_G | umimmio/docs/ の分散ファイル行数推定が実測値と大幅乖離（EXAMPLES: 記載~200行 → 実測22行、USAGE: 記載~200行 → 実測68行） | 統合作業量の誤認 |
| H5 | PLAN | 「総ドキュメント数 ~210ファイル」と記載。実測は309ファイル | 計画の信頼性 |

---

## 2. カテゴリ別詳細

### CAT_A: コア仕様 — 75%

**良好な点:**
- 14ファイル全存在確認済み
- 保持判断は適切（全て正本として機能）

**問題点:**
- ARCHITECTURE.md: 記載~200行 → 実測527行（+327行のズレ）
- UMIP.md: 記載~200行 → 実測477行（+277行のズレ）
- `lib/umiusb/` パス参照が複数ドキュメントで陳腐化

### CAT_B: OS設計 — 65%

**良好な点:**
- umios-architecture/ 41ファイル全存在
- Phase整合性は正確

**問題点:**
- spec/ 内の行数: system-services.md 記載~120行 → 実測381行（3.2倍）
- archive/umi-kernel/ 3ファイルが「完全同一→削除」と誤判定（実際は異なるバージョン）
- README.md/index.md 統合判定が不適切

### CAT_C: プロトコル — 92%

**良好な点:**
- 全14ファイル存在確認
- バージョン記載（0.1.0〜0.8.0）が正確
- 統廃合判定（DATA→DATA_SPEC統合、archive削除）は妥当

**軽微な懸念:**
- USB Audio の lib側正本（lib/umi/usb/docs/）との内容比較が未完

### CAT_D: 開発ガイド — 96%

**良好な点:**
- 全19ファイル存在確認
- CODING_RULE.md (429行)、DEBUGGING_GUIDE.md (847行) は完全一致
- 統廃合判定（GUIDELINE.md→DESIGN_PATTERNS.md改名等）は全て妥当

**軽微な懸念:**
- RELEASE.md と RELEASE_GUIDE.md の重複度が未検証

### CAT_E: HAL/ドライバ設計 — 100%

**完璧:**
- 58ファイル全存在確認
- 内容も100%一致
- 統廃合判定は完全に正確

### CAT_F: DSP/技術資料 — 98.5%

**ほぼ完璧:**
- ファイル存在100%、内容一致95%
- TB303_WAVESHAPER_DOC.md の行数推定のみ軽微なズレ（記載~500行 → 実測267行）

### CAT_G: ライブラリドキュメント — 82%

**良好な点:**
- 全ファイル存在確認
- bench_old 削除提案は完全に妥当（★★★★★評価）
- umimmio 統合提案の方向性は適切

**問題点:**
- umimmio/docs/ の行数推定が全般的に過大
  - EXAMPLES.md: 記載~200 → 実測22 (11%)
  - USAGE.md: 記載~200 → 実測68 (34%)
  - TESTING.md: 記載~150 → 実測60 (40%)
- bench_old/KNOWN_ISSUES.md: 記載~200+ → 実測542行（過小推定）

### DOCUMENT_INVENTORY / CONSOLIDATION_PLAN — 72%

**良好な点:**
- Phase 1-6 の記述は実ファイル配置と整合
- カテゴリ分類は基本的に正確
- 削除対象リストは実用的

**問題点:**
- 総ドキュメント数の乖離（210 vs 309）
- CAT_*.md 7本が INVENTORY に未記載
- archive/umi-kernel/ 同一ファイル数の不一致

---

## 3. 行数精度の全体分析

行数推定は全カテゴリで共通の課題として検出された。

| 精度帯 | カテゴリ |
|--------|---------|
| ±10%以内（高精度） | CAT_E, CAT_F, CAT_D |
| ±30%以内（許容範囲） | CAT_C |
| ±50%超（低精度） | CAT_A, CAT_B, CAT_G |

**推奨:** 統廃合実施前に全対象ファイルの `wc -l` による実測値更新を実施すること。

---

## 4. 統廃合判定の信頼性

全カテゴリの統廃合アクションを検証した結果:

| 判定 | 件数 | 評価 |
|------|------|------|
| 正しい判定 | 42件 | 保持/削除/統合の方針が妥当 |
| 修正が必要 | 3件 | archive同一判定誤り(U2)、README/index統合誤り(H2)、行数修正(H4) |
| 条件付き妥当 | 2件 | USB Audio正本確認(CAT_C)、umiport DESIGN.md作成(CAT_G) |

**統廃合判定の信頼性: 89%** — 修正3件を反映すれば実行可能なレベル。

---

## 5. 修正アクション一覧（優先度順）

### Phase 0: 監査結果に基づく即時修正

```
□ U1: CLAUDE.md パス修正 docs/refs/specs/ARCHITECTURE.md → docs/refs/ARCHITECTURE.md
□ U2: CAT_B の archive 同一判定を修正（3ファイルは「異なるバージョン、保持」に変更）
□ U3: plan.md の旧文書参照パスを spec/ に更新
□ U4: DOCUMENT_INVENTORY に docs/plan/ セクションを追加（CAT_A〜G + AUDIT_REPORT）
□ H1: CONCEPTS.md, NOMENCLATURE.md のコードパス lib/umiusb/ → lib/umi/usb/ に更新
□ H2: CAT_B の README.md/index.md 統合判定を撤回
□ H3: archive/umi-kernel/ 同一ファイル数を統一（5件: OVERVIEW, DESIGN_DECISIONS, MEMORY, FILESYSTEM, SHELL）
□ H4: CAT_G の umimmio/docs/ 行数推定を実測値に更新
□ H5: CONSOLIDATION_PLAN の総ドキュメント数を修正（除外条件を明確化）
```

### Phase 0 完了後 → 統廃合 Phase 1〜6 を実行可能

---

## 6. 結論

9つの docs/plan/ ドキュメント全体として **平均正確性 83.6%** で、統廃合計画としては**実行可能なレベル**にある。

**修正が必要な重大問題は9件**（URGENT 4件 + HIGH 5件）。特に:
1. CLAUDE.md のパス破損（U1）は開発フロー全体に影響
2. archive ファイルの誤削除判定（U2）はデータ損失リスク
3. DOCUMENT_INVENTORY の完全性欠如（U4）は計画の信頼性に影響

これら Phase 0 修正を実施後、CONSOLIDATION_PLAN の Phase 1〜6 は安全に実行可能と判断する。
