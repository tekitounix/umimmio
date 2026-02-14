# PAL カテゴリ一覧

全 14 カテゴリの概要マトリクスと完全性チェックリスト。
各カテゴリの詳細と生成ヘッダのコード例は `categories/` を参照。

---

## 1. 全体マトリクス

| # | カテゴリ | 概要 | 所属レイヤ | 詳細 |
|---|---------|------|-----------|------|
| C1 | コアペリフェラルレジスタ (MMIO) | NVIC, SCB, SysTick 等 | L1–L2 | [C01](categories/C01_CORE_PERIPHERALS.md) |
| C2 | コア特殊レジスタ・命令イントリンシクス (非 MMIO) | MSR/MRS, CSR, バリア, 排他アクセス, DSP/SIMD | L1–L2 | [C02](categories/C02_CORE_INTRINSICS.md) |
| C3 | コアシステム定義 | コアタイプ, FPU/MPU/TrustZone 有無 | L2 | [C03](categories/C03_CORE_SYSTEM.md) |
| C4 | 割り込み / 例外ベクター | IRQ 番号, ハンドラ名, コア例外 | L1 + L3 | [C04](categories/C04_VECTORS.md) |
| C5 | メモリマップ | Flash, SRAM, CCM 等の base / size | L3–L4 | [C05](categories/C05_MEMORY_MAP.md) |
| C6 | ペリフェラルレジスタ | GPIO, UART, SPI, DMA 等のレジスタ定義 | L3 | [C06](categories/C06_PERIPHERAL_REGISTERS.md) |
| C7 | GPIO ピンマルチプレクシング | AF / FUNCSEL / GPIO Matrix マッピング | L3–L4 | [C07](categories/C07_GPIO_MUX.md) |
| C8 | クロックツリー | PLL, バスプリスケーラ, ペリフェラルイネーブル | L3 | [C08](categories/C08_CLOCK_TREE.md) |
| C9 | DMA マッピング | ストリーム/チャネル × ペリフェラルリクエスト | L3–L4 | [C09](categories/C09_DMA_MAPPING.md) |
| C10 | 電力管理 | 電源ドメイン, スリープモード, LP ペリフェラル | L3 | [C10](categories/C10_POWER.md) |
| C11 | セキュリティ / 保護 | TrustZone SAU/IDAU, Flash 保護 | L2–L3 | [C11](categories/C11_SECURITY.md) |
| C12 | デバイスメタデータ | パッケージ, 温度範囲, シリコンリビジョン | L4 | [C12](categories/C12_DEVICE_META.md) |
| C13 | リンカ / スタートアップ | メモリレイアウト, スタック初期化, C ランタイム | L1–L4 | [C13](categories/C13_LINKER_STARTUP.md) |
| C14 | デバッグ / トレース | SWD/JTAG, ITM, DWT, ETM | L1–L2 | [C14](categories/C14_DEBUG_TRACE.md) |

---

## 2. プラットフォーム対応マトリクス

各カテゴリが対象プラットフォームでどのような形で現れるかの概要。

| カテゴリ | STM32 | RP2040/RP2350 | ESP32-S3/P4 | i.MX RT |
|---------|-------|--------------|-------------|---------|
| C1 コアペリフェラル (MMIO) | Cortex-M NVIC/SCB/SysTick | M0+: 簡易NVIC / M33: フルNVIC / Hazard3: PLIC | Xtensa: Interrupt Matrix / RISC-V: CLIC | Cortex-M7 NVIC/SCB/SysTick |
| C2 コアイントリンシクス (非MMIO) | PRIMASK/BASEPRI, DSB/ISB, LDREX/STREX, SSAT/USAT, SIMD | PRIMASK (M0+) / BASEPRI+TZ (M33) / CSR (Hazard3) | rsr/wsr, MEMW, MAC16, PIE SIMD / CSR (P4) | PRIMASK/BASEPRI, DSB/ISB, LDREX/STREX, キャッシュ操作 |
| C3 コアシステム | M4F (FPU+MPU+DSP) | M0+ (基本) / M33 (TZ+FPU) / Hazard3 (PMP) | LX7 (dual) / RISC-V HP+LP | M7 (FPU+Cache+MPU) |
| C4 ベクター | NVIC 82–160 IRQ | NVIC 26/52 IRQ | Interrupt Matrix 99+ / CLIC 128+64 | NVIC 160 IRQ |
| C5 メモリ | Flash+SRAM+CCM | SRAM(6バンク)+XIP | SRAM+RTC SRAM+PSRAM | FlexRAM(動的)+QSPI |
| C6 ペリフェラル | MODER/AFR GPIO, USART, SPI | SIO+IO_BANK+PADS, PL011, PL022 | GPIO Matrix, UART, SPI | IOMUXC+GPIO, LPUART, LPSPI |
| C7 GPIO MUX | **AF テーブル** (AF0–15) | **3 層 FUNCSEL** (F1–F9) | **GPIO Matrix** (any-to-any) | **IOMUXC** (ALT0–7+Daisy) |
| C8 クロック | RCC (PLL→AHB/APB) | XOSC/PLL_SYS/PLL_USB | XTAL→PLL→個別分周 | CCM (ARM PLL→多段) |
| C9 DMA | DMA1/2 固定テーブル | 12ch DREQ | GDMA 動的 | eDMA+DMAMUX |
| C10 電力 | Sleep/Stop/Standby | Sleep/Dormant | Active/Light/Deep+ULP | Run/Wait/Stop+SNVS |
| C11 セキュリティ | RDP/WRP | (最小) / SecureBoot+TZ (RP2350) | Flash暗号化+SecureBoot+eFuse | HAB+BEE+SNVS |
| C12 メタ | 1,100+ バリアント | 少数バリアント | 少数バリアント | 数十バリアント |
| C13 リンカ | 標準 Cortex-M | Boot2 (XIP 初期化) | ROM bootloader | FlexRAM 構成依存 |
| C14 デバッグ | SWD+SWO (ITM/DWT) | SWD (各コア独立) | USB-Serial-JTAG / JTAG | SWD+ETM |

