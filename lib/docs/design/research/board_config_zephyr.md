# Zephyr RTOS --- ボード設定スキーマ詳細調査

**分類:** ボード設定スキーマ調査 (DeviceTree + Kconfig + board.yml)
**概要:** Zephyr のボード設定は DeviceTree (DTS) を唯一のハードウェア記述源泉とし、クロック・ピン・メモリ・デバッグの全設定を DTS バインディングとして型安全に記述する。HWMv2 の board.yml がメタデータを補完し、Kconfig がソフトウェア設定を制御する。

---

## 1. ボードディレクトリの完全ファイル構成

### STM32F4 Discovery の例

```
boards/st/stm32f4_disco/
├── board.yml                    HWMv2 メタデータ
├── board.cmake                  デバッグランナー設定
├── Kconfig.stm32f4_disco        ボード Kconfig シンボル
├── stm32f4_disco_defconfig      デフォルト Kconfig 値
├── stm32f4_disco.dts            DeviceTree ソース (メイン)
├── stm32f4_disco.yaml           ボードドキュメント用メタデータ
└── doc/
    └── index.rst                ドキュメント
```

### nRF52840 DK の例

```
boards/nordic/nrf52840dk/
├── board.yml
├── board.cmake
├── Kconfig.nrf52840dk
├── nrf52840dk_nrf52840_defconfig
├── nrf52840dk_nrf52840.dts
├── nrf52840dk_nrf52840.overlay      (オプション: 追加オーバーレイ)
└── nrf52840dk_nrf52840-pinctrl.dtsi  ピン設定の分離ファイル
```

### ESP32-C3 DevKitM の例

```
boards/espressif/esp32c3_devkitm/
├── board.yml
├── board.cmake
├── Kconfig.esp32c3_devkitm
├── esp32c3_devkitm_defconfig
├── esp32c3_devkitm.dts
└── esp32c3_devkitm-pinctrl.dtsi
```

---

## 2. board.yml (HWMv2) スキーマ

HWMv2 で導入されたボードメタデータファイル。ボードと SoC の関係、バリアント、リビジョンを宣言的に記述する。

### 基本構造

```yaml
board:
  name: stm32f4_disco
  vendor: st
  socs:
    - name: stm32f407xg
  # 省略時のデフォルトは socs[0]
```

### バリアント付き (nRF5340 DK)

```yaml
board:
  name: nrf5340dk
  vendor: nordic
  socs:
    - name: nrf5340
      variants:
        - name: nrf5340/cpuapp       # アプリケーションコア
        - name: nrf5340/cpunet       # ネットワークコア
        - name: nrf5340/cpuapp/ns    # Non-Secure バリアント
```

### リビジョン付き

```yaml
board:
  name: my_board
  vendor: mycompany
  revisions:
    format: letter               # letter | number | custom
    default: B
    revisions:
      - name: A
      - name: B
        exact: true              # このリビジョンのみマッチ
```

### extend (ボード拡張)

```yaml
board:
  name: my_custom_board
  extend: nucleo_f411re
  vendor: mycompany
```

### board.yml スキーマフィールド一覧

| フィールド | 型 | 必須 | 説明 |
|-----------|-----|------|------|
| `board.name` | string | Yes | ボード識別子 |
| `board.vendor` | string | Yes | ベンダー識別子 |
| `board.socs` | list | Yes | 搭載 SoC リスト |
| `board.socs[].name` | string | Yes | SoC 名 (soc.yml で定義) |
| `board.socs[].variants` | list | No | SoC バリアント |
| `board.revisions` | object | No | ボードリビジョン管理 |
| `board.revisions.format` | string | No | リビジョン命名形式 |
| `board.revisions.default` | string | No | デフォルトリビジョン |
| `board.extend` | string | No | 拡張元ボード名 |
| `board.full_name` | string | No | 人間可読なフルネーム |
| `board.qualifier_requirements` | list | No | 必須修飾子 |

---

## 3. DeviceTree クロック設定

Zephyr のクロック設定は DTS バインディングで型安全に記述される。各バインディングには YAML スキーマが存在し、ビルド時に型・範囲の検証が行われる。

### STM32F4 クロックツリー全体像

