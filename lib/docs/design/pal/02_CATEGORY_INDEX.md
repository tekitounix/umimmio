# PAL カテゴリ一覧

全 13 カテゴリの概要マトリクスと完全性チェックリスト。
各カテゴリの詳細と生成ヘッダのコード例は `categories/` を参照。

---

## 1. 全体マトリクス

| # | カテゴリ | 概要 | 所属レイヤ | 詳細 |
|---|---------|------|-----------|------|
| C1 | コアペリフェラルレジスタ | NVIC, SCB, SysTick 等 | L1–L2 | [C01](categories/C01_CORE_PERIPHERALS.md) |
| C2 | コアシステム定義 | コアタイプ, FPU/MPU/TrustZone 有無 | L2 | [C02](categories/C02_CORE_SYSTEM.md) |
| C3 | 割り込み / 例外ベクター | IRQ 番号, ハンドラ名, コア例外 | L1 + L3 | [C03](categories/C03_VECTORS.md) |
| C4 | メモリマップ | Flash, SRAM, CCM 等の base / size | L3–L4 | [C04](categories/C04_MEMORY_MAP.md) |
| C5 | ペリフェラルレジスタ | GPIO, UART, SPI, DMA 等のレジスタ定義 | L3 | [C05](categories/C05_PERIPHERAL_REGISTERS.md) |
| C6 | GPIO ピンマルチプレクシング | AF / FUNCSEL / GPIO Matrix マッピング | L3–L4 | [C06](categories/C06_GPIO_MUX.md) |
| C7 | クロックツリー | PLL, バスプリスケーラ, ペリフェラルイネーブル | L3 | [C07](categories/C07_CLOCK_TREE.md) |
| C8 | DMA マッピング | ストリーム/チャネル × ペリフェラルリクエスト | L3–L4 | [C08](categories/C08_DMA_MAPPING.md) |
| C9 | 電力管理 | 電源ドメイン, スリープモード, LP ペリフェラル | L3 | [C09](categories/C09_POWER.md) |
| C10 | セキュリティ / 保護 | TrustZone SAU/IDAU, Flash 保護 | L2–L3 | [C10](categories/C10_SECURITY.md) |
| C11 | デバイスメタデータ | パッケージ, 温度範囲, シリコンリビジョン | L4 | [C11](categories/C11_DEVICE_META.md) |
| C12 | リンカ / スタートアップ | メモリレイアウト, スタック初期化, C ランタイム | L1–L4 | [C12](categories/C12_LINKER_STARTUP.md) |
| C13 | デバッグ / トレース | SWD/JTAG, ITM, DWT, ETM | L1–L2 | [C13](categories/C13_DEBUG_TRACE.md) |

---

## 2. プラットフォーム対応マトリクス

各カテゴリが対象プラットフォームでどのような形で現れるかの概要。

