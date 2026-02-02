# 00: 実装計画

> 各設計ドキュメント (01〜05) の実装タスクを抽出した ToDo リスト。
> 各タスクの設計詳細は関連ドキュメントを参照。

---

## 実装ルール (CLAUDE.md より)

**全フェーズ共通で遵守すること。**

### ワークフロー

| ルール | 内容 |
|--------|------|
| **計画と実装を分離** | 計画・調査・設計フェーズではコード変更しない。確認を得てから実装に移る |
| **ビルド成功だけで完了にしない** | ファームウェアタスクは **build → flash → デバッガ検証** の全工程を経て完了 |
| **既存コードを先に読む** | 変更前に現在の実装を理解する。盲目的な書き換え禁止 |
| **インクリメンタルに変更** | 大きな変更はレビュー可能なステップに分割する |
| **古い実装への巻き戻し禁止** | ロールバックによる「修正」は行わない |
| **変更後テスト実行** | ライブラリコード変更後は `xmake test` を実行。失敗状態でコミットしない |

### 各フェーズの実装手順テンプレート

```
1. 関連ドキュメント・既存コードを読む
2. 対象ファイル・API を特定
3. build/flash/test パスを確認
4. 完了基準を定義（何をどう検証するか）
5. → 確認を得てから実装開始
6. インクリメンタルに実装（1変更1コミット単位を推奨）
7. xmake test でホストテスト通過
8. xmake build stm32f4_kernel で ARM ビルド通過
9. xmake flash-kernel で書き込み
10. デバッガ (pyOCD/GDB) で動作検証
11. → 検証完了をもってタスク完了
```

### コードスタイル

| 項目 | 規則 |
|------|------|
| 規格 | C++23 |
| フォーマッタ | clang-format (LLVM base, 4-space indent, 120 char) |
| 関数/メソッド/変数/constexpr | `lower_case` |
| 型/クラス/concept | `CamelCase` |
| enum 値 | `UPPER_CASE` |
| 名前空間 | `lower_case` |
| メンバ変数 | プレフィックス/サフィックスなし。`m_` 禁止、`_` サフィックス禁止。必要なら `this->` |
| ポインタ/参照 | 左寄せ: `int* ptr` ✓ |
| エラー処理 | `Result<T>` またはエラーコード。カーネル/オーディオパスで例外禁止 |
| constexpr | `constexpr` のみ。冗長な `inline` を付けない (C++17 以降は暗黙的に inline) |

### リアルタイム安全性 (ISR / audio callback / process())

**ハード制約 — 違反はUBまたはオーディオグリッチを引き起こす:**

- ヒープ確保禁止 (`new`, `malloc`, `std::vector` の growth)
- ブロッキング同期禁止 (`mutex`, `semaphore`)
- 例外禁止 (`throw`)
- stdio 禁止 (`printf`, `cout`)

### デバッグアダプタ注意事項

- アダプタ無応答は USB の問題ではない
- `pgrep -fl pyocd`, `pgrep -fl openocd` で孤立プロセスを確認
- 特定 PID のみ kill — 広範なパターンでの kill 禁止

---

## ライブラリ構成ルール

`lib/` 以下の各ライブラリは独立した構成を持つ。umiport も同様に独立ライブラリとして配置する。

### ディレクトリ構成テンプレート

```
lib/<library>/
├── xmake.lua         # ライブラリターゲット定義
├── include/          # または直接ヘッダ配置（umiport は特殊構成）
├── docs/             # 設計ドキュメント
│   └── design/       # 設計ドキュメント（ナンバリング）
└── test/
    ├── xmake.lua     # テストターゲット定義（独立）
    ├── test_*.cc     # ホスト単体テスト
    └── stub_*.hh     # テスト用スタブ
```

### 既存ライブラリの構成例