```dts
/* stm32f4_disco.dts */
&clocks {
    /* RCC (Reset and Clock Control) ノード */
    /* DTS バインディング: st,stm32-rcc */
};

&pll {
    /* PLL 設定 */
    /* DTS バインディング: st,stm32f4-pll-clock */
    div-m = <8>;       /* PLLM: VCO 入力分周 (2-63) */
    mul-n = <336>;     /* PLLN: VCO 乗算 (50-432) */
    div-p = <2>;       /* PLLP: システムクロック分周 (2,4,6,8) */
    div-q = <7>;       /* PLLQ: USB/SDIO 分周 (2-15) */
    clocks = <&clk_hse>;  /* PLL ソースクロック */
    status = "okay";
};
```

### RCC バインディング (st,stm32-rcc)

```yaml
# dts/bindings/clock/st,stm32-rcc.yaml
compatible: "st,stm32-rcc"
properties:
  "#clock-cells":
    const: 2        # バスID + ビット位置
  clocks:
    required: true  # PLL 出力への参照
```

ペリフェラルのクロック有効化は `clocks` プロパティでバスとビット位置を指定する:

```dts
&usart2 {
    /* APB1 バスの USART2EN ビット (17) */
    clocks = <&rcc STM32_CLOCK_BUS_APB1 0x00020000>;
    status = "okay";
};
```

### PLL バインディング (st,stm32f4-pll-clock)

```yaml
# dts/bindings/clock/st,stm32f4-pll-clock.yaml
compatible: "st,stm32f4-pll-clock"
properties:
  div-m:
    type: int
    required: true
    description: "Division factor for PLL input clock (2..63)"
  mul-n:
    type: int
    required: true
    description: "PLL multiplication factor (50..432)"
  div-p:
    type: int
    required: true
    enum: [2, 4, 6, 8]
    description: "PLL division factor for main system clock"
  div-q:
    type: int
    required: true
    description: "Division factor for USB OTG FS, SDIO (2..15)"
  clocks:
    required: true
    description: "PLL source clock (HSE or HSI)"
```

### HSE バインディング (st,stm32-hse-clock)

```yaml
# dts/bindings/clock/st,stm32-hse-clock.yaml
compatible: "st,stm32-hse-clock"
properties:
  clock-frequency:
    type: int
    required: true
    description: "HSE oscillator frequency in Hz"
  hse-bypass:
    type: boolean
    description: "HSE bypass mode (external clock source, not crystal)"
```

```dts
/* STM32F4 Discovery: 8MHz HSE (クリスタル) */
clk_hse: clk-hse {
    #clock-cells = <0>;
    compatible = "st,stm32-hse-clock";
    clock-frequency = <DT_FREQ_M(8)>;
    /* hse-bypass なし → クリスタル発振 */
};
```

```dts
/* Nucleo ボード: 8MHz HSE (STLink MCO バイパス) */
clk_hse: clk-hse {
    #clock-cells = <0>;
    compatible = "st,stm32-hse-clock";
    clock-frequency = <DT_FREQ_M(8)>;
    hse-bypass;    /* 外部クロックソース */
};
```

### バスプリスケーラ

```dts
&rcc {
    clocks = <&pll>;
    clock-frequency = <DT_FREQ_M(168)>;
    ahb-prescaler = <1>;
    apb1-prescaler = <4>;     /* APB1: 168/4 = 42 MHz */
    apb2-prescaler = <2>;     /* APB2: 168/2 = 84 MHz */
};
```

### STM32H7 の 3 PLL 設定例

```dts
/* STM32H7 は 3 つの独立した PLL を持つ */
&pll {
    div-m = <4>;
    mul-n = <400>;
    div-p = <2>;     /* SYSCLK: 400 MHz */
    div-q = <4>;     /* per_ck 系 */
    div-r = <2>;
    clocks = <&clk_hse>;
    status = "okay";
};

&pll2 {
    div-m = <4>;
    mul-n = <240>;
    div-p = <2>;
    div-r = <2>;     /* ADC クロック等 */
    clocks = <&clk_hse>;
    status = "okay";
};

&pll3 {
    div-m = <4>;
    mul-n = <192>;
    div-p = <2>;
    div-q = <4>;     /* I2S/SAI クロック */
    clocks = <&clk_hse>;
    status = "okay";
};
```

---

## 4. DeviceTree pinctrl 設定

Zephyr の pinctrl はベンダーごとに異なるマクロと DTS バインディングを使用する。

### STM32: STM32_PINMUX マクロ

