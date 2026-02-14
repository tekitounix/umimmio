# UMI 設計解析レポート

**解析日:** 2026-02-14
**前提:** [INVESTIGATION.md](INVESTIGATION.md) の調査結果に基づく
**目的:** レイヤー間の設計整合性・品質を評価し、統合設計の方向性を導出する

---

## 1. 解析の視点

本レポートは以下の4軸で UMI の設計を解析する。

| 軸 | 問い |
|---|---|
| **A. 設計パターンの品質** | 各レイヤーの抽象化は適切か？ |
| **B. レイヤー間の結合度** | 疎結合と統合のバランスは取れているか？ |
| **C. 設計と実装の乖離** | ドキュメントはコードの実態を反映しているか？ |
| **D. 変革的統合の可能性** | 全レイヤーが一体化した時、何が可能になるか？ |

---

## 2. 設計パターンの品質評価

### 2.1 レイヤー別スコアカード

| レイヤー | 抽象化 | 命名一貫性 | 型安全性 | テスト | ドキュメント | 総合 |
|---------|-------|----------|---------|-------|------------|------|
| ビルドシステム | ★★★★★ | ★★★★☆ | N/A | N/A | ★★★★☆ | **8.7/10** |
| PAL/MMIO | ★★★★★ | ★★★★★ | ★★★★★ | ★★★★☆ | ★★★★☆ | **9.2/10** |
| HAL/ポート | ★★★★★ | ★★★★★ | ★★★★★ | ★★★★☆ | ★★★☆☆ | **8.6/10** |
| OS/カーネル (lib) | ★★★★★ | ★★★★☆ | ★★★★★ | ★★★★★ | ★★★★☆ | **9.0/10** |
| OS/カーネル (実装) | ★★★☆☆ | ★★★☆☆ | ★★★★☆ | ★★☆☆☆ | ★★☆☆☆ | **5.4/10** |
| コア/オーディオ | ★★★★★ | ★★★★★ | ★★★★★ | ★★★★☆ | ★★★★☆ | **9.2/10** |

**最も重要な発見:** カーネル **ライブラリ** (lib/umi/kernel/) は 9.0/10 と極めて高品質だが、カーネル **実装** (examples/stm32f4_kernel/) は 5.4/10 と大きく乖離している。この差は「設計の問題」ではなく「実装の技術負債」である。

### 2.2 設計パターンの解析

#### パターン1: Concept-Based Contracts (umihal)

```cpp
// 階層的Concept設計 — {Domain}Basic → {Domain}WithFeature → Full
template <typename T>
concept UartBasic = requires(T& u, const uart::Config& cfg) {
    { u.init(cfg) } -> std::same_as<Result<void>>;
};

template <typename T>
concept UartAsync = UartBasic<T> && requires(T& u, std::span<const uint8_t> data) {
    { u.write_async(data) } -> std::same_as<Result<void>>;
};
```

**評価: 10/10** — C++23 Concepts の教科書的な使用。`#ifdef` ゼロ、vtable ゼロ、コンパイル時に全て解決。各プラットフォームの実装は `static_assert` で検証される。

**軽微な課題:**
- `ClockTree::init()` が `void` を返す — 失敗セマンティクスの欠如
- トランスポート Concept にアドレス範囲の制約がない (7bit vs 10bit I2C)

#### パターン2: Template-Based HAL Bridge (Hw\<Impl\>)

```cpp
template <class Impl>
struct Hw {
    static void enter_critical() { Impl::enter_critical(); }
    static void exit_critical() { Impl::exit_critical(); }
    // Optional methods via requires
    static void cache_invalidate(void* addr, std::size_t size) {
        if constexpr (requires { Impl::cache_invalidate(addr, size); }) {
            Impl::cache_invalidate(addr, size);
        }
    }
};
```

**評価: 9/10** — カーネルとアーキテクチャの完全な分離。`if constexpr (requires {...})` によるオプショナルメソッドのパターンは秀逸。