```
lib/
├── umiusb/           ← USBスタック
│   ├── xmake.lua
│   ├── include/
│   ├── docs/design/  ← 00-implementation-plan.md, 01-..., 02-...
│   └── test/
│       ├── xmake.lua
│       └── test_*.cc
├── umifs/            ← ファイルシステム
│   ├── xmake.lua
│   ├── docs/
│   └── test/
│       ├── xmake.lua
│       └── test_*.cc
├── umidsp/           ← DSP
│   ├── xmake.lua
│   └── test/
│       ├── xmake.lua
│       └── test_*.cc
└── umiport/          ← HAL + Driver（新規）
    ├── xmake.lua     ← umi-port ルール定義 + ターゲット定義
    ├── docs/
    │   └── design/   ← 本ドキュメント群を移動
    ├── test/
    │   ├── xmake.lua ← テストターゲット定義
    │   ├── test_concept_compliance.cc  ← Concept充足テスト
    │   ├── test_hal_stm32f4.cc         ← F4 HAL単体テスト
    │   └── stub_registers.hh           ← レジスタスタブ
    ├── concepts/
    ├── common/
    ├── arch/
    ├── mcu/
    ├── board/
    └── platform/
```

---

## テスト戦略

### テストディレクトリ構成

`lib/umiusb/test/` の構造に倣い、`lib/umiport/test/` に全テストを集約する。

```
lib/umiport/test/
├── xmake.lua                      # テストターゲット定義
│
│  ── ホスト単体テスト ──
├── test_concept_compliance.cc     # 全 Concept の static_assert 検証
├── test_hal_stm32f4.cc            # STM32F4 HAL のスタブ使用テスト
├── test_hal_stm32h7.cc            # STM32H7 HAL のスタブ使用テスト
├── test_board_spec.cc             # BoardSpec/McuInit の定数検証
├── test_audio_driver.cc           # AudioDriver Concept 実装のロジックテスト
├── stub_registers.hh              # レジスタメモリをRAM上にエミュレート
│
│  ── Renode シミュレータテスト ──
├── renode_port_test.cc            # 最小起動・ペリフェラル初期化検証
├── port_test.resc                 # Renode スクリプト
└── port_test.robot                # Robot Framework テスト
```

### テストレベル

| レベル | 場所 | 実行方法 | 対象 |
|--------|------|---------|------|
| **L1: ホスト単体テスト** | `lib/umiport/test/test_*.cc` | `xmake run test_port_*` | Concept充足、HALロジック、ボード定数 |
| **L2: Renode シミュレータ** | `lib/umiport/test/renode_*.cc` | `xmake renode-port-test` | ペリフェラル初期化、クロック設定 |
| **L3: 実機テスト** | STM32F4-Discovery / Daisy Seed | `xmake flash-kernel` + pyOCD | 実際のHW動作、オーディオ、USB |

### L1: ホスト単体テスト

スタブレジスタを使ってHWなしでロジックをテストする。

```cpp
// stub_registers.hh — テスト用レジスタエミュレーション
// 実際のレジスタアドレスの代わりにRAM上の変数を使用
namespace stub {
struct RccRegs {
    uint32_t CR{};
    uint32_t CFGR{};
    uint32_t AHB1ENR{};
    // ...
};
} // namespace stub
```

テスト対象:
- **Concept充足**: 全レイヤーの全型が対応するConceptを `static_assert` で満たすことを検証
- **HALロジック**: クロック設定計算、GPIO AF設定、DMA設定のレジスタ書き込み値を検証
- **BoardSpec**: 定数値（クロック周波数、バッファサイズ等）の妥当性を検証
- **AudioDriver**: start/stop/configure のシーケンスを検証

### xmake.lua テンプレート (lib/umiport/test/xmake.lua)

```lua
-- lib/umiport/test/xmake.lua
local test_dir = os.scriptdir()
local umiport_dir = path.directory(test_dir)
local lib_dir = path.directory(umiport_dir)
local root_dir = path.directory(lib_dir)

-- Concept 充足テスト
target("test_port_concepts")
    add_rules("host.test")
    set_default(true)
    add_files(path.join(test_dir, "test_concept_compliance.cc"))
    add_includedirs(path.join(root_dir, "tests"))
    add_includedirs(path.join(umiport_dir, "concepts"))
    -- F4ターゲットのインクルードパスを設定してテスト
    add_includedirs(path.join(umiport_dir, "common"))
    add_includedirs(path.join(umiport_dir, "arch/cm4"))
    add_includedirs(path.join(umiport_dir, "mcu/stm32f4"))
    add_includedirs(path.join(umiport_dir, "board/stm32f4_disco"))
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()

-- ... 他のテストターゲットも同様
```