```dts
/* boards/st/stm32f4_disco/stm32f4_disco-pinctrl.dtsi */
#include <st/f4/stm32f407vgtx-pinctrl.dtsi>

&pinctrl {
    usart2_tx_pa2: usart2_tx_pa2 {
        pinmux = <STM32_PINMUX('A', 2, AF7)>;
        /* STM32_PINMUX(ポート, ピン番号, Alternate Function) */
    };
    usart2_rx_pa3: usart2_rx_pa3 {
        pinmux = <STM32_PINMUX('A', 3, AF7)>;
    };

    i2s3_ck_pc10: i2s3_ck_pc10 {
        pinmux = <STM32_PINMUX('C', 10, AF6)>;
        slew-rate = "very-high-speed";
    };
    i2s3_sd_pc12: i2s3_sd_pc12 {
        pinmux = <STM32_PINMUX('C', 12, AF6)>;
        slew-rate = "very-high-speed";
    };
};

/* ペリフェラルから pinctrl を参照 */
&usart2 {
    pinctrl-0 = <&usart2_tx_pa2 &usart2_rx_pa3>;
    pinctrl-names = "default";
    status = "okay";
};

&i2s3 {
    pinctrl-0 = <&i2s3_ck_pc10 &i2s3_sd_pc12>;
    pinctrl-names = "default";
    status = "okay";
};
```

**STM32_PINMUX マクロの内部構造:**

```c
/* STM32_PINMUX('A', 2, AF7) → 0x0027 */
/* ビットフィールド: [15:12]=ポートインデックス [11:8]=AF番号 [7:4]=ピン番号 [3:0]=予約 */
#define STM32_PINMUX(port, pin, af) \
    ((((port) - 'A') << 12) | ((af) << 8) | ((pin) << 4))
```

### pinctrl の追加プロパティ (STM32)

```dts
/* 出力ピン設定 */
&pinctrl {
    uart_tx: uart_tx {
        pinmux = <STM32_PINMUX('A', 9, AF7)>;
        bias-pull-up;                    /* プルアップ有効 */
        /* bias-pull-down; */            /* プルダウン */
        /* bias-disable; */              /* ハイインピーダンス (デフォルト) */
        slew-rate = "very-high-speed";   /* low-speed | medium-speed | high-speed | very-high-speed */
        drive-push-pull;                 /* プッシュプル (デフォルト) */
        /* drive-open-drain; */          /* オープンドレイン */
    };
};
```

### Nordic (nRF): NRF_PSEL マクロ

```dts
/* boards/nordic/nrf52840dk/nrf52840dk_nrf52840-pinctrl.dtsi */
&pinctrl {
    uart0_default: uart0_default {
        group1 {
            psels = <NRF_PSEL(UART_TX, 0, 6)>;
            /* NRF_PSEL(機能名, ポート番号, ピン番号) */
        };
        group2 {
            psels = <NRF_PSEL(UART_RX, 0, 8)>;
            bias-pull-up;
        };
    };
    uart0_sleep: uart0_sleep {
        group1 {
            psels = <NRF_PSEL(UART_TX, 0, 6)>,
                    <NRF_PSEL(UART_RX, 0, 8)>;
            low-power-enable;
        };
    };
};

&uart0 {
    pinctrl-0 = <&uart0_default>;
    pinctrl-1 = <&uart0_sleep>;
    pinctrl-names = "default", "sleep";
    status = "okay";
};
```

**Nordic の特徴:** ピンは任意の GPIO に自由にルーティング可能 (GPIO Matrix)。STM32 のような固定 Alternate Function 制約がない。グループ化して省電力状態も定義する。

### ESP32: PERIPHERAL_SIGNAL_GPIOxx 形式

```dts
/* boards/espressif/esp32c3_devkitm/esp32c3_devkitm-pinctrl.dtsi */
&pinctrl {
    uart0_default: uart0_default {
        group1 {
            pinmux = <UART0_TX_GPIO21>;
            output-high;
        };
        group2 {
            pinmux = <UART0_RX_GPIO20>;
            bias-pull-up;
        };
    };

    spim2_default: spim2_default {
        group1 {
            pinmux = <SPIM2_MISO_GPIO2>,
                     <SPIM2_SCLK_GPIO6>;
        };
        group2 {
            pinmux = <SPIM2_MOSI_GPIO7>;
            output-low;
        };
    };
};
```

**ESP32 の特徴:** GPIO Matrix によりほぼ全ピンに任意ペリフェラルをルーティング可能。マクロ名が `ペリフェラル_信号_GPIO番号` の形式。

### pinctrl ベンダー比較

