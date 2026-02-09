# ハードウェア記述の統合設計: umiport への集約

**ステータス:** 設計提案
**作成日:** 2026-02-08
**前提文書:** XMAKE_RULE_ORDERING.md, LINKER_DESIGN.md, PLATFORM_COMPARISON.md

---

## 1. 問題提起

### 1.1 現状のデータ分布

ハードウェアに関する情報が、ビルドシステムのパッケージ（arm-embedded）とプロジェクト内ライブラリ（umiport）に分裂している:

```
arm-embedded パッケージ (xmake-repo/synthernet/)
├── database/mcu-database.json        MCU メモリサイズ・アドレス・ベンダー・デバッグ設定
├── database/cortex-m.json            コア → コンパイラフラグ・FPU・target triple
├── database/build-options.json       最適化レベル・C/C++ 標準・LTO
├── database/toolchain-configs.json   ツールチェーンパス・リンカシンボル名
├── linker/common.ld                  汎用リンカスクリプト（--defsym でメモリ注入）
├── plugins/flash/database/flash-targets.json   PyOCD デバイスパック情報
└── rules/vscode/modules/mcu_info.lua VSCode デバッグ用 MCU 情報解決

umiport (lib/umiport/)
├── src/stm32f4/startup.cc            MCU 固有スタートアップ
├── src/stm32f4/syscalls.cc           MCU 固有 newlib スタブ
├── src/stm32f4/linker.ld             MCU 固有リンカスクリプト
├── include/umiport/board/*/          ボード固有 platform.hh, board.hh
├── include/umiport/mcu/stm32f4/     MCU 固有 MMIO レジスタ定義
├── include/umiport/arm/cortex-m/    アーキテクチャ共通 DWT・CoreDebug
└── renode/stm32f4_test.repl          Renode プラットフォーム記述
```

### 1.2 分裂がもたらす問題

| 問題 | 具体例 |
|------|--------|
| **新 MCU 追加時に2箇所を編集** | mcu-database.json（パッケージ内）と umiport/src/（プロジェクト内）の両方を変更し、`xmake dev-sync` が必要 |
| **データの重複** | flash/ram サイズが JSON とリンカスクリプトの両方に存在 |
| **シンボル名の不整合** | common.ld は `_data_start` / `_data_end`、umiport の linker.ld は `_sdata` / `_edata` |
| **逆参照** | mcu-database.json が `"renode_repl": "lib/umiport/renode/..."` とプロジェクトパスを参照 |
| **成長への脆弱性** | JSON の flat 構造では arch → family → MCU → board の階層的オーバーライドを表現できない |

### 1.3 本質的な問い

情報を2つの軸で分類すると:

| 軸 | 内容 | 変化の頻度 | 自然な所在 |
|---|------|-----------|-----------|
| **ハードウェア記述** | MCU メモリ、コア種別、ボード定数、レジスタ、リンカ、startup | MCU/ボード追加のたびに成長 | umiport |
| **ビルドメカニズム** | コンパイラフラグ生成、ツールチェーン検出、リンカスクリプト合成 | 安定、めったに変わらない | arm-embedded パッケージ |

現在の arm-embedded パッケージはこの2つが混在している。`mcu-database.json` はハードウェア記述なのに、ビルドメカニズムと同じパッケージに入っている。

---

## 2. 設計原則

### 2.1 分離の指針

```
arm-embedded パッケージ = ビルドメカニズム + アーキテクチャ仕様（普遍的知識）
umiport                = ハードウェア記述（プロジェクト固有、成長するデータ）
```

**普遍的知識**とは「ARM Cortex-M4F は fpv4-sp-d16 FPU を持つ」のような、ARM アーキテクチャの仕様から導かれる不変の事実。これはどのプロジェクトでも同じ。

**ハードウェア記述**とは「STM32F407VG は 1M Flash + 128K SRAM + 64K CCM を持つ」のような、特定の MCU/ボードの物理特性。これはプロジェクトが使う MCU が増えるたびに追加される。

### 2.2 インタフェース契約

2つのシステム間のインタフェースは **コア名（`cortex-m4f`）** 1つに集約する:

```
umiport が提供: MCU 名 → コア名の対応
arm-embedded が消費: コア名 → コンパイラフラグ
```

これにより arm-embedded パッケージは MCU 固有の情報を一切持つ必要がなくなる。

---

## 3. 情報の分類と移動計画

### 3.1 現在の mcu-database.json の各フィールドの帰属

```json
{
  "stm32f407vg": {
    "core": "cortex-m4f",           → umiport（MCU → コア対応）
    "flash": "1M",                  → umiport（リンカスクリプトに統合）
    "ram": "128K",                  → umiport（リンカスクリプトに統合）
    "flash_origin": "0x08000000",   → umiport（リンカスクリプトに統合）
    "ram_origin": "0x20000000",     → umiport（リンカスクリプトに統合）
    "vendor": "st",                 → umiport（デバッグ・フラッシュ設定）
    "openocd_target": "stm32f4x",   → umiport（デバッグ設定）
    "device_name": "STM32F407VG",   → umiport（表示名）
    "renode_repl": "lib/umiport/renode/stm32f4_test.repl"  → umiport（既にumiport内にある）
  }
}
```

**全フィールドが umiport に帰属する。** arm-embedded パッケージに残すべきものはない。

### 3.2 現在の cortex-m.json の各セクションの帰属