| カテゴリ | STM32 | RP2040/RP2350 | ESP32-S3/P4 | i.MX RT |
|---------|-------|--------------|-------------|---------|
| C1 コアペリフェラル | Cortex-M NVIC/SCB/SysTick | M0+: 簡易NVIC / M33: フルNVIC / Hazard3: CSR+PLIC | Xtensa: Special Reg + Interrupt Matrix / RISC-V: CSR+CLIC | Cortex-M7 NVIC/SCB/SysTick |
| C2 コアシステム | M4F (FPU+MPU+DSP) | M0+ (基本) / M33 (TZ+FPU) / Hazard3 (PMP) | LX7 (dual) / RISC-V HP+LP | M7 (FPU+Cache+MPU) |
| C3 ベクター | NVIC 82–160 IRQ | NVIC 26/52 IRQ | Interrupt Matrix 99+ / CLIC 128+64 | NVIC 160 IRQ |
| C4 メモリ | Flash+SRAM+CCM | SRAM(6バンク)+XIP | SRAM+RTC SRAM+PSRAM | FlexRAM(動的)+QSPI |
| C5 ペリフェラル | MODER/AFR GPIO, USART, SPI | SIO+IO_BANK+PADS, PL011, PL022 | GPIO Matrix, UART, SPI | IOMUXC+GPIO, LPUART, LPSPI |
| C6 GPIO MUX | **AF テーブル** (AF0–15) | **3 層 FUNCSEL** (F1–F9) | **GPIO Matrix** (any-to-any) | **IOMUXC** (ALT0–7+Daisy) |
| C7 クロック | RCC (PLL→AHB/APB) | XOSC/PLL_SYS/PLL_USB | XTAL→PLL→個別分周 | CCM (ARM PLL→多段) |
| C8 DMA | DMA1/2 固定テーブル | 12ch DREQ | GDMA 動的 | eDMA+DMAMUX |
| C9 電力 | Sleep/Stop/Standby | Sleep/Dormant | Active/Light/Deep+ULP | Run/Wait/Stop+SNVS |
| C10 セキュリティ | RDP/WRP | (最小) / SecureBoot+TZ (RP2350) | Flash暗号化+SecureBoot+eFuse | HAB+BEE+SNVS |
| C11 メタ | 1,100+ バリアント | 少数バリアント | 少数バリアント | 数十バリアント |
| C12 リンカ | 標準 Cortex-M | Boot2 (XIP 初期化) | ROM bootloader | FlexRAM 構成依存 |
| C13 デバッグ | SWD+SWO (ITM/DWT) | SWD (各コア独立) | USB-Serial-JTAG / JTAG | SWD+ETM |

---

## 3. 完全性チェックリスト

### 3.1 必須 (MUST) — これがなければビルドできない

| # | 項目 | 対応カテゴリ |
|---|------|------------|
| M1 | コアシステム定義 (アーキテクチャ, FPU, MPU 有無等) | C2 |
| M2 | メモリマップ (Flash/SRAM ベースアドレスとサイズ) | C4 |
| M3 | 割り込みベクターテーブル (IRQ 番号 + ハンドラ名) | C3 |
| M4 | リンカスクリプト用メモリリージョン | C12 |
| M5 | スタートアップコード (ベクターテーブル配置、C ランタイム初期化) | C12 |

### 3.2 重要 (SHOULD) — ペリフェラル利用に必要

| # | 項目 | 対応カテゴリ |
|---|------|------------|
| S1 | ペリフェラルレジスタ定義 (SVD ベース) | C5 |
| S2 | コアペリフェラルレジスタ (NVIC, SCB, SysTick) | C1 |
| S3 | GPIO ピンマルチプレクシング | C6 |
| S4 | クロックツリー (ペリフェラルイネーブル含む) | C7 |
| S5 | DMA マッピング | C8 |

### 3.3 推奨 (MAY) — 高度な機能に必要

| # | 項目 | 対応カテゴリ |
|---|------|------------|
| O1 | 電力管理定義 | C9 |
| O2 | セキュリティ / 保護 | C10 |
| O3 | デバッグ / トレース (DWT, ITM) | C13 |
| O4 | デバイスメタデータ (パッケージ, 温度範囲) | C11 |
| O5 | ベンダー固有カテゴリ (PIO, GPIO Matrix, IOMUXC 等) | 各カテゴリ §5 |

---

## 4. 段階的拡大ロードマップ

### Phase 1: 最小実行可能 (LED 点滅)

```
M1 + M2 + M3 + M4 + M5 + S1 (GPIO のみ) + S2 (NVIC, SysTick)
```

GPIO 出力 + SysTick 割り込みで LED 点滅が可能な最小セット。

### Phase 2: UART / SPI / I2C

```
Phase 1 + S1 (全ペリフェラル) + S3 (GPIO AF) + S4 (クロック) + S5 (DMA)
```

通信ペリフェラルの完全利用が可能。

### Phase 3: プロダクション品質

```
Phase 2 + O1–O5 + ベンダー固有カテゴリ
```

低消費電力、セキュリティ、デバッグ含む完全サポート。
