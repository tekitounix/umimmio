# modm / CMSIS-Pack / Mbed OS / Rust embedded --- ボード設定フォーマット詳細調査

**分類:** ボード設定スキーマ調査 (コード生成 / XML / JSON 継承 / クレート)
**概要:** 4 つのフレームワークにおけるボードレベルの MCU 設定・クロック・ピン・メモリ・デバッグ定義を詳細に比較する。modm はデータ駆動型コード生成、CMSIS-Pack は XML メタデータ、Mbed OS は JSON 継承チェーン、Rust embedded は型システム + クレート分離でボード設定を表現する。

---

# Part 1: modm (最重要 --- UMI への類似度が最も高い)

## 1.1 module.lb 構造

modm のボードサポートは lbuild モジュールシステムで管理される。各 BSP は `module.lb` ファイルで定義される Python モジュール。

### init() --- モジュール登録

```python
# modm/src/modm/board/disco_f407vg/module.lb
def init(module):
    module.name = ":board:disco-f407vg"
    module.description = "STM32F4 Discovery Board"
```

### prepare() --- 依存関係と条件

```python
def prepare(module, options):
    # デバイスチェック: このボードは STM32F407VG のみ
    if not options[":target"].partname.startswith("stm32f407vg"):
        return False

    # モジュール依存関係の宣言
    module.depends(
        ":platform:clock",
        ":platform:gpio",
        ":platform:core",
        ":platform:uart:2",
        ":platform:spi:1",
        ":platform:i2s:3",
    )
    return True
```

### build() --- コード生成

```python
def build(env):
    env.outbasepath = "modm/src/modm/board"
    env.substitutions = {
        "with_logger": True,
        "with_assert": env.has_module(":architecture:assert"),
    }
    env.template("board.hpp.in")
    env.copy("board.cpp")
    env.copy(".")
    env.log.info("Generated board support for disco-f407vg")
```

---

## 1.2 board.hpp の内容

modm のボード定義の核心。クロック設定、ピン定義、初期化関数を C++ で型安全に記述する。

### SystemClock 構造体 (constexpr + enable)

```cpp
// board.hpp (modm/src/modm/board/disco_f407vg/)
namespace Board {

using namespace modm::platform;

/// STM32F407 running at 168 MHz from external 8 MHz HSE
struct SystemClock
{
    // クロック周波数を constexpr で宣言
    static constexpr uint32_t Frequency = 168_MHz;
    static constexpr uint32_t Ahb  = Frequency;
    static constexpr uint32_t Apb1 = Frequency / 4;    // 42 MHz
    static constexpr uint32_t Apb2 = Frequency / 2;    // 84 MHz

    // ペリフェラル固有のクロック
    static constexpr uint32_t Adc     = Apb2;
    static constexpr uint32_t Spi1    = Apb2;
    static constexpr uint32_t Spi2    = Apb1;
    static constexpr uint32_t Usart2  = Apb1;
    static constexpr uint32_t I2s     = Frequency;
    static constexpr uint32_t Timer1  = Apb2 * 2;      // APB2 タイマ倍速
    static constexpr uint32_t Timer2  = Apb1 * 2;      // APB1 タイマ倍速

    /// PLL を設定してクロックツリーを有効化
    static bool enable()
    {
        // HSE (8 MHz) を有効化
        Rcc::enableExternalCrystal();

        // PLL 設定: HSE(8MHz) / M(8) * N(336) / P(2) = 168 MHz
        const Rcc::PllFactors pllFactors{
            .pllM = 8,       // VCO 入力: 8MHz / 8 = 1MHz
            .pllN = 336,     // VCO 出力: 1MHz * 336 = 336MHz
            .pllP = 2,       // システム: 336MHz / 2 = 168MHz
            .pllQ = 7,       // USB: 336MHz / 7 = 48MHz
        };
        Rcc::enablePll(Rcc::PllSource::Hse, pllFactors);

        // バスプリスケーラ設定
        Rcc::setAhbPrescaler(Rcc::AhbPrescaler::Div1);
        Rcc::setApb1Prescaler(Rcc::Apb1Prescaler::Div4);
        Rcc::setApb2Prescaler(Rcc::Apb2Prescaler::Div2);

        // フラッシュウェイトステート設定 (168MHz → 5WS)
        Rcc::setFlashLatency<Frequency>();

        // システムクロックソースを PLL に切替
        Rcc::enableSystemClock(Rcc::SystemClockSource::Pll);

        // 周波数検証 (コンパイル時アサート)
        Rcc::updateCoreFrequency<Frequency>();
        return true;
    }
};

} // namespace Board
```

### ピン定義 (型エイリアス)

