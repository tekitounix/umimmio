# UMI 統合設計提案

**作成日:** 2026-02-14
**前提:** [INVESTIGATION.md](INVESTIGATION.md), [ANALYSIS.md](ANALYSIS.md)
**目的:** ドキュメント統廃合と設計統合の具体的な実行計画を提案する

---

## 1. 提案の概要

ANALYSIS.md で導出された3原則に基づき、ドキュメントと設計の統合を3フェーズで実行する。

| フェーズ | 名称 | 目的 | 期間目安 |
|---------|------|------|---------|
| **Phase 0** | 基盤修復 | 壊れたナビゲーションの修復、監査結果の反映 | 即座 |
| **Phase 1** | ドキュメント統廃合 | CONSOLIDATION_PLAN の実行 + 統合設計文書の新設 | 短期 |
| **Phase 2** | 設計統合 | MCU DB-PAL-BSP パイプラインの構築 | 中期 |

**CONSOLIDATION_PLAN との関係:** Phase 0 は AUDIT_REPORT の修正事項。Phase 1 は CONSOLIDATION_PLAN の Phase 1-6 に統合設計文書の新設を追加。Phase 2 は本レポートで新たに提案する設計統合。

---

## 2. Phase 0: 基盤修復

### 2.0 目的

AUDIT_REPORT で検出された9件の重大問題と、ANALYSIS.md で特定されたナビゲーション破損を修正する。他の全作業の前提条件。

### 2.1 CLAUDE.md のパス修正

| # | 現状 (壊れている) | 修正後 |
|---|------------------|--------|
| 1 | `docs/refs/specs/ARCHITECTURE.md` | `docs/refs/ARCHITECTURE.md` |
| 2 | `docs/new/DESIGN_CONTEXT_API.md` | `docs/archive/DESIGN_CONTEXT_API.md` |
| 3 | `docs/new/UMI_SYSTEM_ARCHITECTURE.md` | `docs/archive/UMI_SYSTEM_ARCHITECTURE.md` |
| 4 | `docs/refs/reference/API_APPLICATION.md` | `docs/refs/API_APPLICATION.md` |

### 2.2 AUDIT_REPORT の URGENT 修正

| # | アクション |
|---|-----------|
| U1 | 上記 CLAUDE.md パス修正 (2.1 で完了) |
| U2 | CAT_B: archive/umi-kernel/ 3ファイルの削除判定を撤回 → 「異なるバージョン、保持」に変更 |
| U3 | plan.md の旧パス参照を spec/ に更新 |
| U4 | DOCUMENT_INVENTORY に docs/plan/ セクション追加 |

### 2.3 AUDIT_REPORT の HIGH 修正

| # | アクション |
|---|-----------|
| H1 | CONCEPTS.md, NOMENCLATURE.md の `lib/umiusb/` → `lib/umi/usb/` パス修正 |
| H2 | CAT_B: README.md/index.md 統合判定を撤回 |
| H3 | archive/umi-kernel/ 同一ファイル数を5件に統一 |
| H4 | CAT_G: umimmio/docs/ 行数推定を実測値に更新 |
| H5 | CONSOLIDATION_PLAN の総ドキュメント数を修正 |

---

## 3. Phase 1: ドキュメント統廃合 + 統合設計文書

### 3.1 CONSOLIDATION_PLAN の実行

CONSOLIDATION_PLAN の Phase 1-6 をそのまま実行する。ANALYSIS.md の検証により、Phase 0 修正後のアクションは 89% の信頼性で実行可能と確認済み。

具体的なアクションは CONSOLIDATION_PLAN を参照。以下に差分のみ記載する。

### 3.2 統合設計文書の新設

ANALYSIS.md §8.3 で特定された「欠けている文書」を新設する。

#### 文書A: docs/DESIGN_PHILOSOPHY.md — 設計哲学

**なぜ必要か:** UMI の全設計判断の根底にある原則が明文化されていない。新規貢献者や外部評価者が設計の意図を理解できない。

**内容構成:**

