# ESP-IDF / PlatformIO / Arduino --- ボード設定フォーマット詳細調査

**分類:** ボード設定スキーマ調査 (sdkconfig / board.json / boards.txt)
**概要:** 3 つのフレームワークにおけるボードレベルの MCU 設定・クロック・ピン・メモリ・デバッグ定義を詳細に比較する。ESP-IDF は Kconfig ベースのチップ設定、PlatformIO は JSON フラット定義、Arduino はプロパティファイルでそれぞれ異なるアプローチを取る。

---

# Part 1: ESP-IDF

## 1.1 sdkconfig フォーマット

ESP-IDF のボード・チップ設定は Kconfig ベースの `sdkconfig` ファイルで一元管理される。形式は `KEY=VALUE` のフラットなプロパティファイル。

```ini
# sdkconfig (menuconfig で生成)
CONFIG_IDF_TARGET="esp32s3"
CONFIG_IDF_TARGET_ESP32S3=y

# クロック設定
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240

# フラッシュ設定
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="4MB"
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y

# パーティションテーブル
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# PSRAM (ESP32-S3)
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y

# FreeRTOS
CONFIG_FREERTOS_HZ=1000
CONFIG_FREERTOS_UNICORE=n
```

### sdkconfig.defaults チェーン

```
sdkconfig.defaults                      全ターゲット共通
sdkconfig.defaults.<IDF_TARGET>         チップ固有 (例: sdkconfig.defaults.esp32s3)
sdkconfig.defaults.<build_name>         ビルドバリアント固有
sdkconfig                              ユーザーカスタマイズ (menuconfig 生成)
```

後のファイルが前の設定を上書きする積層方式。`sdkconfig` は `.gitignore` に入れ、`sdkconfig.defaults` をバージョン管理する運用が推奨される。

### ボードコンセプトの不在

ESP-IDF には **ボードという概念が存在しない**。設定の対象はチップ (SoC) のみ。ボード固有の設定 (LED ピン、外部デバイス等) はアプリケーションコードまたは sdkconfig.defaults で管理する。

```
Zephyr:   SoC DTSI → Board DTS → App Overlay (3段階)
ESP-IDF:  SoC (IDF_TARGET) → sdkconfig (2段階、ボード層なし)
```

---

## 1.2 クロック設定

### CPU 周波数

```ini
# 選択肢は SoC ごとに異なる
# ESP32: 80, 160, 240 MHz
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y

# ESP32-C3: 80, 160 MHz
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_160=y

# ESP32-S3: 80, 160, 240 MHz
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
```

### Dynamic Frequency Scaling (DFS)

```ini
CONFIG_PM_ENABLE=y
CONFIG_PM_DFS_INIT_AUTO=y
# DFS は CPU を自動的にダウンクロックして消費電力を削減
# Light Sleep 時は XTAL (40MHz) まで低下可能
```

### クロックソース

ESP32 シリーズのクロックは以下のソースから選択:

| ソース | 周波数 | 用途 |
|--------|--------|------|
| XTAL | 40 MHz (標準) | PLL 入力、低電力モード |
| PLL | 320/480 MHz (内部) | CPU/ペリフェラルクロック |
| RC_FAST | 17.5 MHz (概算) | 低精度用途 |
| RC_SLOW | 150 kHz (概算) | RTC |
| XTAL32K | 32.768 kHz (外付け) | RTC 高精度 |

**ESP32 には PLL パラメータの直接設定がない。** CPU 周波数を指定すると、ドライバが PLL を自動設定する。STM32 のような PLLM/PLLN/PLLP 手動設定は不要。

---

## 1.3 GPIO Matrix (3 層ピンルーティング)

ESP32 のピンルーティングは 3 つのレイヤで構成される。

```
┌──────────────────┐
│  RTC IO MUX      │  ← 超低電力モード用 (Deep Sleep 中も動作)
│  (GPIO 0-21のみ) │
├──────────────────┤
│  IO MUX          │  ← 高速直結 (UART0, SPI Flash 等)
│  (固定ルーティング)│
├──────────────────┤
│  GPIO Matrix     │  ← 汎用ルーティング (任意GPIO ↔ 任意ペリフェラル)
│  (自由割当)       │
└──────────────────┘
```