```cpp
namespace Board {

// LED ピン
using LedGreen  = GpioOutputD12;
using LedOrange = GpioOutputD13;
using LedRed    = GpioOutputD14;
using LedBlue   = GpioOutputD15;
using Leds = SoftwareGpioPort<LedBlue, LedRed, LedOrange, LedGreen>;

// ボタン
using Button = GpioInputA0;

// UART ピン
using Uart2Tx = GpioOutputA2;
using Uart2Rx = GpioInputA3;

// I2S (オーディオ) ピン
using I2s3Ck  = GpioOutputC10;
using I2s3Sd  = GpioOutputC12;
using I2s3Ws  = GpioOutputA4;
using I2s3Mck = GpioOutputC7;

// オーディオ DAC (CS43L22) 制御
using AudioReset = GpioOutputD4;

} // namespace Board
```

### initialize() 関数

```cpp
namespace Board {

/// ボード初期化 (クロック + GPIO + ペリフェラル)
inline void initialize()
{
    SystemClock::enable();
    SysTickTimer::initialize<SystemClock>();

    // LED を出力モードに設定
    LedGreen::setOutput(Gpio::OutputType::PushPull);
    LedOrange::setOutput(Gpio::OutputType::PushPull);
    LedRed::setOutput(Gpio::OutputType::PushPull);
    LedBlue::setOutput(Gpio::OutputType::PushPull);

    // ボタンを入力モード (プルダウン) に設定
    Button::setInput(Gpio::InputType::Floating);

    // UART ピンを接続 (Alternate Function 自動設定)
    Uart2Tx::connect(Usart2::Tx);
    Uart2Rx::connect(Usart2::Rx);
}

} // namespace Board
```

---

## 1.3 クロックツリーの C++ 表現

modm はクロック設定を完全に C++ コードとして表現する。コンパイル時に周波数計算が行われる。

### PllFactors 構造体

```cpp
struct PllFactors {
    uint8_t pllM;     // VCO 入力分周 (2-63)
    uint16_t pllN;    // VCO 乗算 (50-432)
    uint8_t pllP;     // システムクロック分周 (2,4,6,8)
    uint8_t pllQ;     // USB/SDIO 分周 (2-15)
};
```

### Rcc クラスの主要メソッド

```cpp
class Rcc {
public:
    static void enableExternalCrystal();          // HSE (クリスタル)
    static void enableExternalClock();            // HSE (バイパス)
    static void enableInternalClock();            // HSI (16MHz)

    static void enablePll(PllSource source, const PllFactors& factors);

    static void setAhbPrescaler(AhbPrescaler prescaler);
    static void setApb1Prescaler(Apb1Prescaler prescaler);
    static void setApb2Prescaler(Apb2Prescaler prescaler);

    template<uint32_t frequency>
    static void setFlashLatency();                // コンパイル時にウェイトステート計算

    static void enableSystemClock(SystemClockSource source);

    template<uint32_t frequency>
    static void updateCoreFrequency();            // SysTick 更新
};
```

### バスプリスケーラの enum

```cpp
enum class AhbPrescaler : uint32_t {
    Div1 = 0, Div2 = 0x80, Div4 = 0x90, Div8 = 0xA0,
    Div16 = 0xB0, Div64 = 0xC0, Div128 = 0xD0,
    Div256 = 0xE0, Div512 = 0xF0,
};

enum class Apb1Prescaler : uint32_t {
    Div1 = 0, Div2 = 0x1000, Div4 = 0x1400,
    Div8 = 0x1800, Div16 = 0x1C00,
};
```

---

## 1.4 ピン Muxing (テンプレートメタプログラミング)

modm の最も特徴的な設計。GPIO ピンの Alternate Function (AF) 接続をコンパイル時に型チェックする。

### connect<> テンプレート

```cpp
// GpioA2 を USART2::Tx として接続
GpioA2::connect(Usart2::Tx);
// → コンパイル時に PA2 の AF7 が USART2_TX であることを検証
// → 間違った AF を指定するとコンパイルエラー

// GpioA5 を SPI1::Sck として接続
GpioA5::connect(Spi1::Sck);
// → PA5 の AF5 が SPI1_SCK であることを検証
```

### GPIO 型定義

```cpp
// GPIO ピンは型として定義される
// 各ピンは固有の型を持ち、利用可能な AF が型レベルで記述される
struct GpioA2 {
    static constexpr uint8_t port = 0;  // GPIOA
    static constexpr uint8_t pin = 2;
    // AF テーブルはデバイスデータから自動生成
    // AF7 = USART2_TX, AF3 = TIM9_CH1, ...
};
```

### コンパイル時 AF 検証

```cpp
// 不正な接続はコンパイルエラーになる
GpioA2::connect(Spi1::Mosi);
// → ERROR: PA2 には SPI1_MOSI の AF が存在しない

// 正しい接続のみコンパイル通過
GpioA2::connect(Usart2::Tx);
// → OK: PA2 の AF7 = USART2_TX
```

---