```
1. 根本原則
   - #ifdef ゼロ: なぜ条件コンパイルを排除するのか
   - ゼロオーバーヘッド: Concepts + テンプレートで vtable を排除する理由
   - ワンソース・マルチターゲット: process(AudioContext&) の思想

2. 設計パターンカタログ
   - Concept-Based Contracts (umihal)
   - Template-Based HAL Bridge (Hw<Impl>)
   - Link-Time Symbol Injection (write_bytes)
   - BASEPRI-Based Critical Sections (audio 不可侵)
   - JSON-Driven Build Automation (MCU DB)

3. リアルタイム制約の設計表明
   - process() で禁止される操作と、その理由
   - ISR 優先度設計 (DMA > SysTick > PendSV)
   - signal() vs notify() の使い分け

4. 品質基準
   - LIBRARY_SPEC v2.0.0 準拠の意味
   - テスト戦略 (static_assert → unit → integration → Renode)
```

**情報源:** 既存コードから抽出。新規の設計判断ではなく、暗黙知の明文化。

#### 文書B: docs/LAYER_INTERFACES.md — レイヤー間インターフェース仕様

**なぜ必要か:** 各レイヤーの内部設計は文書化されているが、レイヤー間の「継ぎ目」が文書化されていない。

**内容構成:**

```
1. レイヤー概観図 (INVESTIGATION.md §1 の拡張)

2. インターフェース定義
   2.1 Build ↔ PAL/HAL/Port
       - MCU DB → embedded rule → コンパイルフラグ注入
       - ボードルール → スタートアップ/リンカ自動注入
       - (Phase 2) MCU DB → PAL ヘッダ選択

   2.2 PAL ↔ HAL
       - Device/Register/Field → Concept 実装の中で使用
       - Transport 抽象 (Direct, I2C, SPI)

   2.3 HAL ↔ Kernel
       - Hw<Impl> テンプレートが唯一の接点
       - オプショナルメソッドの if constexpr パターン
       - MaskedCritical<HW> RAII ラッパー

   2.4 Kernel ↔ Application
       - Syscall ABI (番号, 引数規約)
       - SharedMemory + TripleBuffer
       - EventRouter のルーティングテーブル

   2.5 umirtm ↔ umiport
       - write_bytes() リンク時注入
       - Platform::Output::putc() ブリッジ

3. データフロー図
   - オーディオパス: DMA → SpscQueue → process() → DMA
   - 制御パス: USB → SysEx → EventRouter → AudioContext
   - デバッグパス: RTT → write_bytes() → ホスト
```

#### 文書C: docs/guides/MCU_ADDITION_GUIDE.md — MCU 追加ガイド

**なぜ必要か:** 新 MCU サポートの追加手順が分散しており、全体像が不明。

**内容構成:**

```
1. 前提条件
   - MCU データシート
   - SVD ファイル (CMSIS-Pack)
   - 評価ボード

2. 手順
   Step 1: MCU DB にエントリ追加
     - mcu-database.json に新 MCU 定義
     - cortex-m.json にコアプロファイル (必要なら)

   Step 2: PAL ヘッダ作成
     - Phase 1: 手動でレジスタ定義
     - Phase 2: svd2ral で自動生成

   Step 3: Platform 実装
     - lib/umiport/include/umiport/board/{board}/
     - platform.hh (Platform concept 実装)
     - static_assert で HAL 準拠検証

   Step 4: BSP 作成
     - examples/{target}/src/bsp.hh
     - ボード固有のピン配置、メモリマップ

   Step 5: リンカスクリプト
     - 標準: common.ld (MCU DB から自動注入)
     - カスタム: カーネル/アプリ分割が必要な場合

   Step 6: テスト
     - static_assert → ユニットテスト → Renode シミュレーション

3. チェックリスト
```

#### 文書D: docs/guides/KERNEL_IMPLEMENTATION_GUIDE.md — カーネル実装ガイド

**なぜ必要か:** kernel.cc の技術負債を解消し、新プラットフォームでのカーネル実装を標準化するため。