### xmake.lua テンプレート (lib/umiport/xmake.lua)

```lua
-- lib/umiport/xmake.lua
-- UMI-Port: Hardware Abstraction & Driver Layer

-- umi-port ルール: common/ と concepts/ を自動付与
rule("umi-port")
    on_load(function (target)
        local umiport_dir = path.join(os.projectdir(), "lib/umiport")
        target:add("includedirs", path.join(umiport_dir, "common"))
        target:add("includedirs", path.join(umiport_dir, "concepts"))
    end)
rule_end()

-- テストをインクルード
includes("test")
```

---

## 目標

Daisy Pod カーネルの実装を最終目標とし、
UMIプロジェクト全体の HAL / Driver / Middleware 構造を根本からリファクタリングする。

## 前提

- **libDaisy 完全非依存** — レジスタ直接操作による独自実装
- **マクロ排除** — `#ifdef` による切り替えは一切使わない
- **Concept駆動** — レイヤー契約を C++23 Concept で形式定義
- libDaisy は `.refs/libDaisy/` にリファレンスとしてのみ配置

---

## フェーズ一覧

| Phase | 内容 | 関連ドキュメント | 破壊的変更 |
|-------|------|----------------|-----------|
| 0 | port/ リファクタリング（既存STM32F4） | [01](01-principles.md), [02](02-port-architecture.md), [05](05-migration.md) | include パス全変更 |
| 1 | STM32H7 HAL + 最小起動 | [02](02-port-architecture.md), [03](03-concept-contracts.md) | なし |
| 2 | オーディオ出力 | [03](03-concept-contracts.md), [04](04-hw-separation.md) | なし |
| 3 | 全二重オーディオ + カーネル | [04](04-hw-separation.md) | なし |
| 4 | USB + Pod HID | [04](04-hw-separation.md) | なし |
| 5 | QSPI + SDRAM（オプション） | — | なし |

---

## Phase 0: port/ リファクタリング（既存STM32F4）

> 詳細: [01](01-principles.md), [02](02-port-architecture.md), [05](05-migration.md)

既存の散在した `backend/` + `kernel/port/` を新しい `lib/umiport/` 構造に移行する。
**既存ターゲット（STM32F4-Discovery）が壊れないことを保証しつつ**構造を整理。

### ライブラリ初期構築

- [ ] `lib/umiport/` ディレクトリを作成
- [ ] `lib/umiport/xmake.lua` を作成（`umi-port` ルール定義）
- [ ] `lib/umiport/test/xmake.lua` を作成
- [ ] `lib/umiport/docs/design/` に設計ドキュメント群を移動

### Concept 定義

- [ ] `lib/umiport/concepts/arch_concepts.hh` — CacheOps, FpuOps, ContextSwitch, ArchTraits
- [ ] `lib/umiport/concepts/mcu_concepts.hh` — GpioPort, ClockControl, DmaStream, AudioBus, I2cBus
- [ ] `lib/umiport/concepts/board_concepts.hh` — BoardSpec, Codec, McuInit, AudioDriver, FaultReport

### ファイル移行

- [ ] 既存ファイルを [05: 移行マッピング](05-migration.md) に従い移動
- [ ] `#include` パスを全て更新
- [ ] xmake.lua のインクルードパス設定を `umi-port` ルールに移行

### HW 分離

- [ ] `stm32_otg.hh` を `lib/umiusb/include/hal/` から `lib/umiport/mcu/stm32f4/mcu/` へ移動
- [ ] カーネルの HW 直接依存を Concept 経由に書き換え

### テスト・検証