| レイヤ | 遅延 | 制約 | 用途 |
|--------|------|------|------|
| IO MUX | 最小 | 固定ピンマッピング | 高速ペリフェラル (SPI Flash, JTAG) |
| GPIO Matrix | 約 25ns 追加 | ほぼ制約なし | 汎用ペリフェラル割当 |
| RTC IO MUX | -- | 一部ピンのみ | Deep Sleep 時の GPIO |

### コードでのピン設定

```c
// GPIO Matrix 経由 (任意ピン → UART)
uart_config_t uart_config = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
};
uart_param_config(UART_NUM_1, &uart_config);
// GPIO16 → UART1_TX, GPIO17 → UART1_RX (任意選択)
uart_set_pin(UART_NUM_1, 16, 17, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
```

---

## 1.4 パーティションテーブル

ESP-IDF はフラッシュメモリをパーティションテーブルで管理する。CSV 形式で記述。

```csv
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x6000,
phy_init, data, phy,     0xf000,   0x1000,
factory,  app,  factory, 0x10000,  1M,
storage,  data, spiffs,  ,         0xF0000,
```

### フィールド定義

| フィールド | 説明 | 値の例 |
|-----------|------|--------|
| Name | パーティション名 (最大 16 文字) | `factory`, `ota_0`, `nvs` |
| Type | パーティション種別 | `app` (0x00), `data` (0x01) |
| SubType | サブタイプ | `factory`, `ota_0`..`ota_15`, `nvs`, `phy`, `spiffs` |
| Offset | 開始アドレス (空欄で自動計算) | `0x10000` |
| Size | サイズ (K/M 接尾辞対応) | `1M`, `0x100000` |
| Flags | フラグ | `encrypted` |

### OTA 用パーティション例

```csv
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x4000,
otadata,  data, ota,     0xd000,   0x2000,
phy_init, data, phy,     0xf000,   0x1000,
ota_0,    app,  ota_0,   0x10000,  0x1E0000,
ota_1,    app,  ota_1,   0x1F0000, 0x1E0000,
storage,  data, spiffs,  ,         0x20000,
```

---

## 1.5 soc_caps.h (SoC 能力宣言)

各チップの能力を C マクロで一元的に宣言する。ボード定義ではないが、ボード設定の基盤となる。

### ESP32 vs ESP32-S3 比較

| マクロ | ESP32 | ESP32-S3 | 説明 |
|--------|-------|----------|------|
| `SOC_CPU_CORES_NUM` | 2 | 2 | CPU コア数 |
| `SOC_GPIO_PIN_COUNT` | 40 | 49 | GPIO ピン総数 |
| `SOC_UART_NUM` | 3 | 3 | UART インスタンス数 |
| `SOC_I2C_NUM` | 2 | 2 | I2C インスタンス数 |
| `SOC_I2S_NUM` | 2 | 2 | I2S インスタンス数 |
| `SOC_SPI_PERIPH_NUM` | 4 | 4 | SPI インスタンス数 |
| `SOC_ADC_PERIPH_NUM` | 2 | 2 | ADC ユニット数 |
| `SOC_ADC_MAX_CHANNEL_NUM` | 10 | 10 | ADC チャネル数 |
| `SOC_DAC_PERIPH_NUM` | 2 | 0 | DAC ユニット数 |
| `SOC_SPIRAM_SUPPORTED` | 1 | 1 | PSRAM サポート |
| `SOC_USB_OTG_SUPPORTED` | 0 | 1 | USB OTG サポート |
| `SOC_TWAI_CONTROLLER_NUM` | 1 | 1 | CAN コントローラ数 |
| `SOC_BT_SUPPORTED` | 1 | 1 | Bluetooth サポート |
| `SOC_WIFI_SUPPORTED` | 1 | 1 | WiFi サポート |

---

## 1.6 フラッシュサイズ設定