## 1.5 modm-devices XML (GPIO AF データ)

ベンダーデータ (CubeMX XML) から正規化された GPIO AF テーブル。コード生成の入力データ。

```xml
<!-- modm-devices 正規化済み GPIO データ -->
<device platform="stm32" family="f4" name="07" pin="v" size="g">
  <driver type="gpio" name="stm32-f2f4">
    <gpio port="A" pin="2">
      <signal name="TIM2_CH3" af="1"/>
      <signal name="TIM5_CH3" af="2"/>
      <signal name="TIM9_CH1" af="3"/>
      <signal name="USART2_TX" af="7"/>
    </gpio>
    <gpio port="A" pin="5">
      <signal name="TIM2_CH1" af="1"/>
      <signal name="TIM8_CH1N" af="3"/>
      <signal name="SPI1_SCK" af="5"/>
      <signal name="DAC_OUT2" af="0"/>
    </gpio>
  </driver>

  <memory name="flash" access="rx" start="0x08000000" size="1048576"/>
  <memory name="sram1" access="rwx" start="0x20000000" size="131072"/>
  <memory name="ccm" access="rw" start="0x10000000" size="65536"/>
</device>
```

---

## 1.6 コード生成パイプライン

### Jinja2 テンプレート (board.hpp.in)

```cpp
// board.hpp.in (Jinja2 テンプレート)
#pragma once

#include <modm/platform.hpp>
#include <modm/architecture/interface/clock.hpp>

namespace Board
{
using namespace modm::platform;

struct SystemClock
{
    static constexpr uint32_t Frequency = {{ frequency }}_MHz;
    static constexpr uint32_t Ahb  = Frequency;
    static constexpr uint32_t Apb1 = Frequency / {{ apb1_div }};
    static constexpr uint32_t Apb2 = Frequency / {{ apb2_div }};

{% for peripheral, clock in peripheral_clocks.items() %}
    static constexpr uint32_t {{ peripheral }} = {{ clock }};
{% endfor %}

    static bool enable();
};

{% for name, gpio in leds.items() %}
using {{ name }} = Gpio{{ gpio.type }}{{ gpio.port }}{{ gpio.pin }};
{% endfor %}

} // namespace Board
```

### env.template() vs env.copy()

```python
def build(env):
    # Jinja2 テンプレートから生成 (変数展開あり)
    env.template("board.hpp.in")
    # → board.hpp.in を Jinja2 処理して board.hpp を出力

    # そのままコピー (変数展開なし)
    env.copy("board.cpp")
    # → board.cpp をそのまま出力先にコピー

    # ディレクトリごとコピー
    env.copy(".", ignore=env.ignore_files("*.lb", "*.md"))
```

---

## 1.7 クロス MCU ボードサポートパターン

```python
# Nucleo ファミリのボードサポート例
# modm/src/modm/board/nucleo_f411re/module.lb
def prepare(module, options):
    if not options[":target"].partname == "stm32f411ret6":
        return False
    module.depends(
        ":platform:clock",
        ":platform:gpio",
        ":platform:uart:2",        # ST-Link Virtual COM Port
    )
    return True
```

modm は **1 ボード = 1 module.lb** の原則。異なる MCU を搭載するボードは別モジュールとして定義する。ボード間の継承は `<extends>` タグで modm の project.xml レベルで行われる (module.lb レベルでの継承はなし)。

---

# Part 2: CMSIS-Pack

## 2.1 .pdsc ボード要素 XML スキーマ

CMSIS-Pack のボード定義は DFP (Device Family Pack) とは分離された BSP (Board Support Pack) 内に定義される。

```xml
<!-- Board element in .pdsc -->
<board vendor="STMicroelectronics"
       name="STM32F4-Discovery"
       revision="Rev.C"
       salesContact="https://www.st.com/stm32f4discovery"
       orderForm="https://estore.st.com/stm32f407g-disc1.html">

  <description>STMicroelectronics STM32F4-Discovery Board</description>
  <image small="Images/stm32f4_disco_small.png"
         large="Images/stm32f4_disco_large.png"/>

  <!-- 搭載デバイス -->
  <mountedDevice deviceIndex="0"
                 Dvendor="STMicroelectronics:13"
                 Dname="STM32F407VGTx"/>

  <!-- 互換デバイス (同パッケージの別品番) -->
  <compatibleDevice deviceIndex="0"
                    Dvendor="STMicroelectronics:13"
                    DsubFamily="STM32F407"/>

  <!-- ボード機能 -->
  <feature type="ODbg" n="1" name="On-board ST-LINK/V2"/>
  <feature type="XTAL" n="8000000" name="8 MHz HSE Crystal"/>
  <feature type="XTAL" n="32768" name="32.768 kHz LSE Crystal"/>
  <feature type="PWR" n="5" name="USB Powered"/>
  <feature type="RAM" n="1" name="128 Kbyte SRAM"/>
  <feature type="ROM" n="1" name="1 Mbyte internal Flash"/>
  <feature type="Button" n="2" name="Push-buttons: User and Reset"/>
  <feature type="LED" n="8" name="LEDs: 4 user, 1 power, 2 USB"/>
  <feature type="ConnOther" n="1" name="Audio DAC CS43L22"/>
  <feature type="AccSens" n="1" name="3-axis Accelerometer LIS3DSH/LIS302DL"/>
  <feature type="MIC" n="1" name="Digital Microphone MP45DT02"/>
  <feature type="USB" n="1" name="USB OTG FS with micro-AB connector"/>
  <feature type="LineOut" n="1" name="3.5mm Audio Jack"/>

  <!-- デバッグインターフェース -->
  <debugInterface adapter="ST-Link" connector="Mini-USB"/>
  <debugInterface adapter="JTAG/SWD" connector="20-pin Cortex debug + ETM"/>
</board>
```

