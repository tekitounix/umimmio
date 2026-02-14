# MCU ファミリー間の設定差異 --- 詳細調査

**分類:** ボード設定スキーマ調査 (MCU ファミリー間ハードウェア差異)
**概要:** ボード設定スキーマの設計にあたり、対象 MCU ファミリーのハードウェア差異を包括的に調査する。クロック、メモリ、DMA、割り込み、デバッグ、ブートの各側面で、ファミリー間の相違点がボード定義フォーマットにどう影響するかを分析する。

---

## 1. MCU ファミリー別詳細

### 1.1 STM32F4 (Cortex-M4F)

UMI の初期ターゲット。中堅性能の Cortex-M4F。

**クロック:**
- PLL: 1 基 (メイン PLL のみ)
- パラメータ: PLLM (2-63), PLLN (50-432), PLLP (2,4,6,8), PLLQ (2-15)
- ソース: HSE (外部水晶 4-26MHz), HSI (内蔵 16MHz)
- 最大周波数: 168 MHz (F407), 180 MHz (F446)
- バスプリスケーラ: AHB (1-512), APB1 (1-16, max 42MHz), APB2 (1-16, max 84MHz)

**メモリ:**
- Flash: 0x0800_0000 (256K-2M)
- SRAM1: 0x2000_0000 (最大 112K)
- SRAM2: 0x2001_C000 (16K, 一部品番)
- **CCM RAM: 0x1000_0000 (64K)** --- DMA アクセス不可、CPU 専用高速 RAM
- Backup SRAM: 0x4002_4000 (4K)

**DMA:**
- DMA1 + DMA2: 各 8 ストリーム、計 16 ストリーム
- 集中型コントローラ (任意ペリフェラル → 任意メモリ)
- DMA リクエストはチャネルセレクタで制御
- CCM RAM は DMA 不可 (AHB バスに接続されていない)

**ブート:**
- BOOT0 ピン + BOOT1 ピンで起動モード選択
  - BOOT0=0: Flash 起動 (0x0800_0000)
  - BOOT0=1, BOOT1=0: System Memory (内蔵ブートローダー)
  - BOOT0=1, BOOT1=1: SRAM 起動 (0x2000_0000)

**デバッグ:** SWD (2 ピン) + JTAG (5 ピン)、SWO トレース出力

---

### 1.2 STM32H7 (Cortex-M7F)

高性能 ARM Cortex-M7F。オーディオ向けの豊富なペリフェラルと複雑なクロック/メモリアーキテクチャ。

**クロック:**
- **PLL: 3 基** (PLL1, PLL2, PLL3)
  - 全 PLL が Fractional-N 対応 (小数分周で高精度周波数生成)
  - PLL1: SYSCLK (max 480MHz)
  - PLL2: ADC, 汎用
  - PLL3: I2S/SAI, USB, SPI, UART
- PLL パラメータ: DIVM (1-63), DIVN (4-512), DIVP/Q/R (1-128)
- 分数分周器: FRACN (0-8191) で PLL 出力を微調整可能
- VOS (Voltage Output Scaling): VOS0-VOS3 で電圧スケーリング、最大周波数に影響
  - VOS0 + ブーストモード → 480 MHz
  - VOS1 → 400 MHz
  - VOS3 → 200 MHz

**電源:**
- **SMPS + LDO のデュアル電源レギュレータ**
  - SMPS → LDO カスケード (高効率)
  - LDO のみ (低ノイズ、オーディオ向き)
  - SMPS のみ (最高効率)
  - Direct SMPS (バイパス)
- 電源モード設定がボード回路に依存 → ボード設定で必須

**メモリ (7+ リージョン):**
- ITCM: 0x0000_0000 (64K) --- 命令 TCM (ゼロウェイト)
- DTCM: 0x2000_0000 (128K) --- データ TCM (ゼロウェイト、DMA 不可)
- AXI SRAM: 0x2400_0000 (512K) --- 汎用、DMA 可
- SRAM1: 0x3000_0000 (128K) --- D2 ドメイン
- SRAM2: 0x3002_0000 (128K) --- D2 ドメイン
- SRAM3: 0x3004_0000 (32K) --- D2 ドメイン
- SRAM4: 0x3800_0000 (64K) --- D3 ドメイン (低電力保持可能)
- Backup SRAM: 0x3880_0000 (4K)
- Flash: 0x0800_0000 (1-2M)