| セクション | 帰属先 | 理由 |
|-----------|--------|------|
| `cores` (cortex-m4f → target triple, FPU) | arm-embedded | ARM アーキテクチャの普遍的仕様 |
| `common.all_cores` (flags) | arm-embedded | ベアメタルビルドの普遍的フラグ |
| `common.fpu_enabled/disabled` | arm-embedded | FPU 有無による普遍的フラグ |
| `toolchain_specific` | arm-embedded | gcc-arm / clang-arm の普遍的設定 |
| `linker_options` | arm-embedded | ツールチェーン固有のリンカフラグ |
| `semihosting` | arm-embedded | ツールチェーン固有のデバッグオプション |

**cortex-m.json は全て arm-embedded に残す。** これはハードウェア記述ではなくアーキテクチャ仕様。

### 3.3 現在の build-options.json / toolchain-configs.json の帰属

| ファイル | 帰属先 | 理由 |
|---------|--------|------|
| build-options.json | arm-embedded | 最適化・言語標準はビルドメカニズム |
| toolchain-configs.json | **削除** | `PACKAGE_PATHS` は Lua コードにインライン化。`MEMORY_SYMBOLS` / `LINKER_SCRIPTS` は umiport 移行により不要 |

`PACKAGE_PATHS` の内容（`base_path`, `lib_prefix`）は embedded ルールの Lua コード内にハードコードする。これらはツールチェーン名 (`gcc-arm`, `clang-arm`) から一意に決まる定数であり、JSON で外部化する理由がない。実際に embedded ルールの複数箇所で既にハードコードされたパス（`packages/g/gcc-arm`, `packages/c/clang-arm`）が使われており、JSON との二重管理が発生している。

### 3.4 現在の flash-targets.json の帰属

```json
{
  "stm32f407vg": {
    "vendor": "STMicroelectronics",    → umiport（mcu-database と重複）
    "part_number": "STM32F407VG",      → umiport（device_name と同等）
    "pack_name": "stm32f4",            → umiport（PyOCD デバイスパック）
    "auto_install_pack": true,         → umiport（フラッシュ設定）
    "pack_install_command": "..."      → umiport（フラッシュ設定）
  }
}
```

**全フィールドが umiport に帰属する。** mcu-database.json と統合すべき。

### 3.5 移動のまとめ

| 移動元 | 移動先 | 内容 |
|--------|--------|------|
| `mcu-database.json` 全体 | umiport | MCU 定義（メモリ・コア・デバッグ・表示名） |
| `flash-targets.json` 全体 | umiport | フラッシュ設定（PyOCD パック・エイリアス） |
| `cortex-m.json` | **残す** | アーキテクチャ仕様 |
| `build-options.json` | **残す** | ビルドオプション |
| `toolchain-configs.json` | **削除** | `PACKAGE_PATHS` は Lua にインライン化。他セクションは不要 |
| `common.ld` | **削除** | umiport が全てのリンカスクリプトを提供 |

---

## 4. umiport のハードウェア記述階層

### 4.1 多様なベンダー・アーキテクチャへの対応

MCU の世界は STM32 だけではない。設計は以下の全てに対応できなければならない:

| ベンダー | ファミリ例 | CPU アーキテクチャ | 命名パターン |
|---------|----------|-------------------|-------------|
| ST | STM32F4, STM32H7, STM32WB | ARM Cortex-M | `stm32` + type + series で一意 |
| NXP | LPC5500, Kinetis, i.MX RT | ARM Cortex-M | ファミリごとに完全に異なる規則 |
| Nordic | nRF52, nRF53, nRF91 | ARM Cortex-M | `nrf` + series 番号でコア確定 |
| Espressif | ESP32, ESP32-S3, ESP32-C3 | Xtensa / **RISC-V** | 接尾文字で Xtensa (S/無印) vs RISC-V (C/H/P) |
| Microchip | SAMD21, SAME70, PIC32MX | ARM / **MIPS** | SAM=ARM, PIC32=MIPS |
| TI | MSP430, TM4C, CC26xx | **独自16bit** / ARM | ファミリプレフィックスで判別 |
| Renesas | RA4, RX65N, RL78 | ARM / **独自RXvN** / **独自16bit** | RA=ARM, RX=独自, RL78=独自 |
| GigaDevice | GD32F303, GD32VF103 | ARM / **RISC-V** | `GD32F`=ARM, `GD32VF`=RISC-V |
| WCH | CH32V307, CH32F103 | **RISC-V** / ARM | `CH32V`=RISC-V, `CH32F`=ARM |
| Raspberry Pi | RP2040, RP2350 | ARM / **RISC-V (選択式)** | RP2350 は両方のコアを搭載 |

**重要な事実:**
- 型番の命名規則はベンダーごとに完全に異なる
- 同一ベンダーでもファミリ間で規則が変わる（NXP の LPC vs Kinetis vs i.MX RT）
- 型番パースによるファミリ推定は汎用的には不可能

### 4.2 階層構造

```
family (MCU ファミリ)
  └─ mcu (MCU バリアント)
       └─ board (ボード)
```

各階層で下位が上位を**部分オーバーライド**する:

| 階層 | 例 | 定義する情報 |
|------|---|------------|
| family | `stm32f4`, `nrf52`, `esp32c3` | コア、メモリアドレス、startup/syscalls、デバッグ・フラッシュのデフォルト |
| mcu | `stm32f407vg`, `nrf52840` | flash/ram サイズ、デバイス名、メモリ領域の上書き |
| board | `stm32f4-renode`, `daisy-seed` | HSE 周波数、ピン設定、デバッグプローブ選択、platform.hh、シミュレータ設定 |

