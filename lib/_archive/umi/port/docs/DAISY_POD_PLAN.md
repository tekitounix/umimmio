# Daisy Seed / Daisy Pod 対応計画

## 概要

STM32H750ベースの Daisy Seed ボードを UMI カーネルに対応させる。
Daisy Seed（コアボード）と Daisy Pod（拡張ボード）は明確に分離し、
将来的に Daisy Patch, Daisy Field, Daisy Petal 等の他の拡張ボードにも対応可能な構造とする。

**libDaisy には完全非依存**。レジスタ直接操作による独自実装で実現する。
libDaisy はリファレンスとしてのみ参照する（`.refs/libDaisy/`）。

詳細なアーキテクチャ設計は [バックエンド切り替えアーキテクチャ](BACKEND_SWITCHING.md) を参照。

## レイヤー分離

```
ミドルウェア（kernel, umiusb）  ← HW非依存、Conceptのみ使用
        ↓ Concept経由で注入
port/board/daisy_seed/          ← Driver（HALを束ねてConcept実装を提供）
        ↓ 使う
port/mcu/stm32h7/               ← HAL（レジスタ操作）
port/arch/cm7/                  ← PAL（CPUコア固有、直交軸）
port/common/                    ← CM共通基盤
```

### 各レイヤーの責務