**キャッシュ:**
- I-Cache (16KB) + D-Cache (16KB)
- DMA バッファの D-Cache コヒーレンシ問題 → `SCB_CleanDCache_by_Addr()` が必要
- キャッシュ無効領域を MPU で設定する必要がある (DMA バッファ用)

**DMA (4 コントローラ):**
- DMA1, DMA2: 従来型 (D2 ドメイン)
- BDMA: Basic DMA (D3 ドメイン、低電力)
- MDMA: Master DMA (メモリ間転送、ドメイン横断)
- ドメイン制約: DMA1/DMA2 は D2 ドメインの SRAM のみアクセス可能

**デュアルコアオプション (H745/H747/H755/H757):**
- Cortex-M7 (D1 ドメイン) + Cortex-M4 (D2 ドメイン)
- 各コアに独立したフラッシュバンク
- HSEM (ハードウェアセマフォ) によるコア間同期
- 共有 SRAM (SRAM1-3) でデータ交換

**デバッグ:** SWD + JTAG + ETM (Embedded Trace Macrocell) + SWO

---

### 1.3 STM32U5 (Cortex-M33)

TrustZone 対応の超低消費電力 Cortex-M33。セキュリティ機能がメモリマップに大きく影響。

**クロック:**
- PLL: 3 基 (PLL1, PLL2, PLL3)
- **HSI 廃止**: MSIS (Multi-Speed Internal) + MSIK (Kernel clock) に置換
  - MSIS: 100kHz - 48MHz (12 段階)
  - MSIK: 100kHz - 48MHz (12 段階、ペリフェラル独立クロック)
- HSE: 外部水晶 (4-48MHz)
- 最大周波数: 160 MHz

**TrustZone メモリ分割:**
- **SAU (Security Attribution Unit):** アドレス空間を Secure / Non-Secure に分割
- **GTZC (Global TrustZone Controller):** ペリフェラルのセキュリティ属性制御
  - TZSC: ペリフェラル単位のセキュア/非セキュア指定
  - MPCBB: SRAM のブロック単位 (256B) セキュリティ指定
- **メモリマップが Secure / Non-Secure で二重化:**
  - Secure Flash: 0x0C00_0000
  - Non-Secure Flash: 0x0800_0000
  - Secure SRAM: 0x3000_0000
  - Non-Secure SRAM: 0x2000_0000

```
┌─────────────────────────────────────────┐
│              Non-Secure World           │
│  Flash: 0x0800_0000  SRAM: 0x2000_0000 │
├─────────────────────────────────────────┤
│              Secure World               │
│  Flash: 0x0C00_0000  SRAM: 0x3000_0000 │
├─────────────────────────────────────────┤
│         Non-Secure Callable (NSC)       │
│         (Secure → NS ゲートウェイ)       │
└─────────────────────────────────────────┘
```

**DMA:**
- **GPDMA (General Purpose DMA):** DMA1/DMA2 を統合した新世代 DMA
  - 16 チャネル (リンクドリスト対応)
  - TrustZone 対応: チャネル単位で Secure/Non-Secure 指定

**デバッグ:** SWD + JTAG + SWO。TrustZone デバッグには DAP セキュリティ設定が必要。

---

### 1.4 nRF52840 (Cortex-M4F)

Nordic Semiconductor の BLE SoC。PLL なし、SoftDevice による独自のメモリモデル。

**クロック:**
- **PLL なし** (STM32 と根本的に異なる)
- HFCLK: 64 MHz (内部 RC または外部 32MHz 水晶から分周)
  - HFXO: 32 MHz 外部水晶 → 内部で 2 倍 = 64 MHz
  - HFRC: 64 MHz 内部 RC (精度低い)
- LFCLK: 32.768 kHz (3 ソース選択)
  - LFXO: 32.768 kHz 外部水晶 (高精度)
  - LFRC: 32.768 kHz 内部 RC
  - LFSYNTH: HFCLK から合成
- **CPU 周波数は固定 64 MHz** (変更不可)