**L1 (ホスト単体):**
- [ ] `test_concept_compliance.cc` — 全 Concept の static_assert 検証
- [ ] `xmake test` パス（既存テスト + 新規テスト全て）

**L3 (実機):**
- [ ] `xmake build stm32f4_kernel` パス
- [ ] `xmake build headless_webhost` パス
- [ ] `xmake flash-kernel` → pyOCD で動作検証（リグレッションなし）
- [ ] `xmake coding format` でスタイル違反なし

### クリーンアップ

- [ ] 旧 `lib/umios/backend/` を削除
- [ ] 旧 `lib/umios/kernel/port/` を削除

**完了条件**: `xmake test` + `xmake build stm32f4_kernel` + `xmake build headless_webhost` 全パス

---

## Phase 1: STM32H7 HAL + 最小起動（LED 点滅）

> 詳細: [02](02-port-architecture.md), [03](03-concept-contracts.md)

STM32H750 の最小 HAL を `lib/umiport/mcu/stm32h7/` に実装。

### HAL 実装

- [ ] `lib/umiport/mcu/stm32h7/mcu/rcc.hh` — HSE 16MHz → PLL1 → 480MHz (boost mode)、PWR VOS0
- [ ] `lib/umiport/mcu/stm32h7/mcu/pwr.hh` — 電源設定、SMPS
- [ ] `lib/umiport/mcu/stm32h7/mcu/gpio.hh` — 基本GPIO制御

### PAL 実装

- [ ] `lib/umiport/arch/cm7/arch/cache.hh` — D-Cache / I-Cache 有効化
- [ ] `lib/umiport/arch/cm7/arch/fpu.hh` — 倍精度FPU
- [ ] `lib/umiport/arch/cm7/arch/context.hh` — コンテキストスイッチ
- [ ] `lib/umiport/arch/cm7/arch/traits.hh` — M7特性定数

### Driver 実装

- [ ] `lib/umiport/board/daisy_seed/board/bsp.hh` — Seed LED (PC7)、基本クロック定数
- [ ] `lib/umiport/board/daisy_seed/board/mcu_init.cc` — `init_clocks()`, `init_gpio()` 最小実装

### カーネル

- [ ] `examples/daisy_pod_kernel/kernel.ld` — リンカスクリプト（内蔵Flash実行）
- [ ] `examples/daisy_pod_kernel/src/main.cc` — Reset_Handler、最小エントリ
- [ ] xmake.lua に daisy_pod_kernel ターゲット追加

### テスト・検証

**L1 (ホスト単体):**
- [ ] `test_hal_stm32h7.cc` — RCC PLL計算、GPIO設定の検証
- [ ] `test_concept_compliance.cc` に H7/CM7 のConcept充足追加
- [ ] `xmake test` パス

**L3 (実機 Daisy Seed):**
- [ ] `xmake build daisy_pod_kernel` パス
- [ ] `xmake flash-daisy-pod-kernel` → LED (PC7) 点滅
- [ ] SysTick 動作確認
- [ ] デバッガ (pyOCD) 接続確認

---

## Phase 2: オーディオ出力

> 詳細: [03](03-concept-contracts.md), [04](04-hw-separation.md)

SAI1 経由でオーディオ出力。

### HAL 実装

- [ ] `lib/umiport/mcu/stm32h7/mcu/sai.hh` — SAI1 Block A: Master TX
- [ ] `lib/umiport/mcu/stm32h7/mcu/dma.hh` — DMA + DMAMUX、Circular mode
- [ ] `lib/umiport/mcu/stm32h7/mcu/i2c.hh` — I2Cドライバ（WM8731用）

### Driver 実装

- [ ] `lib/umiport/board/daisy_seed/board/codec.hh` — AK4556 リセットピン制御
- [ ] `lib/umiport/board/daisy_seed/board/audio.hh` — SAI1構成
- [ ] `lib/umiport/board/daisy_seed/board/audio_driver.hh` — AudioDriver Concept実装
- [ ] MPU設定: SRAM_D2 non-cacheable（DMAバッファ用）