```ini
# フラッシュサイズ (Kconfig)
CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y
# CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
# CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
# CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y

# フラッシュモード
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y    # Quad I/O (最高速)
# CONFIG_ESPTOOLPY_FLASHMODE_DIO=y  # Dual I/O
# CONFIG_ESPTOOLPY_FLASHMODE_QOUT=y # Quad Output

# フラッシュ周波数
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
# CONFIG_ESPTOOLPY_FLASHFREQ_40M=y
```

---

## 1.7 デュアルコア処理

```ini
# デュアルコア/シングルコア選択 (ESP32, ESP32-S3)
CONFIG_FREERTOS_UNICORE=n           # デュアルコアモード
# CONFIG_FREERTOS_UNICORE=y         # シングルコアモード

# メインタスクのコアピン固定
CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0=y
# CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1=y
# CONFIG_ESP_MAIN_TASK_AFFINITY_NO_AFFINITY=y
```

```c
// コードでのタスクピン固定
xTaskCreatePinnedToCore(
    audio_task,       // タスク関数
    "audio",          // タスク名
    4096,             // スタックサイズ
    NULL,             // パラメータ
    configMAX_PRIORITIES - 1,  // 優先度
    &audio_task_handle,
    1                 // Core 1 に固定
);
```

---

# Part 2: PlatformIO

## 2.1 board.json 完全スキーマ

### フィールド構造

```json
{
  "build": {
    "core": "string",
    "cpu": "string",
    "extra_flags": "string",
    "f_cpu": "string",
    "framework": ["string"],
    "hwids": [["VID", "PID"]],
    "mcu": "string",
    "variant": "string"
  },
  "connectivity": ["string"],
  "debug": {
    "default_tools": ["string"],
    "jlink_device": "string",
    "onboard_tools": ["string"],
    "openocd_board": "string",
    "openocd_target": "string",
    "svd_path": "string"
  },
  "frameworks": ["string"],
  "name": "string",
  "upload": {
    "disable_flushing": "boolean",
    "maximum_ram_size": "integer",
    "maximum_size": "integer",
    "protocol": "string",
    "protocols": ["string"],
    "require_upload_port": "boolean",
    "speed": "integer"
  },
  "url": "string",
  "vendor": "string"
}
```

### 必須 vs オプションフィールド

| フィールド | 必須 | 説明 |
|-----------|------|------|
| `build.mcu` | Yes | MCU 型番 |
| `build.cpu` | Yes | CPU コア (cortex-m4, esp32s3 等) |
| `build.f_cpu` | Yes | CPU 周波数 (Hz、接尾辞 L 付き) |
| `build.framework` | Yes | 対応フレームワークリスト |
| `name` | Yes | ボード表示名 |
| `upload.maximum_size` | Yes | フラッシュサイズ (bytes) |
| `upload.maximum_ram_size` | Yes | RAM サイズ (bytes) |
| `upload.protocol` | Yes | デフォルト書き込みプロトコル |
| `vendor` | Yes | ベンダー名 |
| `build.extra_flags` | No | 追加コンパイルフラグ |
| `build.variant` | No | Arduino variant ディレクトリ名 |
| `connectivity` | No | 接続機能 (wifi, bluetooth, ethernet 等) |
| `debug.*` | No | デバッグ設定 |
| `url` | No | ボード製品ページ URL |

---

## 2.2 実ボード JSON 例

### esp32dev

```json
{
  "build": {
    "core": "esp32",
    "extra_flags": ["-DARDUINO_ESP32_DEV"],
    "f_cpu": "240000000L",
    "flash_mode": "dio",
    "framework": ["arduino", "espidf"],
    "mcu": "esp32",
    "variant": "esp32"
  },
  "connectivity": ["wifi", "bluetooth", "ethernet", "can"],
  "debug": {
    "default_tools": ["esp-prog"],
    "onboard_tools": ["esp-prog"],
    "openocd_board": "esp32-wrover.cfg"
  },
  "frameworks": ["arduino", "espidf"],
  "name": "Espressif ESP32 Dev Module",
  "upload": {
    "flash_size": "4MB",
    "maximum_ram_size": 327680,
    "maximum_size": 4194304,
    "protocol": "esptool",
    "protocols": ["esptool", "espota"],
    "require_upload_port": true,
    "speed": 460800
  },
  "url": "https://www.espressif.com/en/products/devkits/esp32-devkitc",
  "vendor": "Espressif"
}
```