**SoftDevice メモリ予約:**
- SoftDevice (BLE プロトコルスタック) がフラッシュと RAM の先頭を占有
- アプリケーションは SoftDevice の後ろから使用する

```
Flash (1 MB):
  0x00000000 ┌─────────────────┐
             │   MBR (4 KB)    │  Master Boot Record
  0x00001000 ├─────────────────┤
             │  SoftDevice      │  BLE スタック (~152 KB, S140)
  0x00027000 ├─────────────────┤
             │  Application     │  ← アプリはここから
  0x000F4000 ├─────────────────┤
             │  Bootloader      │  (OTA 時)
  0x00100000 └─────────────────┘

RAM (256 KB):
  0x20000000 ┌─────────────────┐
             │  SoftDevice RAM  │  (~8-40 KB, 設定依存)
  0x20002000 ├─────────────────┤ (例: 8KB 時)
             │  Application RAM │
  0x20040000 └─────────────────┘
```

**SoftDevice RAM サイズは BLE 設定に依存:**
- 接続数、MTU サイズ、GATT テーブルサイズで変動
- リンカスクリプトの RAM 開始アドレスを動的に調整する必要がある

**DMA:**
- **EasyDMA (ペリフェラル内蔵):** 集中型 DMA コントローラなし
  - 各ペリフェラル (SPI, I2S, UART 等) に専用の簡易 DMA が内蔵
  - ポインタ + 長さのみ設定 (チャネル/ストリーム設定不要)
  - RAM のみアクセス可能 (Flash 直接読出しは不可)

**ブート:** BOOT0/1 ピンなし。UICR (User Information Configuration Registers) で起動設定。

**デバッグ:** SWD のみ (JTAG なし)

**Flash:** 0x0000_0000 (STM32 の 0x0800_0000 とは異なる)

---

### 1.5 RP2040 (Dual Cortex-M0+)

Raspberry Pi 製デュアルコア Cortex-M0+。内蔵フラッシュなし、PIO が特徴的。

**クロック:**
- **PLL: 2 基** (SYS PLL + USB PLL)
  - SYS PLL: 通常 125 MHz (最大 133 MHz)
  - USB PLL: 48 MHz (USB 要件)
- PLL パラメータ: REFDIV, FBDIV (16-320), POSTDIV1 (1-7), POSTDIV2 (1-7)
- リファレンスクロック: 外部 12 MHz 水晶 (XOSC)
- ROSC: 内部リングオシレータ (周波数不定、低電力用)

**メモリ (内蔵フラッシュなし):**
- **外部 QSPI Flash (XIP):** 0x1000_0000 (ボード実装依存、通常 2-16MB)
  - Execute-In-Place: フラッシュから直接コード実行
  - XIP キャッシュ (16KB) で高速化
- SRAM: 0x2000_0000 (264K、6 バンクにストライプ配置)
  - Bank0-3: 各 64KB (ストライプ: 両コアからの同時アクセスを高速化)
  - Bank4: 4KB
  - Bank5: 4KB
- ROM: 0x0000_0000 (16KB、ブートローダー + ライブラリ)

**boot2 (第 2 段階ブートローダー):**
- ROM ブートローダーが外部フラッシュの先頭 256 バイトを SRAM にロード
- boot2 が QSPI フラッシュの初期化 (速度、モード設定) を実行
- boot2 はフラッシュチップ依存 → **ボード設定で boot2 を指定する必要がある**

```c
// boot2 の例 (W25Q080 用)
// pico-sdk/src/rp2_common/boot_stage2/boot2_w25q080.S
// フラッシュを QPI モードに設定し、XIP を有効化
```

**PIO (Programmable I/O):**
- 2 基の PIO ブロック (各 4 ステートマシン)
- 任意のカスタムプロトコルをハードウェアで実装可能
- I2S, WS2812, VGA 信号等を PIO で生成
- ボード設定でピンアサインを指定する必要がある

**DMA:** 12 チャネルの DMA コントローラ。全 SRAM バンクにアクセス可能。

**デバッグ:** SWD のみ (Core 0 + Core 1 をマルチドロップで接続)

---

### 1.6 ESP32-C3 (RISC-V)

Espressif の RISC-V シングルコア。WiFi + BLE 対応の低コスト SoC。