```cpp
// STM32F4での具体化
struct Stm32F4Hw {
    static void enter_critical() {
        __asm__ volatile("msr basepri, %0" ::"r"(0x10u) : "memory");
    }
    // Audio DMA (priority 0) is NOT masked — BASEPRI = 0x10 のみマスク
};
using HW = umi::Hw<Stm32F4Hw>;
umi::Kernel<8, 4, HW, 1> g_kernel;
```

BASEPRI を用いたクリティカルセクション設計は、オーディオ DMA の不可侵性を型レベルで保証している。

#### パターン3: Link-Time Symbol Injection (write_bytes)

```
umirtm (宣言のみ)               umiport (定義)
┌──────────────────────┐      ┌──────────────────────┐
│ extern void          │      │ void write_bytes(..) {│
│   write_bytes(span); │──────│   ::write(1, ...);    │  ← Host
│                      │  link│   Platform::putc();   │  ← STM32F4
│                      │      │   fwrite(...);        │  ← WASM
└──────────────────────┘      └──────────────────────┘
```

**評価: 10/10** — コンパイル時の依存ゼロ。リンカが正しい実装を選択。3プラットフォームで検証済み。欠落時はリンクエラーで明確にフィードバック。

#### パターン4: JSON-Driven Build Automation

```
mcu-database.json → embedded rule (1026行 Lua)
  → コンパイルフラグ生成
  → リンカシンボル注入 (-Wl,--defsym)
  → FPU 自動設定
  → メモリ使用量レポート
  → VSCode 設定生成
```

**評価: 9/10** — MCU を `set_values("embedded.mcu", "stm32f407vg")` と1行で指定するだけで、ツールチェーン・フラグ・リンカ・IDE 設定が全て自動生成される。

**課題:**
- CCM アドレスがハードコード (STM32F4: `0x10000000` 固定)
- メモリパーティション（カーネル/アプリ分割）が手動リンカスクリプト依存
- ペリフェラル情報 (UART数, SPI数等) が MCU DB に未記載

---

## 3. レイヤー間結合度の解析

### 3.1 結合度マトリクス

```
              Build   PAL   HAL   Port   Kernel(lib)  Kernel(impl)  Core
Build          —      ○     ○      ○        ×            ○          ×
PAL            ×      —     ×      ○        ×            ×          ×
HAL            ×      ×     —      ×        ×            ×          ×
Port           ×      ○     ○      —        ×            ○          ×
Kernel(lib)    ×      ×     ×      ×        —            ×          ○
Kernel(impl)   ×      ×     ×      ○        ○            —          ○
Core           ×      ×     ×      ×        ×            ×          —

○ = 依存あり   × = 依存なし
```

**解析:**

1. **HAL (umihal) は完全に独立** — 他の一切に依存しない。Concepts 定義のみ。理想的な設計。

2. **Kernel ライブラリも高い独立性** — Core (AudioContext) にのみ依存。HAL にも Port にも依存しない。`Hw<Impl>` テンプレートパラメータでのみ接続。

3. **Kernel 実装が結合ポイント** — Port, Kernel(lib), Core の全てを結合する。これは意図通り。統合の責務を実装層に集中させている。

4. **ビルドシステムは PAL/HAL/Port と並行関係** — コンパイル時にフラグとリンカ設定を注入するが、コード依存はない。

### 3.2 疎結合の評価

| 接続 | 結合方式 | 評価 |
|------|---------|------|
| HAL → Port | Concept 検証 (static_assert) | ★★★★★ 完璧 |
| Kernel(lib) → HW | テンプレートパラメータ | ★★★★★ 完璧 |
| umirtm → umiport | リンク時シンボル注入 | ★★★★★ 完璧 |
| Build → 各層 | コンパイルフラグ/リンカシンボル | ★★★★☆ 良好 |
| BSP → MCU DB | **手動同期** | ★★☆☆☆ 改善必要 |
| mcu.cc → PAL | **未統合** (手動レジスタ操作) | ★★☆☆☆ 改善必要 |

**最も重要な結合上の問題:**

BSP 定義 (`bsp.hh`) と MCU DB (`mcu-database.json`) が独立して同じ情報を持っている。

```cpp
// bsp.hh (手動)
inline constexpr uint32_t cpu_freq_hz = 168000000;
inline constexpr uintptr_t sram_base = 0x20000000;
```