### feature タイプ一覧

| type 値 | 説明 | n の意味 |
|---------|------|----------|
| `ODbg` | オンボードデバッグプローブ | プローブ数 |
| `XTAL` | 水晶発振子 | 周波数 (Hz) |
| `PWR` | 電源 | 電圧 (V) |
| `RAM` | 外部 RAM | バンク数 |
| `ROM` | 外部 ROM/Flash | バンク数 |
| `USB` | USB コネクタ | コネクタ数 |
| `ETH` | Ethernet | ポート数 |
| `CAN` | CAN バス | インターフェース数 |
| `Button` | ボタン | ボタン数 |
| `LED` | LED | LED 数 |
| `ConnOther` | その他のコネクタ/デバイス | デバイス数 |
| `AccSens` | 加速度センサ | センサ数 |
| `MIC` | マイク | マイク数 |
| `LineOut` | ライン出力 | 出力数 |
| `LineIn` | ライン入力 | 入力数 |

### debugInterface 要素

| 属性 | 説明 | 値の例 |
|------|------|--------|
| `adapter` | デバッグアダプタ名 | `ST-Link`, `J-Link`, `CMSIS-DAP`, `JTAG/SWD` |
| `connector` | 物理コネクタ | `Mini-USB`, `20-pin Cortex debug`, `Tag-Connect` |

---

## 2.2 Device vs Board の分離

CMSIS-Pack はデバイス (DFP) とボード (BSP) を明確に分離する。

```
DFP (STM32F4xx_DFP):
  ├── デバイス定義 (STM32F407VGTx)
  │   ├── メモリマップ (Flash, SRAM, CCM)
  │   ├── 割り込みベクタテーブル
  │   ├── SVD (レジスタ定義)
  │   └── フラッシュアルゴリズム (.FLM)
  └── スタートアップコード

BSP (STM32F4-Discovery_BSP):
  ├── ボード定義 (board 要素)
  │   ├── mountedDevice → DFP のデバイスを参照
  │   ├── feature リスト
  │   └── debugInterface
  ├── ボード初期化コード
  └── ボード固有コンフィグファイル
```

**ボード (BSP) はクロック・ピンのデフォルト値を持たない。** メタデータ (搭載デバイス、機能リスト、デバッグ方式) のみ。実際のクロック・ピン設定はプロジェクトレベルまたはフレームワーク (STM32CubeMX 等) に委譲される。

---

# Part 3: Mbed OS

## 3.1 targets.json 完全継承チェーン

Mbed OS の最も洗練された継承システム。5 段階の継承チェーンで MCU からボードまでを段階的に定義する。