**クロック:**
- **PLL の概念が ARM と異なる:** CPU 周波数のみ設定 (80/160 MHz)
- PLL は内部的に存在するが、ユーザーが直接パラメータを設定しない
- XTAL: 40 MHz (固定)
- RC_FAST: ~17.5 MHz (低精度)
- 周波数設定: `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ` で選択するだけ

**メモリ:**
- 内蔵 SRAM: 400 KB (データ + 命令共有)
- 外部フラッシュ: SPI Flash (4-16 MB、ボード実装依存)
- RTC Memory: 8 KB (Deep Sleep 保持)
- パーティションテーブルで Flash レイアウトを管理

**GPIO Matrix:**
- 22 GPIO ピン
- ほぼ全てのペリフェラル信号を任意 GPIO にルーティング可能
- IO MUX: 高速固定ルーティング (一部ペリフェラル用)
- GPIO Matrix: 低速自由ルーティング (汎用)

**割り込み:**
- **RISC-V 割り込みコントローラ (NVIC ではない)**
- PLIC (Platform-Level Interrupt Controller) ベース
- 31 外部割り込みソース
- Vectored / Non-vectored モード選択可能

**ブート:**
- ROM ブートローダー → 2nd ブートローダー (パーティションから) → アプリケーション
- Secure Boot v2 対応
- GPIO ストラッピングピンでブートモード選択

**デバッグ:**
- **内蔵 USB-JTAG コントローラ** (外部プローブ不要)
- USB シリアル + USB JTAG が 1 本の USB-C で利用可能
- OpenOCD 対応

---

### 1.7 ESP32-S3 (Xtensa Dual-Core)

Espressif のデュアル Xtensa LX7 コア。AI/ML 向け拡張命令と PSRAM 対応。

**クロック:**
- CPU 周波数: 80/160/240 MHz (設定で選択)
- PLL: 内部 (パラメータはドライバが自動管理)
- XTAL: 40 MHz

**メモリ:**
- 内蔵 SRAM: 512 KB
- **PSRAM (外付け):** 最大 8 MB (OPI/QPI)
  - OPI PSRAM: Octal SPI (高帯域)
  - QPI PSRAM: Quad SPI
  - `CONFIG_SPIRAM=y` + `CONFIG_SPIRAM_MODE_OCT=y` で有効化
  - ボード設定で PSRAM 搭載有無と種類を指定
- RTC Memory: 8 KB

**デュアルコア:**
- Xtensa LX7 x 2 (PRO_CPU + APP_CPU)
- `xTaskCreatePinnedToCore()` でコアアフィニティ指定
- `CONFIG_FREERTOS_UNICORE=y` でシングルコア動作も可能

**ULP (Ultra-Low-Power) コプロセッサ:**
- RISC-V ベースの ULP コプロセッサ
- Deep Sleep 中もセンサ読取り等が可能
- ULP プログラムは専用メモリに配置

**割り込み:** Xtensa 割り込みアーキテクチャ (NVIC でも PLIC でもない)。7 段階の割り込み優先度。NMI サポート。

**デバッグ:** 内蔵 USB-JTAG + USB CDC (ESP32-C3 と同様)

---

### 1.8 GD32VF103 (RISC-V)

GigaDevice の RISC-V MCU。STM32F1 互換ペリフェラルだが RISC-V コア。

**クロック:**
- PLL: 1 基 (STM32F1 と同等)
- ソース: HXTAL (外部 4-32MHz), IRC8M (内蔵 8MHz)
- 最大周波数: 108 MHz
- バスプリスケーラ: AHB, APB1 (max 54MHz), APB2 (max 108MHz)
- **名称が STM32F1 と異なる:** HSE→HXTAL, HSI→IRC8M, LSE→LXTAL, LSI→IRC40K

**メモリ:**
- Flash: 0x0800_0000 (128K) --- STM32F1 と同一アドレス
- SRAM: 0x2000_0000 (32K)
- Boot ROM: 0x1FFF_B000 (内蔵ブートローダー)