```json
// mcu-database.json (自動利用可能)
"ram_origin": "0x20000000",
"ram": "128K"
```

`sram_base` は MCU DB の `ram_origin` と同一値。だが接続がない。

### 3.3 結合度の理想と現実

```
現状:                          理想:
BSP ←×→ MCU DB                BSP ←→ MCU DB (検証 or 生成)
mcu.cc ←×→ PAL                mcu.cc → PAL → レジスタ操作
Kernel ←×→ Build Config       Kernel ← Build Config (スタック/タスク設定)
```

---

## 4. 設計と実装の乖離分析

### 4.1 乖離マップ

| 領域 | 設計(docs) | 実装(code) | 乖離度 | 影響 |
|------|-----------|-----------|--------|------|
| スケジューラ | O(1) bitmap | ★★★★★ 完全一致 | 0% | なし |
| EventRouter | sample-accurate routing | ★★★★★ 完全一致 | 0% | なし |
| SpscQueue | lock-free SPSC | ★★★★★ 完全一致 | 0% | なし |
| MPU 設定 | MemoryUsage 構造体 | 手動 constexpr | 15% | 軽微 |
| StorageService | BlockDevice + VFS | テンプレートのみ | 70% | 中 |
| Fault Handler | fault_handler.hh | 定義済みだが未統合 | 100% | 高 |
| FS Syscall | syscall 60-83 定義 | スタブ (`result = 0`) | 100% | 高 |
| MIDI SysEx | SysExAssembler 設計 | 部分実装 | 40% | 中 |

### 4.2 カーネル実装の技術負債

**kernel.cc (1256行) の構成分析:**

```
[行 1-100]     インクルード, 定数定義           ← 良好
[行 100-160]   グローバル変数 (15個)            ← 要構造化
[行 160-280]   USB コールバック設定             ← 良好
[行 280-430]   デバッグ変数 (129個 volatile)    ← 技術負債の核心
[行 430-640]   process_audio_frame() (204行)   ← 要分割
[行 640-760]   タスクエントリ関数               ← 良好
[行 760-870]   init/start/setup 関数           ← 良好
[行 870-950]   syscall ハンドラ                 ← 部分的にスタブ
[行 950-1256]  USB/MIDI 処理                   ← 良好
```

**核心的問題: デバッグ変数が実装の 23% を占める**

129個の `volatile` 変数がメインロジックと混在し、以下の問題を引き起こしている:

1. **可読性の破壊** — 実装ロジックがデバッグコードに埋もれる
2. **ISR でのメモリバリア** — `volatile` 書き込みがメモリバリアを生成、リアルタイム性能に影響
3. **テスト不可能** — グローバル状態がユニットテスト化を阻害

**process_audio_frame() の責務過多:**

1関数に5つの責務が混在:

```
process_audio_frame(buf)
  ├─ AppConfig スワップ (TripleBuffer)
  ├─ PDM デシメーション
  ├─ USB Audio OUT 読み出し (75行)  ← 最大
  ├─ アプリ process() + シンセ
  └─ USB Audio IN 書き込み
```

ただし重要な注記: **このコードは動作する。** スケジューラ、SpscQueue、EventRouter、BASEPRI マスキング等のコアメカニズムは教科書品質で正しく実装されている。技術負債は「動かない」のではなく「保守しにくい」レベルの問題である。

### 4.3 ドキュメント参照の健全性

| ドキュメント | 参照数 | 有効 | 壊れ | 健全率 |
|------------|-------|------|------|--------|
| CLAUDE.md | 7 | 3 | 4 | **43%** |
| docs/README.md | 30+ | 8 | 22+ | **27%** |
| docs/umi-kernel/plan.md | 15+ | 12 | 3 | 80% |
| lib/docs/design/README.md | 50+ | 45 | 5 | 90% |
| umios-architecture/ | 41 | 41 | 0 | **100%** |

**最も深刻な発見:** プロジェクトの2大ナビゲーション文書 (CLAUDE.md, docs/README.md) が共に 50% 未満の健全率。ドキュメントへの入口が壊れている。