### disco_f407vg (STM32F4 Discovery)

```json
{
  "build": {
    "core": "stm32",
    "cpu": "cortex-m4",
    "extra_flags": "-DSTM32F407xx",
    "f_cpu": "168000000L",
    "framework": ["arduino", "stm32cube", "mbed"],
    "mcu": "stm32f407vgt6",
    "variant": "DISCO_F407VG"
  },
  "connectivity": ["can"],
  "debug": {
    "default_tools": ["stlink"],
    "jlink_device": "STM32F407VG",
    "onboard_tools": ["stlink"],
    "openocd_target": "stm32f4x",
    "svd_path": "STM32F407x.svd"
  },
  "frameworks": ["arduino", "stm32cube", "mbed"],
  "name": "ST STM32F4DISCOVERY",
  "upload": {
    "maximum_ram_size": 131072,
    "maximum_size": 1048576,
    "protocol": "stlink",
    "protocols": ["stlink", "jlink", "cmsis-dap", "blackmagic", "mbed"]
  },
  "url": "https://www.st.com/en/evaluation-tools/stm32f4discovery.html",
  "vendor": "ST"
}
```

### esp32-s3-devkitc-1

```json
{
  "build": {
    "core": "esp32",
    "extra_flags": [
      "-DARDUINO_ESP32S3_DEV",
      "-DARDUINO_USB_MODE=1",
      "-DARDUINO_RUNNING_CORE=1",
      "-DARDUINO_EVENT_RUNNING_CORE=1"
    ],
    "f_cpu": "240000000L",
    "flash_mode": "qio",
    "framework": ["arduino", "espidf"],
    "mcu": "esp32s3",
    "variant": "esp32s3"
  },
  "connectivity": ["wifi", "bluetooth"],
  "debug": {
    "default_tools": ["esp-builtin"],
    "onboard_tools": ["esp-builtin"],
    "openocd_target": "esp32s3"
  },
  "frameworks": ["arduino", "espidf"],
  "name": "Espressif ESP32-S3-DevKitC-1-N8 (8 MB QD, No PSRAM)",
  "upload": {
    "flash_size": "8MB",
    "maximum_ram_size": 327680,
    "maximum_size": 8388608,
    "protocol": "esptool",
    "protocols": ["esptool", "esp-builtin", "espota"],
    "require_upload_port": true,
    "speed": 460800
  },
  "url": "https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html",
  "vendor": "Espressif"
}
```

### nucleo_h743zi (STM32H7)

```json
{
  "build": {
    "core": "stm32",
    "cpu": "cortex-m7",
    "extra_flags": "-DSTM32H743xx",
    "f_cpu": "480000000L",
    "framework": ["arduino", "stm32cube", "mbed"],
    "mcu": "stm32h743zit6",
    "variant": "NUCLEO_H743ZI"
  },
  "connectivity": ["ethernet", "can"],
  "debug": {
    "default_tools": ["stlink"],
    "jlink_device": "STM32H743ZI",
    "onboard_tools": ["stlink"],
    "openocd_target": "stm32h7x",
    "svd_path": "STM32H7x3.svd"
  },
  "frameworks": ["arduino", "stm32cube", "mbed"],
  "name": "ST Nucleo H743ZI",
  "upload": {
    "maximum_ram_size": 524288,
    "maximum_size": 2097152,
    "protocol": "stlink",
    "protocols": ["stlink", "jlink", "cmsis-dap", "blackmagic", "mbed"]
  },
  "url": "https://www.st.com/en/evaluation-tools/nucleo-h743zi.html",
  "vendor": "ST"
}
```

---

## 2.3 Variant システム

`build.variant` プロパティが Arduino フレームワークの variant ディレクトリを参照する。