**ペリフェラル:**
- STM32F103 と互換のペリフェラルセット (USART, SPI, I2C, ADC, TIM 等)
- レジスタアドレスも STM32F1 互換
- **ただしペリフェラルインデックスが 0 始まり:**
  - STM32: USART1, USART2, USART3
  - GD32VF: USART0, USART1, USART2

**割り込み:**
- **ECLIC (Enhanced Core Local Interrupt Controller)** --- NVIC ではない
  - RISC-V 標準の CLIC を拡張
  - Vectored / Non-vectored モード
  - 86 外部割り込みソース
  - 16 段階の優先度

**デバッグ:** JTAG のみ (SWD 非対応)。OpenOCD + RISC-V デバッグモジュール。

---

## 2. ファミリー間比較表

| 項目 | STM32F4 | STM32H7 | STM32U5 | nRF52840 | RP2040 | ESP32-C3 | ESP32-S3 | GD32VF103 |
|------|---------|---------|---------|----------|--------|----------|----------|-----------|
| **Arch** | Cortex-M4F | Cortex-M7F | Cortex-M33 | Cortex-M4F | Dual M0+ | RISC-V | Dual Xtensa | RISC-V |
| **PLL 数** | 1 | 3 (Frac-N) | 3 | 0 | 2 | 内部自動 | 内部自動 | 1 |
| **Max MHz** | 168-180 | 480 | 160 | 64 (固定) | 133 | 160 | 240 | 108 |
| **Flash base** | 0x0800_0000 | 0x0800_0000 | 0x0800_0000 | 0x0000_0000 | 外部XIP | 外部SPI | 外部SPI | 0x0800_0000 |
| **RAM regions** | 3 (SRAM+CCM+BKP) | 7+ (TCM+AXI+SRAM*4+BKP) | 4+ (S/NS分割) | 1 | 6 banks | 1 | 1+PSRAM | 1 |
| **DMA model** | 集中型x2 | 集中型x2+BDMA+MDMA | GPDMA | EasyDMA | 12ch | GDMA | GDMA | 集中型x2 |
| **IRQ ctrl** | NVIC | NVIC | NVIC | NVIC | NVIC | PLIC | Xtensa | ECLIC |
| **Debug** | SWD+JTAG | SWD+JTAG+ETM | SWD+JTAG | SWD only | SWD only | USB-JTAG | USB-JTAG | JTAG only |
| **TrustZone** | No | No | Yes | No | No | No | No | No |
| **Dual-core** | No | Option | No | No | Yes | No | Yes | No |
| **BLE** | No | No | No | Yes | No | Yes | Yes | No |
| **WiFi** | No | No | No | No | No | Yes | Yes | No |
| **PSRAM** | No | No | No | No | No | No | Yes | No |
| **Boot pins** | BOOT0+BOOT1 | BOOT0 | BOOT0 | UICR | -- | GPIO strap | GPIO strap | BOOT0 |

---

## 3. ボード定義スキーマフィールド分類

### 3.1 ユニバーサルフィールド (全 MCU 共通)

どの MCU ファミリーでも必須のフィールド。

| フィールド | 型 | 説明 | 例 |
|-----------|-----|------|---|
| `board.name` | string | ボード識別子 | `"stm32f4_disco"` |
| `board.vendor` | string | ベンダー名 | `"st"` |
| `mcu.name` | string | MCU 型番 | `"stm32f407vgt6"` |
| `mcu.arch` | string | アーキテクチャ | `"cortex-m4f"`, `"riscv32imc"` |
| `mcu.max_frequency` | int | 最大 CPU 周波数 (Hz) | `168000000` |
| `memory.flash.start` | hex | Flash 開始アドレス | `0x08000000` |
| `memory.flash.size` | int | Flash サイズ (bytes) | `1048576` |
| `memory.sram.start` | hex | メイン SRAM 開始 | `0x20000000` |
| `memory.sram.size` | int | メイン SRAM サイズ | `131072` |
| `debug.interface` | string | デバッグインターフェース | `"swd"`, `"jtag"` |
| `debug.probe` | string | デフォルトプローブ | `"stlink"`, `"jlink"` |
| `console.uart` | string | コンソール UART | `"usart2"` |
| `console.baudrate` | int | コンソールボーレート | `115200` |

### 3.2 ファミリー固有フィールド

特定の MCU ファミリーでのみ必要なフィールド。