**内容構成:**

```
1. Hw<Impl> の実装方法
   - 必須メソッド一覧 (enter_critical, exit_critical, etc.)
   - オプショナルメソッド (cache_invalidate)
   - テスト方法

2. デバッグメトリクスの管理方針
   - constexpr bool で有効/無効を切替可能にする
   - メトリクス構造体への集約パターン
   - リリースビルドでの除去保証

3. process_audio_frame() の分割パターン
   - 各段階 (PDM, USB OUT, process, USB IN) の独立化
   - テスト可能な設計

4. グローバル状態の構造化
   - KernelState 構造体への統合
   - 同期方式の統一 (TripleBuffer vs volatile の使い分け)
```

### 3.3 統廃合後のドキュメント構成

CONSOLIDATION_PLAN §4 の構成に、新設文書を追加:

```
docs/
├── README.md                    # 目次 (修復済み)
├── DESIGN_PHILOSOPHY.md         # [新設A] 設計哲学
├── LAYER_INTERFACES.md          # [新設B] レイヤー間インターフェース
├── NOMENCLATURE.md              # 命名体系
├── PROJECT_STRUCTURE.md         # プロジェクト構成
│
├── refs/                        # コア仕様 (既存)
├── umi-kernel/                  # OS設計 (既存, 整理後)
├── umios-architecture/          # OS設計仕様 (既存)
├── umi-sysex/                   # プロトコル (統合後)
│
├── guides/                      # [拡張] 開発ガイド
│   ├── MCU_ADDITION_GUIDE.md    # [新設C]
│   ├── KERNEL_IMPLEMENTATION_GUIDE.md  # [新設D]
│   └── SIMULATION.md            # dev/から移動
│
├── dev/                         # 開発者向け (整理後)
├── dsp/                         # DSP技術資料 (既存)
├── hw_io/                       # HW I/O設計 (既存)
│
├── archive/                     # アーカイブ (整理後)
└── plan/                        # 計画文書
    ├── DOCUMENT_INVENTORY.md
    ├── CONSOLIDATION_PLAN.md
    ├── AUDIT_REPORT.md
    ├── INVESTIGATION.md
    ├── ANALYSIS.md
    └── PROPOSAL.md (本文書)
```

---

## 4. Phase 2: 設計統合

### 4.1 概要

Phase 2 は ANALYSIS.md §6 で特定された3つの「飛躍点」を実現する。ドキュメント統廃合とは独立して進行可能だが、Phase 1 の設計文書がガイドとなる。

### 4.2 飛躍点 1: MCU DB の拡充

**目的:** MCU DB を真の Single Source of Truth にする。

**現状の MCU DB スキーマ:**
```json
{
  "core": "cortex-m4f",
  "flash": "1M",
  "ram": "128K",
  "flash_origin": "0x08000000",
  "ram_origin": "0x20000000",
  "vendor": "st"
}
```

**拡張後のスキーマ:**
```json
{
  "core": "cortex-m4f",
  "flash": "1M",
  "ram": "128K",
  "flash_origin": "0x08000000",
  "ram_origin": "0x20000000",
  "vendor": "st",
  "max_clock_mhz": 168,

  "ccm": {
    "origin": "0x10000000",
    "size": "64K"
  },

  "peripherals": {
    "usart": ["USART1", "USART2", "USART3", "UART4", "UART5"],
    "spi": ["SPI1", "SPI2", "SPI3"],
    "i2c": ["I2C1", "I2C2", "I2C3"],
    "dma": { "streams": 16 },
    "usb": "fs"
  },

  "svd": "STM32F407.svd"
}
```

**新設フィールドの用途:**

| フィールド | 利用先 |
|-----------|--------|
| `max_clock_mhz` | BSP 検証、性能見積もり |
| `ccm` | リンカスクリプト自動生成（現在ハードコード） |
| `peripherals` | MCU 追加ガイドの自動チェックリスト |
| `svd` | PAL 自動生成パイプラインへの入力 |