**孤立ドキュメント:** 30+ ファイルがどこからも参照されていない（docs/plan/, docs/archive/ の大部分）。

---

## 5. レイヤー間設計の整合性

### 5.1 抽象化レベルの整合性

```
レベル5: Application  ProcessorLike → process(AudioContext&)
レベル4: OS/Runtime   Kernel<N,M,HW,C> → スケジューラ、IPC
レベル3: HAL          30+ Concepts → GPIO, UART, I2S, Audio...
レベル2: PAL/MMIO     Device<Block<Register<Field>>>
レベル1: Build        MCU DB → フラグ/リンカ/IDE 自動生成
```

**整合性評価:**

- **L5↔L4:** ★★★★★ — `ProcessorLike` が `process(AudioContext&)` で明確に接続
- **L4↔L3:** ★★★★★ — `Hw<Impl>` がテンプレートで完全分離
- **L3↔L2:** ★★★★☆ — 設計は完璧だが、PAL 自動生成 (Phase 2) が未実装
- **L2↔L1:** ★★★☆☆ — MCU DB は PAL ヘッダ選択を設計済みだが、まだ手動

**ギャップの本質:** L3↔L2↔L1 の接続が「設計済み・未実装」の状態。これは Phase 2 (SVD→C++ 自動生成) の実現で解消される。

### 5.2 命名規約の層間整合性

| レイヤー | namespace | 型命名 | 関数命名 | 定数 |
|---------|-----------|-------|---------|------|
| Build | N/A (Lua) | N/A | snake_case | UPPER_CASE |
| PAL/MMIO | `umi::mmio` | CamelCase | lower_case | N/A |
| HAL | `umi::hal` | CamelCase | lower_case | N/A |
| Port | `umi::port` | CamelCase | lower_case | N/A |
| Kernel | `umi::kernel` | CamelCase | lower_case | UPPER_CASE (enum) |
| Core | `umi::core` | CamelCase | lower_case | N/A |

**結論:** C++ コード全体で CODING_RULE.md 準拠。完璧な一貫性。

### 5.3 エラーハンドリングの層間整合性

| レイヤー | 方式 | 一貫性 |
|---------|------|--------|
| HAL | `Result<T>` (std::expected) | ★★★★★ |
| Port | `Result<T>` | ★★★★★ |
| Kernel | 戻り値 / errno 的 | ★★★★☆ |
| Core | 制約で回避 (process は void) | ★★★★★ |
| Build | Lua error / os.raise | ★★★★☆ |

**軽微な不整合:** `Platform::Output::putc()` は `void` 返り (エラー無視)。Host 実装は `::write()` の返り値を無視、STM32F4 は `rtm::write()` を `std::ignore` で破棄。出力パスのエラーは意図的に無視されているが、これはドキュメント化されていない設計判断。

---

## 6. 変革的統合のポテンシャル分析

### 6.1 現状の自動化チェーン

```
現在のワークフロー:
MCU選択 → [embedded rule] → フラグ + リンカ → ビルド → フラッシュ → 動作

自動化済み:  ████████████████████░░░░░░░  (70%)
手動が必要:  ░░░░░░░░░░░░░░░░░░░░█████░░  (20%)
未実装:      ░░░░░░░░░░░░░░░░░░░░░░░░░██  (10%)
```

### 6.2 統合による自動化の完全体

以下は各レイヤーの統合が完成した時に実現可能になる自動化パイプライン:

```
Phase 2完了後:
MCU選択
  → MCU DB [flash/ram/core/peripherals]
    → embedded rule [フラグ/リンカ/IDE]
    → PAL自動生成 [SVD → C++ヘッダ → レジスタ型]
      → BSP検証 [bsp.hh vs MCU DB の自動整合性チェック]
        → ビルド → フラッシュ → Renode自動テスト → 動作

自動化済み:  ████████████████████████████  (95%)
手動が必要:  ░░░░░░░░░░░░░░░░░░░░░░░░░░█  (5% — ボード固有ピン配置のみ)
```

### 6.3 設計上の「飛躍点」

3つの重要な統合ポイントが、実現すれば全体の質的変化を起こす:

#### 飛躍点 1: MCU DB → PAL 自動生成