**STM32 固有:**

| フィールド | 型 | 適用 | 説明 |
|-----------|-----|------|------|
| `clock.hse_frequency` | int | 全 STM32 | HSE 水晶周波数 |
| `clock.hse_bypass` | bool | 全 STM32 | HSE バイパスモード |
| `clock.pll.m/n/p/q` | int | F4, H7, U5 | PLL パラメータ |
| `clock.pll2.*`, `clock.pll3.*` | int | H7, U5 | 追加 PLL |
| `clock.vos_level` | int | H7 | 電圧スケーリングレベル |
| `power.regulator` | string | H7 | `"ldo"`, `"smps"`, `"smps_ldo"` |
| `memory.ccm.*` | object | F4 | CCM RAM 設定 |
| `memory.dtcm.*`, `memory.itcm.*` | object | H7 | TCM 設定 |
| `memory.axi_sram.*` | object | H7 | AXI SRAM 設定 |
| `trustzone.enabled` | bool | U5 | TrustZone 有効化 |
| `trustzone.secure_flash_size` | int | U5 | セキュアフラッシュサイズ |

**nRF52840 固有:**

| フィールド | 型 | 説明 |
|-----------|-----|------|
| `clock.hfclk_source` | string | `"hfxo"` or `"hfrc"` |
| `clock.lfclk_source` | string | `"lfxo"`, `"lfrc"`, `"lfsynth"` |
| `softdevice.name` | string | `"s140"`, `"s132"`, `"none"` |
| `softdevice.flash_size` | int | SoftDevice フラッシュ使用量 |
| `softdevice.ram_size` | int | SoftDevice RAM 使用量 |

**RP2040 固有:**

| フィールド | 型 | 説明 |
|-----------|-----|------|
| `flash.external` | bool | 常に `true` |
| `flash.chip` | string | フラッシュチップ型番 |
| `flash.size` | int | 外部フラッシュサイズ |
| `boot2.name` | string | boot2 識別子 |
| `pio.pin_base` | int | PIO ピンベース (オプション) |

**ESP32 固有:**

| フィールド | 型 | 説明 |
|-----------|-----|------|
| `flash.mode` | string | `"qio"`, `"dio"`, `"qout"` |
| `flash.speed` | string | `"80m"`, `"40m"` |
| `psram.enabled` | bool | PSRAM 搭載有無 |
| `psram.mode` | string | `"qspi"`, `"opi"` |
| `psram.size` | int | PSRAM サイズ |
| `partition.table` | string | パーティションテーブルファイル |
| `dual_core.enabled` | bool | デュアルコア使用 |

### 3.3 オプションフィールド (センシブルデフォルト付き)

| フィールド | デフォルト | 説明 |
|-----------|----------|------|
| `clock.lse_frequency` | `32768` | LSE 周波数 (ほぼ全ボード同一) |
| `debug.speed` | `4000` (kHz) | デバッグクロック速度 |
| `debug.reset_type` | `"hw"` | リセット方式 |
| `console.flow_control` | `false` | UART フロー制御 |
| `boot.watchdog_timeout` | `0` | ウォッチドッグタイムアウト (0=無効) |
| `power.battery_backed` | `false` | バッテリバックアップ有無 |

---

## 4. クロスカッティングコンサーン

### 4.1 割り込みコントローラの乖離

```
ARM Cortex-M: NVIC (Nested Vectored Interrupt Controller)
  → STM32F4, STM32H7, STM32U5, nRF52840, RP2040

RISC-V: PLIC/CLIC ベース
  → ESP32-C3: PLIC (Platform-Level Interrupt Controller)
  → GD32VF103: ECLIC (Enhanced Core Local Interrupt Controller)

Xtensa: 独自割り込みアーキテクチャ
  → ESP32-S3: 7 段階優先度、NMI サポート
```

**ボード設定への影響:**
- ベクタテーブルの形式が異なる (ARM: 固定テーブル、RISC-V: mtvec レジスタ)
- 割り込み優先度のビット幅が異なる (ARM: 0-15, ECLIC: 0-15, Xtensa: 0-6)
- スタートアップコードの割り込み初期化が完全に異なる

### 4.2 DMA モデルの乖離