```json
{
  "Target": {
    "boot_stack_size": "0x400",
    "default_lib": "std",
    "supported_form_factors": [],
    "is_disk_virtual": false,
    "macros": [],
    "device_has": [],
    "features": [],
    "detect_code": [],
    "extra_labels": [],
    "public": false,
    "supported_toolchains": [],
    "default_toolchain": "ARM"
  },

  "MCU_STM32": {
    "inherits": ["Target"],
    "public": false,
    "device_has_add": ["SERIAL", "SERIAL_FC", "STDIO_MESSAGES", "FLASH",
                       "I2C", "ANALOGIN", "PORTOUT", "PORTINOUT", "PORTIN",
                       "INTERRUPTIN", "PWMOUT", "SPI", "USTICKER",
                       "LPTICKER", "SLEEP", "RESET_REASON", "WATCHDOG"],
    "extra_labels_add": ["STM32"],
    "macros_add": ["TRANSACTION_QUEUE_SIZE_SPI=2"],
    "supported_toolchains": ["ARM", "GCC_ARM", "IAR"],
    "config": {
      "clock_source": {
        "help": "Mask of available clocks",
        "value": "USE_PLL_HSE_EXTC|USE_PLL_HSI"
      },
      "lse_available": {
        "help": "Define if LSE is available on the board",
        "value": 1
      }
    }
  },

  "MCU_STM32F4": {
    "inherits": ["MCU_STM32"],
    "public": false,
    "core": "Cortex-M4F",
    "extra_labels_add": ["STM32F4"],
    "device_has_add": ["SERIAL_ASYNCH", "FLASH", "MPU",
                       "TRNG", "CRC", "CAN", "ANALOGOUT"],
    "macros_add": ["USB_STM_HAL", "USBHOST_OTHER"],
    "config": {
      "clock_source": {
        "value": "USE_PLL_HSE_EXTC|USE_PLL_HSE_XTAL|USE_PLL_HSI"
      }
    }
  },

  "MCU_STM32F411xE": {
    "inherits": ["MCU_STM32F4"],
    "public": false,
    "extra_labels_add": ["STM32F411xE"],
    "device_has_add": ["TRNG"],
    "device_has_remove": ["CAN", "ANALOGOUT"],
    "mbed_rom_start": "0x08000000",
    "mbed_rom_size": "0x80000",
    "mbed_ram_start": "0x20000000",
    "mbed_ram_size": "0x20000"
  },

  "NUCLEO_F411RE": {
    "inherits": ["MCU_STM32F411xE"],
    "public": true,
    "device_name": "STM32F411RETx",
    "detect_code": ["0740"],
    "device_has_add": ["USBDEVICE"],
    "supported_form_factors": ["ARDUINO_UNO", "MORPHO"],
    "overrides": {
      "clock_source": "USE_PLL_HSE_EXTC"
    }
  }
}
```

### 継承解決の例 (NUCLEO_F411RE)

```
プロパティ "device_has" の解決:
  Target:           []
  + MCU_STM32:      +[SERIAL, SERIAL_FC, STDIO_MESSAGES, FLASH, I2C, ...]
  + MCU_STM32F4:    +[SERIAL_ASYNCH, FLASH, MPU, TRNG, CRC, CAN, ANALOGOUT]
  + MCU_STM32F411xE: +[TRNG], -[CAN, ANALOGOUT]
  + NUCLEO_F411RE:  +[USBDEVICE]
  = 最終値: [SERIAL, SERIAL_FC, STDIO_MESSAGES, FLASH, I2C, ANALOGIN,
             PORTOUT, PORTINOUT, PORTIN, INTERRUPTIN, PWMOUT, SPI,
             USTICKER, LPTICKER, SLEEP, RESET_REASON, WATCHDOG,
             SERIAL_ASYNCH, MPU, TRNG, CRC, USBDEVICE]
```

---

## 3.2 全フィールドと型・デフォルト値

| フィールド | 型 | デフォルト | 説明 |
|-----------|-----|----------|------|
| `inherits` | string[] | -- | 継承元ターゲットリスト |
| `public` | boolean | false | ユーザーに公開するか |
| `core` | string | -- | CPU コア名 |
| `device_name` | string | -- | CMSIS デバイス名 |
| `device_has` | string[] | [] | ハードウェア機能リスト |
| `features` | string[] | [] | ソフトウェア機能リスト |
| `extra_labels` | string[] | [] | 追加ラベル (ビルド条件分岐用) |
| `macros` | string[] | [] | コンパイル時マクロ定義 |
| `supported_toolchains` | string[] | [] | 対応ツールチェーン |
| `supported_form_factors` | string[] | [] | フォームファクター |
| `detect_code` | string[] | [] | ボード自動検出コード |
| `mbed_rom_start` | hex string | -- | ROM 開始アドレス |
| `mbed_rom_size` | hex string | -- | ROM サイズ |
| `mbed_ram_start` | hex string | -- | RAM 開始アドレス |
| `mbed_ram_size` | hex string | -- | RAM サイズ |
| `boot_stack_size` | hex string | "0x400" | ブートスタックサイズ |
| `default_lib` | string | "std" | 標準ライブラリ |
| `config` | object | {} | 設定パラメータ (help + value) |
| `overrides` | object | {} | 親の config 値の上書き |

---

## 3.3 `_add` / `_remove` 修飾子システム

```json
{
  "MY_CUSTOM_BOARD": {
    "inherits": ["MCU_STM32F411xE"],
    "device_has_add": ["USB_DEVICE", "I2C_ASYNCH"],
    "device_has_remove": ["TRNG"],
    "extra_labels_add": ["MY_CUSTOM"],
    "macros_add": ["CUSTOM_BOARD=1", "HSE_VALUE=12000000"],
    "macros_remove": ["USB_STM_HAL"]
  }
}
```

**修飾子ルール:**
- `property_add`: 親の配列にアイテムを追加
- `property_remove`: 親の配列からアイテムを削除
- `property` (無修飾): 親の値を完全に上書き
- `_add` と `_remove` は同じプロパティに対して同時に使用可能