**現状:** PAL ヘッダは手書き (Phase 1)
**到達点:** SVD → C++ レジスタ型の自動生成

```
MCU DB: "stm32f407vg"
  → SVD: STM32F407.svd
    → svd2ral: Device<GPIOD<ODR<Field<12,1>>>>
      → 型安全なレジスタ操作
```

**影響:** 新 MCU サポートのコストが「数週間の手書き」から「数時間の検証」に変化。

#### 飛躍点 2: BSP + MCU DB の統一ソース

**現状:** `bsp.hh` (手動 constexpr) と `mcu-database.json` (JSON) が独立
**到達点:** MCU DB を Single Source of Truth とし、BSP の MCU 固有部分を自動導出

```
mcu-database.json
  ├─ ram_origin: 0x20000000  → bsp.hh の memory::sram_base を検証/生成
  ├─ flash: 1M               → リンカスクリプトのメモリ定義
  └─ core: cortex-m4f        → FPU フラグ + PAL ヘッダ選択
```

**影響:** BSP に残るのはボード固有の情報（LED ピン配置、オーディオ設定等）のみ。MCU データシートの情報は全て MCU DB に一本化。

#### 飛躍点 3: ビルドシステム → カーネル構成の制御

**現状:** カーネルの構成 (タスク数、スタックサイズ) がソースコードにハードコード
**到達点:** xmake 設定からカーネルパラメータを注入

```lua
-- xmake.lua
set_values("kernel.max_tasks", 8)
set_values("kernel.audio.buffer_size", 64)
set_values("kernel.app.stack_size", "16K")
```

```cpp
// 自動生成される kernel_config.hh
inline constexpr size_t max_tasks = 8;
inline constexpr size_t audio_buffer_size = 64;
```

**影響:** 同一カーネルコードで、MCU のリソースに応じた最適構成が自動決定可能になる。

---

## 7. ドキュメント設計の品質差マトリクス

### 7.1 ライブラリドキュメントの品質マップ

LIBRARY_SPEC v2.0.0 を基準として:

| ライブラリ | DESIGN.md | INDEX.md | TESTING.md | docs/ja/ | README.md | 総合 |
|-----------|-----------|----------|-----------|---------|-----------|------|
| umibench | ★★★★★ | ★★★★★ | ★★★★★ | ★★★★★ | ★★★★★ | **Gold** |
| umimmio | ★★★★☆ | ★★★★☆ | ★★★★☆ | ★★★★☆ | ★★★★☆ | **Silver** |
| umirtm | ★★★★☆ | ★★★★☆ | ★★★★☆ | ★★★★☆ | ★★★★☆ | **Silver** |
| umitest | ★★★★☆ | ★★★★☆ | ★★★★☆ | ★★★★☆ | ★★★★☆ | **Silver** |
| umihal | ★★☆☆☆ | ✗ | ✗ | ✗ | ★★★☆☆ | **Bronze** |
| umiport | ★★☆☆☆ | ✗ | ✗ | ✗ | ★★★☆☆ | **Bronze** |
| kernel | ✗ | ✗ | ✗ | ✗ | ✗ | **未整備** |

### 7.2 設計文書の配置問題

現状、設計文書が4階層に分散:

```
docs/                          ← プロジェクト全体の仕様 (157 MD files)
  ├─ refs/                    ← コア仕様 (13 files)
  ├─ umios-architecture/      ← OS 仕様 (41 files) — 最も整然
  ├─ umi-kernel/              ← カーネル仕様 (旧形式)
  └─ archive/                 ← アーカイブ (15+ files)

lib/docs/                      ← ライブラリ標準・ガイド
  ├─ standards/               ← コーディング規約, ライブラリ仕様
  ├─ guides/                  ← デバッグ, テスト
  └─ design/                  ← HAL/PAL 設計 (最大の文書群)
      └─ pal/                 ← 14 カテゴリ + サブ文書

lib/umi/*/docs/                ← ライブラリ固有ドキュメント
lib/*/docs/                    ← スタンドアロンライブラリのドキュメント
```

**問題:** 「HAL の設計を理解したい」時に参照すべき場所が不明確。