```
集中型 DMA (STM32F4, STM32H7, GD32VF103):
  → 中央コントローラがペリフェラル-メモリ間転送を管理
  → チャネル/ストリーム番号の割当が必要
  → メモリ領域制約あり (STM32F4 CCM 不可、H7 ドメイン制約)

ペリフェラル内蔵 DMA (nRF52840 EasyDMA):
  → 各ペリフェラルに専用 DMA
  → ポインタ + 長さのみ設定
  → RAM のみアクセス可能

汎用 DMA (RP2040, ESP32):
  → チャネルベース、ペリフェラル制約なし
```

**ボード設定への影響:**
- DMA バッファのメモリ配置制約がファミリーごとに異なる
- リンカスクリプトで DMA 不可領域 (CCM, DTCM) を区別する必要がある
- STM32H7 ではメモリドメイン (D1/D2/D3) とキャッシュコヒーレンシも管理が必要

### 4.3 Flash 配置アドレスの乖離

```
0x0800_0000: STM32 ファミリー, GD32VF103
0x0000_0000: nRF52840 (Flash が 0 番地にマップ)
0x1000_0000: RP2040 (外部 XIP Flash)
SPI Flash:   ESP32-C3, ESP32-S3 (パーティションテーブルで管理)
```

**ボード設定への影響:**
- リンカスクリプトの FLASH ORIGIN が MCU 依存
- デバッグツールのフラッシュ書き込みアルゴリズムが異なる
- nRF52840 の SoftDevice 予約領域は動的にサイズが変わる

### 4.4 ツールチェーンの乖離

```
ARM Cortex-M:  arm-none-eabi-gcc / armclang
RISC-V:        riscv32-unknown-elf-gcc / riscv-none-elf-gcc
Xtensa:        xtensa-esp32s3-elf-gcc (ESP-IDF 付属)
```

**ボード設定への影響:**
- ツールチェーンプレフィックスが MCU アーキテクチャ依存
- コンパイルフラグが完全に異なる:
  - ARM: `-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16`
  - RISC-V: `-march=rv32imc -mabi=ilp32`
  - Xtensa: `-mlongcalls -fstrict-volatile-bitfields`

### 4.5 リンカスクリプト生成

各ファミリーで必要なメモリセクションが大きく異なる。

```ld
/* STM32F4 — 3 リージョン */
MEMORY {
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1M
    SRAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 128K
    CCM (rw)    : ORIGIN = 0x10000000, LENGTH = 64K
}

/* STM32H7 — 7+ リージョン */
MEMORY {
    FLASH (rx)    : ORIGIN = 0x08000000, LENGTH = 2M
    ITCM (rwx)    : ORIGIN = 0x00000000, LENGTH = 64K
    DTCM (rwx)    : ORIGIN = 0x20000000, LENGTH = 128K
    AXI_SRAM (rwx): ORIGIN = 0x24000000, LENGTH = 512K
    SRAM1 (rwx)   : ORIGIN = 0x30000000, LENGTH = 128K
    SRAM2 (rwx)   : ORIGIN = 0x30020000, LENGTH = 128K
    SRAM3 (rwx)   : ORIGIN = 0x30040000, LENGTH = 32K
    SRAM4 (rwx)   : ORIGIN = 0x38000000, LENGTH = 64K
}

/* nRF52840 + SoftDevice S140 — SoftDevice 予約 */
MEMORY {
    FLASH (rx)  : ORIGIN = 0x00027000, LENGTH = 0xD9000  /* SD 後 */
    RAM (rwx)   : ORIGIN = 0x20002000, LENGTH = 0x3E000  /* SD 後 */
}

/* RP2040 — 外部 XIP Flash */
MEMORY {
    FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 2M  /* ボード依存 */
    RAM (rwx)   : ORIGIN = 0x20000000, LENGTH = 264K
}
```

### 4.6 スタートアップコードの乖離