| 項目 | STM32 | Nordic (nRF) | ESP32 |
|------|-------|-------------|-------|
| マクロ | `STM32_PINMUX(port, pin, AF)` | `NRF_PSEL(func, port, pin)` | `PERIPHERAL_SIGNAL_GPIOxx` |
| ルーティング | 固定 AF テーブル制約あり | 自由ルーティング | GPIO Matrix で自由 |
| グループ化 | 不要 (1 ピン 1 ノード) | `group1`, `group2` で分類 | `group1`, `group2` で分類 |
| 省電力状態 | `pinctrl-1` で定義可能 | `sleep` 状態を明示定義 | 未対応 |
| AF 検証 | ビルド時に AF 番号を検証 | 検証なし (自由ルーティング) | 検証なし (GPIO Matrix) |

---

## 5. メモリレイアウト (DeviceTree)

### SoC DTSI レベル (disabled 状態で宣言)

```dts
/* dts/arm/st/f4/stm32f407.dtsi */
/ {
    soc {
        /* ペリフェラルは SoC DTSI で disabled 状態で宣言 */
        usart1: serial@40011000 {
            compatible = "st,stm32-usart", "st,stm32-uart";
            reg = <0x40011000 0x400>;
            clocks = <&rcc STM32_CLOCK_BUS_APB2 0x00000010>;
            interrupts = <37 0>;
            status = "disabled";  /* ボードレベルで有効化 */
        };
    };
};
```

### パッケージ DTSI レベル (メモリサイズ指定)

```dts
/* dts/arm/st/f4/stm32f407Xg.dtsi */
/ {
    sram0: memory@20000000 {
        compatible = "mmio-sram";
        reg = <0x20000000 DT_SIZE_K(128)>;
    };

    ccm0: memory@10000000 {
        compatible = "zephyr,memory-region", "mmio-sram";
        reg = <0x10000000 DT_SIZE_K(64)>;
        zephyr,memory-region = "CCM";
    };
};

&flash0 {
    reg = <0x08000000 DT_SIZE_K(1024)>;
};
```

### ボード DTS レベル (有効化と設定)

```dts
/* boards/st/stm32f4_disco/stm32f4_disco.dts */
/ {
    model = "STMicroelectronics STM32F4DISCOVERY board";
    compatible = "st,stm32f4-disco";

    chosen {
        zephyr,console = <&usart2>;
        zephyr,shell-uart = <&usart2>;
        zephyr,sram = <&sram0>;
        zephyr,flash = <&flash0>;
        zephyr,ccm = <&ccm0>;
        zephyr,code-partition = <&slot0_partition>;
    };

    leds {
        compatible = "gpio-leds";
        green_led_4: led_4 {
            gpios = <&gpiod 12 GPIO_ACTIVE_HIGH>;
            label = "User LD4";
        };
    };
};

/* ボードレベルでペリフェラルを有効化 */
&usart2 {
    pinctrl-0 = <&usart2_tx_pa2 &usart2_rx_pa3>;
    pinctrl-names = "default";
    current-speed = <115200>;
    status = "okay";  /* disabled → okay で有効化 */
};
```

### `chosen` ノードの主要フィールド

| フィールド | 型 | 説明 |
|-----------|-----|------|
| `zephyr,console` | phandle | コンソール出力デバイス |
| `zephyr,shell-uart` | phandle | シェル入出力デバイス |
| `zephyr,sram` | phandle | メイン SRAM (リンカスクリプト生成に使用) |
| `zephyr,flash` | phandle | メインフラッシュ (リンカスクリプト生成に使用) |
| `zephyr,ccm` | phandle | CCM RAM (Cortex-M4 固有) |
| `zephyr,code-partition` | phandle | コード配置パーティション |
| `zephyr,entropy` | phandle | エントロピーソース (RNG) |
| `zephyr,canbus` | phandle | デフォルト CAN バス |
| `zephyr,display` | phandle | デフォルトディスプレイ |
| `zephyr,bt-hci` | phandle | Bluetooth HCI デバイス |
| `zephyr,ieee802154` | phandle | IEEE 802.15.4 デバイス |

---

## 6. Kconfig 設定

### Kconfig.<board> (ボード Kconfig シンボル)

```kconfig
# boards/st/stm32f4_disco/Kconfig.stm32f4_disco
config BOARD_STM32F4_DISCO
    bool "STM32F4 Discovery"
    depends on SOC_STM32F407XG
    select SOC_SERIES_STM32F4X
```

### _defconfig (デフォルト Kconfig 値)