### 4.3 飛躍点 2: BSP 検証の自動化

**目的:** BSP (bsp.hh) と MCU DB の整合性を自動的に保証する。

**方式: ビルド時検証 (推奨)**

BSP に MCU DB 由来の情報を含む場合、`static_assert` でビルド時に検証:

```cpp
// bsp.hh に追加
#include <mcu_info.hh>  // embedded ルールが MCU DB から生成

static_assert(memory::sram_base == mcu::ram_origin,
    "BSP sram_base must match MCU DB ram_origin");
static_assert(memory::sram_size <= mcu::ram_size,
    "BSP sram_size exceeds MCU RAM capacity");
```

`mcu_info.hh` は embedded ルールが MCU DB の JSON から自動生成するヘッダ:

```cpp
// 自動生成: mcu_info.hh
namespace mcu {
    inline constexpr uintptr_t flash_origin = 0x08000000;
    inline constexpr size_t flash_size = 1024 * 1024;
    inline constexpr uintptr_t ram_origin = 0x20000000;
    inline constexpr size_t ram_size = 128 * 1024;
    inline constexpr uint32_t max_clock_mhz = 168;
}
```

**影響:**
- BSP とMCU DB の矛盾がコンパイルエラーで即座に検出される
- MCU DB の変更がBSPの検証に自動反映される
- 手動同期の必要性が消滅する

### 4.4 飛躍点 3: カーネル構成のビルド時注入

**目的:** カーネルパラメータをビルドシステムから制御可能にする。

**xmake 側:**
```lua
-- xmake.lua
set_values("kernel.max_tasks", 8)
set_values("kernel.max_timers", 4)
set_values("kernel.audio.buffer_size", 64)
set_values("kernel.audio.sample_rate", 48000)
```

**embedded ルール側で生成:**
```cpp
// 自動生成: kernel_config.hh
namespace umi::kernel::config {
    inline constexpr size_t max_tasks = 8;
    inline constexpr size_t max_timers = 4;
    inline constexpr size_t audio_buffer_size = 64;
    inline constexpr uint32_t audio_sample_rate = 48000;
}
```

**カーネル実装側:**
```cpp
#include <kernel_config.hh>
using namespace umi::kernel::config;
umi::Kernel<max_tasks, max_timers, HW, 1> g_kernel;
```

**影響:**
- 同一カーネルソースで、MCU リソースに応じた最適構成が可能
- xmake.lua だけで完結する構成管理
- BSP + カーネル + ビルドシステムの三位一体

---

## 5. 実行計画

### 5.1 Phase 0: 基盤修復

```
推定作業量: 2-3時間 (自動化可能な修正が大半)

□ CLAUDE.md パス修正 (4箇所)
□ CAT_B 修正 (3件)
□ plan.md パス更新
□ DOCUMENT_INVENTORY 更新
□ CONCEPTS.md, NOMENCLATURE.md パス修正
□ CAT_G 行数修正
□ CONSOLIDATION_PLAN 数値修正
```

**依存関係:** なし。即座に実行可能。

### 5.2 Phase 1: ドキュメント統廃合 + 新設

```
推定作業量: 各サブフェーズ 1-2日

Phase 1.0: 基盤修復 (Phase 0) ← 前提条件
Phase 1.1: 重複除去・クリーンアップ (CONSOLIDATION_PLAN Phase 1)
Phase 1.2: カーネル文書一本化 (CONSOLIDATION_PLAN Phase 2)
Phase 1.3: SysEx文書統合 (CONSOLIDATION_PLAN Phase 3)
Phase 1.4: dev/lib/docs 整理 (CONSOLIDATION_PLAN Phase 4)
Phase 1.5: USB/MIDI住み分け (CONSOLIDATION_PLAN Phase 5)
Phase 1.6: 新設文書の作成
  □ DESIGN_PHILOSOPHY.md
  □ LAYER_INTERFACES.md
  □ MCU_ADDITION_GUIDE.md
  □ KERNEL_IMPLEMENTATION_GUIDE.md
Phase 1.7: ナビゲーション再構築 (CONSOLIDATION_PLAN Phase 6)
  □ docs/README.md 全面書き直し
  □ CLAUDE.md 参照テーブル更新
  □ copilot-instructions.md 更新
```