> **vendor 階層は設けない。** PLATFORM_COMPARISON.md の調査で、13 システム全てにおいて vendor は分類用ラベルに過ぎず、継承元としての実体を持つシステムは存在しなかった。ディレクトリのグループ化（`st/`, `nordic/`）は family/mcu のファイル配置で自然に実現される。デバッグプローブはボードレベルで設定する（同一ベンダーでもボードによって異なるため）。

### 4.3 ディレクトリ配置

```
lib/umiport/
├── database/
│   ├── index.lua                      MCU 名 → ファイルパス索引
│   │
│   ├── family/                        ファミリ共通定義
│   │   ├── stm32f1.lua                Cortex-M3, flash/ram origin 共通
│   │   ├── stm32f4.lua                Cortex-M4F, flash/ram/ccm origin 共通
│   │   ├── stm32h7.lua                Cortex-M7, 複数 SRAM 領域
│   │   ├── stm32wb.lua                Cortex-M4F + M0+ デュアルコア
│   │   ├── nrf52.lua                  Cortex-M4F, BLE 5.x
│   │   ├── nrf53.lua                  Dual Cortex-M33
│   │   ├── esp32.lua                  Xtensa LX6, デュアルコア
│   │   ├── esp32c3.lua                RISC-V, シングルコア
│   │   ├── rp2040.lua                 Dual Cortex-M0+
│   │   ├── rp2350.lua                 Dual Cortex-M33 / RISC-V (選択式)
│   │   └── ...
│   │
│   └── mcu/                           MCU バリアント（ファミリ別サブディレクトリ）
│       ├── stm32f4/
│       │   ├── stm32f401re.lua
│       │   ├── stm32f407vg.lua
│       │   └── stm32f446re.lua
│       ├── stm32h7/
│       │   └── stm32h743zi.lua
│       ├── stm32f1/
│       │   └── stm32f103c8.lua
│       ├── nrf52/
│       │   ├── nrf52832.lua
│       │   └── nrf52840.lua
│       ├── esp32c3/
│       │   └── esp32c3.lua
│       └── rp2040/
│           └── rp2040.lua
│
├── src/                               （既存 + 拡張）
│   ├── stm32f4/                       ファミリ単位でグループ
│   │   ├── startup.cc
│   │   ├── syscalls.cc
│   │   └── sections.ld               セクション配置（手書き）
│   ├── nrf52/
│   │   ├── startup.cc
│   │   └── ...
│   └── ...
│
├── include/umiport/                   （既存）
│   ├── board/
│   │   ├── stm32f4-renode/
│   │   │   ├── platform.hh
│   │   │   └── board.hh
│   │   └── stm32f4-disco/
│   │       ├── platform.hh
│   │       └── board.hh
│   ├── mcu/stm32f4/
│   │   └── uart_output.hh
│   └── arm/cortex-m/
│       ├── cortex_m_mmio.hh
│       └── dwt.hh
│
└── renode/                            （既存）
    └── stm32f4_test.repl
```

> **ベンダーサブディレクトリ（`st/`, `nordic/`）は設けない。** ファミリ名自体がベンダーを暗黙的に示している（`stm32f4` = ST, `nrf52` = Nordic）。ベンダーディレクトリを挟むと `database/family/st/stm32f4.lua` のように深くなるが、`database/family/stm32f4.lua` で十分。MCU サブディレクトリもファミリ名で直接グループ化する。

### 4.4 データベースファイルの形式

#### なぜ Lua か

- xmake のビルドルールから `dofile()` で直接読み込める（パーサー不要）
- コメントが書ける
- 条件分岐やデフォルト値の表現が自然
- JSON と異なり、継承・参照が言語機能で可能

#### `database/family/stm32f4.lua` — ファミリ共通

```lua
-- STM32F4 ファミリ共通定義
return {
    core = "cortex-m4f",
    vendor = "st",                      -- 分類ラベル（継承元ではない）
    openocd_target = "stm32f4x",
    flash_pack = "stm32f4",
    flash_tool = "pyocd",
    src_dir = "stm32f4",               -- startup/syscalls/sections.ld の場所

    -- メモリ領域（ファミリ共通のアドレス）
    memory = {
        FLASH = { attr = "rx",  origin = 0x08000000 },
        SRAM  = { attr = "rwx", origin = 0x20000000 },
        CCM   = { attr = "rwx", origin = 0x10000000 },
    },

    -- シミュレータ
    renode_repl = "stm32f4_test.repl",
}
```

> family.lua は `return { ... }` でテーブルを直接返す。vendor.lua からの `dofile()` 継承は不要。ファミリ定義は自己完結する。

#### `database/mcu/stm32f4/stm32f407vg.lua` — MCU バリアント

```lua
-- STM32F407VG: Cortex-M4F, 1M Flash, 128K SRAM, 64K CCM
local family = dofile(path.join(os.scriptdir(), "../../family/stm32f4.lua"))

family.device_name = "STM32F407VG"

-- メモリサイズ（MCU バリアント固有）
family.memory.FLASH.length = "1M"
family.memory.SRAM.length  = "128K"
family.memory.CCM.length   = "64K"

-- PyOCD エイリアス
family.flash_alias = "stm32f407vgtx"

return family
```

#### 非 ARM の例: `database/family/esp32c3.lua`