- `lib/umihal/README.md` — 概要のみ
- `lib/docs/design/` — 詳細な PAL/HAL 設計
- `docs/refs/CONCEPTS.md` — Concept の参照仕様

3箇所を横断する必要があり、相互参照も不十分。

---

## 8. 統合設計への示唆

### 8.1 設計の強みと弱み — 要約

| | 強み | 弱み |
|---|---|---|
| **設計哲学** | `#ifdef` ゼロ、型安全、ゼロオーバーヘッド | 哲学がドキュメントに明文化されていない |
| **抽象化** | 各レイヤーの独立性が極めて高い | レイヤー間の「継ぎ目」のドキュメントが薄い |
| **自動化** | embedded ルールは組み込み開発の常識を変えうる | PAL 自動生成が未実装で潜在力が半減 |
| **品質** | コアライブラリは教科書品質 | 実装層の技術負債が品質認識を下げている |
| **ドキュメント** | umios-architecture は精緻 | ナビゲーション文書が壊れ、到達できない |

### 8.2 統合設計の3原則

調査と解析の結果、UMI の統合設計は以下の3原則に基づくべきと結論づける:

**原則1: 設計情報の Single Source of Truth**

```
MCU の情報 → mcu-database.json のみ
Concept 定義 → umihal のみ
カーネル仕様 → umios-architecture/ のみ
ライブラリ標準 → LIBRARY_SPEC のみ
```

重複は一切許さない。参照はあっても、コピーは作らない。

**原則2: 情報の流れは上から下へ**

```
MCU DB (Source of Truth)
  ↓ 生成/検証
BSP + PAL ヘッダ
  ↓ Concept 検証
HAL 実装
  ↓ テンプレート注入
Kernel
  ↓ process() 呼び出し
Application
```

下位レイヤーが上位レイヤーの情報を持つことは許さない。

**原則3: 文書は「なぜ」を語り、コードは「何を」語る**

- API の使い方 → コード内 Doxygen
- 設計判断の理由 → 設計文書 (ADR 形式)
- レイヤー間の接続理由 → 統合設計文書 (新設)

### 8.3 ドキュメント統合の方向性

CONSOLIDATION_PLAN の Phase 1-6 は「整理」であり正しい。だが、整理の先に必要な「統合設計文書」が欠けている。

**欠けている文書類:**

1. **設計哲学文書** — 「なぜ `#ifdef` ゼロか」「なぜリンク時注入か」を明文化
2. **レイヤー間インターフェース仕様** — 各レイヤーの接続点を1文書で俯瞰
3. **MCU 追加ガイド** — 新 MCU を追加する際の全手順（MCU DB → PAL → BSP → テスト）
4. **カーネル実装ガイド** — `Hw<Impl>` の実装方法、デバッグ変数の管理方針
5. **統合テスト仕様** — Renode シミュレーション + ホストテストの併用戦略

---

## 9. 結論

### 9.1 設計品質の総合評価

UMI の設計は **2つの異なる世界** から構成されている:

**世界1: 基盤設計 (8.8/10)**
- HAL Concepts、`Hw<Impl>`、リンク時注入、MCU DB、embedded ルール
- これらは組み込みオーディオシステムの「あるべき姿」を示している
- 世界で最も先進的な組み込みフレームワーク設計の一つと評価できる

**世界2: 実装層 (5.4/10)**
- kernel.cc のデバッグ残留物、未完のサービス、手動 BSP
- これらは「動作するプロトタイプ」の段階にある
- 基盤設計の品質を外部からは見えにくくしている

### 9.2 最も重要な洞察

**UMI の真の価値は「抽象化の質」にある。** 各レイヤーが独立して進化できる設計は、通常の組み込みプロジェクトでは達成困難。この設計を活かすために:

1. **実装の技術負債を清算する** — kernel.cc のリファクタリングが最優先
2. **PAL 自動生成を実現する** — 設計のポテンシャルを解放する鍵
3. **ドキュメントのナビゲーションを修復する** — 設計の素晴らしさに到達できない現状を解消

### 9.3 次のステップ

具体的な統合設計提案は [PROPOSAL.md](PROPOSAL.md) で述べる。