```
framework-arduinoststm32/
└── variants/
    └── DISCO_F407VG/
        ├── variant_DISCO_F407VG.h      ピンマッピング
        ├── variant_DISCO_F407VG.cpp    ピン設定コード
        ├── PeripheralPins.c            ペリフェラルピン割当テーブル
        └── ldscript.ld                 リンカスクリプト
```

---

## 2.4 platformio.ini からの board_build オーバーライド

```ini
[env:custom_disco]
platform = ststm32
board = disco_f407vg
framework = arduino

# board.json の値をプロジェクトレベルで上書き
board_build.mcu = stm32f407vgt6
board_build.f_cpu = 168000000L

# フラッシュサイズ上書き
board_upload.maximum_size = 1048576

# カスタムリンカスクリプト
board_build.ldscript = custom_memory.ld

# デバッグツール上書き
debug_tool = jlink

# 追加ビルドフラグ
build_flags =
    -DUSE_FULL_LL_DRIVER
    -DHSE_VALUE=8000000U
```

---

# Part 3: Arduino

## 3.1 boards.txt フォーマット

### 基本形式

```
BOARD_ID.property=value
BOARD_ID.property.sub=value
```

ドット区切りのフラットなキーバリュー形式。配列やネストオブジェクトは表現不可。全ボードが 1 ファイルに集約される。

### Arduino Uno 完全エントリ

```properties
# --- Arduino Uno ---
uno.name=Arduino Uno

# ビルド設定
uno.build.board=AVR_UNO
uno.build.core=arduino
uno.build.f_cpu=16000000L
uno.build.mcu=atmega328p
uno.build.variant=standard

# アップロード設定
uno.upload.maximum_size=32256
uno.upload.maximum_data_size=2048
uno.upload.protocol=arduino
uno.upload.speed=115200
uno.upload.tool=avrdude
uno.upload.tool.default=avrdude
uno.upload.tool.network=arduino_ota

# ブートローダー設定
uno.bootloader.extended_fuses=0xFD
uno.bootloader.file=optiboot/optiboot_atmega328.hex
uno.bootloader.high_fuses=0xDE
uno.bootloader.lock_bits=0x0F
uno.bootloader.low_fuses=0xFF
uno.bootloader.tool=avrdude
uno.bootloader.unlock_bits=0x3F

# USB 識別
uno.vid.0=0x2341
uno.pid.0=0x0043
uno.vid.1=0x2341
uno.pid.1=0x0001
uno.vid.2=0x2A03
uno.pid.2=0x0043
```

### フィールドカテゴリ一覧

| カテゴリ | プレフィックス | 主要フィールド |
|---------|-------------|---------------|
| メタデータ | `name` | ボード表示名 |
| ビルド | `build.*` | `board`, `core`, `f_cpu`, `mcu`, `variant`, `extra_flags` |
| アップロード | `upload.*` | `maximum_size`, `maximum_data_size`, `protocol`, `speed`, `tool` |
| ブートローダー | `bootloader.*` | `file`, `tool`, `high_fuses`, `low_fuses`, `extended_fuses` |
| USB 識別 | `vid.*`, `pid.*` | VID/PID ペア |
| シリアルモニタ | `serial.*` | `disableDTR`, `disableRTS` |

---

## 3.2 Menu システム (バリアント選択)

### 基本構文

```properties
# menu 定義 (boards.txt の先頭)
menu.cpu=Processor

# ボードごとの menu 項目
mega.menu.cpu.atmega2560=ATmega2560 (Mega 2560)
mega.menu.cpu.atmega2560.build.mcu=atmega2560
mega.menu.cpu.atmega2560.build.f_cpu=16000000L
mega.menu.cpu.atmega2560.build.board=AVR_MEGA2560
mega.menu.cpu.atmega2560.upload.maximum_size=253952
mega.menu.cpu.atmega2560.upload.speed=115200

mega.menu.cpu.atmega1280=ATmega1280
mega.menu.cpu.atmega1280.build.mcu=atmega1280
mega.menu.cpu.atmega1280.build.f_cpu=16000000L
mega.menu.cpu.atmega1280.build.board=AVR_MEGA
mega.menu.cpu.atmega1280.upload.maximum_size=126976
mega.menu.cpu.atmega1280.upload.speed=57600
```