```lua
-- ESP32-C3: RISC-V シングルコア
return {
    core = "riscv32-imc",              -- RISC-V（ARM ではない）
    vendor = "espressif",
    flash_tool = "esptool",
    src_dir = "esp32c3",

    memory = {
        -- ESP32 は内蔵 SRAM のみ。Flash は外付け SPI
        IRAM = { attr = "rwx", origin = 0x40380000, length = "400K" },
        DRAM = { attr = "rw",  origin = 0x3FC80000, length = "400K" },
    },
}
```

#### 複雑なメモリの例: `database/family/stm32h7.lua`

```lua
-- STM32H7 ファミリ: Cortex-M7, 複数 SRAM 領域
return {
    core = "cortex-m7f",
    vendor = "st",
    openocd_target = "stm32h7x",
    flash_pack = "stm32h7",
    flash_tool = "pyocd",
    src_dir = "stm32h7",

    -- STM32H7 は非常に複雑なメモリマップを持つ
    memory = {
        FLASH    = { attr = "rx",  origin = 0x08000000 },
        DTCM     = { attr = "rwx", origin = 0x20000000, length = "128K" },  -- DMA 不可
        AXI_SRAM = { attr = "rwx", origin = 0x24000000 },                   -- メイン SRAM
        SRAM1    = { attr = "rwx", origin = 0x30000000 },
        SRAM2    = { attr = "rwx", origin = 0x30020000 },
        SRAM4    = { attr = "rwx", origin = 0x38000000 },
        ITCM     = { attr = "rwx", origin = 0x00000000, length = "64K" },   -- 命令 TCM
        BACKUP   = { attr = "rwx", origin = 0x38800000, length = "4K" },
    },

    renode_repl = "stm32h7_test.repl",
}
```

#### デュアルアーキテクチャの例: `database/family/rp2350.lua`

```lua
-- RP2350: Dual Cortex-M33 OR Dual RISC-V Hazard3（ソフトウェア/OTP で選択）
return {
    -- デフォルトは ARM モード。RISC-V モードはボード定義で上書き可能
    core = "cortex-m33f",
    alt_core = "riscv32-hazard3",      -- 代替コア
    vendor = "raspberrypi",
    flash_tool = "picotool",
    src_dir = "rp2350",

    memory = {
        FLASH = { attr = "rx",  origin = 0x10000000, length = "16M" },  -- 外付け QSPI
        SRAM  = { attr = "rwx", origin = 0x20000000, length = "520K" },
    },
}
```

### 4.5 MCU ファイルのルックアップ

型番の命名規則がベンダーごとに異なるため、パターンマッチによるファイルパス推定は汎用的には不可能。代わりに **MCU 名 → ファイルパスの索引** を用意する:

```lua
-- database/index.lua — MCU 名からファイルパスへのマッピング
-- 新 MCU 追加時にここにも1行追加する
return {
    -- ST
    stm32f103c8  = "stm32f1/stm32f103c8",
    stm32f103rc  = "stm32f1/stm32f103rc",
    stm32f401re  = "stm32f4/stm32f401re",
    stm32f405rg  = "stm32f4/stm32f405rg",
    stm32f407vg  = "stm32f4/stm32f407vg",
    stm32f411ve  = "stm32f4/stm32f411ve",
    stm32f446re  = "stm32f4/stm32f446re",
    stm32f746zg  = "stm32f7/stm32f746zg",
    stm32h533re  = "stm32h5/stm32h533re",
    stm32h743zi  = "stm32h7/stm32h743zi",

    -- Nordic
    nrf52832     = "nrf52/nrf52832",
    nrf52840     = "nrf52/nrf52840",

    -- Espressif
    esp32c3      = "esp32c3/esp32c3",

    -- Raspberry Pi
    rp2040       = "rp2040/rp2040",
    rp2350       = "rp2350/rp2350",
}
```

ルックアップ:

```lua
local index = dofile(path.join(umiport_dir, "database", "index.lua"))
local mcu_path = index[mcu_name:lower()]
if not mcu_path then
    raise("umiport: Unknown MCU '%s'. Add it to database/index.lua", mcu_name)
end
local mcu_config = dofile(path.join(umiport_dir, "database", "mcu", mcu_path .. ".lua"))
```

この設計の利点:
- **ベンダー固有のパターンマッチが不要** — `stm32` の正規表現も `nrf` のパターンも不要
- **全 MCU が1箇所で一覧できる** — `index.lua` がカタログとして機能
- **ファイルシステムの走査が不要** — `os.dirs()` + glob を使わない

### 4.6 オーバーライドの仕組み

Lua の `dofile()` + テーブル上書きにより、2段階の継承チェーンが実現される:

```
family/stm32f4.lua
  core = "cortex-m4f"
  vendor = "st"                      ← 分類ラベル
  memory.FLASH.origin = 0x08000000
  memory.SRAM.origin = 0x20000000
          ↓ dofile で読み込み
mcu/stm32f4/stm32f407vg.lua
  device_name = "STM32F407VG"        ← MCU 固有値を追加
  memory.FLASH.length = "1M"         ← MCU 固有値を追加
  memory.SRAM.length = "128K"        ← MCU 固有値を追加
```

新しい MCU を追加する際:
1. `database/mcu/<family>/<name>.lua` を1ファイル作成
2. `database/index.lua` に1行追加

ファミリが既に存在すれば family lua を書く必要すらない。新ファミリの場合は `family/<family>.lua` も追加する。

