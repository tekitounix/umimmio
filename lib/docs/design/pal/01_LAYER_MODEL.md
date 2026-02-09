# PAL 4 層モデル — ハードウェア定義のスコープ分類

---

## 1. 概要

ハードウェア定義は、その適用範囲（スコープ）に基づいて 4 層に分類できる。
この分類は PAL のディレクトリ構造、コード生成の粒度、データソースの選択に直結する。

```
┌─────────────────────────────────────────────────────────────┐
│ Layer 1: アーキテクチャ共通 (Architecture-Universal)         │
│   全 Cortex-M / 全 RISC-V / 全 Xtensa に共通               │
├─────────────────────────────────────────────────────────────┤
│ Layer 2: コアプロファイル固有 (Core-Profile-Specific)        │
│   Cortex-M4F / Cortex-M33 / RISC-V RV32IMAC 等に固有       │
├─────────────────────────────────────────────────────────────┤
│ Layer 3: MCU ファミリ固有 (MCU-Family-Specific)             │
│   STM32F4xx / RP2040 / ESP32-S3 等のシリーズ全体に共通      │
├─────────────────────────────────────────────────────────────┤
│ Layer 4: デバイスバリアント固有 (Device-Variant-Specific)    │
│   STM32F407VGT6 / RP2040-B2 等の個別チップ固有              │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. 各レイヤの定義

### 2.1 Layer 1: アーキテクチャ共通

ISA アーキテクチャの仕様で定義される、全チップ共通の定義。

| アーキテクチャ | 含まれる定義 | 例 |
|-------------|------------|-----|
| ARM Cortex-M | コア例外テーブル構造、NVIC/SCB/SysTick レジスタ | Reset, NMI, HardFault, SysTick_Handler |
| RISC-V | 標準 CSR、mtime/mtimecmp | mstatus, mie, mtvec, mepc |
| Xtensa | Special Registers、Window Registers | PS, EPC, CCOUNT |

**特性**: ARM Architecture Reference Manual / RISC-V ISA Manual で定義。変更されない。
**生成戦略**: 手書きまたは AI 一括生成で全チップ共通化。

### 2.2 Layer 2: コアプロファイル固有

同一 ISA 内でのコアバリエーションによる差異。

| コア | L1 からの差分 | 例 |
|------|--------------|-----|
| Cortex-M0/M0+ | MPU なし、FPU なし、DWT 簡易版 | RP2040 |
| Cortex-M4F | MPU あり、FPU (FPv4-SP)、DSP 命令 | STM32F4 |
| Cortex-M7 | I/D キャッシュ、FPU (FPv5-DP) | i.MX RT1060 |
| Cortex-M33 | TrustZone (SAU)、MPU v8、FPU | RP2350 (ARM mode) |
| Hazard3 RISC-V | PMP、カスタム CSR | RP2350 (RISC-V mode) |
| Xtensa LX7 | レジスタウィンドウ、Interrupt Matrix | ESP32-S3 |
| RISC-V (ESP32-P4) | CLIC、PMP | ESP32-P4 HP/LP cores |

**特性**: コアプロファイルの TRM (Technical Reference Manual) で定義。
**生成戦略**: コアプロファイルごとに 1 セットのヘッダを生成。

### 2.3 Layer 3: MCU ファミリ固有

ベンダーが実装したペリフェラルの構造。同一ファミリ内の全チップで共通。

| ファミリ | 含まれる定義 |
|---------|------------|
| STM32F4xx | GPIO (MODER/AFR 方式), USART, SPI, I2C, TIM, DMA, RCC のレジスタ構造 |
| RP2040 | SIO, IO_BANK0, PADS_BANK0, PIO, UART (PL011), SPI (PL022), Timer |
| ESP32-S3 | GPIO Matrix, UART, SPI, I2C, Timer Group, GDMA, RTC |
| i.MX RT1060 | IOMUXC, LPUART, LPSPI, LPI2C, GPT, eDMA, CCM |

**特性**: SVD ファイルの主要カバー範囲。ペリフェラルの IP version で区別。
**生成戦略**: SVD + パッチ → umimmio 型ベースのコード生成。

### 2.4 Layer 4: デバイスバリアント固有

同一ファミリ内でも型番により異なるパラメータ。

| パラメータ | バリアントでの差異例 |
|----------|-------------------|
| Flash サイズ | STM32F407VG: 1MB, STM32F407VE: 512KB |
| SRAM サイズ | STM32F407VG: 192KB, STM32F405RG: 192KB |
| ペリフェラル有無 | STM32F405 には Ethernet なし, F407 にはあり |
| GPIO AF テーブル | パッケージ (LQFP100 vs LQFP144) でピン数とAF が変化 |
| パッケージ | LQFP100, LQFP144, WLCSP, BGA |
| 温度範囲 | Commercial (-40~85°C), Industrial (-40~105°C) |

**特性**: データシート、CubeMX DB、CMSIS-Pack で取得。
**生成戦略**: デバイスメタデータ DB から条件分岐またはバリアント別ヘッダ生成。

---

## 3. レイヤとカテゴリの対応

| カテゴリ | L1 | L2 | L3 | L4 |
|---------|----|----|----|----|
| C1 コアペリフェラルレジスタ | NVIC, SCB, SysTick | MPU, FPU, DWT, ITM, SAU | — | — |
| C2 コアシステム定義 | — | FPU/MPU/DSP/TZ 有無 | — | NVIC 優先度ビット数 |
| C3 割り込みベクター | コア例外 | — | デバイス固有 IRQ | テーブルサイズ |
| C4 メモリマップ | — | — | メモリ種類 | base/size |
| C5 ペリフェラルレジスタ | — | — | レジスタ構造 | インスタンス有無 |
| C6 GPIO ピンマルチプレクシング | — | — | MUX 方式 | AF テーブル |
| C7 クロックツリー | — | — | PLL/バス構造 | 最大周波数 |
| C8 DMA マッピング | — | — | DMA モデル | チャネル割当 |
| C9 電力管理 | — | — | スリープモード | ドメイン構成 |
| C10 セキュリティ | — | SAU/PMP | Flash 保護 | OTP/eFuse |
| C11 デバイスメタデータ | — | — | — | 全項目 |
| C12 リンカ / スタートアップ | C ランタイム初期化 | セクション配置 | Boot 方式 | メモリサイズ |
| C13 デバッグ / トレース | CoreDebug | ITM, DWT, ETM | デバッグI/F | — |

---

## 4. ディレクトリ構造への反映

```
pal/
├── arch/                    # L1: アーキテクチャ共通
│   ├── arm/cortex_m/        # Cortex-M 共通 (NVIC, SCB, SysTick, C ランタイム)
│   ├── riscv/               # RISC-V 共通 (CSR, mtime)
│   └── xtensa/              # Xtensa 共通
├── core/                    # L2: コアプロファイル固有
│   ├── cortex_m4f/          # Cortex-M4F (MPU, FPU, DWT, ITM)
│   ├── cortex_m7/           # Cortex-M7 (Cache, FPv5-DP)
│   ├── cortex_m33/          # Cortex-M33 (SAU, TrustZone)
│   └── hazard3/             # RP2350 RISC-V (PMP, カスタム CSR)
├── mcu/                     # L3: MCU ファミリ固有
│   ├── stm32f4/             # STM32F4xx ペリフェラルレジスタ
│   ├── rp2040/              # RP2040 ペリフェラル
│   ├── esp32s3/             # ESP32-S3 ペリフェラル
│   └── imxrt1060/           # i.MX RT1060 ペリフェラル
└── device/                  # L4: デバイスバリアント固有
    ├── stm32f407vg/          # メモリサイズ, AF テーブル, IRQ テーブル
    ├── rp2040/               # (RP2040 はバリアント差異が少ない)
    └── esp32s3/              # ESP32-S3 バリアント
```

この構造は生成されるヘッダファイルのディレクトリに対応する。
ビルドシステムはターゲットデバイスに基づいて適切なレイヤのヘッダを選択する。