### ESP32 Arduino の Menu 項目

ESP32 Arduino コアは大量の menu 項目を定義する。

```properties
menu.CPUFreq=CPU Frequency
menu.FlashMode=Flash Mode
menu.FlashSize=Flash Size
menu.PSRAM=PSRAM
menu.PartitionScheme=Partition Scheme
menu.DebugLevel=Core Debug Level
menu.LoopCore=Arduino Runs On
menu.EventsCore=Events Run On
menu.UploadSpeed=Upload Speed
menu.EraseFlash=Erase All Flash Before Sketch Upload
```

### ESP32-S3 DevKitC-1 の Menu 定義例

```properties
# CPU 周波数
esp32s3.menu.CPUFreq.240=240MHz (WiFi)
esp32s3.menu.CPUFreq.240.build.f_cpu=240000000L
esp32s3.menu.CPUFreq.160=160MHz (WiFi)
esp32s3.menu.CPUFreq.160.build.f_cpu=160000000L
esp32s3.menu.CPUFreq.80=80MHz (WiFi)
esp32s3.menu.CPUFreq.80.build.f_cpu=80000000L

# フラッシュモード
esp32s3.menu.FlashMode.qio=QIO 80MHz
esp32s3.menu.FlashMode.qio.build.flash_mode=dio
esp32s3.menu.FlashMode.qio.build.boot=qio
esp32s3.menu.FlashMode.qio.build.flash_freq=80m

# フラッシュサイズ
esp32s3.menu.FlashSize.4M=4MB (32Mb)
esp32s3.menu.FlashSize.4M.build.flash_size=4MB
esp32s3.menu.FlashSize.8M=8MB (64Mb)
esp32s3.menu.FlashSize.8M.build.flash_size=8MB
esp32s3.menu.FlashSize.16M=16MB (128Mb)
esp32s3.menu.FlashSize.16M.build.flash_size=16MB

# PSRAM
esp32s3.menu.PSRAM.disabled=Disabled
esp32s3.menu.PSRAM.disabled.build.defines=
esp32s3.menu.PSRAM.enabled=QSPI PSRAM
esp32s3.menu.PSRAM.enabled.build.defines=-DBOARD_HAS_PSRAM
esp32s3.menu.PSRAM.opi=OPI PSRAM
esp32s3.menu.PSRAM.opi.build.defines=-DBOARD_HAS_PSRAM

# パーティションスキーム
esp32s3.menu.PartitionScheme.default=Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)
esp32s3.menu.PartitionScheme.default.build.partitions=default
esp32s3.menu.PartitionScheme.huge_app=Huge APP (3MB No OTA/1MB SPIFFS)
esp32s3.menu.PartitionScheme.huge_app.build.partitions=huge_app
esp32s3.menu.PartitionScheme.huge_app.upload.maximum_size=3145728

# デバッグレベル
esp32s3.menu.DebugLevel.none=None
esp32s3.menu.DebugLevel.none.build.code_debug=0
esp32s3.menu.DebugLevel.verbose=Verbose
esp32s3.menu.DebugLevel.verbose.build.code_debug=5

# コア選択
esp32s3.menu.LoopCore.1=Core 1
esp32s3.menu.LoopCore.1.build.loop_core=-DARDUINO_RUNNING_CORE=1
esp32s3.menu.LoopCore.0=Core 0
esp32s3.menu.LoopCore.0.build.loop_core=-DARDUINO_RUNNING_CORE=0
```

---

## 3.3 クロスプラットフォーム参照

Arduino コアは複数のベンダーが提供可能。`boards.txt` から別ベンダーのコアを参照する仕組み:

```properties
# boards.txt 内でコア参照
myboard.build.core=esp32:esp32
# 形式: VENDOR_ID:CORE_ID
```

### Boards Manager package_index.json