---

## 5. arm-embedded パッケージの変更

### 5.1 embedded ルールの変更: `embedded.mcu` → `embedded.core`

現在の embedded ルールは `embedded.mcu` から MCU 名を取得し、mcu-database.json を引いてコア名を得ている。これを **`embedded.core` を直接受け取る** 設計に変更する。

**変更前:**
```lua
-- embedded ルール (on_load)
local mcu = target:values("embedded.mcu")           -- "stm32f407vg"
local mcu_config = mcu_db.get_config(mcu)           -- JSON lookup
local core_config = get_core_config(mcu_config.core) -- "cortex-m4f"
-- → コンパイラフラグ設定
```

**変更後:**
```lua
-- embedded ルール (on_load)
local core = target:values("embedded.core")          -- "cortex-m4f"（直接指定）
if not core then
    raise("embedded: 'embedded.core' is required. Set it via set_values() or use umiport.board rule.")
end
local core_config = get_core_config(core)
-- → コンパイラフラグ設定
```

### 5.2 embedded ルールから除去される責務

| 責務 | 現在 | 変更後 |
|------|------|--------|
| MCU → コア名解決 | embedded が mcu-database.json で解決 | umiport が解決し `embedded.core` を設定 |
| メモリサイズ → `--defsym` | embedded が注入 | umiport が linker.ld を直接提供（--defsym 不要） |
| メモリ使用量の表示 | embedded が mcu_config.flash/ram を参照 | umiport がデータを `target:data()` に格納 |
| Renode .repl パス | embedded が mcu_config.renode_repl を参照 | umiport がデータを `target:data()` に格納 |
| VSCode デバッグ設定 | vscode ルールが mcu-database.json を直接読む | umiport がデータを `target:data()` に格納 |
| フラッシュ設定 | flash プラグインが flash-targets.json を読む | umiport がデータを `target:data()` に格納 |

### 5.3 arm-embedded パッケージに残るもの

```
arm-embedded パッケージ（変更後）
├── database/
│   ├── cortex-m.json            ← 変更なし: アーキテクチャ仕様
│   └── build-options.json       ← 変更なし: ビルドオプション
├── rules/
│   └── embedded/xmake.lua      ← embedded.core を受け取る設計に変更
│                                   PACKAGE_PATHS は Lua コード内にインライン化
├── plugins/
│   └── flash/xmake.lua         ← umiport からデータ取得に変更
└── rules/
    └── vscode/                  ← umiport からデータ取得に変更

削除:
├── database/mcu-database.json          ← umiport の Lua データベースに移行
├── database/toolchain-configs.json     ← PACKAGE_PATHS は Lua にインライン化、他は不要
├── linker/common.ld                    ← umiport が memory.ld を生成
└── plugins/flash/database/flash-targets.json ← umiport の Lua データベースに統合
```

### 5.4 toolchain-configs.json の完全削除

ファイル全体を削除する。各セクションの移行先:

| セクション | 対処 |
|-----------|------|
| `MEMORY_SYMBOLS` | 削除（`--defsym` 方式の廃止。umiport が memory.ld を生成する） |
| `LINKER_SCRIPTS` | 削除（`common.ld` の廃止。リンカスクリプトは全て umiport が管理する） |
| `PACKAGE_PATHS` | embedded ルールの Lua コード内にインライン化 |

`PACKAGE_PATHS` のインライン化:

```lua
-- embedded ルール内（toolchain-configs.json の代替）
local TOOLCHAIN_PATHS = {
    ["gcc-arm"]   = { base = "packages/g/gcc-arm",   lib_prefix = "arm-none-eabi/lib/" },
    ["clang-arm"] = { base = "packages/c/clang-arm",  lib_prefix = "lib/clang-runtimes/arm-none-eabi/" },
}
```

これらはツールチェーン名から一意に決まる定数であり、JSON で外部化する意味がない。

---

## 6. umiport.board ルールの改修

### 6.1 新しい責務

umiport.board ルールは以下の全てを担う:

1. MCU データベースの読み込みと解決
2. `embedded.core` の設定（embedded ルールへの入力）
3. ボード固有 includedirs の設定
4. startup.cc / syscalls.cc の追加
5. リンカスクリプトの設定
6. デバッグ・フラッシュ・Renode データの `target:data()` への格納

### 6.2 実装

```lua
rule("umiport.board")
    on_load(function(target)
        local board = target:values("umiport.board")
        if not board then return end

        local umiport_dir = path.join(os.scriptdir(), "..")
        local board_include = path.join(umiport_dir, "include/umiport/board", board)

        -- ボード固有 includedirs
        target:add("includedirs", board_include, {public = false})

        -- MCU データベースの解決（index.lua による統一ルックアップ）
        local mcu_name = target:values("embedded.mcu")
        if not mcu_name then return end
        if type(mcu_name) == "table" then mcu_name = mcu_name[1] end

        local db_dir = path.join(umiport_dir, "database")
        local index = dofile(path.join(db_dir, "index.lua"))
        local mcu_path = index[mcu_name:lower()]
        if not mcu_path then
            raise("umiport: Unknown MCU '%s'. Add it to database/index.lua", mcu_name)
        end
        local mcu_config = dofile(path.join(db_dir, "mcu", mcu_path .. ".lua"))

        -- embedded.core を設定（embedded ルールの on_load で参照される）
        -- ※ embedded ルールより左に add_rules されている場合に有効
        -- ※ 順序非依存にするため set_values を Phase 0 で推奨
        target:set("values", "embedded.core", mcu_config.core)

        -- MCU データを target:data() に格納
        -- （after_link, on_run, vscode, flash から参照される）
        target:data_set("umiport.mcu_config", mcu_config)
        target:data_set("umiport.mcu_name", mcu_name)
    end)

    on_config(function(target)
        local board = target:values("umiport.board")
        if not board then return end

        local umiport_dir = path.join(os.scriptdir(), "..")
        local mcu_config = target:data("umiport.mcu_config")
        if not mcu_config then return end

        -- startup 無効化オプション
        local use_startup = target:values("umiport.startup")
        if use_startup == "false" then return end

        -- MCU ファミリからソースディレクトリを決定
        local src_dir = mcu_config.src_dir
        if src_dir then
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

            -- sections.ld（INCLUDE memory.ld を含む）を適用
            local sections_ld = path.join(mcu_src, "sections.ld")
            target:add("ldflags", "-T" .. sections_ld, {force = true})
            target:add("ldflags", "-L" .. path.directory(memory_ld_path), {force = true})
        end
    end)
```

