# BSP 継承パターン総合調査

**ステータス:** 調査完了
**作成日:** 2026-02-09
**目的:** UMI の BSP アーキテクチャ設計判断のための、組み込みフレームワーク・ビルドシステム・ソフトウェア設計パターンの包括的調査
**調査範囲:** 8 リサーチエージェントによる並列調査結果の統合

---

## 目次

1. [Zephyr RTOS](#1-zephyr-rtos)
2. [ESP-IDF](#2-esp-idf)
3. [PlatformIO](#3-platformio)
4. [Mbed OS](#4-mbed-os)
5. [CMSIS-Pack](#5-cmsis-pack)
6. [Arduino](#6-arduino)
7. [Rust Embedded](#7-rust-embedded)
8. [NuttX RTOS](#8-nuttx-rtos)
9. [modm](#9-modm)
10. [libopencm3](#10-libopencm3)
11. [その他のビルドシステム](#11-その他のビルドシステム)
12. [他ドメインのオーバーレイ・デルタパターン](#12-他ドメインのオーバーレイデルタパターン)
13. [xmake の BSP 関連機能](#13-xmake-の-bsp-関連機能)
14. [開発者体験 (DX) の知見](#14-開発者体験-dx-の知見)
15. [総合比較表](#15-総合比較表)
16. [参照ソース](#16-参照ソース)

---

## 1. Zephyr RTOS

### 1.1 HWMv2 ボードモデル

Zephyr v3.6 以降で導入された Hardware Model v2 (HWMv2) は、従来のフラットなボードディレクトリ構造をベンダーベースの階層構造に再編した。

**ディレクトリ構造:**

```
zephyr/
├── boards/
│   ├── st/                           ベンダーディレクトリ
│   │   └── nucleo_f411re/            ボードディレクトリ
│   │       ├── board.yml             メタデータ（SoC・リビジョン・バリアント）
│   │       ├── nucleo_f411re.dts     DeviceTree ソース
│   │       ├── nucleo_f411re_defconfig  デフォルト Kconfig
│   │       ├── Kconfig.nucleo_f411re SoC 選択チェーン
│   │       └── board.cmake           デバッグランナー設定
│   └── nordic/
│       └── nrf52840dk/
│           ├── board.yml
│           └── ...
├── soc/
│   └── st/stm32/                     SoC 定義（ベンダー別）
│       ├── soc.yml                   ファミリ/シリーズ/SoC 階層
│       ├── stm32f4x/                 シリーズ固有
│       └── common/                   ベンダー共通
└── dts/arm/st/f4/                    DeviceTree ソース
    ├── stm32f4.dtsi                  ファミリ基底
    ├── stm32f411.dtsi                SoC 固有
    └── stm32f411Xe.dtsi              パッケージ/メモリ固有
```

**board.yml の構造:**

```yaml
board:
  name: nucleo_f411re
  vendor: st
  socs:
    - name: stm32f411xe
  runners:
    - openocd
    - jlink
    - stlink
  revisions:
    - name: "Rev.A"
    - name: "Rev.B"
      default: true
```

**soc.yml の構造:**

```yaml
family:
  - name: stm32f4
    socs:
      - name: stm32f401xe
      - name: stm32f407xg
      - name: stm32f411xe
    series:
      - name: stm32f40x
        socs:
          - name: stm32f405xx
          - name: stm32f407xx
```

### 1.2 DeviceTree オーバーレイメカニズム

Zephyr の DeviceTree は最大 6-8 段の include チェーンでハードウェアを記述する。

**include チェーンの例（nucleo_f411re）:**

```
armv7-m.dtsi              (1) Cortex-M 共通プロパティ
  └── stm32f4.dtsi        (2) STM32F4 ファミリ共通ペリフェラル
      └── stm32f411.dtsi  (3) STM32F411 SoC 固有ペリフェラル
          └── stm32f411Xe.dtsi  (4) パッケージ（ピン数・メモリサイズ）
              └── nucleo_f411re.dts  (5) ボード固有（LED, ボタン, コネクタ）
                  └── app.overlay  (6) アプリケーション固有（ユーザー定義）
```

**オーバーレイファイル (.overlay):**

`.overlay` ファイルは `.dts` と同じ構文だが、ベース DTS の上に差分を適用する。ビルドシステムが自動検出するルール:

| 検出パス | 条件 |
|---------|------|
| `boards/<board>.overlay` | ボード名に一致すれば自動適用 |
| `app.overlay` | プロジェクトルートに存在すれば自動適用 |
| `-DDTC_OVERLAY_FILE=<path>` | CMake 変数で明示指定 |
| `EXTRA_DTC_OVERLAY_FILE` | 追加オーバーレイ（複数指定可） |

```dts
/* boards/nucleo_f411re.overlay — ボード固有の上書き */
&usart2 {
    status = "okay";
    current-speed = <115200>;
    pinctrl-0 = <&usart2_tx_pa2 &usart2_rx_pa3>;
};

&i2c1 {
    status = "okay";
    /* ボード上の I2C デバイスを追加 */
    sensor@48 {
        compatible = "ti,tmp102";
        reg = <0x48>;
    };
};
```

### 1.3 Kconfig による SoC/ボード選択チェーン

Zephyr はボード選択から SoC・アーキテクチャの Kconfig シンボルを連鎖的に有効化する。

```kconfig
# Kconfig.nucleo_f411re
config BOARD_NUCLEO_F411RE
    bool "ST Nucleo F411RE"
    depends on SOC_STM32F411XE
    select SOC_SERIES_STM32F4X

# SoC レベル
config SOC_STM32F411XE
    bool
    select SOC_SERIES_STM32F4X
    select CPU_CORTEX_M4
    select CPU_HAS_FPU

# アーキテクチャレベル
config CPU_CORTEX_M4
    bool
    select CPU_CORTEX_M
    select ARMV7_M_ARMV8_M_MAINLINE
```

この `select` チェーンにより、ボードを指定するだけで必要な全コンフィグが自動的に有効化される。

### 1.4 リンカスクリプトテンプレート生成

Zephyr はリンカスクリプトを DeviceTree から自動生成する。

**処理パイプライン:**

```
DeviceTree (.dts/.dtsi)
  → dtc (DeviceTree Compiler)
    → gen_defines.py
      → C ヘッダマクロ (devicetree_generated.h)
        → C プリプロセッサ
          → linker.ld テンプレート展開
            → linker.cmd (最終リンカスクリプト)
```

DeviceTree のメモリノード:

```dts
sram0: memory@20000000 {
    compatible = "mmio-sram";
    reg = <0x20000000 DT_SIZE_K(128)>;
};
/ { chosen { zephyr,sram = &sram0; }; };
```

テンプレート内でマクロ参照:

```ld
/* zephyr/include/zephyr/arch/arm/cortex_m/scripts/linker.ld */
#include <zephyr/linker/linker-defs.h>

MEMORY {
    FLASH (rx) : ORIGIN = ROM_ADDR, LENGTH = ROM_SIZE
    SRAM (rwx) : ORIGIN = RAM_ADDR, LENGTH = RAM_SIZE
}
```

### 1.5 ボード拡張メカニズム

HWMv2 では `board.yml` 内の `extend:` キーワードで既存ボードを拡張できる。

```yaml
# boards/vendor/my_custom_board/board.yml
board:
  name: my_custom_board
  extend: nucleo_f411re    # 既存ボードを基底として拡張
  vendor: mycompany
  socs:
    - name: stm32f411xe
```

拡張されたボードは基底ボードの DTS・Kconfig をベースに、差分のみを追加定義する。

### 1.6 シールドメカニズム

シールド（拡張基板）は独立した DTS オーバーレイとして定義される。

```
zephyr/boards/shields/
└── x_nucleo_iks01a3/
    ├── x_nucleo_iks01a3.overlay   シールドの DTS
    ├── Kconfig.shield              シールドの Kconfig
    └── doc/
```

ビルド時に `-DSHIELD=x_nucleo_iks01a3` で有効化。複数シールドのスタックも可能。

### 1.7 Out-of-Tree ボードサポート

**BOARD_ROOT:**

```cmake
# CMakeLists.txt
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/custom_boards)
```

`custom_boards/boards/<vendor>/<board>/` にボード定義を配置すると、Zephyr 本体を変更せずにカスタムボードを追加できる。

**Module システム:**

```yaml
# zephyr/module.yml
build:
  board_root: .
  soc_root: .
  dts_root: .
```

West モジュールとして配布すれば、`west update` でボード定義を取得可能。

### 1.8 評価

| 観点 | 評価 | 詳細 |
|------|------|------|
| データ継承 | ◎ | DTS include チェーン + Kconfig select チェーン |
| リンカ生成 | ◎ | テンプレートから完全自動生成 |
| ボード拡張 | ◎ | `extend:` + overlay + shield の多段構成 |
| Out-of-Tree | ◎ | BOARD_ROOT, Module, Shield の3方式 |
| 学習コスト | △ | DeviceTree + Kconfig + CMake の三重設定 |
| ファイル数 | △ | 1ボード最低 5 ファイル |
| ツールチェーン依存 | △ | dtc, gen_defines.py, CMake の3ツール必須 |

**参照:**
- [Board Porting Guide](https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html)
- [HWMv2 Migration](https://docs.zephyrproject.org/latest/hardware/porting/hwmv2.html)
- [Devicetree HOWTOs](https://docs.zephyrproject.org/latest/build/dts/howtos.html)
- [Shield Guide](https://docs.zephyrproject.org/latest/hardware/porting/shields.html)

---

## 2. ESP-IDF

### 2.1 4層抽象化アーキテクチャ

ESP-IDF は明確な 4 層のハードウェア抽象化を持つ。

```
┌─────────────────────────────────────────┐
│  Driver API (esp_driver_gpio 等)        │  ← ユーザー向け API
├─────────────────────────────────────────┤
│  HAL (hal/*.h)                          │  ← OS 非依存、static inline
├─────────────────────────────────────────┤
│  LL  (hal/*_ll.h)                       │  ← レジスタ操作ラッパー
├─────────────────────────────────────────┤
│  SoC (soc/<chip>/include/soc/)          │  ← レジスタ定義・SoC 能力宣言
└─────────────────────────────────────────┘
```

| 層 | 場所 | 役割 | OS 依存 |
|---|------|------|---------|
| SoC | `components/soc/<chip>/` | レジスタ定義、`soc_caps.h` | なし |
| LL (Low-Level) | `components/hal/<chip>/include/hal/` | static inline レジスタ操作 | なし |
| HAL | `components/hal/include/hal/` | チップ共通の HAL インタフェース | なし |
| Driver | `components/esp_driver_*/` | FreeRTOS 統合、ユーザー API | あり |

### 2.2 soc_caps.h 宣言パターンと Kconfig 自動生成

ESP-IDF の最も特徴的な設計は `soc_caps.h` による能力宣言パターンである。

**soc_caps.h（唯一の源泉）:**

```c
/* components/soc/esp32s3/include/soc/soc_caps.h */
#define SOC_UART_NUM            3
#define SOC_UART_FIFO_LEN       128
#define SOC_UART_SUPPORT_RTC_CLK    1

#define SOC_I2C_NUM             2
#define SOC_I2C_SUPPORT_SLAVE   1

#define SOC_GPIO_PIN_COUNT      49
#define SOC_GPIO_SUPPORT_RTC_INDEPENDENT 1

#define SOC_ADC_PERIPH_NUM      2
#define SOC_ADC_MAX_CHANNEL_NUM 10
#define SOC_ADC_ARBITER_SUPPORTED 1
```

**自動 Kconfig 生成パイプライン:**

```
soc_caps.h (#define マクロ)
  → gen_soc_caps_kconfig.py (Python スクリプト)
    → Kconfig.soc_caps.in (自動生成)
      → menuconfig で可視化
```

```python
# gen_soc_caps_kconfig.py の処理
# SOC_UART_NUM = 3 → config SOC_UART_NUM / default 3
# SOC_UART_SUPPORT_RTC_CLK = 1 → config SOC_UART_SUPPORT_RTC_CLK / default y
```

生成される `Kconfig.soc_caps.in`:

```kconfig
# Auto-generated from soc_caps.h — DO NOT EDIT
config SOC_UART_NUM
    int
    default 3

config SOC_UART_SUPPORT_RTC_CLK
    bool
    default y
```

この設計により、C ヘッダと Kconfig の間でデータの不整合が発生しない。pre-commit フックで同期を強制する。

### 2.3 リンカフラグメントシステム

ESP-IDF は独自の `ldgen` ツールでリンカスクリプトを生成する。

**リンカフラグメント (.lf ファイル):**

```
[mapping:wifi]
archive: libwifi.a
entries:
    * (noflash)              # WiFi ライブラリ全体を IRAM に配置

[mapping:freertos]
archive: libfreertos.a
entries:
    port (noflash)           # FreeRTOS ポート層を IRAM に配置
    tasks:vTaskSwitchContext (noflash)  # 特定関数のみ IRAM に
```

**テンプレートと生成:**

```
components/esp_system/ld/<chip>/
├── memory.ld.in              メモリ領域定義（チップ固有）
└── sections.ld.in            セクション配置（マーカー付きテンプレート）
```

`sections.ld.in` 内のマーカー:

```ld
.iram0.text : {
    /* ldgen がここにフラグメントから集めたエントリを挿入 */
    mapping[iram0_text]
} > iram0_0_seg
```

ldgen の処理:

```
.lf ファイル群 + sections.ld.in
  → ldgen.py
    → sections.ld (マーカーを実際のオブジェクト配置に置換)
```

### 2.4 sdkconfig.defaults レイヤリング

ESP-IDF は sdkconfig のデフォルト値を複数レイヤーで合成する。

```
sdkconfig.defaults                  全ターゲット共通
sdkconfig.defaults.<IDF_TARGET>     チップ固有 (例: sdkconfig.defaults.esp32s3)
sdkconfig                          ユーザーカスタマイズ（menuconfig で生成）
```

適用順序: `sdkconfig.defaults` → `sdkconfig.defaults.<target>` → `sdkconfig`。後の設定が前の設定を上書きする。

### 2.5 コンポーネントモデル

各コンポーネントは独立した CMakeLists.txt を持ち、依存関係を宣言する。

```cmake
# components/esp_driver_uart/CMakeLists.txt
idf_component_register(
    SRCS "src/uart.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio hal soc
    PRIV_REQUIRES esp_pm
)
```

**評価:**

| 観点 | 評価 | 詳細 |
|------|------|------|
| 単一源泉 | ◎ | soc_caps.h が唯一の源泉、Kconfig は自動生成 |
| リンカ生成 | ◎ | ldgen + フラグメントで分散定義・自動統合 |
| データ継承 | △ | ファイルパス (`<chip>/`) による暗黙的切り替え、明示的継承なし |
| 学習コスト | △ | Kconfig + CMake + soc_caps.h + ldgen の四重構成 |
| コンポーネント分離 | ◎ | 明確な依存宣言と選択的ビルド |

**参照:**
- [Hardware Abstraction](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/hardware-abstraction.html)
- [Linker Script Generation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/linker-script-generation.html)
- [Build System](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html)

---

## 3. PlatformIO

### 3.1 board.json の構造

PlatformIO は各ボードを 1 つの JSON ファイルで記述する。

```json
{
  "build": {
    "core": "stm32",
    "cpu": "cortex-m4",
    "extra_flags": "-DSTM32F446xx",
    "f_cpu": "180000000L",
    "framework": ["arduino", "stm32cube", "mbed"],
    "mcu": "stm32f446ret6",
    "product_line": "STM32F446xx",
    "variant": "NUCLEO_F446RE"
  },
  "connectivity": ["can"],
  "debug": {
    "default_tools": ["stlink"],
    "jlink_device": "STM32F446RE",
    "openocd_board": "st_nucleo_f4.cfg",
    "openocd_target": "stm32f4x",
    "svd_path": "STM32F446x.svd"
  },
  "frameworks": ["arduino", "stm32cube", "mbed"],
  "name": "ST Nucleo F446RE",
  "upload": {
    "maximum_ram_size": 131072,
    "maximum_size": 524288,
    "protocol": "stlink",
    "protocols": ["jlink", "stlink", "blackmagic", "mbed"]
  },
  "url": "https://www.st.com/...",
  "vendor": "ST"
}
```

**主要フィールド一覧:**

| セクション | フィールド | 用途 |
|-----------|----------|------|
| `build.cpu` | コア名 | コンパイラフラグ決定 |
| `build.mcu` | MCU 型番 | define マクロ |
| `build.extra_flags` | 追加フラグ | ファミリ/デバイス define |
| `build.f_cpu` | CPU 周波数 | F_CPU マクロ |
| `build.variant` | バリアント名 | pins_arduino.h 参照 |
| `debug.default_tools` | デフォルトプローブ | デバッグ開始時の初期選択 |
| `debug.openocd_target` | OpenOCD ターゲット | デバッグ設定生成 |
| `debug.svd_path` | SVD ファイルパス | レジスタビューア |
| `upload.maximum_ram_size` | RAM サイズ (bytes) | リンカスクリプト生成 |
| `upload.maximum_size` | Flash サイズ (bytes) | リンカスクリプト生成 |
| `upload.protocol` | デフォルトプロトコル | フラッシュ書き込み |

### 3.2 platform.py による動的パッケージ選択

各プラットフォーム（ststm32, espressif32 等）は `platform.py` で動的にパッケージを選択する。

```python
# platforms/ststm32/platform.py
class Ststm32Platform(PlatformBase):
    def configure_default_packages(self, variables, targets):
        board = variables.get("board")
        board_config = self.board_config(board)
        mcu = board_config.get("build.mcu", "")
        frameworks = variables.get("pioframework", [])

        # フレームワークに応じてパッケージを有効化
        if "arduino" in frameworks:
            self.packages["framework-arduinoststm32"]["optional"] = False
        if "stm32cube" in frameworks:
            self.packages["framework-stm32cubef4"]["optional"] = False

        # MCU ファミリに応じてツールチェーンを選択
        if mcu.startswith("stm32wl"):
            # STM32WL はデュアルコアで特殊なリンカが必要
            self.packages["toolchain-gccarmnoneeabi"]["version"] = ">=10.3"

        return PlatformBase.configure_default_packages(self, variables, targets)
```

### 3.3 リンカスクリプト 3 段フォールバック

PlatformIO のリンカスクリプト解決順序:

```
1. ユーザー指定     board_build.ldscript = custom.ld
     ↓ なければ
2. フレームワーク   framework-arduinoststm32/variants/<variant>/ldscript.ld
     ↓ なければ
3. テンプレート生成  board.json の upload.maximum_size / maximum_ram_size から生成
```

テンプレート生成は FLASH + RAM の 2 リージョンのみ対応。CCM-RAM、DTCM、複数 SRAM 等は非対応。

### 3.4 board.json に継承がない問題

PlatformIO の最大の弱点は **board.json 間に継承メカニズムが存在しない** ことである。

```
platforms/ststm32/boards/
├── nucleo_f401re.json        STM32F401RE の全データ
├── nucleo_f411re.json        STM32F411RE の全データ（ほぼコピー）
├── nucleo_f446re.json        STM32F446RE の全データ（ほぼコピー）
├── blackpill_f411ce.json     STM32F411CE の全データ（ほぼコピー）
└── ... (291 ファイル)
```

同一 MCU ファミリのボード間で `build.cpu`, `build.extra_flags`, `debug.openocd_target` 等が完全に重複している。MCU の情報が変更された場合、関連する全ての board.json を手動で更新する必要がある。

### 3.5 プロジェクトローカル boards/ ディレクトリ

```
my_project/
├── boards/
│   └── my_custom_board.json    プロジェクトローカルボード定義
├── platformio.ini
└── src/
```

`platformio.ini` で `boards_dir = boards` を指定するか、デフォルトの `boards/` ディレクトリに JSON を置くだけでカスタムボードが使える。

**評価:**

| 観点 | 評価 | 詳細 |
|------|------|------|
| ボード追加の容易さ | ◎ | JSON 1 ファイルのみ |
| デバッグ統合 | ◎ | 300+ ボードで即座にデバッグ可能 |
| データ継承 | ✕ | board.json 間に継承なし、大量重複 |
| リンカ生成 | △ | 2 リージョン限定テンプレート |
| 動的選択 | ○ | platform.py でパッケージの条件選択 |

**参照:**
- [Custom Embedded Boards](https://docs.platformio.org/en/latest/platforms/creating_board.html)
- [platform-ststm32 (GitHub)](https://github.com/platformio/platform-ststm32)

---

## 4. Mbed OS

### 4.1 targets.json の `inherits` チェーン

Mbed OS の targets.json は JSON ベースの継承システムとしては最も体系的な実装の一つである。

```json
{
  "Target": {
    "boot_stack_size": "0x400",
    "default_lib": "std",
    "network-default-interface-type": "ETHERNET"
  },
  "MCU_STM32": {
    "inherits": ["Target"],
    "device_has_add": ["SERIAL", "STDIO_MESSAGES", "FLASH"],
    "extra_labels_add": ["STM32"]
  },
  "MCU_STM32F4": {
    "inherits": ["MCU_STM32"],
    "core": "Cortex-M4F",
    "device_has_add": ["SERIAL_ASYNCH", "FLASH", "MPU"],
    "extra_labels_add": ["STM32F4"]
  },
  "MCU_STM32F411xE": {
    "inherits": ["MCU_STM32F4"],
    "device_has_add": ["TRNG"],
    "extra_labels_add": ["STM32F411xE"],
    "macros_add": ["STM32F411xE"],
    "mbed_rom_start": "0x08000000",
    "mbed_rom_size": "0x80000",
    "mbed_ram_start": "0x20000000",
    "mbed_ram_size": "0x20000"
  },
  "NUCLEO_F411RE": {
    "inherits": ["MCU_STM32F411xE"],
    "detect_code": ["0740"],
    "device_name": "STM32F411RETx"
  }
}
```

### 4.2 `_add` / `_remove` プロパティ修飾子

`_add` と `_remove` サフィックスにより、親から継承した配列型プロパティの差分管理が可能。

```json
{
  "MY_BOARD": {
    "inherits": ["MCU_STM32F411xE"],
    "device_has_add": ["USB_DEVICE"],
    "device_has_remove": ["TRNG"],
    "macros_add": ["MY_BOARD=1"],
    "extra_labels_add": ["MY_BOARD"]
  }
}
```

| 修飾子 | 動作 | 例 |
|--------|------|---|
| `property_add` | 親の配列にアイテムを追加 | `"device_has_add": ["USB"]` |
| `property_remove` | 親の配列からアイテムを削除 | `"device_has_remove": ["TRNG"]` |
| `property` (無修飾) | 親の値を完全に上書き | `"mbed_rom_size": "0x40000"` |

### 4.3 解決順序

Mbed OS のターゲット解決は Python の MRO (Method Resolution Order) に類似した順序で行われる。

```
NUCLEO_F411RE
  → MCU_STM32F411xE
    → MCU_STM32F4
      → MCU_STM32
        → Target (基底)
```

多重継承も可能:

```json
{
  "MY_DUAL_BOARD": {
    "inherits": ["MCU_STM32F4", "SOME_MIXIN"],
    "resolution_order_override": ["MCU_STM32F4", "SOME_MIXIN", "MCU_STM32", "Target"]
  }
}
```

### 4.4 custom_targets.json

ユーザーはプロジェクトルートに `custom_targets.json` を置くことで、Mbed OS 本体を変更せずにカスタムターゲットを追加できる。

```json
// custom_targets.json
{
  "MY_CUSTOM_BOARD": {
    "inherits": ["MCU_STM32F411xE"],
    "detect_code": ["FFFF"],
    "device_name": "STM32F411CEUx",
    "mbed_rom_size": "0x80000",
    "mbed_ram_size": "0x20000"
  }
}
```

### 4.5 EOL ステータスと教訓

Mbed OS は 2026 年 7 月に EOL を迎える。targets.json の設計には以下の教訓がある:

| 良かった点 | 問題点 |
|-----------|--------|
| `inherits` による明確な継承チェーン | targets.json が巨大モノリシック（1ファイルに全ターゲット） |
| `_add`/`_remove` による差分管理 | チェーンが深くなると値の出所の追跡が困難 |
| custom_targets.json によるユーザー拡張 | JSON には条件分岐がなく、複雑なロジック表現が不可能 |
| Linter による継承パターンの自動検証 | Python MRO 類似の解決順序は非プログラマに直感的でない |

**評価:**

| 観点 | 評価 | 詳細 |
|------|------|------|
| 継承メカニズム | ◎ | `inherits` + `_add`/`_remove` が直感的 |
| 単一源泉 | ○ | 1 ファイルに集約だが巨大 |
| メモリ定義 | △ | JSON 内値とリンカのプリプロセッサマクロの二重管理 |
| ユーザー拡張 | ○ | custom_targets.json |
| 保守性 | ✕ | 巨大モノリシック JSON、EOL |

**参照:**
- [Adding and Configuring Targets](https://os.mbed.com/docs/mbed-os/v6.16/program-setup/adding-and-configuring-targets.html)
- [Mbed OS targets.json (GitHub)](https://github.com/ARMmbed/mbed-os/blob/master/targets/targets.json)

---

## 5. CMSIS-Pack

### 5.1 4 階層デバイスヒエラルキー

ARM CMSIS-Pack は業界標準として 4 階層のデバイス定義を規定する。

```
Dvendor (STMicroelectronics)
  └── Dfamily (STM32F4)
      └── DsubFamily (STM32F407)
          └── Dname (STM32F407VGTx)
              └── Dvariant (任意)
```

各階層でプロパティが累積的に継承され、下位階層で上書き可能。

### 5.2 PDSC (Pack Description) XML 構造

```xml
<?xml version="1.0" encoding="UTF-8"?>
<package xmlns:xs="http://www.w3.org/2001/XMLSchema-instance"
         schemaVersion="1.7.28">
  <vendor>STMicroelectronics</vendor>
  <name>STM32F4xx_DFP</name>

  <devices>
    <family Dfamily="STM32F4" Dvendor="STMicroelectronics:13">
      <processor Dcore="Cortex-M4" DcoreVersion="r0p1"
                 Dfpu="SP_FPU" Dendian="Little-endian"
                 Dmpu="MPU" Dclock="180000000"/>

      <subFamily DsubFamily="STM32F407">
        <processor Dclock="168000000"/>

        <device Dname="STM32F407VGTx">
          <memory name="IROM1" access="rx"
                  start="0x08000000" size="0x100000"
                  startup="1" default="1"/>
          <memory name="IRAM1" access="rwx"
                  start="0x20000000" size="0x20000"
                  default="1"/>
          <memory name="IRAM2" access="rwx"
                  start="0x10000000" size="0x10000"/>
          <algorithm name="Flash/STM32F4xx_1024.FLM"
                     start="0x08000000" size="0x100000"
                     default="1"/>
          <debug svd="SVD/STM32F407.svd"/>
        </device>

        <device Dname="STM32F407VETx">
          <memory name="IROM1" access="rx"
                  start="0x08000000" size="0x80000"
                  startup="1" default="1"/>
          <!-- IRAM1/IRAM2 は subFamily から継承 -->
        </device>
      </subFamily>
    </family>
  </devices>
</package>
```

### 5.3 メモリ・デバッグ・フラッシュアルゴリズム定義

**メモリ定義の属性:**

| 属性 | 意味 | 値 |
|------|------|---|
| `name` | メモリ領域名 | IROM1, IRAM1, IRAM2 等 |
| `access` | アクセス権限 | `r`(read), `w`(write), `x`(execute), `s`(secure), `n`(non-secure), `p`(privileged) |
| `start` | 開始アドレス | 16 進数 |
| `size` | サイズ | 16 進数 |
| `startup` | 起動メモリか | `1` / `0` |
| `default` | デフォルト配置先か | `1` / `0` |

**フラッシュアルゴリズム:**

```xml
<algorithm name="Flash/STM32F4xx_1024.FLM"
           start="0x08000000" size="0x100000"
           RAMstart="0x20000000" RAMsize="0x1000"
           default="1"/>
```

`.FLM` ファイルはフラッシュ書き込み用の ELF バイナリ。pyOCD, probe-rs, Keil 等のデバッグツールが共通で使用する。

### 5.4 DFP vs BSP パック

| パック種別 | 内容 | 提供者 |
|-----------|------|--------|
| DFP (Device Family Pack) | デバイス定義、SVD、フラッシュアルゴリズム、スタートアップ | シリコンベンダー |
| BSP (Board Support Pack) | ボード固有の初期化、ピン設定、デフォルトコンフィグ | ボードベンダー |

BSP パックは DFP パックの特定デバイスを参照し、ボード固有の設定を追加する。

**評価:**

| 観点 | 評価 | 詳細 |
|------|------|------|
| 4 階層継承 | ◎ | family → subFamily → device → variant の属性累積 |
| メモリ定義 | ◎ | XML 内に構造化されたメモリ記述 |
| ツール統合 | ◎ | Keil, IAR, pyOCD, probe-rs, SEGGER 等が共通対応 |
| リンカスクリプト | ○ | ツールチェーン別で Pack 内に提供 |
| 人間可読性 | △ | XML が冗長 |
| オープンソース親和性 | △ | 従来は Keil 中心、CMSIS-Toolbox でオープン化が進行中 |

**参照:**
- [Open-CMSIS-Pack Spec](https://open-cmsis-pack.github.io/Open-CMSIS-Pack-Spec/main/html/pdsc_family_pg.html)
- [CMSIS-Toolbox](https://github.com/Open-CMSIS-Pack/cmsis-toolbox)
- [Memfault: Peeking Inside CMSIS-Packs](https://interrupt.memfault.com/blog/cmsis-packs)

---

## 6. Arduino

### 6.1 boards.txt フラットプロパティ形式

Arduino のボード定義はフラットなキーバリュー形式。

```properties
# boards.txt
uno.name=Arduino Uno
uno.build.board=AVR_UNO
uno.build.core=arduino
uno.build.f_cpu=16000000L
uno.build.mcu=atmega328p
uno.build.variant=standard
uno.upload.maximum_size=32256
uno.upload.maximum_data_size=2048
uno.upload.protocol=arduino
uno.upload.speed=115200
uno.upload.tool=avrdude

mega.name=Arduino Mega 2560
mega.build.board=AVR_MEGA2560
mega.build.core=arduino
mega.build.f_cpu=16000000L
mega.build.mcu=atmega2560
mega.build.variant=mega
mega.upload.maximum_size=253952
mega.upload.maximum_data_size=8192
mega.upload.protocol=wiring
mega.upload.speed=115200
mega.upload.tool=avrdude

# メニューによるバリアント選択
mega.menu.cpu.atmega2560=ATmega2560 (Mega 2560)
mega.menu.cpu.atmega2560.build.mcu=atmega2560
mega.menu.cpu.atmega2560.upload.maximum_size=253952
mega.menu.cpu.atmega1280=ATmega1280
mega.menu.cpu.atmega1280.build.mcu=atmega1280
mega.menu.cpu.atmega1280.upload.maximum_size=126976
```

### 6.2 platform.txt ビルドレシピテンプレート

```properties
# platform.txt — ビルドコマンドテンプレート
compiler.c.cmd=avr-gcc
compiler.c.flags=-c -g -Os -w -std=gnu11 -ffunction-sections -fdata-sections -MMD

# レシピは {変数} で boards.txt の値を参照
recipe.c.o.pattern="{compiler.path}{compiler.c.cmd}" {compiler.c.flags} -mmcu={build.mcu} -DF_CPU={build.f_cpu} -DARDUINO={runtime.ide.version} -DARDUINO_{build.board} {includes} "{source_file}" -o "{object_file}"

recipe.cpp.o.pattern="{compiler.path}{compiler.cpp.cmd}" {compiler.cpp.flags} -mmcu={build.mcu} -DF_CPU={build.f_cpu} -DARDUINO={runtime.ide.version} {includes} "{source_file}" -o "{object_file}"

recipe.c.combine.pattern="{compiler.path}{compiler.c.elf.cmd}" {compiler.c.elf.flags} -mmcu={build.mcu} -o "{build.path}/{build.project_name}.elf" {object_files} "-L{build.path}" -lm
```

### 6.3 Variant メカニズム

```
hardware/arduino/avr/
├── boards.txt
├── platform.txt
└── variants/
    ├── standard/           Arduino Uno
    │   └── pins_arduino.h  ピンマッピング定義
    ├── mega/               Arduino Mega
    │   └── pins_arduino.h
    └── leonardo/           Arduino Leonardo
        └── pins_arduino.h
```

`pins_arduino.h` はデジタル/アナログピン番号から物理ピンへのマッピングを定義する。

```cpp
// variants/standard/pins_arduino.h (Arduino Uno)
#define NUM_DIGITAL_PINS    20
#define NUM_ANALOG_INPUTS   6
#define LED_BUILTIN         13

static const uint8_t A0 = 14;
static const uint8_t A1 = 15;
```

### 6.4 boards.local.txt オーバーライド

ユーザーはプラットフォームの `boards.txt` と同じディレクトリに `boards.local.txt` を置くことで、既存ボードの設定を上書きできる。

```properties
# boards.local.txt
uno.build.f_cpu=8000000L    # クロック周波数を変更
uno.upload.speed=57600       # アップロード速度を変更
```

ただし、この方式はプラットフォーム更新で上書きされるリスクがある。

### 6.5 継承の不在

Arduino には **ボード間の継承メカニズムが一切存在しない**。各ボードは完全に独立した設定を持つ。同一 MCU を使うボードでも、全てのプロパティを個別に記述する必要がある。

`menu` 構文によるバリアント選択（MCU バリアント、クロック周波数の選択肢）は同一ボード内のオプション分岐であり、ボード間の継承ではない。

**評価:**

| 観点 | 評価 | 詳細 |
|------|------|------|
| シンプルさ | ◎ | プロパティファイルで直感的 |
| Boards Manager 配布 | ◎ | ワンクリックインストール |
| 継承メカニズム | ✕ | ボード間の継承なし |
| 構造化データ | ✕ | フラット、配列やネスト不可 |
| リンカスクリプト | △ | variant ディレクトリに手書き |

**参照:**
- [Arduino Platform Specification](https://arduino.github.io/arduino-cli/latest/platform-specification/)
- [Arduino Boards Manager](https://arduino.github.io/arduino-cli/latest/package_index_json-specification/)

---

## 7. Rust Embedded

### 7.1 PAC/HAL/BSP クレート分離

Rust Embedded エコシステムは最も明確な層分離を持つ。

```
Target Triple (thumbv7em-none-eabihf)
  └── PAC (stm32f4)                      SVD から自動生成
      └── HAL (stm32f4xx-hal)            ファミリ HAL
          └── BSP (stm32f4-discovery)    ボード固有
```

| 層 | 責務 | 生成方法 | 例 |
|---|------|---------|---|
| Target Triple | コンパイラターゲット指定 | rustc 組み込み / JSON 定義 | `thumbv7em-none-eabihf` |
| PAC | レジスタ定義 (安全な型付きアクセス) | SVD → svd2rust で自動生成 | `stm32f4` crate |
| HAL | ペリフェラル抽象 (embedded-hal trait 実装) | 手書き | `stm32f4xx-hal` crate |
| BSP | ボード固有ピン設定・外部デバイス | 手書き | `stm32f4-discovery` crate |

```rust
// BSP クレートの例
use stm32f4xx_hal::{gpio::*, pac, prelude::*};

pub struct Board {
    pub led: Pin<'D', 12, Output<PushPull>>,
    pub button: Pin<'A', 0, Input>,
}

impl Board {
    pub fn new(dp: pac::Peripherals) -> Self {
        let gpiod = dp.GPIOD.split();
        let gpioa = dp.GPIOA.split();
        Board {
            led: gpiod.pd12.into_push_pull_output(),
            button: gpioa.pa0.into_input(),
        }
    }
}
```

### 7.2 embedded-hal 1.0 トレイト

embedded-hal 1.0 は非同期対応を含むハードウェア抽象トレイトの標準を定義する。

```rust
// embedded-hal 1.0 の主要トレイト
pub trait InputPin {
    fn is_high(&mut self) -> Result<bool, Self::Error>;
    fn is_low(&mut self) -> Result<bool, Self::Error>;
}

pub trait OutputPin {
    fn set_low(&mut self) -> Result<(), Self::Error>;
    fn set_high(&mut self) -> Result<(), Self::Error>;
}

pub trait SpiBus<Word = u8> {
    fn transfer(&mut self, read: &mut [Word], write: &[Word])
        -> Result<(), Self::Error>;
}

// embedded-hal-async 1.0
pub trait I2c<A: AddressMode = SevenBitAddress> {
    async fn write(&mut self, address: A, write: &[u8])
        -> Result<(), Self::Error>;
    async fn read(&mut self, address: A, read: &mut [u8])
        -> Result<(), Self::Error>;
}
```

ドライバクレート（例: センサドライバ）は embedded-hal トレイトにのみ依存し、特定の HAL 実装に依存しない。

### 7.3 Embassy の "no-BSP" アプローチ

Embassy は従来の BSP クレートパターンを廃止し、HAL クレートを直接使用するアプローチを採用する。

```rust
// Embassy: BSP クレートなし、HAL を直接使用
#[embassy_executor::main]
async fn main(_spawner: embassy_executor::Spawner) {
    let p = embassy_stm32::init(Default::default());

    // ピン設定はアプリケーションコードで直接行う
    let mut led = Output::new(p.PD12, Level::Low, Speed::Low);

    loop {
        led.toggle();
        Timer::after_millis(500).await;
    }
}
```

Embassy の理由:
- BSP クレートはボードごとに作成・保守が必要で、更新が滞りやすい
- ユーザーは結局 BSP の中身を理解する必要がある（「80% Done 問題」）
- HAL クレートを直接使う方が柔軟性が高い

### 7.4 memory.x + cortex-m-rt リンカフレームワーク

Rust Embedded の最もシンプルなメモリ定義:

```
/* memory.x — ユーザーが記述する唯一のリンカ関連ファイル */
MEMORY {
    FLASH : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM   : ORIGIN = 0x20000000, LENGTH = 128K
}
```

`cortex-m-rt` クレートが提供する `link.x` がセクション配置を標準化:

```ld
/* cortex-m-rt の link.x（ユーザーは編集不要） */
INCLUDE memory.x

SECTIONS {
    .vector_table ORIGIN(FLASH) : { ... } > FLASH
    .text : { ... } > FLASH
    .rodata : { ... } > FLASH
    .data : { ... } > RAM AT > FLASH
    .bss (NOLOAD) : { ... } > RAM
    .uninit (NOLOAD) : { ... } > RAM
}
```

ユーザーはメモリ領域だけ書けばよく、セクション配置は `cortex-m-rt` が標準化する。

### 7.5 probe-rs YAML チップ定義

probe-rs は CMSIS-Pack からデバイスデータを自動抽出し、YAML で管理する。

```yaml
# probe-rs chip definition
name: STM32F407VGTx
manufacturer:
  id: 0x20
  cc: 0x16
memory_map:
  - Ram:
      range:
        start: 0x20000000
        end: 0x20020000
      cores: [main]
  - Nvm:
      range:
        start: 0x08000000
        end: 0x08100000
      cores: [main]
      is_boot_memory: true
  - Ram:
      range:
        start: 0x10000000
        end: 0x10010000
      cores: [main]
flash_algorithms:
  - name: STM32F4xx_1024
    default: true
```

### 7.6 Tock OS コンポーネントパターン

Tock OS は Rust で書かれた組み込み OS で、独自のコンポーネントパターンを持つ。

```rust
// Tock OS: Component パターンでボード初期化を構造化
pub struct UartComponent {
    uart_mux: &'static MuxUart<'static>,
}

impl Component for UartComponent {
    type StaticInput = &'static mut MaybeUninit<[u8; 64]>;
    type Output = &'static Console<'static>;

    fn finalize(self, s: Self::StaticInput) -> Self::Output {
        let console = Console::new(self.uart_mux, s);
        console
    }
}
```

**評価:**

| 観点 | 評価 | 詳細 |
|------|------|------|
| 層分離 | ◎ | PAC/HAL/BSP が独立クレート |
| 型安全性 | ◎ | コンパイル時レジスタアクセス検証 |
| メモリ定義 | ◎ | memory.x が極めてシンプル |
| リンカ標準化 | ◎ | cortex-m-rt が提供 |
| Embassy の DX | ○ | BSP 不要の直接 HAL 使用 |
| エントリーバリア | △ | 層の多さが初学者に困難 |

**参照:**
- [Embedded Rust Book](https://docs.rust-embedded.org/book/)
- [embedded-hal 1.0](https://docs.rs/embedded-hal/1.0.0/embedded_hal/)
- [Embassy](https://embassy.dev/)
- [probe-rs](https://probe.rs/)
- [Tock OS](https://www.tockos.org/)

---

## 8. NuttX RTOS

### 8.1 3 層ディレクトリ構造

NuttX は arch/chip/board の 3 層ディレクトリで物理構造を直接反映する。

```
nuttx/
├── arch/
│   └── arm/
│       ├── src/
│       │   ├── common/               アーキテクチャ共通 (コンテキストスイッチ等)
│       │   ├── armv7-m/              ISA 固有
│       │   ├── stm32/               チップファミリ固有
│       │   │   ├── stm32_gpio.c
│       │   │   ├── stm32_uart.c
│       │   │   └── Make.defs        チップレベルビルド定義
│       │   └── stm32h7/             別チップファミリ
│       └── include/
│           └── stm32/
│               └── chip.h           チップ定義ヘッダ
├── boards/
│   └── arm/
│       └── stm32/
│           └── stm32f4discovery/
│               ├── configs/          構成バリアント
│               │   ├── nsh/
│               │   │   └── defconfig  NSH シェル構成
│               │   ├── usbnsh/
│               │   │   └── defconfig  USB-NSH 構成
│               │   └── audio/
│               │       └── defconfig  オーディオ構成
│               ├── scripts/
│               │   ├── ld.script     リンカスクリプト (手書き)
│               │   └── Make.defs     ボードレベルビルド定義
│               ├── src/
│               │   ├── stm32_boot.c  ボード初期化
│               │   ├── stm32_bringup.c
│               │   └── Makefile
│               └── include/
│                   └── board.h       ボード定数
```

### 8.2 Kconfig + defconfig + Make.defs

**Kconfig:** ペリフェラル有効化、OS 機能選択。

```kconfig
# arch/arm/src/stm32/Kconfig
config STM32_USART2
    bool "USART2"
    default n
    select ARCH_HAVE_SERIAL_TERMIOS
    depends on STM32_STM32F40XX || STM32_STM32F41XX
```

**defconfig:** 特定構成のスナップショット。

```
# boards/arm/stm32/stm32f4discovery/configs/nsh/defconfig
CONFIG_ARCH="arm"
CONFIG_ARCH_CHIP="stm32"
CONFIG_ARCH_CHIP_STM32F407VG=y
CONFIG_ARCH_BOARD="stm32f4discovery"
CONFIG_STM32_USART2=y
CONFIG_USART2_SERIAL_CONSOLE=y
CONFIG_RAM_START=0x20000000
CONFIG_RAM_SIZE=131072
```

**Make.defs:** ビルドフラグ定義。

```makefile
# boards/arm/stm32/stm32f4discovery/scripts/Make.defs
include $(TOPDIR)/tools/Config.mk
include $(TOPDIR)/arch/arm/src/armv7-m/Toolchain.defs

ARCHSCRIPT = -T$(BOARD_DIR)$(DELIM)scripts$(DELIM)ld.script
```

### 8.3 同一ボードの複数構成

NuttX の特徴的な設計は、同一ボードに対して複数の `defconfig` を持てること。

```
stm32f4discovery/configs/
├── nsh/defconfig          最小 NSH シェル
├── usbnsh/defconfig       USB 経由 NSH
├── audio/defconfig        オーディオ再生
├── wifi/defconfig         WiFi シールド使用
├── elf/defconfig          ELF ローダー
└── posix_spawn/defconfig  POSIX spawn テスト
```

`tools/configure.sh stm32f4discovery:nsh` で構成を選択。同一ハードウェアで異なるソフトウェア構成を切り替え可能。

### 8.4 Out-of-Tree ボード

NuttX はカスタムボードを Out-of-Tree で定義可能。

```bash
# 外部ボードディレクトリを指定
./tools/configure.sh -B /path/to/my_boards my_board:nsh
```

ボードディレクトリの構造は NuttX 本体のボードと同一。

**評価:**

| 観点 | 評価 | 詳細 |
|------|------|------|
| ディレクトリ構造 | ◎ | arch/chip/board が物理ディレクトリに直接対応 |
| 構成バリアント | ◎ | 同一ボードで nsh, usbnsh 等の複数構成 |
| Kconfig 統合 | ○ | Linux kernel 由来の成熟した設定システム |
| リンカスクリプト | △ | ボードごとに手書き、テンプレート生成なし |
| Out-of-Tree | ○ | 外部ディレクトリ指定可能 |

**参照:**
- [NuttX Custom Boards](https://nuttx.apache.org/docs/latest/guides/customboards.html)
- [NuttX Configuration](https://nuttx.apache.org/docs/latest/quickstart/configuring.html)

---

## 9. modm

### 9.1 modm-devices データ抽出パイプライン

modm は 4,557 デバイスのデータベースを持つ、最も包括的なデバイスデータシステム。

**データソース:**

| ソース | ベンダー | 抽出方法 |
|--------|---------|---------|
| CubeMX データベース | ST (STM32) | XML パース + Python スクリプト |
| Atmel Target Description Files | Microchip (AVR, SAM) | XML パース |
| CMSIS-Pack PDSC | 各ベンダー | XML パース |

**パイプライン:**

```
ベンダーデータ (CubeMX XML, ATDF, PDSC)
  → extraction scripts (Python)
    → normalized device data (XML)
      → DevicesCache (パフォーマンス最適化)
        → lbuild クエリで使用
```

**modm-devices の内部構造:**

```
modm-devices/
├── tools/
│   ├── generator/                抽出スクリプト群
│   │   ├── dfg/                  Device File Generator
│   │   │   ├── stm32/           ST 固有パーサー
│   │   │   ├── avr/             AVR 固有パーサー
│   │   │   └── sam/             SAM 固有パーサー
│   │   └── raw-device-data/     ベンダー提供の生データ
│   └── device/
│       └── modm/                正規化済みデバイスデータ
│           ├── stm32f407vg.xml
│           ├── stm32f411re.xml
│           └── ... (4,557 ファイル)
```

**正規化済みデバイスデータの例:**

```xml
<!-- modm-devices/tools/device/modm/stm32f407vg.xml -->
<device platform="stm32" family="f4" name="07" pin="v" size="g" package="t">
  <driver type="core" name="cortex-m4f"/>
  <driver type="gpio" name="stm32-f2f4"/>
  <driver type="uart" name="stm32"/>
  <driver type="spi" name="stm32"/>

  <memory name="flash" access="rx" start="0x08000000" size="1048576"/>
  <memory name="sram1" access="rwx" start="0x20000000" size="131072"/>
  <memory name="ccm" access="rw" start="0x10000000" size="65536"/>
</device>
```

### 9.2 lbuild モジュールシステム + Jinja2 テンプレート

lbuild は modm のモジュール管理・コード生成システム。

```python
# modm/src/modm/platform/uart/module.lb
def init(module):
    module.name = "uart"

def prepare(module, options):
    device = options[":target"]
    # このデバイスに UART ドライバが存在するか確認
    if not device.has_driver("uart:stm32"):
        return False
    module.depends(":platform:gpio", ":platform:rcc")
    return True

def build(env):
    device = env[":target"]
    driver = device.get_driver("uart:stm32")

    # Jinja2 テンプレートからコード生成
    env.template("uart.hpp.in", "uart.hpp")
    env.template("uart.cpp.in", "uart.cpp")

    # ドライバのインスタンス数に応じてファイル生成
    for instance in driver["instance"]:
        env.template("uart_instance.hpp.in",
                     "uart_{}.hpp".format(instance),
                     substitutions={"id": instance})
```

**Jinja2 テンプレートの例:**

```cpp
// uart.hpp.in
#pragma once
#include <modm/platform/gpio/gpio.hpp>

namespace modm::platform {

{% for instance in driver.instance %}
class Uart{{ instance }} : public UartBase {
public:
    static void initialize(uint32_t baudrate);
    static bool write(uint8_t data);
    static bool read(uint8_t& data);

    // テンプレートからデバイスデータを注入
    static constexpr uint32_t base_address = {{ "0x{:08X}".format(instance.base_address) }};
    static constexpr IRQn_Type irq = {{ instance.irq }};
};
{% endfor %}

} // namespace modm::platform
```

### 9.3 完全コード生成

modm はリンカスクリプト、スタートアップ、ベクタテーブルを全てテンプレートから生成する。

**生成対象:**

| 生成物 | テンプレート | データソース |
|--------|------------|-----------|
| `startup.c` | `startup_platform.c.in` | ベクタテーブルサイズ、メモリレイアウト |
| `vectors.c` | `vectors.c.in` | 割り込みベクタ一覧 |
| `linkerscript.ld` | `ram.ld.in` / `dual_bank.ld.in` | メモリ領域、セクション配置 |
| `gpio.hpp` | `gpio.hpp.in` | GPIO ピン定義、AF マッピング |
| `uart.hpp/.cpp` | `uart.hpp.in`, `uart.cpp.in` | UART インスタンス、レジスタアドレス |

**生成されたリンカスクリプト（STM32F407VG）:**

```ld
/* Auto-generated by modm — DO NOT EDIT */
MEMORY {
    FLASH (rx)    : ORIGIN = 0x08000000, LENGTH = 1M
    CCM   (rw)    : ORIGIN = 0x10000000, LENGTH = 64K
    SRAM1 (rwx)   : ORIGIN = 0x20000000, LENGTH = 112K
    SRAM2 (rwx)   : ORIGIN = 0x2001C000, LENGTH = 16K
    BACKUP (rwx)  : ORIGIN = 0x40024000, LENGTH = 4K
}

MAIN_STACK_SIZE = DEFINED(MAIN_STACK_SIZE) ? MAIN_STACK_SIZE : 3072;

SECTIONS { ... }
```

### 9.4 ボード継承 (`<extends>`)

```xml
<!-- project.xml -->
<library>
  <extends>modm:nucleo-f429zi</extends>

  <options>
    <option name=":build:build.path">build/custom</option>
  </options>

  <modules>
    <module>modm:platform:uart:2</module>
    <module>modm:driver:lis3mdl</module>
  </modules>
</library>
```

`<extends>` でボードの全設定（デバイス選択、ピン設定、デフォルトモジュール）を継承し、追加モジュールのみ宣言する。

### 9.5 ペリフェラル IP バージョンベースのコード共有

modm はペリフェラルの IP バージョンを認識し、同一 IP を共有するデバイス間でコードを共有する。

```
modm/src/modm/platform/
├── gpio/
│   ├── stm32-f2f4/         GPIO IP: F2, F4 で共通
│   ├── stm32-f0f3l0/       GPIO IP: F0, F3, L0 で共通
│   └── stm32-h7/           GPIO IP: H7 固有
├── uart/
│   ├── stm32/              USART IP: 大半の STM32 で共通
│   └── stm32-extended/     USART IP: 拡張版 (FIFO 付き)
└── spi/
    └── stm32/              SPI IP: 共通
```

デバイスデータの `driver` 属性で IP バージョンを識別:

```xml
<driver type="gpio" name="stm32-f2f4"/>   <!-- F2, F4 共通 GPIO IP -->
<driver type="uart" name="stm32"/>         <!-- 標準 USART IP -->
```

**評価:**

| 観点 | 評価 | 詳細 |
|------|------|------|
| 単一源泉 | ★ | modm-devices が 4,557 デバイスの唯一のソース |
| 選択的包含 | ★ | モジュール単位で必要なもののみ生成 |
| リンカ生成 | ★ | テンプレートから完全生成 |
| スタートアップ生成 | ★ | テンプレートから完全生成 |
| IP ベース共有 | ◎ | ペリフェラル IP バージョンで共有単位を決定 |
| ボード継承 | ◎ | `<extends>` + デバイスデータの階層的オーバーレイ |
| 独自ツール依存 | △ | lbuild (Python3) + Jinja2 が必須 |

**参照:**
- [How modm Works](https://modm.io/how-modm-works/)
- [modm-devices (GitHub)](https://github.com/modm-io/modm-devices)
- [lbuild (GitHub)](https://github.com/modm-io/lbuild)

---

## 10. libopencm3

### 10.1 devices.data パターンマッチング

libopencm3 はテキストベースのデバイスデータベースでパターンマッチ継承を実現する。

```
# ld/devices.data — パターンマッチ型デバイスデータベース
# パターン              親            データ
stm32f407?g*         stm32f407     ROM=1024K RAM=128K
stm32f407?e*         stm32f407     ROM=512K  RAM=128K
stm32f407??          stm32f40x
stm32f405??          stm32f40x
stm32f40x            stm32f4       RAM=128K  CCM=64K
stm32f41x            stm32f4       RAM=128K  CCM=64K
stm32f4              stm32         ROM_OFF=0x08000000 RAM_OFF=0x20000000 CCM_OFF=0x10000000
stm32                END           CPPFLAGS=-mthumb ARCH_FLAGS=-mcpu=cortex-m4\ -mfloat-abi=hard\ -mfpu=fpv4-sp-d16
```

**パターンマッチルール:**
- `?` は任意の1文字
- `*` は任意の文字列
- 右の親列をたどることでデータが累積的にマージされる

**STM32F407VGT6 の解決例:**

```
stm32f407?g* → stm32f407 → stm32f40x → stm32f4 → stm32 → END

結果:
  ROM=1024K  (stm32f407?g* から)
  RAM=128K   (stm32f40x から)
  CCM=64K    (stm32f40x から)
  ROM_OFF=0x08000000  (stm32f4 から)
  RAM_OFF=0x20000000  (stm32f4 から)
  CCM_OFF=0x10000000  (stm32f4 から)
  CPPFLAGS=-mthumb    (stm32 から)
  ARCH_FLAGS=-mcpu=cortex-m4 ...  (stm32 から)
```

### 10.2 genlink.py + C プリプロセッサリンカ生成

**処理パイプライン:**

```
devices.data + MCU 名
  → genlink.py (Python)
    → generated.<mcu>.ld (パラメータ #define)
      → C プリプロセッサ
        → linker.ld.S テンプレート展開
          → 最終リンカスクリプト
```

**genlink.py の出力:**

```c
/* generated.stm32f407vgt6.ld */
#define ROM       1024K
#define RAM       128K
#define CCM       64K
#define ROM_OFF   0x08000000
#define RAM_OFF   0x20000000
#define CCM_OFF   0x10000000
```

**linker.ld.S テンプレート:**

```ld
/* ld/linker.ld.S */
#include "generated.stm32f407vgt6.ld"

MEMORY {
    rom (rx)  : ORIGIN = ROM_OFF, LENGTH = ROM
    ram (rwx) : ORIGIN = RAM_OFF, LENGTH = RAM
#if defined(CCM)
    ccm (rw)  : ORIGIN = CCM_OFF, LENGTH = CCM
#endif
}

SECTIONS {
    .text : { ... } > rom
    .data : { ... } > ram AT > rom
    .bss  : { ... } > ram
}
```

C プリプロセッサの条件分岐 (`#if defined(CCM)`) により、CCM を持つデバイスと持たないデバイスで同一テンプレートを共有する。

### 10.3 ペリフェラル IP バージョン共有 (common/ ディレクトリ)

libopencm3 の最も秀逸な設計: **ファミリではなくペリフェラル IP バージョンでコード共有単位を決定**。

```
lib/stm32/common/
├── usart_common_all.c          全 STM32 ファミリ共通 USART 操作
├── usart_common_f124.c         F1, F2, F4 の USART IP 共通
├── gpio_common_all.c           全 STM32 GPIO 共通操作
├── gpio_common_f0234.c         F0, F2, F3, F4 の GPIO IP 共通
├── i2c_common_v1.c             I2C v1 IP を持つファミリ群
├── i2c_common_v2.c             I2C v2 IP を持つファミリ群
├── spi_common_all.c            全 STM32 SPI 共通
├── spi_common_v1.c             SPI v1 IP
├── timer_common_all.c          全 STM32 Timer 共通
├── flash_common_f234.c         F2, F3, F4 の Flash IP 共通
├── flash_common_f01.c          F0, F1 の Flash IP 共通
└── rcc_common_all.c            全 STM32 RCC 共通
```

**命名規則:** `<peripheral>_common_<共有するファミリ群>.c`

この設計の利点:
- **新ファミリ追加時**: 既存の IP バージョン共通コードをそのまま再利用
- **IP バージョン変更時**: 該当する common ファイルだけ更新
- **重複排除**: 同一 IP を持つファミリ間でコードが完全に共有される

**評価:**

| 観点 | 評価 | 詳細 |
|------|------|------|
| パターンマッチ継承 | ◎ | devices.data が簡潔でメンテしやすい |
| リンカ生成 | ◎ | genlink + C プリプロセッサで汎用的 |
| IP ベース共有 | ★ | common/ ディレクトリの IP バージョン別共有が秀逸 |
| ボード抽象化 | ✕ | ボード概念が存在しない（デバイスレベルのみ） |
| ボード追加 | ◎ | devices.data に 1 行追加するだけ |

**参照:**
- [libopencm3 ld/README](https://github.com/libopencm3/libopencm3/blob/master/ld/README)
- [devices.data](https://github.com/libopencm3/libopencm3/blob/master/ld/devices.data)
- [genlink.py](https://github.com/libopencm3/libopencm3/blob/master/scripts/genlink.py)

---

## 11. その他のビルドシステム

### 11.1 CMake (stm32-cmake アプローチ)

stm32-cmake は CMake でSTM32 開発を行うためのツールキット。

```cmake
# FindCMSIS.cmake — CMSIS-Pack から情報を抽出
set(STM32_SUPPORTED_FAMILIES F0 F1 F2 F3 F4 F7 G0 G4 H7 L0 L1 L4 L5 U5 WB WL)

# ファミリ → コア対応テーブル
set(STM32_F4_CORE cortex-m4)
set(STM32_H7_CORE cortex-m7)
set(STM32_L0_CORE cortex-m0plus)

# リンカスクリプト: CMSIS-Pack のメモリ定義からテンプレート生成
function(stm32_generate_linker_script TARGET)
    get_target_property(DEVICE ${TARGET} STM32_DEVICE)
    # CMSIS-Pack からメモリ情報を取得
    stm32_get_memory_info(${DEVICE} FLASH_ORIGIN FLASH_SIZE RAM_ORIGIN RAM_SIZE)
    configure_file(${STM32_LINKER_TEMPLATE} ${CMAKE_BINARY_DIR}/linker.ld @ONLY)
endfunction()
```

**特徴:** CMSIS-Pack のデバイスデータを直接利用し、CMake のネイティブ機能でリンカスクリプトを生成。

### 11.2 Bazel / Buck2 — constraint ベースプラットフォーム選択

```python
# BUILD.bazel — constraint による MCU プラットフォーム定義
constraint_setting(name = "mcu_family")
constraint_value(name = "stm32f4", constraint_setting = ":mcu_family")
constraint_value(name = "stm32h7", constraint_setting = ":mcu_family")

platform(
    name = "stm32f407_discovery",
    constraint_values = [
        ":stm32f4",
        "@platforms//cpu:armv7e-m",
        "@platforms//os:none",
    ],
)

# select() でプラットフォーム依存コードを切り替え
cc_library(
    name = "hal",
    srcs = select({
        ":stm32f4": ["hal_stm32f4.cc"],
        ":stm32h7": ["hal_stm32h7.cc"],
    }),
    deps = select({
        ":stm32f4": [":stm32f4_pac"],
        ":stm32h7": [":stm32h7_pac"],
    }),
)
```

**特徴:**
- `constraint_setting` / `constraint_value` / `platform` の 3 要素でプラットフォームを宣言的に定義
- `select()` で条件分岐（ファイル選択、依存関係選択）
- ツールチェーン解決もプラットフォーム constraint に基づく
- **継承メカニズムはない** が、constraint の組み合わせで表現力を確保

### 11.3 Meson クロスコンパイルファイル

```ini
# cross/stm32f407.ini
[host_machine]
system = 'none'
cpu_family = 'arm'
cpu = 'cortex-m4'
endian = 'little'

[binaries]
c = 'arm-none-eabi-gcc'
cpp = 'arm-none-eabi-g++'
ar = 'arm-none-eabi-ar'

[built-in options]
c_args = ['-mcpu=cortex-m4', '-mthumb', '-mfloat-abi=hard', '-mfpu=fpv4-sp-d16']
cpp_args = ['-mcpu=cortex-m4', '-mthumb', '-mfloat-abi=hard', '-mfpu=fpv4-sp-d16']
c_link_args = ['-T', 'linker.ld', '--specs=nosys.specs']
```

**特徴:**
- INI 形式のクロスファイルで全クロスコンパイル設定を宣言
- ボード/MCU の概念はなく、ツールチェーン設定のみ
- リンカスクリプトはユーザーが手動で指定

### 11.4 Nix / Nixpkgs クロスコンパイル

```nix
# flake.nix — Nix によるクロスコンパイル
{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      # クロスコンパイル: ARM bare-metal
      pkgsCross = nixpkgs.legacyPackages.x86_64-linux.pkgsCross.arm-embedded;
    in {
      packages.x86_64-linux.firmware = pkgsCross.stdenv.mkDerivation {
        name = "firmware";
        src = ./.;
        nativeBuildInputs = [ pkgsCross.gcc ];
        buildPhase = ''
          arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -o firmware.elf main.c
        '';
      };
    };
}
```

**特徴:**
- 再現可能なビルド環境を保証
- クロスコンパイルツールチェーンを Nix パッケージとして管理
- BSP/ボードの概念は Nix レベルでは存在しない

### 11.5 Yocto / OpenEmbedded BSP レイヤー

```
meta-stm32mp/                      BSP レイヤー
├── conf/
│   ├── layer.conf                 レイヤー定義
│   └── machine/
│       ├── stm32mp157c-dk2.conf   マシン定義
│       └── stm32mp135f-dk.conf
├── recipes-bsp/
│   ├── u-boot/                    ブートローダーレシピ
│   └── trusted-firmware-a/        TF-A レシピ
└── recipes-kernel/
    └── linux/                     カーネルレシピ
```

**マシン定義:**

```python
# conf/machine/stm32mp157c-dk2.conf
#@TYPE: Machine
#@NAME: stm32mp157c-dk2

require conf/machine/include/st-machine-common-stm32mp.inc
require conf/machine/include/st-machine-providers-stm32mp.inc

DEFAULTTUNE = "cortexa7thf-neon-vfpv4"

# メモリ・パーティション定義
STM32MP_DT_FILES = "stm32mp157c-dk2"
MACHINE_FEATURES += "gpu wifi bluetooth"
```

**特徴:**
- `require` / `include` でマシン定義を階層化
- `MACHINE_FEATURES` で機能を宣言的に列挙
- BSP レイヤーは独立して配布・合成可能
- **BBLAYERS** で必要なレイヤーのみ選択

**評価まとめ:**

| ビルドシステム | プラットフォーム表現 | 継承 | BSP 概念 |
|--------------|-------------------|------|---------|
| CMake (stm32-cmake) | CMake 変数 + CMSIS-Pack | なし（関数呼び出し） | あり（CMSIS 依存） |
| Bazel/Buck2 | constraint + platform | なし（組み合わせ） | なし |
| Meson | INI クロスファイル | なし | なし |
| Nix | Nix 式 | Nix overlay | なし |
| Yocto/OE | MACHINE conf | require/include チェーン | BSP レイヤー |

**参照:**
- [stm32-cmake (GitHub)](https://github.com/ObKo/stm32-cmake)
- [Bazel Platforms](https://bazel.build/concepts/platforms)
- [Bazel Toolchains](https://bazel.build/extending/toolchains)
- [Meson Cross-compilation](https://mesonbuild.com/Cross-compilation.html)
- [Nixpkgs Cross-compilation](https://nixos.org/manual/nixpkgs/stable/#chap-cross)
- [Yocto BSP Developer's Guide](https://docs.yoctoproject.org/bsp-guide/bsp.html)

---

## 12. 他ドメインのオーバーレイ/デルタパターン

BSP の「基底定義 + 差分上書き」パターンは、組み込み以外のソフトウェアドメインにも広く見られる。以下の分析はアーキテクチャ設計の選択肢を広げるためのもの。

### 12.1 CSS Cascade — 詳細度ベースのオーバーライド

```css
/* 詳細度: 低 → 高 */
* { color: black; }                     /* 詳細度 0,0,0,0 */
p { color: gray; }                       /* 詳細度 0,0,0,1 */
.highlight { color: blue; }              /* 詳細度 0,0,1,0 */
#main .highlight { color: red; }         /* 詳細度 0,1,1,0 */
p.highlight { color: green !important; } /* !important で最優先 */
```

**BSP への示唆:**
- 「詳細度」の概念: アーキテクチャ < ファミリ < MCU < ボード の順で詳細度が上がる
- 同一プロパティが複数階層で定義された場合、最も詳細度の高い定義が勝つ
- `!important` のような強制上書きは避けるべき（デバッグが困難になる）

### 12.2 Docker レイヤー — 不変ベース + 可変オーバーレイ

```dockerfile
FROM ubuntu:22.04                    # ベースレイヤー（不変）
RUN apt-get install -y gcc-arm-none-eabi  # ツール追加レイヤー
COPY firmware/ /app/firmware/        # アプリケーションレイヤー
```

**特徴:**
- 各レイヤーは不変 (immutable) — ベースイメージを変更せずに上にレイヤーを積む
- レイヤーの共有によりストレージ・転送効率が向上
- `docker history` でレイヤーの変更履歴を追跡可能

**BSP への示唆:**
- ファミリ定義を「不変ベースレイヤー」として扱い、MCU/ボード定義を「上位レイヤー」として積む
- ベースレイヤーの変更は全派生に影響する（意図的な設計）
- レイヤー数を制限すべき（Docker のベストプラクティス: 最小限のレイヤー）

### 12.3 Nix Overlay — 関数合成 (final/prev)

```nix
# Nix overlay: (final: prev: { ... })
# final = 最終結果の固定点, prev = 直前のレイヤー
myOverlay = final: prev: {
  # prev のパッケージを上書き
  openssl = prev.openssl.overrideAttrs (old: {
    version = "3.1.0";
    src = fetchurl { ... };
  });

  # 新しいパッケージを追加
  myTool = final.callPackage ./my-tool.nix {};
};

# 複数 overlay を合成
nixpkgs.overlays = [ overlay1 overlay2 myOverlay ];
```

**特徴:**
- `prev` で直前のレイヤーを参照、`final` で最終結果（固定点）を参照
- overlay は純粋関数 — 副作用なし、再現可能
- 複数 overlay の合成順序が明確

**BSP への示唆:**
- ファミリ定義 → MCU overlay → ボード overlay の関数合成として BSP を表現可能
- 各レイヤーが前のレイヤーの結果を受け取り、差分を返す
- Lua の `dofile()` + テーブル上書きはこのパターンの簡易版

### 12.4 Terraform モジュール — パラメータ化テンプレート

```hcl
# modules/mcu/main.tf
variable "mcu_name" { type = string }
variable "flash_size" { type = string }
variable "ram_size" { type = string }
variable "core" { type = string }

resource "linker_script" "memory" {
  content = templatefile("memory.ld.tpl", {
    flash_origin = var.flash_origin
    flash_size   = var.flash_size
    ram_origin   = var.ram_origin
    ram_size     = var.ram_size
  })
}

# 使用側
module "stm32f407vg" {
  source     = "./modules/mcu"
  mcu_name   = "stm32f407vg"
  flash_size = "1M"
  ram_size   = "128K"
  core       = "cortex-m4f"
}
```

**BSP への示唆:**
- MCU 定義をパラメータ化された「モジュール」として扱う
- テンプレートとパラメータの分離が明確
- モジュールの入力変数が契約（インタフェース）として機能

### 12.5 Helm values.yaml — 階層的値オーバーライド

```yaml
# values.yaml — デフォルト値
replicaCount: 1
image:
  repository: nginx
  tag: "1.21"
resources:
  limits:
    cpu: 100m
    memory: 128Mi

# values-production.yaml — 本番環境上書き
replicaCount: 3
resources:
  limits:
    cpu: 500m
    memory: 512Mi
```

```bash
# 適用: デフォルト + 環境固有の値をマージ
helm install myapp ./chart -f values.yaml -f values-production.yaml
```

**特徴:**
- YAML のディープマージ — ネストされたキーの部分上書きが可能
- 複数の values ファイルを順序付きで適用
- テンプレート内で `{{ .Values.replicaCount }}` として参照

**BSP への示唆:**
- ファミリ defaults → MCU overrides → ボード overrides のレイヤリング
- ディープマージにより、変更箇所だけを記述すれば十分
- Lua テーブルのマージは YAML のディープマージに相当する

### 12.6 パターン間の比較

| パターン | 上書き方式 | 可逆性 | 型安全性 | ツール要件 |
|---------|----------|--------|---------|----------|
| CSS Cascade | 詳細度ベース | ✕ | ✕ | ブラウザ |
| Docker Layer | 不変レイヤー積層 | ○ (レイヤー削除) | ✕ | Docker |
| Nix Overlay | 関数合成 (final/prev) | ○ (overlay 除去) | ○ (Nix 式) | Nix |
| Terraform Module | パラメータ注入 | N/A (宣言的) | ○ (variable 型) | Terraform |
| Helm Values | YAML ディープマージ | ✕ | △ (JSON Schema) | Helm |
| **Lua dofile()** | **テーブル上書き** | **✕** | **✕** | **Lua (xmake 組み込み)** |

---

## 13. xmake の BSP 関連機能

### 13.1 ルールライフサイクル

xmake のルールは以下の順序で実行される。BSP ルールの設計にはこの順序の理解が不可欠。

```
Phase 0: ターゲット定義のパース
  └── set_values(), add_files(), add_rules() 等が即時評価
  └── 全ルールから target:values() で読み取り可能

Phase 1: on_load()
  └── 全ルールの on_load が add_rules() 宣言順で実行
  └── 全ターゲットの on_load が完了するまでブロック

Phase 2: after_load()
  └── 全ルールの after_load が実行

Phase 3: on_config()
  └── 全ルールの on_config が add_rules() 宣言順で実行

Phase 4: before_build() → コンパイル → after_link()
  └── ビルドフェーズ
```

**重要な制約:**

| 保証されていること | 保証されていないこと |
|------------------|-------------------|
| Phase 0 の `set_values()` は全フェーズから可視 | ルール A の on_load で set した値が ルール B の on_load で見えるかは順序依存 |
| on_load → on_config の順序は全ターゲット横断で保証 | on_config で set_values した値は on_load には遡及しない |
| 同一フェーズ内は add_rules() 宣言順 | ルール間の依存宣言 (`add_deps()`) は存在しない |

### 13.2 target:values() / set_values() — ボードパラメータ

ターゲットに宣言的にパラメータを設定・取得する。

```lua
-- ターゲット定義 (Phase 0)
target("my_firmware")
    add_rules("embedded", "umiport.board")
    set_values("embedded.mcu", "stm32f407vg")       -- Phase 0 で即時可視
    set_values("embedded.core", "cortex-m4f")        -- Phase 0 で即時可視
    set_values("umiport.board", "stm32f4-renode")    -- Phase 0 で即時可視
```

```lua
-- ルール内 (on_load / on_config)
rule("embedded")
    on_load(function(target)
        local core = target:values("embedded.core")  -- Phase 0 の値を読み取り
        if core then
            -- コンパイラフラグ設定
        end
    end)
```

### 13.3 target:data() / target:data_set() — ランタイムデータ

ルール間でランタイムデータを共有するための仕組み。`values` と異なり、文字列以外のオブジェクト（テーブル等）も格納可能。

```lua
-- umiport.board ルール (on_load)
rule("umiport.board")
    on_load(function(target)
        local mcu_config = dofile(mcu_path)
        target:data_set("umiport.mcu_config", mcu_config)  -- テーブルを格納
    end)

-- embedded ルール (after_link)
rule("embedded")
    after_link(function(target)
        local mcu_config = target:data("umiport.mcu_config")  -- テーブルを取得
        if mcu_config and mcu_config.memory then
            -- メモリ使用量表示
        end
    end)
```

### 13.4 ファイル生成: add_configfiles() と io.writefile()

**add_configfiles():** テンプレート変数置換。

```lua
target("firmware")
    set_configvar("FLASH_SIZE", "1M")
    set_configvar("RAM_SIZE", "128K")
    add_configfiles("memory.ld.in")   -- ${FLASH_SIZE} → 1M に置換
```

```ld
/* memory.ld.in */
MEMORY {
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = ${FLASH_SIZE}
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = ${RAM_SIZE}
}
```

**io.writefile():** Lua コードで動的生成。

```lua
-- on_config 内でリンカスクリプトを動的生成
local lines = {"MEMORY {"}
for name, region in pairs(mcu_config.memory) do
    if region.length then
        table.insert(lines, string.format(
            "    %-12s (%s) : ORIGIN = 0x%08X, LENGTH = %s",
            name, region.attr, region.origin, region.length))
    end
end
table.insert(lines, "}")
io.writefile(output_path, table.concat(lines, "\n"))
```

### 13.5 パッケージ埋め込みルールの制約 (on_load 不可)

xmake パッケージ内に定義されたルールでは **`on_load` コールバックが使えない** という制約がある。

```lua
-- xmake パッケージ内のルール定義
-- on_load は無視される（パッケージのロード順序の問題）
package("arm-embedded")
    on_install(function(package)
        -- rules/ ディレクトリにルールファイルを配置
    end)
```

この制約のため、パッケージ内の embedded ルールは on_load の代わりに on_config を使用する場合がある。プロジェクトローカルのルール（umiport.board 等）にはこの制約はない。

**実際の挙動（xmake 2.8.x 以降）:**

パッケージ内ルールでも `on_load` が動作するケースはあるが、パッケージのインストールタイミングとルールのロードタイミングの間で競合が発生する可能性がある。安全のため、パッケージ内ルールでは `on_config` 以降のフェーズを使用することが推奨される。

### 13.6 ルール継承: add_deps()

ルール間の依存関係を宣言し、依存先ルールのコールバックが先に実行されるようにする。

```lua
rule("umiport.board")
    add_deps("embedded")   -- embedded ルールに依存
    on_config(function(target)
        -- embedded ルールの on_config 完了後に実行される
    end)
```

ただし、`add_deps()` は **同一フェーズ内の順序制御** には有効だが、**フェーズ間の依存**（on_load で書いた値を別ルールの on_load で読む）は解決できない。

### 13.7 includes() / dofile() によるモジュラー構成

```lua
-- xmake.lua
includes("database")   -- database/xmake.lua を読み込む
includes("rules")      -- rules/xmake.lua を読み込む

-- database/xmake.lua
function load_mcu_config(mcu_name)
    local index = dofile(path.join(os.scriptdir(), "index.lua"))
    local mcu_path = index[mcu_name:lower()]
    return dofile(path.join(os.scriptdir(), "mcu", mcu_path .. ".lua"))
end
```

### 13.8 option() によるボード選択 CLI

```lua
option("board")
    set_default("stm32f4-renode")
    set_showmenu(true)
    set_description("Target board")
    set_values("stm32f4-renode", "stm32f4-disco", "daisy-seed", "host")
option_end()

-- 使用例
target("firmware")
    set_values("umiport.board", get_config("board"))
```

```bash
# CLI からボード選択
xmake f --board=stm32f4-disco
xmake build
```

---

## 14. 開発者体験 (DX) の知見

### 14.1 「80% Done」問題

多くの BSP フレームワークに共通する問題: **フレームワークが 80% まで自動化してくれるが、残り 20% のカスタマイズに不釣り合いな労力が必要**。

| フレームワーク | 80% (自動) | 残り 20% (手動) |
|--------------|-----------|----------------|
| Arduino | ボード選択、コンパイル、書き込み | カスタムリンカ、低レベルペリフェラル、割り込み優先度 |
| PlatformIO | ボード JSON、デバッグ、依存管理 | 複雑なメモリレイアウト、カスタムフラッシュ手順 |
| Zephyr | DeviceTree、Kconfig、リンカ生成 | カスタム DeviceTree ノード、特殊なブート手順 |
| Embassy | async HAL、タスクスケジューラ | BSP なし（全て手動のピン設定） |

Embassy はこの問題に対して「80% の自動化を諦め、100% を手動にする」というアプローチを取った。結果として「全てが明示的で追跡可能」という DX が実現された。

### 14.2 ボード間で実際に変わるもの

実際のプロジェクトで、ボードを変更した場合に変わる要素の調査:

| 要素 | 変更頻度 | 変更の性質 | 例 |
|------|---------|-----------|---|
| MCU 型番 | 毎回 | 離散値選択 | STM32F407VG → STM32F446RE |
| Flash/RAM サイズ | 毎回 | 数値変更 | 1M/128K → 512K/128K |
| クロック設定 | 毎回 | HSE 周波数、PLL 設定 | 8MHz → 25MHz |
| ピンマッピング | 毎回 | GPIO ポート/ピン番号 | PA2/PA3 → PD5/PD6 |
| デバッグプローブ | ボードによる | プローブ種類、設定 | ST-Link → J-Link |
| 外部デバイス | ボードによる | I2C/SPI デバイスの有無 | コーデック、センサ |
| メモリレイアウト | まれ | セクション配置の調整 | CCM 使用/不使用 |
| スタートアップコード | まれ（ファミリ変更時） | ベクタテーブル、初期化 | F4 → H7 |
| ペリフェラル IP | まれ（ファミリ変更時） | レジスタ構造の変更 | USART v1 → v2 |

### 14.3 Fork-and-Modify アンチパターン

最も一般的で最も問題のある BSP 追加方法:

```
1. 似たボードの BSP ディレクトリをコピー
2. MCU 名、メモリサイズ、ピン設定を手動で書き換え
3. ビルドが通るまで試行錯誤
4. 元のボードの改善が自分のコピーに反映されない
5. 次のボード追加時に再びコピー → 分岐が増殖
```

**このパターンを採用しているシステム:**

| システム | Fork の単位 | 対策 |
|---------|-----------|------|
| Arduino | variant ディレクトリ | なし（コピーが公式手順） |
| NuttX | board ディレクトリ | なし（ドキュメントでコピー推奨） |
| PlatformIO | board.json | なし（継承なし） |

**このパターンを回避しているシステム:**

| システム | 回避方法 |
|---------|---------|
| Zephyr | DTS include チェーン + overlay |
| Mbed OS | `inherits` + `_add`/`_remove` |
| modm | `<extends>` + デバイスデータベース |
| CMSIS-Pack | 4 階層属性累積 |

### 14.4 プロジェクトローカルボード定義の比較

カスタムボードをプロジェクト内に追加する方法の比較:

| システム | 方法 | 必要ファイル数 | 本体変更 |
|---------|------|-------------|---------|
| PlatformIO | `boards/` に JSON | 1 | 不要 |
| Mbed OS | `custom_targets.json` | 1 | 不要 |
| Zephyr | `BOARD_ROOT` + ボードディレクトリ | 5+ | 不要 |
| Arduino | `boards.local.txt` | 1 (制限あり) | 不要 |
| NuttX | `-B` で外部ディレクトリ指定 | 5+ | 不要 |
| modm | `project.xml` で `<extends>` | 1 | 不要 |
| **UMI (現設計)** | **`database/mcu/` + `include/board/`** | **2-3** | **不要** |

### 14.5 設定レイヤリングアプローチの比較

| アプローチ | 採用システム | レイヤー数 | マージ方式 |
|-----------|------------|----------|-----------|
| ファイル include チェーン | Zephyr (DTS), libopencm3 | 6-8 | ノード上書き |
| JSON `inherits` | Mbed OS | 4-5 | `_add`/`_remove` |
| XML 階層属性累積 | CMSIS-Pack | 4 | 下位上書き |
| パターンマッチ継承 | libopencm3 | 4-5 | キーバリューマージ |
| Lua dofile() チェーン | **UMI** | 2 (family→mcu) | テーブルフィールド上書き |
| テンプレート + パラメータ | modm (lbuild + Jinja2) | N/A | 完全生成 |
| フラット（継承なし） | Arduino, PlatformIO | 1 | N/A |
| values ファイルマージ | Helm, Yocto | 2-3 | ディープマージ |

### 14.6 モダンアプローチ (2024-2026)

近年のトレンド:

| トレンド | 代表 | 特徴 |
|---------|------|------|
| HWMv2 | Zephyr v3.6+ | ベンダーベースディレクトリ、board.yml、SoC 分離 |
| no-BSP | Embassy | BSP クレートを廃止、HAL 直接使用 |
| CMSIS-Toolbox | ARM | csolution.yml + CMSIS-Pack のオープン化 |
| AI 支援 | 各種 | LLM によるボード定義の自動生成・検証 |

**CMSIS-Toolbox (csolution.yml):**

```yaml
# csolution.yml — 新世代 CMSIS プロジェクト記述
solution:
  packs:
    - pack: Keil::STM32F4xx_DFP@2.17.0
  target-types:
    - type: STM32F407-Discovery
      device: STM32F407VGTx
      variables:
        - Board-Layer: $SolutionDir()$/Board/STM32F407-Discovery/Board.clayer.yml
```

---

## 15. 総合比較表

### 15.1 ハードウェア記述方式の比較

| システム | 記述形式 | 継承メカニズム | メモリ定義の場所 | リンカスクリプト |
|---------|---------|-------------|----------------|---------------|
| Zephyr | DTS + YAML + Kconfig | DTS 6-8 階層 include + Kconfig select | DTS `<memory>` ノード | テンプレート自動生成 |
| ESP-IDF | C ヘッダ + Kconfig | ファイルパス切り替え (暗黙) | `memory.ld.in` (チップ固有) | ldgen フラグメント統合生成 |
| PlatformIO | JSON | **なし** (ボード間コピペ) | `board.json` upload.* | テンプレート生成 (2 リージョン限定) |
| Mbed OS | JSON | `inherits` + `_add`/`_remove` チェーン | `targets.json` 内フィールド | テンプレート + プリプロセッサマクロ |
| CMSIS-Pack | XML (PDSC) | 4 階層属性累積 (family→sub→device→variant) | `<memory>` 要素 | Pack 内にツールチェーン別提供 |
| Arduino | Properties (.txt) | **なし** (各ボード独立) | `boards.txt` upload.* | variant ディレクトリに手書き |
| Rust Embedded | TOML + Rust 型 | 層ごと独立クレート (PAC/HAL/BSP) | `memory.x` (手書き) | `cortex-m-rt` link.x 標準提供 |
| NuttX | Kconfig + Make | Kconfig 依存関係 | `defconfig` CONFIG_RAM_* | ボードごとに手書き |
| modm | 独自 XML DB | デバイスデータのマージ + `device-*` 属性フィルタ | DB から自動生成 | テンプレート完全生成 |
| libopencm3 | テキスト DB | パターンマッチ継承 (glob 風) | `devices.data` | genlink.py + Cプリプロセッサ生成 |
| Yocto/OE | BitBake conf | `require`/`include` チェーン | マシン conf 変数 | レシピ内で管理 |
| Bazel/Buck2 | BUILD + Starlark | constraint 組み合わせ (継承なし) | ルール内で定義 | ルール内で指定 |

### 15.2 デバッグ・フラッシュ・配布の比較

| システム | デバッグ/フラッシュ設定 | 配布モデル | ボード追加難易度 |
|---------|---------------------|-----------|---------------|
| Zephyr | `board.cmake` (ランナー定義) | West module / BOARD_ROOT | 中 (5 ファイル) |
| ESP-IDF | esptool (チップ自動検出) | IDF コンポーネントマネージャ | 高 (多数ファイル) |
| PlatformIO | `board.json` debug セクション | Platform パッケージ | **低** (JSON 1 つ) |
| Mbed OS | `targets.json` + DAPLink | pip パッケージ | 中 (JSON 追加) |
| CMSIS-Pack | `.FLM` フラッシュアルゴリズム | Pack Manager | 高 (PDSC XML) |
| Arduino | boards.txt upload セクション | Boards Manager | **低** (行追加) |
| Rust Embedded | probe-rs chip.yaml | crates.io (クレート) | 中 (memory.x) |
| NuttX | 外部ツール設定 | Git clone | 中 (ディレクトリコピー) |
| modm | lbuild モジュール | pip + Git | 中 (lbuild 要) |
| libopencm3 | 外部ツール設定 | Git clone | **低** (DB 1 行) |

### 15.3 設計品質の軸別評価

| システム | デバイス-ボード分離 | 型安全性/検証 | 単一源泉 | 選択的包含 | 生成 vs 手書き |
|---------|-----------------|-------------|---------|-----------|--------------|
| Zephyr | ◎ (soc/ vs boards/) | ○ (DTS コンパイラ検証) | ○ | ○ | 生成 |
| ESP-IDF | ◎ (soc/ 独立) | △ (C マクロのみ) | ◎ | △ | 生成 |
| PlatformIO | ✕ (混在) | ✕ (JSON 型チェックなし) | △ | ○ | 生成 (限定) |
| Mbed OS | ○ (MCU → ボード) | △ (Linter 検証) | ○ | △ | 混在 |
| CMSIS-Pack | ◎ (DFP vs BSP) | ○ (XML Schema) | ◎ | ○ | 混在 |
| Arduino | ✕ (混在) | ✕ | △ | ✕ | 手書き |
| Rust Embedded | **★** (PAC/HAL/BSP) | **★** (コンパイル時型検証) | ○ | ○ | 混在 |
| NuttX | ◎ (arch/chip/board) | △ (Kconfig 制約) | △ | △ | 手書き |
| modm | ◎ (DB + board extends) | ○ (Python 検証) | **★** | **★** | **完全生成** |
| libopencm3 | △ (デバイスのみ) | ✕ | ◎ | △ | 生成 |

### 15.4 総合スコア

BSP アーキテクチャ設計における各軸の重み付け評価:

| システム | 継承 | リンカ | DX | 拡張性 | 成熟度 | 総合 |
|---------|------|--------|-----|--------|--------|------|
| **modm** | ◎ | ★ | ○ | ◎ | ◎ | **★** |
| **Zephyr** | ◎ | ◎ | △ | ◎ | ◎ | **◎** |
| **Rust Embedded** | ○ | ◎ | ○ | ◎ | ○ | **◎** |
| ESP-IDF | △ | ◎ | △ | ○ | ◎ | ○ |
| CMSIS-Pack | ◎ | ○ | △ | ○ | ◎ | ○ |
| Mbed OS | ◎ | △ | ○ | ○ | ✕(EOL) | △ |
| libopencm3 | ◎ | ◎ | ○ | △ | ○ | ○ |
| NuttX | △ | △ | ○ | ○ | ◎ | △ |
| PlatformIO | ✕ | △ | ◎ | ○ | ◎ | △ |
| Arduino | ✕ | ✕ | ◎ | △ | ◎ | △ |

---

## 16. 参照ソース

### Zephyr RTOS
- [Board Porting Guide](https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html)
- [SoC Porting Guide](https://docs.zephyrproject.org/latest/hardware/porting/soc_porting.html)
- [HWMv2 Migration Guide](https://docs.zephyrproject.org/latest/hardware/porting/hwmv2.html)
- [Devicetree HOWTOs](https://docs.zephyrproject.org/latest/build/dts/howtos.html)
- [Shield Guide](https://docs.zephyrproject.org/latest/hardware/porting/shields.html)
- [Zephyr GitHub](https://github.com/zephyrproject-rtos/zephyr)

### ESP-IDF
- [Hardware Abstraction](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/hardware-abstraction.html)
- [Linker Script Generation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/linker-script-generation.html)
- [Build System](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html)
- [ESP-IDF GitHub](https://github.com/espressif/esp-idf)

### PlatformIO
- [Custom Embedded Boards](https://docs.platformio.org/en/latest/platforms/creating_board.html)
- [platform-ststm32 (GitHub)](https://github.com/platformio/platform-ststm32)
- [PlatformIO Core](https://docs.platformio.org/en/latest/core/index.html)

### CMSIS-Pack
- [Open-CMSIS-Pack Spec — Family](https://open-cmsis-pack.github.io/Open-CMSIS-Pack-Spec/main/html/pdsc_family_pg.html)
- [CMSIS-Toolbox](https://github.com/Open-CMSIS-Pack/cmsis-toolbox)
- [Memfault: Peeking Inside CMSIS-Packs](https://interrupt.memfault.com/blog/cmsis-packs)

### Mbed OS
- [Adding and Configuring Targets](https://os.mbed.com/docs/mbed-os/v6.16/program-setup/adding-and-configuring-targets.html)
- [Mbed OS targets.json (GitHub)](https://github.com/ARMmbed/mbed-os/blob/master/targets/targets.json)

### Arduino
- [Arduino Platform Specification](https://arduino.github.io/arduino-cli/latest/platform-specification/)
- [Arduino Boards Manager Spec](https://arduino.github.io/arduino-cli/latest/package_index_json-specification/)

### Rust Embedded
- [Embedded Rust Book](https://docs.rust-embedded.org/book/)
- [embedded-hal 1.0](https://docs.rs/embedded-hal/1.0.0/embedded_hal/)
- [Embassy](https://embassy.dev/)
- [probe-rs](https://probe.rs/)
- [Tock OS](https://www.tockos.org/)
- [cortex-m-rt](https://docs.rs/cortex-m-rt/latest/cortex_m_rt/)

### NuttX
- [NuttX Custom Boards](https://nuttx.apache.org/docs/latest/guides/customboards.html)
- [NuttX Configuration](https://nuttx.apache.org/docs/latest/quickstart/configuring.html)

### modm
- [How modm Works](https://modm.io/how-modm-works/)
- [modm-devices (GitHub)](https://github.com/modm-io/modm-devices)
- [lbuild (GitHub)](https://github.com/modm-io/lbuild)
- [modm Board Support](https://modm.io/reference/boards/)

### libopencm3
- [libopencm3 ld/README](https://github.com/libopencm3/libopencm3/blob/master/ld/README)
- [devices.data](https://github.com/libopencm3/libopencm3/blob/master/ld/devices.data)
- [genlink.py](https://github.com/libopencm3/libopencm3/blob/master/scripts/genlink.py)

### ビルドシステム
- [stm32-cmake (GitHub)](https://github.com/ObKo/stm32-cmake)
- [Bazel Platforms](https://bazel.build/concepts/platforms)
- [Bazel Toolchains](https://bazel.build/extending/toolchains)
- [Meson Cross-compilation](https://mesonbuild.com/Cross-compilation.html)
- [Nixpkgs Cross-compilation](https://nixos.org/manual/nixpkgs/stable/#chap-cross)
- [Yocto BSP Developer's Guide](https://docs.yoctoproject.org/bsp-guide/bsp.html)

### xmake
- [xmake Rules](https://xmake.io/#/manual/custom_rule)
- [xmake Target API](https://xmake.io/#/manual/target_instance)
- [xmake Packages](https://xmake.io/#/manual/packages)

---

*Last updated: 2026-02-09*