```kconfig
# boards/st/stm32f4_disco/stm32f4_disco_defconfig
CONFIG_SOC_SERIES_STM32F4X=y
CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC=168000000
CONFIG_SERIAL=y
CONFIG_UART_STM32=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
CONFIG_GPIO=y
CONFIG_CLOCK_CONTROL=y
CONFIG_PINCTRL=y
CONFIG_CLOCK_STM32_HSE_CLOCK=8000000
CONFIG_CLOCK_STM32_SYSCLK_SRC_PLL=y
CONFIG_CLOCK_STM32_PLL_SRC_HSE=y
```

### SoC レベルの Kconfig (何が SoC で提供され、何をボードが有効化するか)

```
SoC Kconfig (depends 宣言のみ):
  CONFIG_SOC_STM32F407XG  → CPU_CORTEX_M4, CPU_HAS_FPU, FPU を select
  CONFIG_CLOCK_CONTROL    → available (SoC に RCC があるため)

Board _defconfig (実際に有効化):
  CONFIG_SERIAL=y         → SoC で利用可能な UART を有効化
  CONFIG_GPIO=y           → SoC で利用可能な GPIO を有効化
  CONFIG_CLOCK_CONTROL=y  → クロック制御を有効化
```

---

## 7. board.cmake (デバッグランナー設定)

```cmake
# boards/st/stm32f4_disco/board.cmake
board_runner_args(openocd "--target=stm32f4x")
board_runner_args(jlink "--device=STM32F407VG")
board_runner_args(stm32cubeprogrammer "--port=swd")
board_runner_args(pyocd "--target=stm32f407vg")

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/stm32cubeprogrammer.board.cmake)
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
```

```cmake
# boards/nordic/nrf52840dk/board.cmake
board_runner_args(jlink "--device=nrf52840_xxaa" "--speed=4000")
board_runner_args(pyocd "--target=nrf52840" "--frequency=4000000")
board_runner_args(nrfjprog)

include(${ZEPHYR_BASE}/boards/common/nrfjprog.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
```

---

## 8. マルチコアサポート (nRF5340)

nRF5340 は Cortex-M33 (アプリケーション) + Cortex-M33 (ネットワーク) のデュアルコア構成。Zephyr は各コアを独立したボードバリアントとして扱う。

### board.yml

```yaml
board:
  name: nrf5340dk
  vendor: nordic
  socs:
    - name: nrf5340
      variants:
        - name: nrf5340/cpuapp
        - name: nrf5340/cpunet
        - name: nrf5340/cpuapp/ns   # TrustZone Non-Secure
```

### DTS 分離

```
nrf5340dk/
├── nrf5340dk_nrf5340_cpuapp.dts       アプリケーションコア DTS
├── nrf5340dk_nrf5340_cpuapp_defconfig アプリケーションコア Kconfig
├── nrf5340dk_nrf5340_cpunet.dts       ネットワークコア DTS
├── nrf5340dk_nrf5340_cpunet_defconfig ネットワークコア Kconfig
└── nrf5340dk_nrf5340_cpuapp_ns.dts    Non-Secure バリアント
```

### ビルドコマンド

```bash
# アプリケーションコア
west build -b nrf5340dk/nrf5340/cpuapp samples/hello_world

# ネットワークコア
west build -b nrf5340dk/nrf5340/cpunet samples/bluetooth/hci_ipc

# Non-Secure (TrustZone)
west build -b nrf5340dk/nrf5340/cpuapp/ns samples/tfm_integration
```

---

## 9. ビルド時バリデーション

### DTS バインディング型システム

```yaml
# dts/bindings/clock/st,stm32f4-pll-clock.yaml
properties:
  div-p:
    type: int
    required: true
    enum: [2, 4, 6, 8]          # 許容値の列挙制約
    description: "PLL division factor"
  mul-n:
    type: int
    required: true
    # const: 不使用 (範囲制約は description で記述)
    description: "50..432"
```

**バインディングで使用可能な制約:**

| 制約 | 説明 | 例 |
|------|------|---|
| `type` | プロパティの型 | `int`, `string`, `boolean`, `phandle`, `phandle-array`, `array` |
| `required` | 必須プロパティか | `true` / `false` |
| `enum` | 許容値リスト | `[2, 4, 6, 8]` |
| `const` | 固定値 | `2` |
| `default` | デフォルト値 | `1` |
| `description` | 説明文 | ドキュメント生成に使用 |
| `specifier-space` | 参照先のセル空間 | `gpio`, `clock` |