### 6.3 xmake ルール順序問題の対処

`on_load` で `target:set("values", "embedded.core", ...)` を行うが、これは embedded ルールの `on_load` との実行順序に依存する。

**推奨される使用パターン:**

```lua
target("umirtm_stm32f4_renode")
    add_rules("umiport.board", "embedded")   -- umiport.board を先に宣言
    set_values("embedded.mcu", "stm32f407vg")
    set_values("umiport.board", "stm32f4-renode")
```

または、順序非依存にするために:

```lua
target("umirtm_stm32f4_renode")
    add_rules("embedded", "umiport.board")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("embedded.core", "cortex-m4f")   -- Phase 0 で明示指定
    set_values("umiport.board", "stm32f4-renode")
```

後者は `embedded.core` を明示指定するため、ルール順序に依存しない。冗長に見えるが、MCU 名とコア名の対応が宣言的に明示されるため可読性は高い。

**理想的な長期解決:** XMAKE_RULE_ORDERING.md で分析した通り、xmake にルール間依存宣言が実装されれば、`add_rules` の順序に依存せずに済む。

---

## 7. 消費者側の変更

### 7.1 VSCode ルール

現在の mcu_info.lua は `mcu-database.json` を直接読んでいる:

```lua
-- 現在
local mcu_db_path = path.join(os.scriptdir(), "..", "embedded", "database", "mcu-database.json")
info = mcu_info.resolve(mcu_db_path, mcu_name, overrides)
```

変更後は `target:data("umiport.mcu_config")` からデータを取得する:

```lua
-- 変更後
local mcu_config = target:data("umiport.mcu_config")
if not mcu_config then
    raise("vscode: umiport.mcu_config not found. Ensure umiport.board rule is applied.")
end
info = {
    device_name     = mcu_config.device_name,
    openocd_target  = mcu_config.openocd_target,
    vendor          = mcu_config.vendor,
    ram             = mcu_config.memory.SRAM and mcu_config.memory.SRAM.length,
    ram_origin      = mcu_config.memory.SRAM and string.format("0x%08X", mcu_config.memory.SRAM.origin),
    flash_origin    = mcu_config.memory.FLASH and string.format("0x%08X", mcu_config.memory.FLASH.origin),
    renode_repl     = mcu_config.renode_repl,
}
```

`umiport.board` ルールは **必須** である。mcu-database.json は削除されるため、umiport.board を使わずに MCU 情報を取得する手段は存在しない。

### 7.2 Flash プラグイン

現在は flash-targets.json を直接読んでいる。変更後:

```lua
-- 変更後
local mcu_config = target:data("umiport.mcu_config")
if mcu_config then
    local device = mcu_config.flash_alias or mcu_name
    local pack_name = mcu_config.flash_pack
    local auto_install = mcu_config.flash_auto_install
    -- PyOCD 操作
end
```

### 7.3 embedded ルールの after_link（メモリ使用量表示）

現在は mcu_config.flash/ram を参照している。変更後:

```lua
-- 変更後 (after_link)
local mcu_config = target:data("umiport.mcu_config")
if mcu_config and mcu_config.memory then
    local flash_size = mcu_config.memory.FLASH and size_to_bytes(mcu_config.memory.FLASH.length)
    local ram_size = mcu_config.memory.SRAM and size_to_bytes(mcu_config.memory.SRAM.length)
    -- メモリ使用量を計算・表示
end
```

### 7.4 embedded ルールの on_run（Renode 実行）

現在は mcu_data からの Renode パスを参照。変更後:

```lua
-- 変更後 (on_run)
local mcu_config = target:data("umiport.mcu_config")
if mcu_config and mcu_config.renode_repl then
    local repl_path = path.join(umiport_dir, "renode", mcu_config.renode_repl)
    -- Renode スクリプト生成・実行
end
```

---

## 8. リンカスクリプト: memory.ld 生成一本化

### 8.1 設計方針: Lua データベースが唯一の源泉

PLATFORM_COMPARISON.md の調査結果から、成熟した全てのシステム（modm, libopencm3, Zephyr, ESP-IDF）がメモリ定義の唯一の源泉を持ち、リンカスクリプトを**生成**している。手書き memory.ld とデータベースの二重管理は回避すべき問題。

**Lua データベースの `memory` テーブルが唯一の源泉であり、`memory.ld` はビルド時に自動生成する。**