```json
{
  "packages": [{
    "name": "esp32",
    "maintainer": "Espressif Systems",
    "websiteURL": "https://github.com/espressif/arduino-esp32",
    "platforms": [{
      "name": "esp32",
      "architecture": "esp32",
      "version": "2.0.14",
      "url": "https://github.com/espressif/arduino-esp32/releases/...",
      "boards": [
        {"name": "ESP32 Dev Module"},
        {"name": "ESP32-S3 Dev Module"}
      ],
      "toolsDependencies": [{
        "packager": "esp32",
        "name": "xtensa-esp32-elf-gcc",
        "version": "esp-12.2.0_20230208"
      }]
    }]
  }]
}
```

---

## フレームワーク比較

| 項目 | ESP-IDF | PlatformIO | Arduino |
|------|---------|------------|---------|
| 設定形式 | Kconfig (KEY=VALUE) | JSON | Properties (dot-separated) |
| ボード概念 | なし (チップのみ) | あり (board.json) | あり (boards.txt) |
| クロック設定 | sdkconfig で CPU 周波数選択 | board.json の f_cpu | boards.txt の build.f_cpu + menu |
| ピン設定 | C コード (gpio_set_pin) | variant/ ディレクトリ | variant/pins_arduino.h |
| メモリ定義 | パーティション CSV + リンカ | maximum_size/maximum_ram_size | maximum_size/maximum_data_size |
| デバッグ設定 | idf.py monitor / JTAG | debug.* フィールド | なし (外部ツール) |
| 継承 | なし | なし | なし |
| バリアント | sdkconfig.defaults チェーン | build.variant | menu 構文 |
| ビルド時検証 | Kconfig range/depends | なし | なし |
| ファイル数/ボード | 1-2 (sdkconfig.defaults) | 1 (JSON) | 1 (boards.txt 内エントリ) |

---

## UMI への示唆

1. **ESP-IDF の「ボードなし」設計** は対極的だが、チップ能力 (soc_caps.h) を基盤とする設計思想は UMI のデバイス DB と共通。ボード層がない分、ボード固有設定がアプリに散逸する問題を UMI は避けるべき
2. **GPIO Matrix の 3 層構造** はピンルーティングの柔軟性モデルとして参考になる。UMI の pinctrl 設計では IO MUX (固定) vs GPIO Matrix (自由) の区別を抽象化する必要がある
3. **パーティションテーブル CSV** はフラッシュレイアウト定義の最もシンプルな形式。UMI のメモリマップ定義でも CSV/Lua テーブルで同等の表現が可能
4. **PlatformIO の board.json** は 1 ファイル完結の DX が優れるが、継承なしのためデータ重複が深刻。UMI は Lua dofile() チェーンでこの問題を解決済み
5. **Arduino の menu 構文** は同一ボード内のバリアント選択に有用。UMI でも `xmake f --clock=168mhz` のようなオプション選択に応用可能
6. **ESP32 Arduino の大量 menu 項目** (CPU 周波数、フラッシュ、PSRAM、パーティション、デバッグレベル、コア選択) は、ボード設定でどの項目をユーザー選択可能にすべきかの参考リスト

---

## 参照

### ESP-IDF
- [ESP-IDF Build System](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html)
- [GPIO & RTC GPIO](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/gpio.html)
- [Partition Tables](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/partition-tables.html)
- [ESP-IDF GitHub](https://github.com/espressif/esp-idf)

### PlatformIO
- [Custom Embedded Boards](https://docs.platformio.org/en/latest/platforms/creating_board.html)
- [Board JSON Schema](https://docs.platformio.org/en/latest/projectconf/sections/env/options/board/index.html)
- [platform-ststm32 GitHub](https://github.com/platformio/platform-ststm32)
- [platform-espressif32 GitHub](https://github.com/platformio/platform-espressif32)

### Arduino
- [Arduino Platform Specification](https://arduino.github.io/arduino-cli/latest/platform-specification/)
- [Arduino Boards Manager Spec](https://arduino.github.io/arduino-cli/latest/package_index_json-specification/)
- [ESP32 Arduino Core GitHub](https://github.com/espressif/arduino-esp32)