| レイヤー | port/内の場所 | 責務 | 変更頻度 |
|----------|-------------|------|----------|
| **mcu/stm32h7/** (HAL) | `port/mcu/stm32h7/mcu/` | レジスタ直接操作。MCU固有、ボード非依存 | MCU変更時のみ |
| **board/daisy_seed/** (Driver) | `port/board/daisy_seed/board/` | HALを組み合わせてConcept実装。全Daisyボードで共有 | Seedリビジョン変更時 |
| **examples/daisy_*_kernel/** | `examples/` | 派生ボード固有のピン定義とHID初期化のみ | ボード追加時 |

### 将来の拡張ボード対応

```
examples/
  ├── daisy_pod_kernel/       ← Daisy Pod (2ノブ, 2ボタン, エンコーダ, 2xRGB LED)
  ├── daisy_patch_kernel/     ← Daisy Patch (4ノブ, OLED, CV I/O, Gate)
  ├── daisy_field_kernel/     ← Daisy Field (8ノブ, 8LED, 2CV, キーボード)
  └── daisy_petal_kernel/     ← Daisy Petal (6ノブ, フットスイッチ, エフェクタ向け)
```

各カーネルは `port/board/daisy_seed/` を共有し、xmakeインクルードパスの積み重ね（stacking）で
ボード固有部分のみ追加する。

## ターゲットハードウェア

### Daisy Seed（コアボード）

| 項目 | 仕様 |
|------|------|
| MCU | STM32H750IBK6 (Cortex-M7, 480MHz, FPU+DSP) |
| RAM | DTCMRAM 128KB, SRAM_D1 512KB, SRAM_D2 288KB, SRAM_D3 64KB |
| Flash | 内蔵128KB + QSPI 8MB (IS25LP064A) |
| SDRAM | 64MB (IS42S16400J) |
| オーディオコーデック | AK4556 (Rev4) / WM8731 (Rev5) / PCM3060 (Seed2 DFM) |
| オーディオI/F | SAI1 (Block A: Master TX, Block B: Slave RX) |
| GPIO | 32ピン |
| USB | OTG HS (内蔵PHY, FS動作) |

### Daisy Pod（拡張ボード、Seedに接続して使用）

| 項目 | ピン | 備考 |
|------|------|------|
| Knob 1 | D21 (PC4, ADC1_INP4) | アナログ入力 |
| Knob 2 | D15 (PA7, ADC1_INP7) | アナログ入力 |
| Button 1 | D27 (PD11) | デジタル入力 |
| Button 2 | D28 (PA2) | デジタル入力 |
| Encoder A | D26 (PD2) | ロータリーエンコーダ |
| Encoder B | D25 (PD3) | |
| Encoder Click | D13 (PC14) | |
| LED 1 RGB | D20/D19/D18 (PA5/PB1/PA0) | PWMまたはGPIO |
| LED 2 RGB | D17/D24/D23 (PB5/PD12/PA1) | |
| MIDI IN | UART (RX) | |
| Audio | Seedのコーデック経由 | |

## ファイル構成

```
lib/umiport/
  ├── arch/cm7/arch/                     Cortex-M7 (PAL)
  │   ├── cache.hh                       D-Cache / I-Cache 管理
  │   ├── fpu.hh                         倍精度FPU有効化
  │   ├── context.hh                     コンテキストスイッチ（PendSV）
  │   ├── svc_handler.hh                 SVCハンドラ
  │   ├── handlers.cc                    例外ハンドラ実装
  │   └── traits.hh                      M7特性定数
  │
  ├── mcu/stm32h7/mcu/                   STM32H7 HAL
  │   ├── rcc.hh                         クロック (PLL1/2/3, 480MHz)
  │   ├── pwr.hh                         電源 (VOS0, SMPS)
  │   ├── gpio.hh                        GPIO
  │   ├── sai.hh                         SAI
  │   ├── dma.hh                         DMA + DMAMUX
  │   ├── i2c.hh                         I2C
  │   ├── adc.hh                         ADC
  │   ├── fmc.hh                         FMC (SDRAM)
  │   ├── qspi.hh                        QUADSPI
  │   ├── usb_otg.hh                     USB OTG HS
  │   └── irq_num.hh                     IRQ番号定義
  │
  └── board/daisy_seed/board/            Daisy Seed Driver（全Daisyボード共有）
      ├── bsp.hh                         Seedピン配置、クロック、メモリマップ
      ├── codec.hh                       AK4556/WM8731/PCM3060 検出・初期化
      ├── audio.hh                       SAI1構成
      ├── audio_driver.hh               AudioDriver Concept実装（SAI+DMA+codec）
      └── mcu_init.cc                   Seed共通MCU初期化

examples/daisy_pod_kernel/               Pod固有カーネル
  ├── kernel.ld                          リンカスクリプト
  ├── xmake.lua                          ビルド設定
  └── src/
      ├── main.cc                        エントリポイント
      ├── board/
      │   └── pod.hh                     Pod固有ピン/HID定義（ノブ, ボタン, エンコーダ, LED）
      └── kernel.cc/.hh                  カーネル
```

## STM32F4 → STM32H7 主要差分

### クロック

| 項目 | F4 (Discovery) | H7 (Daisy Seed) |
|------|-----------------|-------------------|
| コア | 168MHz | 480MHz (boost) |
| HSE | 8MHz | 16MHz |
| PLL | 1系統 | 3系統 (PLL1/2/3) |
| オーディオクロック | PLLI2S → I2S | PLL3 → SAI |
| 電源 | 標準 | VOS0 (boost) + SMPS |

### オーディオ

| 項目 | F4 (Discovery) | H7 (Daisy Seed) |
|------|-----------------|-------------------|
| I/F | I2S3 (出力のみ) | SAI1 (入出力) |
| コーデック | CS43L22 (DAC) | AK4556 / WM8731 |
| 入力 | PDMマイク (I2S2) | コーデック経由ライン入力 |
| DMA | DMA1固定 | DMA1/2 + DMAMUX |
| バッファ配置 | SRAM | SRAM_D2 (non-cached) |
| Fs | 47,991Hz | 48,000Hz |

### メモリ

| 項目 | F4 | H7 |
|------|-----|-----|
| Flash | 1MB内蔵 | 128KB内蔵 + 8MB QSPI |
| SRAM | 128KB + 64KB CCM | 128KB DTCM + 512KB D1 + 288KB D2 + 64KB D3 |
| 外部RAM | なし | 64MB SDRAM |
| キャッシュ | なし | D-Cache + I-Cache（管理必須） |

### Cortex-M7 固有

- **D-Cache / I-Cache**: 有効化必須、DMAバッファはnon-cached領域に配置するかキャッシュメンテナンス
- **MPU**: より多くのリージョン (16個)、non-cached属性の設定が重要
- **FPU**: 倍精度FPU (DP-FPU)
- **TCM**: ITCM (64KB) + DTCM (128KB)、ゼロウェイトアクセス

## ビルドシステム

```lua
target("daisy_pod_kernel")
    set_kind("binary")
    set_arch("arm")
    add_rules("arm-embedded", "umi-port")
    add_files("examples/daisy_pod_kernel/src/*.cc")
    add_files("lib/umiport/board/daisy_seed/board/mcu_init.cc")
    add_includedirs(
        "lib/umiport/arch/cm7/",
        "lib/umiport/mcu/stm32h7/",
        "lib/umiport/board/daisy_seed/",
        "lib/umiport/platform/embedded/",
        "examples/daisy_pod_kernel/src/"          -- Pod固有オーバーレイ
    )
    set_linker_script("examples/daisy_pod_kernel/kernel.ld")
```

## コーデック対応

| 項目 | AK4556 (Rev4) | WM8731 (Rev5) | PCM3060 (Seed2 DFM) |
|------|----------------|----------------|----------------------|
| 制御 | リセットピンのみ | I2C (7レジスタ) | I2C |
| 初期化 | パルス1回 | I2Cで設定 | I2Cで設定 |
| フォーマット | 自動追従 | MSB Justified/I2S選択 | MSB Justified |
| ボリューム | なし | デジタル+アナログ | なし |

**方針**: `codec.hh` でコーデック抽象（Concept）を定義。
初期化時にボード版検出で切り替え。実行時はコーデック種類に非依存（SAIデータフォーマット共通）。

## リスクと注意点

1. **キャッシュ管理**: DMAバッファのキャッシュコヒーレンシは最大の落とし穴。
   SRAM_D2をnon-cacheable設定にするのが最も安全。
2. **VOS0 boost mode**: 電源シーケンスが厳密。SMPS→LDO切り替え順序に注意。
3. **QSPI実行**: XIP は初期化順序が重要。Phase 1では使わない。
4. **ボード版検出**: AK4556/WM8731/PCM3060の自動検出ロジックが必要
   （libDaisyではGPIO ADC読み取りで判定）。
5. **USB HS vs FS**: H750の USB OTG HS は F407 の OTG FS とレジスタ構成が異なる。