### 8.2 リンカスクリプトの構成

```
リンカスクリプト = memory.ld（生成） + sections.ld（手書き）
```

| ファイル | 内容 | 管理方法 |
|---------|------|---------|
| `memory.ld` | MEMORY { ... } + ENTRY(_start) | **Lua DB から自動生成**（ビルド時） |
| `sections.ld` | SECTIONS { ... } | **ファミリごとに手書き**（`src/<family>/sections.ld`） |

この分離は Rust Embedded の `memory.x` + `link.x` パターンに相当する。メモリ領域定義（MCU バリアント固有）とセクション配置（ファミリ共通）を明確に分離する。

### 8.3 memory.ld 生成の実装

```lua
-- umiport/rules/memory_ld.lua
local function generate_memory_ld(mcu_config, output_path)
    local lines = {"/* Auto-generated from umiport MCU database — DO NOT EDIT */", "MEMORY {"}

    -- 領域名の出力順序を安定させる（アルファベット順）
    local names = {}
    for name, region in pairs(mcu_config.memory) do
        if region.length then  -- length が定義されている領域のみ出力
            table.insert(names, name)
        end
    end
    table.sort(names)

    for _, name in ipairs(names) do
        local region = mcu_config.memory[name]
        lines[#lines+1] = string.format(
            "    %-12s (%s) : ORIGIN = 0x%08X, LENGTH = %s",
            name, region.attr, region.origin, region.length)
    end

    lines[#lines+1] = "}"
    lines[#lines+1] = ""
    lines[#lines+1] = "ENTRY(_start)"
    lines[#lines+1] = ""
    io.writefile(output_path, table.concat(lines, "\n"))
end
```

生成される `memory.ld` の例（STM32F407VG）:

```ld
/* Auto-generated from umiport MCU database — DO NOT EDIT */
MEMORY {
    CCM          (rwx) : ORIGIN = 0x10000000, LENGTH = 64K
    FLASH        (rx)  : ORIGIN = 0x08000000, LENGTH = 1M
    SRAM         (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

ENTRY(_start)
```

生成される `memory.ld` の例（STM32H743ZI — 複雑なメモリマップ）:

```ld
/* Auto-generated from umiport MCU database — DO NOT EDIT */
MEMORY {
    AXI_SRAM     (rwx) : ORIGIN = 0x24000000, LENGTH = 512K
    BACKUP       (rwx) : ORIGIN = 0x38800000, LENGTH = 4K
    DTCM         (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
    FLASH        (rx)  : ORIGIN = 0x08000000, LENGTH = 2M
    ITCM         (rwx) : ORIGIN = 0x00000000, LENGTH = 64K
    SRAM1        (rwx) : ORIGIN = 0x30000000, LENGTH = 128K
    SRAM2        (rwx) : ORIGIN = 0x30020000, LENGTH = 128K
    SRAM4        (rwx) : ORIGIN = 0x38000000, LENGTH = 64K
}

ENTRY(_start)
```

この生成関数は memory テーブルを走査するため、STM32 の CCM も STM32H7 の複数 SRAM も ESP32 の IRAM/DRAM も同じコードで処理できる。

### 8.4 sections.ld の管理

sections.ld はファミリごとに手書きで管理する。これはセクション配置がファミリのメモリアーキテクチャに依存するためである（例: STM32F4 は CCM にスタックを配置、STM32H7 は DTCM にスタック + AXI_SRAM にヒープ）。

```
src/
├── stm32f4/
│   ├── sections.ld        ← STM32F4 ファミリ共通セクション配置
│   ├── startup.cc
│   └── syscalls.cc
├── stm32h7/
│   ├── sections.ld        ← STM32H7 ファミリ共通セクション配置（複数 SRAM 対応）
│   ...
```

sections.ld は `INCLUDE memory.ld` で生成された memory.ld を取り込む:

```ld
/* src/stm32f4/sections.ld — STM32F4 ファミリ共通 */
INCLUDE memory.ld

SECTIONS {
    .text : {
        KEEP(*(.isr_vector))
        *(.text*)
        ...
    } > FLASH

    .data : AT(__sidata) {
        ...
    } > SRAM

    .bss : {
        ...
    } > SRAM

    ._stack (NOLOAD) : {
        ...
    } > CCM
}
```

### 8.5 LINKER_DESIGN.md との関係

LINKER_DESIGN.md のパターンを memory.ld 生成方式に対応付ける:

| パターン | 実現方法 |
|---------|---------|
| **A: 全自動** | umiport が memory.ld を生成 + ファミリ共通 sections.ld |
| **B: セクション追加** | A + アプリの extra_sections.ld（sections.ld 内で `INCLUDE extra_sections.ld`） |
| **C: 完全カスタム** | アプリが独自リンカスクリプトを所有。umiport.board で `umiport.linker = "false"` を指定 |

### 8.6 生成のタイミング

memory.ld は umiport.board ルールの `on_config` フェーズで生成する。出力先はビルドディレクトリ内:

```lua
-- umiport.board ルール (on_config)
local memory_ld_path = path.join(target:autogendir(), "memory.ld")
generate_memory_ld(mcu_config, memory_ld_path)

local sections_ld_path = path.join(umiport_dir, "src", mcu_config.src_dir, "sections.ld")
target:add("ldflags", "-T" .. sections_ld_path, {force = true})
target:add("ldflags", "-L" .. path.directory(memory_ld_path), {force = true})
```

---

