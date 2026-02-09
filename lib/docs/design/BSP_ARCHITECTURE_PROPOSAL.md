# BSP アーキテクチャ提案: 理想的なボード対応基盤の設計

**ステータス:** 提案（レビュー待ち）
**作成日:** 2026-02-09
**前提文書:** PLATFORM_COMPARISON.md（13システム調査）, ARCHITECTURE_FINAL.md, HARDWARE_DATA_CONSOLIDATION.md, LINKER_DESIGN.md
**設計原則:** 完全に理想的な美しい方法のみを考える。開発コストはいくらでもかけられる。

---

## 目次

1. [設計原則](#1-設計原則)
2. [アーキテクチャ概要](#2-アーキテクチャ概要)
3. [ボード定義の構造](#3-ボード定義の構造)
4. [xmake ルールシステム設計](#4-xmake-ルールシステム設計)
5. [ディレクトリ構造](#5-ディレクトリ構造)
6. [継承解決アルゴリズム](#6-継承解決アルゴリズム)
7. [3つのユースケースの開発者体験](#7-3つのユースケースの開発者体験)
8. [既存システムとの比較](#8-既存システムとの比較)
9. [実装ロードマップ](#9-実装ロードマップ)
10. [未解決の論点](#10-未解決の論点)

---

## 1. 設計原則

本アーキテクチャは5つの基本原則に基づく。

### 原則 1: Composition over Inheritance（合成優先）

ボードは MCU + ピン配置 + 外部デバイス + デバッグプローブの**合成**として定義される。
クラス継承のような暗黙的な結合ではなく、Lua テーブルの明示的な合成を採用する。

```lua
-- ボードは「MCU の選択」「ピンの設定」「デバイスの配置」の合成
return {
    mcu = "stm32f407vg",           -- MCU 選択（database から特性が導出される）
    clock = { hse = 8000000 },     -- ボード固有のクロック設定
    pins = { console_tx = "PA9" }, -- ボード固有のピン配置
    debug = { probe = "stlink" },  -- ボード固有のデバッグ環境
}
```

**根拠:** PLATFORM_COMPARISON.md の調査で、Mbed OS の `inherits` チェーンは追跡困難になり EOL に至った。
modm のデバイスデータベースは「合成 + フィルタ」方式で4,557デバイスを管理している。
合成は継承より局所性が高く、変更の影響範囲が予測しやすい。

### 原則 2: Single Source of Truth（唯一の真実の源泉）

ハードウェアに関するデータは **Lua データベースのみ** に定義される。
リンカスクリプト、C++ ヘッダの constexpr 定数、xmake 設定は全てこのデータベースから**導出**される。

```
Lua データベース (唯一の源泉)
  ├── → memory.ld        (自動生成)
  ├── → xmake 設定       (ルールが自動適用)
  ├── → C++ board.hh     (手書きだが、DB の値と一致すべき)
  └── → デバッグ設定      (ルールが自動適用)
```

**根拠:** 現状では `mcu-database.json`（arm-embedded パッケージ）と `linker.ld`（umiport）に
メモリ情報が重複しており、不整合のリスクがある（HARDWARE_DATA_CONSOLIDATION.md Section 1.2）。
modm, libopencm3, Zephyr, ESP-IDF の全てが「唯一の源泉からリンカスクリプトを生成」している。

### 原則 3: Zero-Copy Customization（フォーク不要のカスタマイズ）

既存ボード定義を変更したい場合、元のファイルをコピーやフォークする必要がない。
差分のみを記述した拡張定義で、親ボードの設定を部分的にオーバーライドできる。

```lua
-- 親ボードの 100% をコピーせず、差分の 5% だけを書く
return {
    extends = "stm32f4-disco",
    clock = { hse = 25000000 },  -- 水晶だけ異なる
}
```

**根拠:** Zephyr の HWMv2 は `board extension` でフォーク不要のカスタマイズを実現している。
PlatformIO は `board.json` に継承がなく、同一 MCU の 291 ボードがデータを重複している。
UMI が目指すのは Zephyr の柔軟性と PlatformIO の簡潔さの両立。

### 原則 4: Build-system と Type-system の責務分離

ビルドシステム（xmake Lua）と型システム（C++ Concepts）の責務を明確に分離する。

| 責務 | 担当 | 例 |
|------|------|-----|
| MCU メモリマップ | xmake (Lua DB → memory.ld 生成) | FLASH origin/size |
| コンパイラフラグ | xmake (embedded ルール) | `-mcpu=cortex-m4` |
| ピン配置の定数 | C++ (board.hh constexpr) | `console_tx = PA9` |
| HAL 契約の充足検証 | C++ (concept + static_assert) | `Platform<T>` |
| startup / リンカ選択 | xmake (umiport.board ルール) | `startup.cc`, `sections.ld` |

**根拠:** xmake の Lua はビルド構成の柔軟性を、C++ Concepts はゼロオーバーヘッドの型安全性を提供する。
両者の強みを最大化するには、互いの領域に踏み込まないことが重要。
Rust Embedded の trait ベースアーキテクチャと UMI の concept ベース設計は、
型システム側の責務において非常に類似している。

### 原則 5: Progressive Disclosure（段階的開示）

単純なユースケースは単純な設定で済み、複雑なユースケースは段階的に詳細を指定できる。

| ユースケース | 必要な設定 |
|-------------|-----------|
| 既存ボードをそのまま使う | `set_values("umiport.board", "stm32f4-disco")` の1行 |
| 既存ボードのクロックだけ変える | `extends` で差分定義（3行） |
| 完全な新規ボード | MCU + clock + pins + debug の完全定義（15行） |
| カスタムリンカスクリプト | `umiport.linker = "false"` で自動生成を無効化 |

**根拠:** PlatformIO の成功要因は「JSON 1ファイルでボードが追加できる」簡潔さ。
Zephyr の弱点は最小構成でも DTS + Kconfig + CMake の三重設定が必要な複雑さ。
UMI は「1行で始められ、必要に応じて段階的に制御を奪える」設計を目指す。

---

## 2. アーキテクチャ概要

### 2.1 5層アーキテクチャ

```
Layer 5: Application (examples/, tests/)
  │  set_values("umiport.board", "my-custom-board")
  │  ソースコードはボード非依存
  │
Layer 4: Board Definition (umiport/boards/ or project-local boards/)
  │  Lua テーブル: MCU 選択, ピン配置, クロック設定, デバッグプローブ
  │  C++ platform.hh: concept を満たす型定義
  │
Layer 3: MCU Database (umiport/database/)
  │  family.lua → mcu.lua: メモリマップ, コア種別, ペリフェラル
  │  memory.ld 自動生成の源泉
  │
Layer 2: Platform Abstraction (umiport/include/)
  │  C++ concept 充足型: Platform, Output, Timer
  │  MCU ドライバ: UART, GPIO, I2C, DWT
  │
Layer 1: HAL Concepts (umihal/include/)
     C++ concept 定義: Platform, OutputDevice, PlatformWithTimer
     全ボードが満たすべき契約
```

### 2.2 データフロー

```
                        ┌──────────────────────────┐
                        │  arm-embedded パッケージ   │
                        │  cortex-m.json            │
                        │  (普遍的アーキ仕様)        │
                        └───────────┬──────────────┘
                                    │ embedded.core = "cortex-m4f"
                                    │ → コンパイラフラグ導出
                                    │
  ┌────────────────────────────────▼─────────────────────────────────┐
  │  umiport                                                         │
  │                                                                   │
  │  ┌──────────────────┐    ┌─────────────────────────────────┐     │
  │  │ database/         │    │ boards/                         │     │
  │  │  family/stm32f4   │◀──│  stm32f4-disco/board.lua       │     │
  │  │  mcu/stm32f407vg  │    │    mcu = "stm32f407vg"         │     │
  │  └────────┬─────────┘    │    clock.hse = 8000000          │     │
  │           │ dofile()      └──────────┬──────────────────────┘     │
  │           ▼                          │                            │
  │  ┌────────────────────┐              │                            │
  │  │ 継承解決 (Lua)      │              │                            │
  │  │ family + mcu merge  │              │                            │
  │  └────────┬───────────┘              │                            │
  │           │                          │                            │
  │  ┌────────▼───────────┐    ┌────────▼─────────────────────┐     │
  │  │ memory.ld (生成)    │    │ platform.hh (手書き C++)      │     │
  │  │ MEMORY { FLASH ... }│    │ struct Platform { ... }       │     │
  │  └────────────────────┘    │ static_assert(Platform<P>)    │     │
  │                             └──────────────────────────────┘     │
  └──────────────────────────────────────────────────────────────────┘
```

### 2.3 ボード定義の二重性

ボード定義は **Lua 側**（ビルド構成）と **C++ 側**（型定義）の二重構造を持つ。

| 側 | ファイル | 役割 | 消費者 |
|----|---------|------|--------|
| Lua | `board.lua` | MCU 選択, メモリ, デバッグ設定 | xmake ルール |
| C++ | `platform.hh` | HAL concept 充足型, Output/Timer 結合 | アプリケーションコード |
| C++ | `board.hh` | constexpr 定数（ピン, クロック） | platform.hh / アプリ |

この二重性は「ビルドシステムの責務」と「型システムの責務」の分離原則から自然に導かれる。
Lua 側はコンパイル前に作用し（フラグ, リンカスクリプト）、C++ 側はコンパイル時に作用する（型検証）。

---

## 3. ボード定義の構造

### 3.1 MCU データベース（Lua）

#### A. ファミリ定義

```lua
-- database/family/stm32f4.lua
-- STM32F4 ファミリ共通定義
return {
    core = "cortex-m4f",
    vendor = "st",                       -- 分類ラベル（継承元ではない）

    -- ビルド・デバッグ設定
    openocd_target = "stm32f4x",
    flash_pack = "stm32f4",
    flash_tool = "pyocd",
    src_dir = "stm32f4",                 -- startup/syscalls/sections.ld の場所

    -- メモリ領域（ファミリ共通のアドレス、サイズは MCU が上書き）
    memory = {
        FLASH = { attr = "rx",  origin = 0x08000000 },
        SRAM  = { attr = "rwx", origin = 0x20000000 },
        CCM   = { attr = "rwx", origin = 0x10000000 },
    },

    -- シミュレータ
    renode_repl = "stm32f4_test.repl",
}
```

#### B. MCU バリアント定義

```lua
-- database/mcu/stm32f4/stm32f407vg.lua
-- STM32F407VG: Cortex-M4F, 1M Flash, 128K+64K SRAM, 64K CCM
local family = dofile(path.join(os.scriptdir(), "../../family/stm32f4.lua"))

family.device_name = "STM32F407VG"

-- メモリサイズ（MCU バリアント固有）
family.memory.FLASH.length = "1M"
family.memory.SRAM.length  = "192K"    -- 128K main + 64K auxiliary
family.memory.CCM.length   = "64K"

-- PyOCD フラッシュエイリアス
family.flash_alias = "stm32f407vgtx"

return family
```

```lua
-- database/mcu/stm32f4/stm32f411ce.lua
-- STM32F411CE: Cortex-M4F, 512K Flash, 128K SRAM, CCM なし
local family = dofile(path.join(os.scriptdir(), "../../family/stm32f4.lua"))

family.device_name = "STM32F411CE"

family.memory.FLASH.length = "512K"
family.memory.SRAM.length  = "128K"
family.memory.CCM = nil                 -- CCM メモリなし（ファミリ定義を除去）

family.flash_alias = "stm32f411cetx"

return family
```

#### C. MCU インデックス

```lua
-- database/index.lua
-- MCU 名 → ファイルパスの索引
-- 新 MCU 追加時にここに1行追加する
return {
    -- ST STM32F4 Series
    stm32f401re  = "stm32f4/stm32f401re",
    stm32f407vg  = "stm32f4/stm32f407vg",
    stm32f411ce  = "stm32f4/stm32f411ce",
    stm32f446re  = "stm32f4/stm32f446re",

    -- ST STM32H7 Series
    stm32h743zi  = "stm32h7/stm32h743zi",

    -- Nordic
    nrf52840     = "nrf52/nrf52840",

    -- Raspberry Pi
    rp2040       = "rp2040/rp2040",
}
```

### 3.2 ボード定義（Lua）

#### A. 基本ボード定義（umiport 同梱）

```lua
-- boards/stm32f4-disco/board.lua
-- STM32F4 Discovery Board (STM32F407VG-DISC1)
return {
    mcu = "stm32f407vg",

    -- ボード固有クロック設定
    clock = {
        hse = 8000000,               -- HSE 水晶: 8 MHz
        sysclk = 168000000,          -- SYSCLK: 168 MHz
    },

    -- ボード固有ピン配置
    pins = {
        console_tx = "PA9",           -- USART1 TX
        console_rx = "PA10",          -- USART1 RX
        led_green  = "PD12",
        led_orange = "PD13",
        led_red    = "PD14",
        led_blue   = "PD15",
        audio_i2s_ws   = "PA4",       -- CS43L22 I2S
        audio_i2s_sck  = "PB10",
        audio_i2s_sd   = "PC3",
        audio_i2c_scl  = "PB6",       -- CS43L22 I2C
        audio_i2c_sda  = "PB9",
    },

    -- デバッグ設定
    debug = {
        probe = "stlink",
        interface = "swd",
    },

    -- コンソール設定
    console = {
        uart = "usart1",
        baud = 115200,
    },

    -- 外部デバイス
    devices = {
        audio_codec = "cs43l22",
        accelerometer = "lis3dsh",
    },
}
```

```lua
-- boards/stm32f4-renode/board.lua
-- STM32F4 Renode Virtual Board (Simulation)
return {
    mcu = "stm32f407vg",

    clock = {
        hse = 25000000,               -- Renode デフォルト HSE
        sysclk = 168000000,
    },

    pins = {
        console_tx = "PA9",
        console_rx = "PA10",
    },

    debug = {
        probe = "renode",              -- Renode シミュレータ
        renode_repl = "stm32f4_test.repl",
    },

    console = {
        uart = "usart1",
        baud = 115200,
    },
}
```

#### B. 拡張ボード定義（プロジェクトローカル）

```lua
-- project/boards/my-custom-board/board.lua
-- STM32F4 Discovery を基にしたカスタム基板
-- 異なる水晶と再配置されたオーディオ I2S ピンを持つ
return {
    extends = "stm32f4-disco",

    -- 差分のみ記述（親ボードの全設定を継承）
    clock = {
        hse = 25000000,               -- 水晶を 25 MHz に変更
        -- sysclk は親から継承 (168 MHz)
    },

    pins = {
        audio_i2s_sck = "PB13",       -- I2S SCK を再配置
        -- 他のピンは全て親から継承
    },

    -- debug, console, devices は親から全て継承
}
```

#### C. 新規ボード定義（フルスクラッチ）

```lua
-- project/boards/my-new-board/board.lua
-- WeAct Studio STM32F411CE "Black Pill"
return {
    mcu = "stm32f411ce",

    clock = {
        hse = 25000000,
        sysclk = 100000000,
    },

    pins = {
        console_tx = "PA2",            -- USART2 TX
        console_rx = "PA3",            -- USART2 RX
        led_user   = "PC13",           -- オンボード LED
    },

    debug = {
        probe = "cmsis-dap",
        interface = "swd",
    },

    console = {
        uart = "usart2",
        baud = 115200,
    },
}
```

### 3.3 ボード platform.hh（C++ 側）

board.lua のビルド構成から C++ 型定義への橋渡しは `platform.hh` が担う。

#### 基本パターン

```cpp
// boards/stm32f4-renode/platform.hh
// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief STM32F4 Renode virtual board platform definition.

#include <umihal/concept/platform.hh>
#include <umiport/arm/cortex-m/dwt.hh>
#include <umiport/mcu/stm32f4/uart_output.hh>

namespace umi::port {

/// @brief STM32F4 Renode platform definition.
struct Platform {
    using Output = stm32f4::RenodeUartOutput;
    using Timer  = cortex_m::DwtTimer;

    static void init() { Output::init(); }

    /// @brief Platform name for reports.
    static constexpr const char* name() { return "stm32f4-renode"; }
};

// コンパイル時契約検証
static_assert(umi::hal::Platform<Platform>,
    "Platform must satisfy umi::hal::Platform concept");
static_assert(umi::hal::PlatformWithTimer<Platform>,
    "Platform must satisfy umi::hal::PlatformWithTimer concept");

} // namespace umi::port
```

#### 外部デバイス統合パターン

```cpp
// boards/stm32f4-disco/platform.hh
// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief STM32F4 Discovery board platform definition.

#include <umihal/concept/platform.hh>
#include <umiport/arm/cortex-m/dwt.hh>
#include <umiport/mcu/stm32f4/uart_output.hh>
#include <umiport/mcu/stm32f4/i2c.hh>
#include <umidevice/audio/cs43l22/cs43l22.hh>

namespace umi::port {

/// @brief STM32F4 Discovery platform definition.
struct Platform {
    using Output = stm32f4::UartOutput;
    using Timer  = cortex_m::DwtTimer;
    using I2C    = stm32f4::I2C1;
    using Codec  = umi::device::CS43L22Driver<I2C>;

    static void init() { Output::init(); }

    static constexpr const char* name() { return "stm32f4-disco"; }
};

static_assert(umi::hal::Platform<Platform>);
static_assert(umi::hal::PlatformWithTimer<Platform>);

} // namespace umi::port
```

#### 拡張ボードの platform.hh

拡張ボードでは、差分が C++ 型に影響する場合のみ platform.hh を提供する。
ピン番号の変更だけなら board.hh の constexpr を変更するだけで、platform.hh は親と同一で済む。

```cpp
// project/boards/my-custom-board/platform.hh
// 差分がピン番号のみの場合、親の platform.hh をそのまま転送可能
// ただし board.hh の定数が異なるため、ファイルは必須
#pragma once

#include <umihal/concept/platform.hh>
#include <umiport/arm/cortex-m/dwt.hh>
#include <umiport/mcu/stm32f4/uart_output.hh>

namespace umi::port {

struct Platform {
    using Output = stm32f4::UartOutput;
    using Timer  = cortex_m::DwtTimer;

    static void init() { Output::init(); }
    static constexpr const char* name() { return "my-custom-board"; }
};

static_assert(umi::hal::Platform<Platform>);

} // namespace umi::port
```

### 3.4 board.hh（C++ 定数）

```cpp
// boards/stm32f4-disco/board.hh
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>

namespace umi::board {

/// @brief Board constants for STM32F4 Discovery board.
struct Stm32f4Disco {
    static constexpr uint32_t hse_frequency = 8'000'000;
    static constexpr uint32_t system_clock  = 168'000'000;

    struct Pin {
        static constexpr uint32_t console_tx = 9;    // PA9
        static constexpr uint32_t console_rx = 10;   // PA10
    };

    struct Memory {
        static constexpr uint32_t flash_base = 0x08000000;
        static constexpr uint32_t flash_size = 1024 * 1024;
        static constexpr uint32_t ram_base   = 0x20000000;
        static constexpr uint32_t ram_size   = 128 * 1024;
    };
};

} // namespace umi::board
```

---

## 4. xmake ルールシステム設計

### 4.1 ルールチェーンの全体像

```
embedded ルール (on_load) — arm-embedded パッケージ提供
  │  embedded.core を受け取り、コンパイラフラグを導出
  │  cortex-m.json: コア → target triple, FPU, -mcpu フラグ
  │
  ▼
umiport.board ルール (on_load + on_config) — umiport 提供
  │
  │  on_load:
  │    1. MCU データベース読み込み (index.lua → mcu.lua → family.lua)
  │    2. embedded.core を設定 (MCU → コア名の解決)
  │    3. MCU データを target:data() に格納
  │
  │  on_config:
  │    1. ボード定義読み込み (board.lua)
  │    2. 継承解決 (extends があれば再帰的にマージ)
  │    3. ボード固有 includedirs の設定
  │    4. startup.cc / syscalls.cc の追加
  │    5. memory.ld 生成 (Lua DB → リンカスクリプト)
  │    6. sections.ld の適用
  │    7. デバッグ設定の target:data() 格納
  │
  ▼
アプリケーションターゲット
  ボード非依存ソースをビルド
```

### 4.2 ボード定義の読み込みと継承解決

```lua
-- umiport/rules/board.lua
rule("umiport.board")

    on_load(function(target)
        local board_name = target:values("umiport.board")
        if not board_name then return end

        local umiport_dir = path.join(os.scriptdir(), "..")
        local db_dir = path.join(umiport_dir, "database")

        -- ボード定義の読み込みと継承解決
        local board_config = load_board(umiport_dir, board_name, target)

        -- MCU データベースの解決
        local mcu_name = board_config.mcu
        if not mcu_name then
            raise("umiport: Board '%s' does not specify 'mcu'", board_name)
        end

        local index = dofile(path.join(db_dir, "index.lua"))
        local mcu_path = index[mcu_name:lower()]
        if not mcu_path then
            raise("umiport: Unknown MCU '%s' (board: %s). "
                .. "Add it to database/index.lua", mcu_name, board_name)
        end
        local mcu_config = dofile(path.join(db_dir, "mcu", mcu_path .. ".lua"))

        -- embedded.core を設定（embedded ルールへの入力）
        target:set("values", "embedded.core", mcu_config.core)
        target:set("values", "embedded.mcu", mcu_name)

        -- データを target:data() に格納（他のルール・フェーズから参照可能）
        target:data_set("umiport.mcu_config", mcu_config)
        target:data_set("umiport.board_config", board_config)
        target:data_set("umiport.mcu_name", mcu_name)
    end)

    on_config(function(target)
        local board_name = target:values("umiport.board")
        if not board_name then return end

        local umiport_dir = path.join(os.scriptdir(), "..")
        local mcu_config = target:data("umiport.mcu_config")
        local board_config = target:data("umiport.board_config")
        if not mcu_config then return end

        -- ボード固有 includedirs
        local board_include = resolve_board_include(umiport_dir, board_name, target)
        target:add("includedirs", board_include, {public = false})

        -- startup 無効化オプション
        local use_startup = target:values("umiport.startup")
        if use_startup == "false" then return end

        -- MCU ファミリからソースディレクトリを決定
        local src_dir = mcu_config.src_dir
        if not src_dir then return end

        local mcu_src = path.join(umiport_dir, "src", src_dir)
        target:add("files", path.join(mcu_src, "startup.cc"))
        target:add("files", path.join(mcu_src, "syscalls.cc"))

        -- リンカ無効化オプション
        local use_linker = target:values("umiport.linker")
        if use_linker == "false" then return end

        -- memory.ld を Lua データベースから生成
        local memory_ld_path = path.join(target:autogendir(), "memory.ld")
        generate_memory_ld(mcu_config, memory_ld_path)

        -- embedded ルールが設定した -T フラグ（common.ld）を除去
        local old_flags = target:get("ldflags") or {}
        local new_flags = {}
        for _, flag in ipairs(old_flags) do
            if not flag:find("^%-T") then
                table.insert(new_flags, flag)
            end
        end
        target:set("ldflags", new_flags)

        -- sections.ld (INCLUDE memory.ld を含む) を適用
        local sections_ld = path.join(mcu_src, "sections.ld")
        target:add("ldflags", "-T" .. sections_ld, {force = true})
        target:add("ldflags", "-L" .. path.directory(memory_ld_path), {force = true})
    end)
```

### 4.3 ボード読み込み関数

```lua
-- umiport/rules/board_loader.lua

--- ボード定義をファイルシステムから読み込む
--- 検索順序: プロジェクトローカル boards/ → umiport/boards/
local function find_board_file(umiport_dir, board_name, target)
    -- 1. プロジェクトローカル
    local project_board = path.join(os.projectdir(), "boards", board_name, "board.lua")
    if os.isfile(project_board) then
        return project_board
    end

    -- 2. umiport 同梱
    local umiport_board = path.join(umiport_dir, "boards", board_name, "board.lua")
    if os.isfile(umiport_board) then
        return umiport_board
    end

    raise("umiport: Board '%s' not found.\n"
        .. "Searched:\n"
        .. "  1. %s\n"
        .. "  2. %s\n"
        .. "Create board.lua in one of these locations.",
        board_name,
        path.join(os.projectdir(), "boards", board_name),
        path.join(umiport_dir, "boards", board_name))
end

--- ボード定義を読み込み、extends があれば再帰的に親を解決する
local function load_board(umiport_dir, board_name, target)
    local board_file = find_board_file(umiport_dir, board_name, target)
    local board = dofile(board_file)

    if board.extends then
        local parent_name = board.extends
        board.extends = nil  -- 無限ループ防止

        -- 親ボードを再帰的に読み込み
        local parent = load_board(umiport_dir, parent_name, target)

        -- 子が親を部分オーバーライド（deep merge）
        board = deep_merge(parent, board)
    end

    return board
end

--- ボード固有 includedirs の解決
--- 検索順序: プロジェクトローカル → umiport/include/umiport/board/
local function resolve_board_include(umiport_dir, board_name, target)
    -- プロジェクトローカル
    local project_include = path.join(os.projectdir(), "boards", board_name)
    if os.isdir(project_include) and os.isfile(path.join(project_include, "platform.hh")) then
        return project_include
    end

    -- umiport 同梱
    local umiport_include = path.join(umiport_dir, "include/umiport/board", board_name)
    if os.isdir(umiport_include) then
        return umiport_include
    end

    raise("umiport: Board include directory not found for '%s'", board_name)
end
```

### 4.4 memory.ld 生成

```lua
-- umiport/rules/memory_ld.lua

--- Lua データベースから memory.ld を生成する
--- @param mcu_config table MCU 設定（family + mcu マージ済み）
--- @param output_path string 出力ファイルパス
local function generate_memory_ld(mcu_config, output_path)
    local lines = {
        "/* Auto-generated from umiport MCU database -- DO NOT EDIT */",
        "",
        "MEMORY",
        "{",
    }

    -- 領域名をソートして出力順序を安定させる
    local names = {}
    for name, region in pairs(mcu_config.memory) do
        if region.length then  -- length が定義されている領域のみ出力
            table.insert(names, name)
        end
    end
    table.sort(names)

    for _, name in ipairs(names) do
        local region = mcu_config.memory[name]
        table.insert(lines, string.format(
            "    %-12s (%s) : ORIGIN = 0x%08X, LENGTH = %s",
            name, region.attr, region.origin, region.length))
    end

    table.insert(lines, "}")
    table.insert(lines, "")
    table.insert(lines, "ENTRY(_start)")
    table.insert(lines, "")

    -- ディレクトリが存在しなければ作成
    os.mkdir(path.directory(output_path))
    io.writefile(output_path, table.concat(lines, "\n"))
end
```

生成される `memory.ld` の例（STM32F407VG）:

```ld
/* Auto-generated from umiport MCU database -- DO NOT EDIT */

MEMORY
{
    CCM          (rwx) : ORIGIN = 0x10000000, LENGTH = 64K
    FLASH        (rx)  : ORIGIN = 0x08000000, LENGTH = 1M
    SRAM         (rwx) : ORIGIN = 0x20000000, LENGTH = 192K
}

ENTRY(_start)
```

生成される `memory.ld` の例（STM32F411CE、CCM なし）:

```ld
/* Auto-generated from umiport MCU database -- DO NOT EDIT */

MEMORY
{
    FLASH        (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    SRAM         (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

ENTRY(_start)
```

### 4.5 リンカスクリプトの合成

リンカスクリプトは `sections.ld`（手書き、ファミリ共通）が `INCLUDE memory.ld`（自動生成）を取り込む構造。

```ld
-- src/stm32f4/sections.ld（手書き、ファミリ共通）
INCLUDE memory.ld

_stack_size = DEFINED(_stack_size) ? _stack_size : 8K;
_heap_size  = DEFINED(_heap_size)  ? _heap_size  : 32K;

SECTIONS
{
    .isr_vector : {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text : {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* ... 標準セクション ... */

    .data : AT(_sidata) {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM

    .bss : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > SRAM

    /* CCM がある MCU では使用可能 */
    .ccm (NOLOAD) : {
        *(.ccm)
        *(.ccm*)
    } > CCM
}
```

**合成パターンの分類:**

| パターン | MEMORY | SECTIONS | xmake 設定 |
|---------|--------|----------|-----------|
| **A: 全自動** | DB から生成 | ファミリ共通 sections.ld | デフォルト |
| **B: セクション追加** | DB から生成 | 標準 + `-T extra.ld` | `add_ldflags("-T" .. extra_ld)` |
| **C: 完全カスタム** | アプリ所有 | アプリ所有 | `umiport.linker = "false"` |

### 4.6 deep_merge 関数

```lua
-- umiport/rules/utils.lua

--- 2つの Lua テーブルを深くマージする
--- child の値が parent の値を上書きする
--- テーブルの場合は再帰的にマージ、配列は child で完全置換
local function deep_merge(parent, child)
    local result = {}

    -- 親の全フィールドをコピー
    for k, v in pairs(parent) do
        if type(v) == "table" then
            result[k] = deep_merge({}, v)  -- 深いコピー
        else
            result[k] = v
        end
    end

    -- 子のフィールドで上書き
    for k, v in pairs(child) do
        if type(v) == "table" and type(result[k]) == "table" then
            -- 両方テーブルの場合: 再帰マージ
            result[k] = deep_merge(result[k], v)
        else
            -- それ以外: 子で完全置換
            result[k] = v
        end
    end

    return result
end
```

**マージセマンティクス:**

```lua
-- 親: stm32f4-disco
{ clock = { hse = 8000000, sysclk = 168000000 }, pins = { tx = "PA9", rx = "PA10" } }

-- 子: my-custom-board (extends = "stm32f4-disco")
{ clock = { hse = 25000000 }, pins = { audio_i2s_sck = "PB13" } }

-- マージ結果:
{
    clock = { hse = 25000000, sysclk = 168000000 },  -- hse は上書き、sysclk は親から継承
    pins = { tx = "PA9", rx = "PA10", audio_i2s_sck = "PB13" },  -- 全ピンがマージ
}
```

---

## 5. ディレクトリ構造

### 5.1 umiport パッケージ構造

```
lib/umiport/
├── database/                          MCU データベース（唯一の真実の源泉）
│   ├── index.lua                      MCU 名 → ファイルパス索引
│   ├── family/                        ファミリ共通定義
│   │   ├── stm32f4.lua                Cortex-M4F, flash/ram/ccm 共通アドレス
│   │   ├── stm32h7.lua                Cortex-M7, 複数 SRAM 領域
│   │   ├── nrf52.lua                  Cortex-M4F, BLE 5.x
│   │   └── rp2040.lua                 Dual Cortex-M0+
│   └── mcu/                           MCU バリアント（ファミリ別サブディレクトリ）
│       ├── stm32f4/
│       │   ├── stm32f401re.lua
│       │   ├── stm32f407vg.lua
│       │   ├── stm32f411ce.lua
│       │   └── stm32f446re.lua
│       ├── stm32h7/
│       │   └── stm32h743zi.lua
│       ├── nrf52/
│       │   └── nrf52840.lua
│       └── rp2040/
│           └── rp2040.lua
│
├── boards/                            ボード定義（Lua 側）
│   ├── stm32f4-disco/
│   │   └── board.lua                  MCU, clock, pins, debug, devices
│   ├── stm32f4-renode/
│   │   └── board.lua                  Renode 仮想ボード
│   └── host/
│       └── board.lua                  ホスト PC (MCU なし)
│
├── include/umiport/
│   ├── board/                         ボード定義（C++ 側）
│   │   ├── stm32f4-disco/
│   │   │   ├── platform.hh            Platform 型定義（concept 充足）
│   │   │   └── board.hh               constexpr 定数
│   │   ├── stm32f4-renode/
│   │   │   ├── platform.hh
│   │   │   └── board.hh
│   │   ├── host/
│   │   │   └── platform.hh
│   │   └── wasm/
│   │       └── platform.hh
│   ├── mcu/                           MCU 固有ドライバ
│   │   ├── stm32f4/
│   │   │   ├── uart_output.hh
│   │   │   ├── rcc.hh
│   │   │   ├── gpio.hh
│   │   │   └── i2c.hh
│   │   └── stm32h7/
│   │       └── ...
│   └── arm/cortex-m/                  アーキテクチャ共通
│       ├── cortex_m_mmio.hh           CoreSight DWT/CoreDebug レジスタ
│       └── dwt.hh                     DWT サイクルカウンタ
│
├── rules/                             xmake ルール
│   ├── board.lua                      umiport.board ルール本体
│   ├── board_loader.lua               ボード読み込み・継承解決
│   ├── memory_ld.lua                  memory.ld 生成
│   └── utils.lua                      deep_merge 等のユーティリティ
│
├── src/                               ファミリ別ソース
│   ├── stm32f4/
│   │   ├── startup.cc                 ベクタテーブル、Reset_Handler
│   │   ├── syscalls.cc                _write() + write_bytes()
│   │   └── sections.ld               INCLUDE memory.ld + SECTIONS
│   ├── stm32h7/
│   │   ├── startup.cc
│   │   ├── syscalls.cc
│   │   └── sections.ld
│   ├── host/
│   │   └── write.cc                   umi::rt::detail::write_bytes() → stdout
│   └── wasm/
│       └── write.cc                   umi::rt::detail::write_bytes() → fwrite
│
├── renode/                            Renode プラットフォーム記述
│   └── stm32f4_test.repl
│
└── xmake.lua
```

### 5.2 プロジェクトローカルのボード定義

ユーザーのプロジェクトでカスタムボードを定義する場合:

```
my-project/
├── boards/                            プロジェクトローカルボード
│   ├── my-custom-board/
│   │   ├── board.lua                  extends = "stm32f4-disco"（Lua 側）
│   │   ├── platform.hh               Platform 型定義（C++ 側）
│   │   └── board.hh                   カスタムボード定数
│   └── my-new-board/
│       ├── board.lua                  新規ボード（extends なし）
│       ├── platform.hh
│       └── board.hh
├── src/
│   └── main.cc                        ボード非依存アプリケーション
└── xmake.lua
```

`xmake.lua`:

```lua
set_project("my-synth")
set_languages("c++23")

includes("path/to/umi/lib/umiport")

target("my-synth-app")
    add_rules("embedded", "umiport.board")
    set_values("umiport.board", "my-custom-board")
    add_deps("umiport", "umirtm")
    add_files("src/main.cc")
```

### 5.3 ボード定義の検索順序

| 優先度 | 検索場所 | 用途 |
|--------|---------|------|
| 1 (最高) | `<project>/boards/<name>/board.lua` | プロジェクトローカルボード |
| 2 | `umiport/boards/<name>/board.lua` | umiport 同梱ボード |

C++ ヘッダの検索順序:

| 優先度 | 検索場所 | 用途 |
|--------|---------|------|
| 1 (最高) | `<project>/boards/<name>/` | プロジェクトローカル platform.hh |
| 2 | `umiport/include/umiport/board/<name>/` | umiport 同梱 platform.hh |

---

## 6. 継承解決アルゴリズム

### 6.1 アルゴリズム

```
入力: board_name (例: "my-custom-board")

1. find_board_file(board_name) → board.lua のパスを検索
   a. project/boards/<board_name>/board.lua が存在 → それを使用
   b. umiport/boards/<board_name>/board.lua が存在 → それを使用
   c. どちらもない → エラー

2. board = dofile(board.lua) → Lua テーブルとして読み込み

3. if board.extends then
   a. parent_name = board.extends
   b. board.extends = nil (循環参照防止)
   c. parent = load_board(parent_name)  ← 再帰呼び出し
   d. board = deep_merge(parent, board) ← 子が親を上書き

4. return board
```

### 6.2 deep_merge のセマンティクス

| 親の値 | 子の値 | 結果 | 説明 |
|--------|--------|------|------|
| `number` | `number` | 子の値 | スカラー値は完全上書き |
| `string` | `string` | 子の値 | スカラー値は完全上書き |
| `table` | `table` | 再帰マージ | テーブル同士は深いマージ |
| `table` | `nil` 指定なし | 親の値 | 子が言及しないフィールドは親を維持 |
| 任意 | `false` | `false` | 明示的に無効化 |
| 存在しない | `value` | 子の値 | 子が新フィールドを追加 |

### 6.3 具体例

```lua
-- 親: stm32f4-disco/board.lua
{
    mcu = "stm32f407vg",
    clock = { hse = 8000000, sysclk = 168000000 },
    pins = {
        console_tx = "PA9",
        console_rx = "PA10",
        led_green = "PD12",
        audio_i2s_sck = "PB10",
    },
    debug = { probe = "stlink", interface = "swd" },
    console = { uart = "usart1", baud = 115200 },
    devices = { audio_codec = "cs43l22" },
}

-- 子: my-custom-board/board.lua
{
    extends = "stm32f4-disco",
    clock = { hse = 25000000 },          -- hse だけ変更
    pins = { audio_i2s_sck = "PB13" },   -- 1ピンだけ変更
}

-- 継承解決後:
{
    mcu = "stm32f407vg",                 -- 親から継承
    clock = {
        hse = 25000000,                  -- 子で上書き
        sysclk = 168000000,             -- 親から継承
    },
    pins = {
        console_tx = "PA9",             -- 親から継承
        console_rx = "PA10",            -- 親から継承
        led_green = "PD12",             -- 親から継承
        audio_i2s_sck = "PB13",         -- 子で上書き
    },
    debug = { probe = "stlink", interface = "swd" },  -- 親から継承
    console = { uart = "usart1", baud = 115200 },     -- 親から継承
    devices = { audio_codec = "cs43l22" },             -- 親から継承
}
```

### 6.4 必須フィールドのバリデーション

継承解決後、最終的なボード定義は以下の必須フィールドを持たなければならない:

```lua
local REQUIRED_FIELDS = {
    "mcu",                             -- MCU 名
}

local REQUIRED_FOR_EMBEDDED = {
    "clock.hse",                       -- HSE 周波数
    "debug.probe",                     -- デバッグプローブ
}

local function validate_board(board, board_name)
    for _, field in ipairs(REQUIRED_FIELDS) do
        if not get_nested(board, field) then
            raise("umiport: Board '%s' is missing required field '%s'", board_name, field)
        end
    end
end
```

---

## 7. 3つのユースケースの開発者体験

### ユースケース 1: 既存ボードを使う（最も一般的）

**シナリオ:** STM32F4 Discovery ボードでオーディオアプリケーションを開発する。

**開発者が書くもの:**

`xmake.lua`:
```lua
target("my-audio-app")
    add_rules("embedded", "umiport.board")
    set_values("umiport.board", "stm32f4-disco")
    add_deps("umiport", "umidevice", "umirtm")
    add_files("src/main.cc")
```

`src/main.cc`:
```cpp
#include <umirtm/print.hh>

int main() {
    rt::println("Audio App Starting...");
    // アプリケーションロジック
    return 0;
}
```

**内部で起きること:**

```
1. umiport.board ルール on_load:
   - "stm32f4-disco" → boards/stm32f4-disco/board.lua 読み込み
   - mcu = "stm32f407vg" → database/index.lua → database/mcu/stm32f4/stm32f407vg.lua
   - stm32f407vg.lua が family/stm32f4.lua を dofile → マージ済み MCU 設定取得
   - embedded.core = "cortex-m4f" を設定
   - mcu_config を target:data() に格納

2. embedded ルール on_load:
   - embedded.core = "cortex-m4f" → cortex-m.json 参照
   - -mcpu=cortex-m4, -mfpu=fpv4-sp-d16 等のフラグ設定

3. umiport.board ルール on_config:
   - includedirs: umiport/include/umiport/board/stm32f4-disco/
   - add_files: src/stm32f4/startup.cc, src/stm32f4/syscalls.cc
   - memory.ld 生成 → build/.gens/.../memory.ld
   - ldflags: -T src/stm32f4/sections.ld -L build/.gens/.../

4. ビルド:
   - startup.cc → Reset_Handler → Platform::init() → main()
   - syscalls.cc → write_bytes() → Platform::Output::putc()
```

**CLI コマンド:**

```bash
xmake build my-audio-app         # ビルド
xmake run my-audio-app            # フラッシュ (PyOCD) or Renode シミュレーション
```

### ユースケース 2: 既存ボードを拡張する（カスタム基板）

**シナリオ:** STM32F4 Discovery を基にしたカスタム基板を開発。水晶が 25 MHz に変更され、
I2S のピン配置も異なる。

**開発者が書くもの:**

`boards/my-custom-board/board.lua`:
```lua
return {
    extends = "stm32f4-disco",
    clock = { hse = 25000000 },
    pins = { audio_i2s_sck = "PB13" },
}
```

`boards/my-custom-board/board.hh`:
```cpp
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>

namespace umi::board {

struct MyCustomBoard {
    static constexpr uint32_t hse_frequency = 25'000'000;
    static constexpr uint32_t system_clock  = 168'000'000;

    struct Pin {
        static constexpr uint32_t console_tx = 9;    // PA9（親から継承）
        static constexpr uint32_t console_rx = 10;   // PA10（親から継承）
        static constexpr uint32_t audio_i2s_sck = 13; // PB13（変更）
    };
};

} // namespace umi::board
```

`boards/my-custom-board/platform.hh`:
```cpp
// SPDX-License-Identifier: MIT
#pragma once
#include <umihal/concept/platform.hh>
#include <umiport/arm/cortex-m/dwt.hh>
#include <umiport/mcu/stm32f4/uart_output.hh>

namespace umi::port {

struct Platform {
    using Output = stm32f4::UartOutput;
    using Timer  = cortex_m::DwtTimer;
    static void init() { Output::init(); }
    static constexpr const char* name() { return "my-custom-board"; }
};

static_assert(umi::hal::Platform<Platform>);

} // namespace umi::port
```

`xmake.lua`:
```lua
target("my-custom-app")
    add_rules("embedded", "umiport.board")
    set_values("umiport.board", "my-custom-board")
    add_deps("umiport", "umirtm")
    add_files("src/main.cc")
```

**内部で起きること:**

```
1. "my-custom-board" → project/boards/my-custom-board/board.lua 発見
2. extends = "stm32f4-disco" → umiport/boards/stm32f4-disco/board.lua を読み込み
3. deep_merge: 子の clock.hse と pins.audio_i2s_sck が親を上書き
4. mcu = "stm32f407vg"（親から継承）→ 以降はユースケース 1 と同じ
5. includedirs: project/boards/my-custom-board/ が設定される
   （プロジェクトローカルが優先されるため、親の platform.hh ではなく子の platform.hh が使われる）
```

### ユースケース 3: 新規ボードをゼロから作る

**シナリオ:** WeAct Studio の STM32F411CE "Black Pill" ボードを新規に追加する。
UMI のデータベースに STM32F411CE の定義は既にあるが、ボード定義はない。

**開発者が書くもの:**

`boards/blackpill-f411/board.lua`:
```lua
return {
    mcu = "stm32f411ce",
    clock = {
        hse = 25000000,
        sysclk = 100000000,
    },
    pins = {
        console_tx = "PA2",
        console_rx = "PA3",
        led_user   = "PC13",
    },
    debug = {
        probe = "cmsis-dap",
        interface = "swd",
    },
    console = {
        uart = "usart2",
        baud = 115200,
    },
}
```

`boards/blackpill-f411/board.hh`:
```cpp
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>

namespace umi::board {

struct BlackpillF411 {
    static constexpr uint32_t hse_frequency = 25'000'000;
    static constexpr uint32_t system_clock  = 100'000'000;

    struct Pin {
        static constexpr uint32_t console_tx = 2;   // PA2
        static constexpr uint32_t console_rx = 3;   // PA3
        static constexpr uint32_t led_user   = 13;  // PC13
    };

    struct Memory {
        static constexpr uint32_t flash_base = 0x08000000;
        static constexpr uint32_t flash_size = 512 * 1024;
        static constexpr uint32_t ram_base   = 0x20000000;
        static constexpr uint32_t ram_size   = 128 * 1024;
    };
};

} // namespace umi::board
```

`boards/blackpill-f411/platform.hh`:
```cpp
// SPDX-License-Identifier: MIT
#pragma once
#include <umihal/concept/platform.hh>
#include <umiport/arm/cortex-m/dwt.hh>
#include <umiport/mcu/stm32f4/uart_output.hh>

namespace umi::port {

struct Platform {
    using Output = stm32f4::UartOutput;
    using Timer  = cortex_m::DwtTimer;
    static void init() { Output::init(); }
    static constexpr const char* name() { return "blackpill-f411"; }
};

static_assert(umi::hal::Platform<Platform>);
static_assert(umi::hal::PlatformWithTimer<Platform>);

} // namespace umi::port
```

`xmake.lua`:
```lua
target("blackpill-demo")
    add_rules("embedded", "umiport.board")
    set_values("umiport.board", "blackpill-f411")
    add_deps("umiport", "umirtm")
    add_files("src/main.cc")
```

**内部で起きること:**

```
1. "blackpill-f411" → project/boards/blackpill-f411/board.lua 発見
2. extends なし → 継承解決不要
3. mcu = "stm32f411ce" → database/index.lua → "stm32f4/stm32f411ce"
4. stm32f411ce.lua が family/stm32f4.lua を dofile
   - CCM = nil（STM32F411 には CCM がない）
5. memory.ld 生成: FLASH (512K) + SRAM (128K) のみ（CCM なし）
6. sections.ld の .ccm セクションは MEMORY に CCM がないため空になる
   （GNU ld は未定義のメモリ領域へのセクション配置をスキップ）
7. src/stm32f4/startup.cc, syscalls.cc は同じファミリなので共有
```

**MCU 定義がデータベースにない場合:**

```
エラーメッセージ:
  umiport: Unknown MCU 'stm32f411ce' (board: blackpill-f411).
  Add it to database/index.lua

対応手順:
  1. database/mcu/stm32f4/stm32f411ce.lua を作成（family を dofile、メモリサイズを設定）
  2. database/index.lua に1行追加: stm32f411ce = "stm32f4/stm32f411ce"
```

---

## 8. 既存システムとの比較

### 8.1 比較表

| 軸 | UMI (提案) | modm | Zephyr | PlatformIO | Rust/Embassy |
|----|-----------|------|--------|------------|-------------|
| **データベース形式** | Lua テーブル | 独自 XML + Python | DTS + YAML | JSON | YAML + TOML |
| **データ継承** | `dofile()` + `extends` | マージ + フィルタ | DTS include チェーン | なし | 層ごと独立 |
| **リンカスクリプト** | DB から memory.ld 生成 | テンプレートから全生成 | テンプレート生成 | テンプレート (2領域限定) | memory.x (手書き) |
| **型安全** | C++ Concepts | C++ テンプレート | Kconfig マクロ | なし | Rust traits |
| **ボード追加** | board.lua + platform.hh + board.hh | project.xml + BSP module | 5+ ファイル | JSON 1ファイル | BSP クレート |
| **ビルドシステム** | xmake (Lua) | lbuild (Python) | CMake + Kconfig | SCons (Python) | Cargo |
| **ツール依存** | xmake のみ | Python + Jinja2 + lbuild | Python + cmake + ninja | Python + SCons | Rust toolchain |

### 8.2 各システムとの詳細比較

#### vs modm: 最も影響を受けたシステム

**共通点:**
- MCU データベースをコードから分離し、唯一の真実の源泉とする
- データベースからリンカスクリプトを生成する
- ボード定義が MCU を `extends` で参照する

**差異:**
- modm は Python + Jinja2 でスタートアップコードまで完全生成。UMI は startup.cc を手書きで維持する。
  理由: UMI の startup.cc は C++23 で書かれ、`umi::port::Platform::init()` を呼ぶ構造が
  concept ベース設計の根幹。テンプレート生成は柔軟だが、IDE 上での可読性を損なう
- modm は 4,557 デバイスを持つ巨大 DB。UMI は対象 MCU を限定し、必要に応じて追加
- modm は lbuild (Python 依存)。UMI は xmake の Lua だけで完結し、外部ツール依存がない

#### vs Zephyr: 最も成熟したボード抽象化

**共通点:**
- ボード拡張（フォーク不要のカスタマイズ）
- デバイスとボードの明確な分離
- デバッグプローブ設定のボード帰属

**差異:**
- Zephyr は DTS + Kconfig + CMake の三重設定で学習コストが高い。
  UMI は Lua テーブル1つで完結する（Progressive Disclosure）
- Zephyr の DeviceTree は 8 階層の include チェーンで複雑。
  UMI は family → mcu の 2 段階 + ボード `extends` で十分
- Zephyr は設計が OS 中心。UMI はライブラリ中心で、OS なしのベアメタルが第一市民

#### vs PlatformIO: 最も簡潔なボード追加

**共通点:**
- ボード追加の簡潔さ（PlatformIO: JSON 1ファイル、UMI: Lua + C++ 計3ファイル）
- MCU メモリからリンカスクリプトを生成

**差異:**
- PlatformIO は board.json に継承がなく、291 ボードがデータを重複。
  UMI は `extends` で差分管理
- PlatformIO のリンカ生成は FLASH + RAM の 2 リージョン限定。
  UMI は任意のメモリ領域を扱える（CCM, DTCM, AXI_SRAM 等）
- PlatformIO は Python エコシステム依存。UMI は xmake 単体

#### vs Rust/Embassy: 最も類似した型システム設計

**共通点:**
- trait (Rust) / concept (C++) による HAL 抽象化
- コンパイル時の契約検証（`impl Trait` / `static_assert(concept<T>)`）
- ゼロオーバーヘッド抽象化（vtable なし）
- BSP が PAC/HAL の型を結合する統合点

**差異:**
- Embassy は BSP なしで HAL trait を直接アプリが使用可能。
  UMI も同様に platform.hh なしで MCU ドライバを直接使えるが、推奨しない
- Rust の PAC は SVD から自動生成。UMI の MCU レジスタヘッダは手書き
  （umimmio で抽象化されているが、SVD からの自動生成は未実装）
- Cargo のパッケージ管理は Rust エコシステムの強み。
  UMI は xmake パッケージを使用するが、エコシステムの規模は比較にならない

### 8.3 UMI 固有の強み

1. **xmake の Lua 統一:** データベース定義、ビルドルール、パッケージ管理が全て Lua で統一。
   外部ツール（Python, Jinja2, cmake）への依存がない

2. **concept ベースのゼロオーバーヘッド:** Rust の trait と同等の型安全性を C++23 で実現。
   `static_assert` による即座のコンパイルエラーで「何を実装すべきか」が明確

3. **ワンソースマルチターゲット:** 同じ `main.cc` が ARM (startup.cc), WASM (write.cc), ホスト (write.cc)
   の3環境で動作する。これは `write_bytes()` の link-time 注入で実現

4. **Progressive Disclosure:** 1行（`set_values("umiport.board", "stm32f4-disco")`）で始められ、
   必要に応じて `extends` → フルスクラッチ → カスタムリンカまで段階的に制御を奪える

---

## 9. 実装ロードマップ

### Phase 1: ボードデータベースと継承 (Lua)

**目的:** MCU データベースを Lua 形式で umiport 内に構築し、ボード定義の読み込みと継承解決を実装する。

**作業内容:**

| ステップ | 作業 | 成果物 |
|---------|------|--------|
| 1.1 | `database/family/stm32f4.lua` 作成 | ファミリ共通定義 |
| 1.2 | `database/mcu/stm32f4/stm32f407vg.lua` 作成 | MCU バリアント定義 |
| 1.3 | `database/index.lua` 作成 | MCU 名索引 |
| 1.4 | `boards/stm32f4-disco/board.lua` 作成 | ボード Lua 定義 |
| 1.5 | `boards/stm32f4-renode/board.lua` 作成 | Renode ボード Lua 定義 |
| 1.6 | `rules/board_loader.lua` 実装 | ボード読み込み・継承解決 |
| 1.7 | `rules/utils.lua` 実装 | deep_merge 関数 |

**検証:** Lua テストスクリプトでデータベース読み込みと継承解決が正しく動作することを確認。

### Phase 2: リンカスクリプト生成

**目的:** Lua データベースから memory.ld を自動生成する仕組みを構築する。

**作業内容:**

| ステップ | 作業 | 成果物 |
|---------|------|--------|
| 2.1 | `rules/memory_ld.lua` 実装 | memory.ld 生成関数 |
| 2.2 | `src/stm32f4/sections.ld` 作成 | `INCLUDE memory.ld` 方式のセクション定義 |
| 2.3 | 既存 `src/stm32f4/linker.ld` を分割 | memory.ld + sections.ld 体制への移行 |

**検証:** 生成された memory.ld で既存 ARM ターゲットがビルド・リンクできることを確認。

### Phase 3: プロジェクトローカルボード定義

**目的:** ユーザープロジェクト内にボード定義を配置し、umiport 同梱ボードを拡張できるようにする。

**作業内容:**

| ステップ | 作業 | 成果物 |
|---------|------|--------|
| 3.1 | `rules/board.lua` に on_load + on_config 二段構成を実装 | ルール本体 |
| 3.2 | プロジェクトローカル検索パスの実装 | find_board_file の検索順序 |
| 3.3 | `extends` による継承解決の E2E テスト | テスト用拡張ボード |
| 3.4 | 既存 ARM ターゲットを新ルールに移行 | 各 tests/xmake.lua のリファクタ |

**検証:** 既存全テスト通過 + プロジェクトローカルボードでのビルド成功。

### Phase 4: ボードスキャフォールディング

**目的:** `xmake create-board` タスクで新規ボード定義のテンプレートを自動生成する。

**作業内容:**

| ステップ | 作業 | 成果物 |
|---------|------|--------|
| 4.1 | `xmake create-board` タスク実装 | board.lua, platform.hh, board.hh テンプレート生成 |
| 4.2 | MCU 選択 UI（既知 MCU の一覧表示） | index.lua からの MCU 名一覧 |
| 4.3 | `extends` モードの対話型設定 | 親ボード選択 |

**開発者体験:**

```bash
$ xmake create-board my-new-board

? MCU name (e.g. stm32f407vg): stm32f411ce
? HSE crystal frequency [Hz]: 25000000
? SYSCLK frequency [Hz]: 100000000
? Debug probe (stlink/cmsis-dap/jlink): cmsis-dap
? Extend existing board? (y/n): n

Created:
  boards/my-new-board/board.lua
  boards/my-new-board/platform.hh
  boards/my-new-board/board.hh

Next steps:
  1. Edit board.hh to add pin definitions
  2. Edit platform.hh to configure Output/Timer
  3. Add to your target: set_values("umiport.board", "my-new-board")
```

---

## 10. 未解決の論点

### 論点 1: board.hh と board.lua のデータ二重管理

現在の設計では、ボード定数が board.lua（Lua 側、ビルドシステム用）と board.hh（C++ 側、コンパイル用）の
両方に記述される。例えば HSE 周波数は board.lua の `clock.hse` と board.hh の `hse_frequency` の両方に
定義され、手動で同期する必要がある。

**選択肢:**

| 案 | 方法 | メリット | デメリット |
|----|------|---------|-----------|
| A | 現状維持（手動同期） | シンプル、ツール依存なし | 不整合リスク |
| B | board.lua → board.hh 生成 | Single Source of Truth 完全 | `add_configfiles()` の制約、IDE 上で生成ファイルの追跡が困難 |
| C | board.hh → board.lua 参照 | C++ が主、Lua はメモリ情報のみ | Lua 側でクロック定数が使えない |
| D | board.lua のみ（C++ からは Platform 型メンバ経由） | 重複ゼロ | board.hh の constexpr 利便性を失う |

**推奨:** 段階的に案 A → 案 B の移行。Phase 1-3 では案 A で進め、安定後に xmake の `add_configfiles()` を使った生成パイプラインを構築する。

### 論点 2: ボード Lua 定義の粒度

board.lua にどこまでの情報を含めるか。現在の提案ではピン配置やクロック設定を含むが、
これらは C++ の board.hh にも存在する。

**選択肢:**

| 案 | board.lua の内容 | 用途 |
|----|-----------------|------|
| A (現提案) | MCU + clock + pins + debug + console + devices | xmake ルールでの自動設定、将来のコード生成 |
| B (最小) | MCU + debug のみ | ビルド構成の最低限。clock/pins は C++ のみ |

**推奨:** 案 A。将来的にピンマッピングからの GPIO 初期化コード生成や、クロック設定からの PLL 初期化コード生成を視野に入れると、board.lua に情報を集約しておく方が拡張性が高い。

### 論点 3: 非 ARM アーキテクチャでの `src_dir` パターン

現在の `src_dir` によるファミリ別ソースディレクトリは、`stm32f4/startup.cc` のように
同一ファミリのボード間で startup を共有する設計。しかし RISC-V や Xtensa では
startup の構造が根本的に異なる。

**選択肢:**

| 案 | 方法 |
|----|------|
| A | `src/<family>/` のままアーキテクチャ別に手書き |
| B | `src/<arch>/<family>/` として arch レベルの共有を明示化 |
| C | startup もテンプレートから生成（modm 方式） |

**推奨:** Phase 1-3 では案 A で進める。非 ARM MCU を追加する時点で再評価。

### 論点 4: platform.hh の自動生成可能性

多くのボードで platform.hh は同じパターン（Output = UART, Timer = DWT）になる。
これをテンプレートから生成できれば、ボード追加時のファイル数を減らせる。

**課題:**
- 外部デバイス統合（CS43L22 等）はボード固有のため、テンプレート化が困難
- C++ concept の static_assert は、テンプレート生成よりも手書きの方が IDE 親和性が高い
- platform.hh はプロジェクトの型システム設計の核心であり、自動生成で隠蔽すべきではない

**推奨:** 自動生成は行わない。`xmake create-board` タスクでテンプレートを**スキャフォールド**として
提供し、開発者が必要に応じて編集する方式を維持する。

### 論点 5: xmake ルール順序問題

XMAKE_RULE_ORDERING.md で分析された通り、`umiport.board` の `on_load` で設定した
`embedded.core` が `embedded` ルールの `on_load` に遡及しない場合がある。

**対処方針:**
- `add_rules("umiport.board", "embedded")` の順序を推奨（umiport.board が先）
- または `embedded.core` をユーザーが明示指定（順序非依存）
- 長期的には xmake にルール間依存宣言を提案

### 論点 6: ボード定義のバージョニング

umiport のボード定義が更新された場合、`extends` で拡張しているプロジェクトローカルボードが
影響を受ける可能性がある。

**対処方針:**
- ボード定義に `schema_version` フィールドを追加（将来）
- 破壊的変更はセマンティックバージョニングのメジャー版で管理
- 現時点では UMI が pre-1.0 のため、この問題は後回しで許容される

---

## 付録: 設計判断の参照元

本提案は以下の調査・設計文書に基づいている。

| 参照元 | 貢献 |
|--------|------|
| PLATFORM_COMPARISON.md | 13 組み込みフレームワークの横断比較。MCU DB の唯一源泉化、memory.ld 生成、ボード拡張の必要性を確立 |
| ARCHITECTURE_FINAL.md | umiport の全体構造、concept ベース設計、umiport-boards 統合判断 |
| HARDWARE_DATA_CONSOLIDATION.md | arm-embedded → umiport へのデータ移行設計、Lua データベース形式の決定 |
| LINKER_DESIGN.md | memory.ld + sections.ld 分離設計、3パターンの合成方式 |
| XMAKE_RULE_ORDERING.md | xmake ルール間通信の制約分析、on_load/on_config 二段構成の必要性 |
| MIGRATION_PLAN.md | 7 Phase の段階的移行計画、並列実行戦略 |
| modm | MCU データベース分離、ボード extends、テンプレートからのコード生成 |
| Rust/Embassy | trait/concept ベースの HAL 設計、memory.x のシンプルさ |
| PlatformIO | ボード追加の簡潔さ (JSON 1 ファイル)、Progressive Disclosure |
| Zephyr HWMv2 | フォーク不要のボード拡張、DTS 継承チェーン |
| ESP-IDF | soc_caps.h の宣言的能力パターン、ldgen のフラグメント合成 |
| libopencm3 | devices.data パターンマッチング、IP バージョン別共有 |
| Mbed OS | `_add`/`_remove` による差分管理（Clean Inheritance）、巨大モノリシック JSON の反面教師 |