### Kconfig 検証

```kconfig
config CLOCK_STM32_PLL_M_DIVISOR
    int "PLL M divisor"
    range 2 63           # ビルド時範囲チェック
    default 8

config CLOCK_STM32_PLL_P_DIVISOR
    int "PLL P divisor"
    default 2
    depends on CLOCK_STM32_SYSCLK_SRC_PLL
    # Kconfig は int 範囲のみ。enum 制約は DTS バインディング側
```

### 検証パイプライン

```
1. DTS バインディング検証 (dtc + edtlib)
   → 型チェック、required チェック、enum 制約
2. Kconfig 検証
   → range チェック、depends 条件、select 整合性
3. CMake 検証
   → ターゲット依存関係、ツールチェーン互換性
```

---

## 10. SoC vs Board のデフォルト分担

| 設定項目 | SoC DTSI (disabled) | Board DTS (有効化) |
|----------|---------------------|-------------------|
| ペリフェラルレジスタアドレス | `reg = <0x40011000 0x400>` | -- |
| 割り込み番号 | `interrupts = <37 0>` | -- |
| クロックバス接続 | `clocks = <&rcc ...>` | -- |
| ペリフェラル有効化 | `status = "disabled"` | `status = "okay"` |
| ピン設定 | -- | `pinctrl-0 = <...>` |
| ボーレート等パラメータ | -- | `current-speed = <115200>` |
| LED / ボタン | -- | `gpios = <&gpiod 12 ...>` |
| メモリサイズ | パッケージ DTSI で指定 | -- |
| chosen ノード | -- | `zephyr,console = <...>` |

**原則:** SoC DTSI は「何があるか」を宣言し、Board DTS は「何を使うか」を決定する。

---

## 長所と短所

| 観点 | 評価 | 詳細 |
|------|------|------|
| クロック定義 | ◎ | DTS バインディングで PLL パラメータを型安全に記述。enum 制約あり |
| ピン設定 | ◎ | ベンダー別 pinctrl マクロで統一的記述。AF 検証あり (STM32) |
| メモリ定義 | ◎ | DTS から自動リンカ生成。CCM 等の特殊領域も対応 |
| デバッグ設定 | ◎ | board.cmake で複数ランナーをサポート |
| マルチコア | ◎ | コアごとの DTS/Kconfig 完全分離 |
| ビルド時検証 | ◎ | DTS バインディング + Kconfig の二重検証 |
| 学習コスト | △ | DTS + Kconfig + CMake + board.yml の四重設定 |
| ファイル数 | △ | 最低 5 ファイル、マルチコアでは倍増 |

---

## UMI への示唆

1. **SoC DTSI の「disabled 宣言 → Board で有効化」パターン** は UMI の board 設定で参考になる。デバイス DB にペリフェラルを全宣言し、ボード設定で使用するもののみ有効化する方式
2. **pinctrl のベンダー別マクロ** は Lua テーブルでより簡潔に表現可能。STM32_PINMUX のビットフィールド操作は C++ constexpr で型安全に実装できる
3. **DTS バインディングの型システム** (enum, const, required) は UMI の Lua スキーマ検証の参考モデル。xmake の option 型検証と組み合わせ可能
4. **chosen ノード** のデフォルトデバイス指定パターンは UMI のボード設定で直接採用可能 (`console_uart`, `debug_probe` 等)
5. **マルチコアの DTS/Kconfig 完全分離** はデュアルコア MCU (STM32H7, nRF5340) 対応で必要になる設計パターン

---

## 参照

- [Board Porting Guide](https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html)
- [HWMv2 Board Model](https://docs.zephyrproject.org/latest/hardware/porting/hwmv2.html)
- [Devicetree Bindings Index](https://docs.zephyrproject.org/latest/build/dts/api/bindings.html)
- [Pin Control](https://docs.zephyrproject.org/latest/hardware/pinctrl/index.html)
- [Clock Control](https://docs.zephyrproject.org/latest/services/pm/device_runtime.html)
- [STM32 Clock Control Bindings](https://github.com/zephyrproject-rtos/zephyr/tree/main/dts/bindings/clock)
- [nRF5340 DK Board](https://github.com/zephyrproject-rtos/zephyr/tree/main/boards/nordic/nrf5340dk)
- [STM32F4 Discovery Board](https://github.com/zephyrproject-rtos/zephyr/tree/main/boards/st/stm32f4_disco)