---

## 3.4 config / overrides によるクロックソース

```json
{
  "MCU_STM32": {
    "config": {
      "clock_source": {
        "help": "Mask of available clocks",
        "value": "USE_PLL_HSE_EXTC|USE_PLL_HSI"
      }
    }
  },
  "NUCLEO_F411RE": {
    "inherits": ["MCU_STM32F411xE"],
    "overrides": {
      "clock_source": "USE_PLL_HSE_EXTC"
    }
  },
  "DISCO_F407VG": {
    "inherits": ["MCU_STM32F407xG"],
    "overrides": {
      "clock_source": "USE_PLL_HSE_XTAL"
    }
  }
}
```

Nucleo ボードは STLink の MCO 出力を外部クロックとして使用 (`EXTC`)。Discovery ボードはオンボード水晶を使用 (`XTAL`)。

---

## 3.5 PinNames.h

```cpp
// targets/TARGET_STM32/TARGET_STM32F4/TARGET_STM32F411xE/
//   TARGET_NUCLEO_F411RE/PinNames.h

typedef enum {
    PA_0  = 0x00, PA_1  = 0x01, PA_2  = 0x02, PA_3  = 0x03,
    PA_4  = 0x04, PA_5  = 0x05, PA_6  = 0x06, PA_7  = 0x07,
    PA_8  = 0x08, PA_9  = 0x09, PA_10 = 0x0A, PA_11 = 0x0B,
    PA_12 = 0x0C, PA_13 = 0x0D, PA_14 = 0x0E, PA_15 = 0x0F,
    // ... 全ポート

    // Arduino コネクタマッピング
    ARDUINO_UNO_A0 = PA_0,
    ARDUINO_UNO_A1 = PA_1,
    ARDUINO_UNO_D0 = PA_3,
    ARDUINO_UNO_D1 = PA_2,

    // ボード固有
    LED1    = PA_5,
    LED2    = PA_5,
    BUTTON1 = PC_13,
    SERIAL_TX = PA_2,
    SERIAL_RX = PA_3,
    CONSOLE_TX = SERIAL_TX,
    CONSOLE_RX = SERIAL_RX,

    // USB
    USB_DM = PA_11,
    USB_DP = PA_12,

    NC = (int)0xFFFFFFFF,
} PinName;
```

---

## 3.6 device_has 機能リスト

`device_has` は条件コンパイルの基盤。各エントリは `DEVICE_<name>` マクロとして定義される。

| 機能名 | 説明 |
|--------|------|
| `SERIAL` | UART シリアル通信 |
| `SERIAL_ASYNCH` | 非同期シリアル (DMA) |
| `SERIAL_FC` | フロー制御 |
| `I2C` | I2C マスター |
| `I2C_ASYNCH` | 非同期 I2C |
| `SPI` | SPI マスター |
| `ANALOGIN` | ADC |
| `ANALOGOUT` | DAC |
| `PWMOUT` | PWM 出力 |
| `CAN` | CAN バス |
| `USB` | USB ホスト/デバイス |
| `FLASH` | 内蔵フラッシュ書き込み |
| `MPU` | Memory Protection Unit |
| `TRNG` | 真性乱数生成器 |
| `CRC` | ハードウェア CRC |
| `SLEEP` | スリープモード |
| `WATCHDOG` | ウォッチドッグタイマ |

---

# Part 4: Rust Embedded

## 4.1 BSP クレート構造 (PAC → HAL → BSP)

```
probe-rs (チップ定義)
  └── cortex-m-rt (リンカ + スタートアップ)
      └── stm32f4 (PAC: SVD 自動生成)
          └── stm32f4xx-hal (HAL: ペリフェラル抽象)
              └── stm32f4-discovery (BSP: ボード固有)
```

```toml
# stm32f4-discovery/Cargo.toml
[package]
name = "stm32f4-discovery"
version = "0.4.0"

[dependencies]
stm32f4xx-hal = { version = "0.20", features = ["stm32f407"] }
cortex-m = "0.7"
cortex-m-rt = "0.7"
```

---

## 4.2 Cargo.toml の feature flags (MCU 選択)

```toml
# stm32f4xx-hal/Cargo.toml
[features]
default = []
# MCU バリアントを feature flag で選択
stm32f401 = ["stm32f4/stm32f401"]
stm32f405 = ["stm32f4/stm32f405"]
stm32f407 = ["stm32f4/stm32f407"]
stm32f411 = ["stm32f4/stm32f411"]
stm32f429 = ["stm32f4/stm32f429"]
stm32f446 = ["stm32f4/stm32f446"]

# オプション機能
usb_fs = ["synopsys-usb-otg"]
can = ["bxcan"]
i2s = []
defmt = ["dep:defmt"]
```

### ユーザープロジェクトからの MCU 選択