---

## 3. 完全性チェックリスト

### 3.1 必須 (MUST) — これがなければビルドできない

| # | 項目 | 対応カテゴリ |
|---|------|------------|
| M1 | コアシステム定義 (アーキテクチャ, FPU, MPU 有無等) | C3 |
| M2 | メモリマップ (Flash/SRAM ベースアドレスとサイズ) | C5 |
| M3 | 割り込みベクターテーブル (IRQ 番号 + ハンドラ名) | C4 |
| M4 | リンカスクリプト用メモリリージョン | C13 |
| M5 | スタートアップコード (ベクターテーブル配置、C ランタイム初期化) | C13 |
| M6 | 割り込み制御イントリンシクス (CPSID/CPSIE, PRIMASK) | C2 |
| M7 | メモリバリア命令 (DSB/ISB/DMB) | C2 |
| M8 | スタックポインタアクセス (MSP/PSP) | C2 |

### 3.2 重要 (SHOULD) — ペリフェラル利用に必要

| # | 項目 | 対応カテゴリ |
|---|------|------------|
| S1 | ペリフェラルレジスタ定義 (SVD ベース) | C6 |
| S2 | コアペリフェラルレジスタ (NVIC, SCB, SysTick) | C1 |
| S3 | GPIO ピンマルチプレクシング | C7 |
| S4 | クロックツリー (ペリフェラルイネーブル含む) | C8 |
| S5 | DMA マッピング | C9 |
| S6 | 排他アクセス命令 (LDREX/STREX, LR/SC) | C2 |
| S7 | パワー管理命令 (WFI/WFE/SEV) | C2 |
| S8 | BASEPRI / FAULTMASK (優先度ベース割り込み制御) | C2 |

### 3.3 推奨 (MAY) — 高度な機能に必要

| # | 項目 | 対応カテゴリ |
|---|------|------------|
| O1 | 電力管理定義 | C10 |
| O2 | セキュリティ / 保護 | C11 |
| O3 | デバッグ / トレース (DWT, ITM) | C14 |
| O4 | デバイスメタデータ (パッケージ, 温度範囲) | C12 |
| O5 | ベンダー固有カテゴリ (PIO, GPIO Matrix, IOMUXC 等) | 各カテゴリ §5 |
| O6 | DSP/SIMD 命令イントリンシクス | C2 |
| O7 | TrustZone 命令 (TT, BXNS, SG) | C2 |
| O8 | キャッシュ操作命令 | C2 |

---

## 4. 段階的拡大ロードマップ

### Phase 1: 最小実行可能 (LED 点滅)

```
M1–M8 + S1 (GPIO のみ) + S2 (NVIC, SysTick)
```

GPIO 出力 + SysTick 割り込みで LED 点滅が可能な最小セット。
割り込み制御 (C2) とメモリバリア (C2) はスタートアップコードに必須。

### Phase 2: UART / SPI / I2C

```
Phase 1 + S1 (全ペリフェラル) + S3 (GPIO AF) + S4 (クロック) + S5 (DMA) + S6–S8
```

通信ペリフェラルの完全利用が可能。
排他アクセス (S6) は RTOS 環境での mutex/セマフォに必要。

### Phase 3: プロダクション品質

```
Phase 2 + O1–O8 + ベンダー固有カテゴリ
```

低消費電力、セキュリティ、デバッグ、DSP/SIMD 含む完全サポート。