## 9. 移行戦略

### 9.1 一括移行

フォールバックや段階的互換性は設けない。全てを一度に切り替える。

| 段階 | 作業 | 影響範囲 |
|------|------|---------|
| **Phase 1** | umiport に `database/` ディレクトリを作成し、Lua 形式で MCU データを定義。`index.lua` を作成。`memory_ld.lua`（生成関数）を作成 | umiport のみ |
| **Phase 2** | 既存の `linker.ld` を `sections.ld`（INCLUDE memory.ld 方式）に変換 | umiport のみ |
| **Phase 3** | umiport.board ルールを on_load + on_config の二段構成に改修。MCU データベースを読み込み、`embedded.core` を設定し、memory.ld を生成し、`target:data()` に格納 | umiport のみ |
| **Phase 4** | embedded ルールを `embedded.core` **のみ** を受け取る設計に変更。`embedded.mcu` による JSON ルックアップを完全に削除。`mcu-database.json` を削除 | arm-embedded パッケージ |
| **Phase 5** | VSCode ルールと flash プラグインを `target:data("umiport.mcu_config")` **のみ** からの読み取りに変更。`flash-targets.json` を削除 | arm-embedded パッケージ |
| **Phase 6** | `common.ld` と `toolchain-configs.json` を完全削除。`PACKAGE_PATHS` は embedded ルールの Lua コード内にインライン化 | arm-embedded パッケージ |
| **Phase 7** | 全ターゲットのビルド・テスト・フラッシュ・Renode シミュレーションを確認 | 全体 |

### 9.2 設計方針: umiport は必須依存

arm-embedded パッケージは **umiport と組み合わせて使うことを前提とする**。スタンドアロン動作は設計目標としない。

理由:
- ハードウェア記述（MCU メモリ、デバッグ設定、フラッシュ設定）なしで組み込みビルドを行うことに実用的な意味がない
- フォールバックパスの存在は二重の実装を生み、バグの温床になる
- 「umiport なしでも動く」という要件は、結局 mcu-database.json を維持する理由になり、データの分裂を解消できない

arm-embedded パッケージの責務は **アーキテクチャ仕様に基づくビルドメカニズム** に限定される:
- `embedded.core` からコンパイラフラグを導出（cortex-m.json）
- ツールチェーン検出と設定（Lua コード内のインライン定数）
- ビルドオプション（build-options.json）
- ELF → HEX/BIN 変換、サイズ表示

ハードウェア固有の情報は **全て umiport が提供する**。

---

## 10. 新 MCU 追加の手順比較

### 10.1 現在: 新 MCU（例: STM32L4R5ZI）を追加する場合

```
1. xmake-repo/synthernet/.../mcu-database.json に MCU エントリ追加
2. xmake-repo/synthernet/.../flash-targets.json にフラッシュエントリ追加
3. xmake dev-sync で ~/.xmake/ にコピー
4. lib/umiport/src/stm32l4/ に startup.cc, syscalls.cc, linker.ld を作成
5. lib/umiport/include/umiport/board/stm32l4-<board>/ に platform.hh, board.hh を作成
6. lib/umiport/renode/ に .repl を追加（シミュレーション対応時）
```

3箇所に分散（パッケージ JSON × 2 + プロジェクトソース）。`dev-sync` が必要。

### 10.2 統合後: STM32L4R5ZI を追加する場合

```
1. lib/umiport/database/family/stm32l4.lua               — 作成（なければ）
2. lib/umiport/database/mcu/stm32l4/stm32l4r5zi.lua      — 作成（family を dofile で継承）
3. lib/umiport/database/index.lua                        — 1行追加: stm32l4r5zi = "stm32l4/stm32l4r5zi"
4. lib/umiport/src/stm32l4/                              — startup.cc, syscalls.cc, sections.ld を作成
5. lib/umiport/include/umiport/board/<board>/            — platform.hh, board.hh を作成
6. lib/umiport/renode/ に .repl を追加（シミュレーション対応時）
```

memory.ld は Lua データベースから自動生成されるため、手書き不要。

### 10.3 統合後: 新ベンダー（Nordic nRF52840）を追加する場合

```
1. lib/umiport/database/family/nrf52.lua                 — 作成
2. lib/umiport/database/mcu/nrf52/nrf52840.lua           — 作成
3. lib/umiport/database/index.lua                        — 1行追加: nrf52840 = "nrf52/nrf52840"
4. lib/umiport/src/nrf52/                                — startup.cc, syscalls.cc, sections.ld を作成
5. lib/umiport/include/umiport/board/<board>/            — platform.hh, board.hh を作成
```

**全て umiport 内**。`dev-sync` 不要。ファミリの共通情報は family lua に1回書くだけで、全バリアントが共有する。memory.ld はビルド時に自動生成される。

---

## 11. 関連文書との整合性

| 文書 | 影響 |
|------|------|
| PLATFORM_COMPARISON.md | 本文書の設計判断の根拠。13 システムの調査結果に基づく |
| LINKER_DESIGN.md | memory.ld は手書きから生成に変更。sections.ld（`src/<family>/sections.ld`）が手書き部分を担う |
| XMAKE_RULE_ORDERING.md | `embedded.core` の導入により、on_load 時点で必要な情報が減る。ただし ldflags フィルタ問題は依然として存在 |
| ARCHITECTURE_FINAL.md Section 9.1 | umiport.board ルールの責務が拡大。MCU データベース解決 + memory.ld 生成が追加される |