```toml
# ユーザーの Cargo.toml
[dependencies]
stm32f4xx-hal = { version = "0.20", features = ["stm32f407", "usb_fs", "i2s"] }
```

feature flag で MCU バリアントと利用するペリフェラルを宣言的に選択する。コンパイル時に不要なコードが除去される。

---

## 4.3 lib.rs の init() (クロック設定: ビルダーパターン)

```rust
// stm32f4-discovery/src/lib.rs
use stm32f4xx_hal::{
    gpio::*,
    i2s::*,
    pac,
    prelude::*,
    rcc::RccExt,
};

/// Board-specific initialization
pub fn init() -> (Peripherals, Clocks) {
    let dp = pac::Peripherals::take().unwrap();
    let cp = cortex_m::Peripherals::take().unwrap();

    // クロック設定: ビルダーパターン
    let rcc = dp.RCC.constrain();
    let clocks = rcc.cfgr
        .use_hse(8.MHz())           // 外部 8MHz HSE
        .sysclk(168.MHz())          // システムクロック 168MHz
        .hclk(168.MHz())            // AHB 168MHz
        .pclk1(42.MHz())            // APB1 42MHz
        .pclk2(84.MHz())            // APB2 84MHz
        .require_pll48clk()         // USB 用 48MHz PLL 出力を要求
        .i2s_clk(61440.kHz())       // I2S クロック
        .freeze();                  // 設定を確定・適用
    // .freeze() の時点で PLL パラメータが自動計算される
    // 実現不可能な周波数を要求するとパニック

    (Peripherals { dp, cp }, clocks)
}
```

### ビルダーパターンの意味

```rust
rcc.cfgr
    .use_hse(8.MHz())      // HSE = 8MHz
    .sysclk(168.MHz())     // 希望する SYSCLK
    .freeze();             // PLL パラメータを自動計算して適用
```

ユーザーは **希望する周波数を指定するだけ**。PLLM/PLLN/PLLP の計算は HAL が自動で行う。実現不可能な周波数を指定すると `freeze()` でパニックする。

---

## 4.4 bsp_pins! マクロ (ピン定義)

```rust
// BSP でのピン定義マクロ (rp2040-hal の例)
bsp_pins! {
    /// GPIO 0 - UART0 TX
    Gpio0 {
        name: tx,
        aliases: { FunctionUart, PullNone: UartTx }
    },
    /// GPIO 1 - UART0 RX
    Gpio1 {
        name: rx,
        aliases: { FunctionUart, PullNone: UartRx }
    },
    /// GPIO 25 - On-board LED
    Gpio25 {
        name: led,
        aliases: { FunctionSio, PullNone: Led }
    },
}
```

### STM32 BSP のピン定義 (型エイリアス方式)

```rust
// stm32f4-discovery/src/lib.rs
use stm32f4xx_hal::gpio::*;

/// User LEDs
pub type LedGreen  = Pin<'D', 12, Output<PushPull>>;
pub type LedOrange = Pin<'D', 13, Output<PushPull>>;
pub type LedRed    = Pin<'D', 14, Output<PushPull>>;
pub type LedBlue   = Pin<'D', 15, Output<PushPull>>;

/// User button
pub type UserButton = Pin<'A', 0, Input>;

/// Audio I2S pins
pub type I2s3Ck  = Pin<'C', 10, Alternate<6>>;
pub type I2s3Sd  = Pin<'C', 12, Alternate<6>>;
pub type I2s3Ws  = Pin<'A', 4, Alternate<6>>;
pub type I2s3Mck = Pin<'C', 7, Alternate<6>>;
```

**Alternate<6>** の `6` は AF 番号。コンパイル時に型レベルで検証される。

---

## 4.5 probe-rs YAML チップ定義

probe-rs はデバッグツールとして CMSIS-Pack から抽出したチップ定義を YAML で管理する。

```yaml
# probe-rs chip definition
name: STM32F407VGTx
cores:
  - name: main
    type: armv7em
    registers:
      program_counter: 0xE000EDF8
      stack_pointer: 0xE000EDF8

memory_map:
  - Ram:
      range:
        start: 0x20000000
        end: 0x20020000         # 128 KB SRAM
      cores: [main]
  - Nvm:
      range:
        start: 0x08000000
        end: 0x08100000         # 1 MB Flash
      cores: [main]
      is_boot_memory: true
  - Ram:
      range:
        start: 0x10000000
        end: 0x10010000         # 64 KB CCM
      cores: [main]

flash_algorithms:
  - name: stm32f4xx_1024
    default: true
    description: "Flash algorithm for STM32F4xx 1MB"
    flash_properties:
      address_range:
        start: 0x08000000
        end: 0x08100000
      page_size: 0x4000
      erased_byte_value: 0xFF
    cores: [main]
```

### マルチコア定義 (STM32H745)