### テスト・検証

**L1 (ホスト単体):**
- [ ] `test_audio_driver.cc` — AudioDriver Concept のstart/stop/configureシーケンス
- [ ] `xmake test` パス

**L3 (実機 Daisy Seed):**
- [ ] ヘッドフォンからサイン波出力

---

## Phase 3: 全二重オーディオ + カーネル

> 詳細: [04](04-hw-separation.md)

- [ ] SAI1 Block B (Slave RX) 追加 — 全二重
- [ ] WM8731 I2C 制御（Rev5 対応）
- [ ] コーデック自動検出（AK4556 / WM8731）
- [ ] RTOS 起動、syscall、タスク管理
- [ ] AudioContext マッピング

### テスト・検証

**L1 (ホスト単体):**
- [ ] `xmake test` パス

**L3 (実機 Daisy Seed):**
- [ ] パススルー（入力→出力）動作

---

## Phase 4: USB + Pod HID

- [ ] `lib/umiport/mcu/stm32h7/mcu/usb_otg.hh` — USB OTG HS (FS動作、内蔵PHY)
- [ ] `lib/umiport/mcu/stm32h7/mcu/adc.hh` — ADC1 (ノブ用)
- [ ] USB Audio / MIDI（umiusb に H7 用 Hal 実装を注入）
- [ ] Pod 固有 HID — ノブ、ボタン、エンコーダ、RGB LED
- [ ] HID → UMI Event 変換

### テスト・検証

**L1 (ホスト単体):**
- [ ] `xmake test` パス

**L3 (実機 Daisy Pod):**
- [ ] USB MIDI 受信→シンセ発音、ノブでパラメータ変更

---

## Phase 5: QSPI + SDRAM（オプション）

- [ ] `lib/umiport/mcu/stm32h7/mcu/qspi.hh` — QUADSPI メモリマップドモード
- [ ] `lib/umiport/mcu/stm32h7/mcu/fmc.hh` — FMC SDRAM初期化

### テスト・検証

**L3 (実機 Daisy Seed):**
- [ ] QSPI XIP 実行
- [ ] SDRAM 読み書き

---

## フェーズ間の依存

```
Phase 0 (port/リファクタリング)
  │
  ├→ Phase 1 (H7 HAL + LED点滅)
  │    │
  │    └→ Phase 2 (オーディオ出力)
  │         │
  │         └→ Phase 3 (全二重 + カーネル)
  │              │
  │              └→ Phase 4 (USB + Pod HID)
  │                   │
  │                   └→ Phase 5 (QSPI + SDRAM)
  │
  └→ 既存ターゲット（F4, WASM）は Phase 0 完了時点で動作保証
```

## リスク

| リスク | 影響 | 対策 |
|--------|------|------|
| Phase 0 で既存ビルドが壊れる | 全体ブロック | 段階的移行、各ステップでビルド確認 |
| H7 キャッシュコヒーレンシ | オーディオ破損 | SRAM_D2 non-cacheable が最安全策 |
| VOS0 boost 電源シーケンス | 起動失敗 | libDaisy の初期化順を参照 |
| コーデック版検出 | 音が出ない | GPIO ADC 読み取りで判定（libDaisy 参照） |
| USB HS vs FS レジスタ差異 | USB 不動 | F4 OTG 実装をベースに H7 差分を反映 |

## 関連ドキュメント

| # | ドキュメント | 内容 |
|---|-------------|------|
| 01 | [基本原則](01-principles.md) | マクロ排除、同名ヘッダ、xmake制御のルール |
| 02 | [port/ アーキテクチャ](02-port-architecture.md) | レイヤー構成、HAL/Driver/Middleware関係、派生ボード |
| 03 | [Concept 契約](03-concept-contracts.md) | 各レイヤーの Concept 定義と検証パターン |
| 04 | [HW 分離原則](04-hw-separation.md) | カーネル・ミドルウェアからの HW 漏出排除 |
| 05 | [移行マッピング](05-migration.md) | 現行ファイル→新構成の対応表と手順 |