**依存関係:** Phase 1.1-1.5 は順序依存。Phase 1.6 は Phase 1.1 以降いつでも開始可能。Phase 1.7 は全て完了後。

### 5.3 Phase 2: 設計統合

```
推定作業量: 各ステップ数日-1週間

Phase 2.1: MCU DB スキーマ拡張
  □ ccm, peripherals, svd フィールド追加
  □ 既存 MCU エントリの拡充 (11 MCU)
  □ embedded ルールの拡張対応

Phase 2.2: mcu_info.hh 自動生成
  □ embedded ルールに生成ロジック追加
  □ BSP 検証用 static_assert テンプレート
  □ 既存 BSP への検証追加

Phase 2.3: カーネル構成注入
  □ kernel_config.hh 生成ロジック
  □ xmake 設定 API 定義
  □ 既存カーネル実装の移行

Phase 2.4: (将来) PAL 自動生成
  □ svd2ral リファクタリング
  □ MCU DB の svd フィールドとの連携
  □ 生成ヘッダの品質検証
```

**依存関係:** Phase 2.1 → Phase 2.2 → Phase 2.3 は順序依存。Phase 2.4 は Phase 2.1 以降いつでも開始可能（ただし最も工数が大きい）。

---

## 6. リスクと緩和策

| リスク | 影響 | 緩和策 |
|--------|------|--------|
| ドキュメント移動中のリンク破損 | 高 | 専用ブランチ + CI リンク検証 |
| MCU DB スキーマ変更の後方互換性 | 中 | 新フィールドは全て optional |
| mcu_info.hh 生成の Lua 実装コスト | 低 | embedded ルールの既存パターンを踏襲 |
| 新設文書の初期品質 | 中 | umibench docs をテンプレートとして使用 |
| 進行中の開発との競合 | 高 | Phase 0/1 は `docs/consolidation` ブランチ |

---

## 7. 成果指標

### Phase 0 完了時

- [ ] CLAUDE.md の参照パス: 100% 有効
- [ ] AUDIT_REPORT の URGENT 4件: 全修正
- [ ] AUDIT_REPORT の HIGH 5件: 全修正

### Phase 1 完了時

- [ ] 不要ファイル 25+ 件の削除/archive 移動
- [ ] docs/README.md のリンク: 100% 有効
- [ ] 新設文書 4本の作成
- [ ] 全ナビゲーション文書の参照健全率: 95% 以上

### Phase 2 完了時

- [ ] MCU DB: 全 MCU にペリフェラル情報追加
- [ ] mcu_info.hh: 全 embedded ターゲットで自動生成
- [ ] BSP-MCU DB 整合性: static_assert で保証
- [ ] カーネル構成: xmake.lua から制御可能

---

## 8. 結論

UMI の設計は「基盤の質が極めて高いが、その上に技術負債が積まれ、ドキュメントのナビゲーションが壊れている」状態にある。

本提案は3つのフェーズでこの状態を解消する:

1. **Phase 0 (基盤修復)** — 壊れた入口を直す。即座に実行可能。
2. **Phase 1 (統廃合 + 新設)** — 既存の整理と、欠けていた統合設計文書の追加。
3. **Phase 2 (設計統合)** — MCU DB を中心とした自動化パイプラインの完成。

Phase 2 が完了した時、UMI は以下の状態に到達する:

```
MCU を1行指定するだけで:
  → コンパイルフラグ、リンカスクリプト、IDE 設定が自動生成され
  → PAL ヘッダがレジスタレベルで型安全に提供され
  → BSP との整合性がコンパイル時に保証され
  → カーネル構成が MCU リソースに最適化され
  → Renode シミュレーションで自動テストが実行される
```

これは「組み込みオーディオ開発のあるべき姿」であり、UMI の設計哲学の完全な実現である。