```yaml
name: STM32H745ZITx
cores:
  - name: cm7
    type: armv7em
  - name: cm4
    type: armv7em

memory_map:
  - Ram:
      range: { start: 0x20000000, end: 0x20020000 }
      cores: [cm7]           # M7 のみアクセス可能
  - Ram:
      range: { start: 0x30000000, end: 0x30048000 }
      cores: [cm4]           # M4 のみアクセス可能
  - Ram:
      range: { start: 0x38000000, end: 0x38010000 }
      cores: [cm7, cm4]      # 共有 SRAM
  - Nvm:
      range: { start: 0x08000000, end: 0x08100000 }
      cores: [cm7]
      is_boot_memory: true
  - Nvm:
      range: { start: 0x08100000, end: 0x08200000 }
      cores: [cm4]
```

---

## フレームワーク比較

| 項目 | modm | CMSIS-Pack | Mbed OS | Rust Embedded |
|------|------|-----------|---------|---------------|
| 設定形式 | C++ (board.hpp) | XML (.pdsc) | JSON (targets.json) | Rust (lib.rs + Cargo.toml) |
| クロック定義 | constexpr + Rcc:: | なし (メタデータのみ) | config/overrides | ビルダーパターン |
| ピン定義 | 型エイリアス + connect<> | なし | PinNames.h enum | 型エイリアス + Alternate<N> |
| メモリ定義 | XML → リンカ生成 | XML (memory 要素) | mbed_rom_*/mbed_ram_* | memory.x 手書き |
| デバッグ設定 | なし (外部ツール) | debugInterface 要素 | detect_code | probe-rs YAML |
| 継承 | extends (project.xml) | DFP/BSP 分離 | inherits + _add/_remove | PAC/HAL/BSP クレート階層 |
| コンパイル時検証 | AF 型チェック | なし | なし | AF 型チェック |
| コード生成 | Jinja2 テンプレート | なし | なし | svd2rust (PAC のみ) |

---

## UMI への示唆

1. **modm の board.hpp パターン** は UMI に最も直接的に適用可能。クロック設定を `constexpr` + 関数で表現し、ピンを型エイリアスで定義する方式は UMI の C++ 設計と完全に一致する
2. **modm の connect<> による AF 検証** は UMI でも C++20 concepts で実現可能。コンパイル時にピン AF の正当性を検証する
3. **Mbed OS の `_add`/`_remove` パターン** は Lua テーブルのマージ演算で同等に表現可能。UMI のボード設定で device_has 的な機能フラグ管理に応用できる
4. **CMSIS-Pack のメタデータ専用設計** は、ボード定義に必要な最小限の情報セットの参考になる。クロック・ピンの設定値はメタデータではなくコード側に置く判断
5. **Rust のビルダーパターン** (目標周波数を指定、PLL パラメータ自動計算) は UMI のクロック設定 API の理想形。ユーザーは `sysclk(168_MHz)` と書くだけでよい
6. **probe-rs のマルチコア YAML** はデュアルコア MCU のメモリマップ定義のモデル。各メモリ領域に `cores` 属性でアクセス可能コアを指定する方式は UMI のメモリ定義でも採用すべき

---

## 参照

### modm
- [How modm Works](https://modm.io/how-modm-works/)
- [modm Board Support](https://modm.io/reference/boards/)
- [modm-devices GitHub](https://github.com/modm-io/modm-devices)
- [lbuild GitHub](https://github.com/modm-io/lbuild)
- [modm disco_f407vg BSP](https://github.com/modm-io/modm/tree/develop/src/modm/board/disco_f407vg)

### CMSIS-Pack
- [Open-CMSIS-Pack Board Element](https://open-cmsis-pack.github.io/Open-CMSIS-Pack-Spec/main/html/pdsc_boards_pg.html)
- [CMSIS-Pack Board Examples](https://github.com/Open-CMSIS-Pack/ST_NUCLEO-F411RE_BSP)
- [Open-CMSIS-Pack Spec](https://open-cmsis-pack.github.io/Open-CMSIS-Pack-Spec/main/html/index.html)

### Mbed OS
- [Adding and Configuring Targets](https://os.mbed.com/docs/mbed-os/v6.16/program-setup/adding-and-configuring-targets.html)
- [targets.json GitHub](https://github.com/ARMmbed/mbed-os/blob/master/targets/targets.json)

### Rust Embedded
- [Embedded Rust Book](https://docs.rust-embedded.org/book/)
- [stm32f4xx-hal GitHub](https://github.com/stm32-rs/stm32f4xx-hal)
- [probe-rs](https://probe.rs/)
- [probe-rs target definitions](https://github.com/probe-rs/probe-rs/tree/master/probe-rs-target/targets)
- [cortex-m-rt](https://docs.rs/cortex-m-rt/latest/cortex_m_rt/)