```c
/* ARM Cortex-M: ベクタテーブル + Reset_Handler */
__attribute__((section(".isr_vector")))
const void* vector_table[] = {
    &_estack,           /* 初期スタックポインタ */
    Reset_Handler,      /* エントリポイント */
    NMI_Handler,
    HardFault_Handler,
    /* ... */
};

/* RISC-V (GD32VF103): _start + mtvec 設定 */
.section .init
_start:
    la sp, _estack
    csrw mtvec, t0      /* 割り込みベクタテーブルアドレス設定 */
    call SystemInit
    call main

/* Xtensa (ESP32-S3): call0 ABI + ウィンドウレジスタ */
/* ESP-IDF が自動管理、ユーザー記述不要 */

/* RP2040: boot2 → vector_table → Reset_Handler */
/* boot2 (256 bytes) → メイン flash → Reset_Handler */
```

---

## 5. ボード設定スキーマ設計への提言

### 5.1 階層構造

```
arch (cortex-m4f / riscv32imc / xtensa-lx7)
  └── family (stm32f4 / nrf52 / rp2040 / esp32c3)
      └── mcu (stm32f407vgt6 / nrf52840 / rp2040 / esp32c3)
          └── board (stm32f4_disco / nrf52840dk / pico / esp32c3_devkitm)
```

### 5.2 設定のレイヤリング

```lua
-- arch レベル: ツールチェーン、スタートアップ形式
arch.cortex_m4f = {
    toolchain = "arm-none-eabi",
    flags = "-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16",
    irq_controller = "nvic",
    startup = "cortex_m",
}

-- family レベル: ペリフェラル IP、クロック構造
family.stm32f4 = {
    vendor = "st",
    pll_count = 1,
    has_ccm = true,
    dma_model = "centralized",
    flash_base = 0x08000000,
}

-- mcu レベル: メモリサイズ、ピン数
mcu.stm32f407vgt6 = {
    flash_size = 1024 * 1024,
    sram_size = 128 * 1024,
    ccm_size = 64 * 1024,
    max_frequency = 168000000,
    package = "LQFP100",
}

-- board レベル: ボード固有設定
board.stm32f4_disco = {
    hse_frequency = 8000000,
    hse_bypass = false,
    console_uart = "usart2",
    debug_probe = "stlink",
    leds = { green = "PD12", orange = "PD13", red = "PD14", blue = "PD15" },
}
```

### 5.3 フィールドの出所マトリクス

| 情報 | 出所レベル | 理由 |
|------|-----------|------|
| ツールチェーンプレフィックス | arch | アーキテクチャで一意に決定 |
| コンパイルフラグ | arch + mcu | FPU 有無等が MCU で異なる |
| Flash ベースアドレス | family | ファミリー内で統一 |
| Flash サイズ | mcu | MCU 品番で決定 |
| PLL パラメータ数 | family | ファミリーで統一 |
| PLL 具体値 | board | 外部水晶周波数がボード依存 |
| メモリリージョン構成 | family + mcu | ファミリーで構成決定、サイズは MCU 依存 |
| ピンアサイン (LED等) | board | ボード回路依存 |
| デバッグプローブ | board | ボード搭載プローブ依存 |
| SoftDevice 設定 | board | BLE 設定がアプリ/ボード依存 |
| boot2 選択 | board | 外部フラッシュチップ依存 |
| 電源レギュレータ | board | ボード回路依存 |

---

## 参照

### STM32
- [STM32F407 Reference Manual (RM0090)](https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [STM32H743 Reference Manual (RM0433)](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [STM32U5 Reference Manual (RM0456)](https://www.st.com/resource/en/reference_manual/rm0456-stm32u575585-armbased-32bit-mcus-stmicroelectronics.pdf)

### Nordic
- [nRF52840 Product Specification](https://infocenter.nordicsemi.com/pdf/nRF52840_PS_v1.7.pdf)
- [SoftDevice Specification S140](https://infocenter.nordicsemi.com/topic/sds_s140/SDS/s1xx/s140.html)

### RP2040
- [RP2040 Datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
- [Pico SDK Documentation](https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html)

### ESP32
- [ESP32-C3 Technical Reference](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_en.pdf)
- [ESP32-S3 Technical Reference](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)

### GD32VF103
- [GD32VF103 Datasheet](https://www.gigadevice.com/microcontroller/gd32vf103c8t6/)
- [GD32VF103 User Manual](https://www.gigadevice.com/media/pdf/GD32VF103_User_Manual_Rev1.4.pdf)
