# 組み込みプラットフォームのハードウェア抽象化 比較分析

**ステータス:** 調査完了
**作成日:** 2026-02-08
**目的:** umiport の理想的な階層設計のために、既存の主要プラットフォーム・ビルドシステムのハードウェア記述方式を徹底調査・比較する

---

## 調査対象一覧

| # | システム | 分類 | 主な特徴 |
|---|---------|------|---------|
| 1 | Zephyr RTOS | RTOS | DeviceTree + Kconfig + CMake の三重構成 |
| 2 | ESP-IDF | SDK | soc_caps.h + ldgen フラグメント + コンポーネントモデル |
| 3 | PlatformIO | IDE/ビルドツール | board.json + 動的パッケージ選択 |
| 4 | CMSIS-Pack | 業界標準 | PDSC XML + DFP + フラッシュアルゴリズム |
| 5 | Mbed OS | RTOS (EOL) | targets.json + inherits チェーン |
| 6 | Arduino | フレームワーク | boards.txt + platform.txt + variants |
| 7 | Rust Embedded | エコシステム | PAC/HAL/BSP 層分離 + memory.x |
| 8 | NuttX RTOS | RTOS | arch/chip/board 3層ディレクトリ + Kconfig |
| 9 | modm | コード生成 | modm-devices DB + lbuild + Jinja2 テンプレート |
| 10 | libopencm3 | ライブラリ | devices.data + genlink.py + テンプレートリンカ |
| 11 | Yocto/OpenEmbedded | ビルドシステム | MACHINE + BSP レイヤー合成 |
| 12 | Bazel/Buck2 | ビルドシステム | constraint + select() + ツールチェーン解決 |
| 13 | Meson | ビルドシステム | クロスファイル + host_machine |

---

## 1. Zephyr RTOS

### 階層構造

```
zephyr/
├── arch/arm/core/cortex_m/        アーキテクチャ固有（ISA実装、コンテキストスイッチ）
├── soc/st/stm32/                  SoC 定義（ベンダー別、soc.yml で階層管理）
│   ├── soc.yml                    ファミリ/シリーズ/SoC の YAML メタデータ
│   ├── stm32f4x/                  シリーズ固有
│   └── common/                    STM32 共通
├── boards/st/nucleo_f411re/       ボード定義（ベンダー別、HWMv2）
│   ├── board.yml                  メタデータ
│   ├── nucleo_f411re.dts          DeviceTree
│   ├── Kconfig.nucleo_f411re      SoC 選択
│   ├── nucleo_f411re_defconfig    デフォルト設定
│   └── board.cmake                デバッグプローブ設定
└── dts/arm/st/f4/                 DeviceTree ソース（6段 include チェーン）
    ├── stm32f4.dtsi               ファミリ基底
    ├── stm32f411.dtsi             SoC 固有
    └── stm32f411Xe.dtsi           パッケージ/メモリ固有
```

### メモリ定義

DeviceTree の `<memory>` ノードでアドレス・サイズを定義。`chosen` ノードでリンカに使用するメモリを指定:

```dts
sram0: memory@20000000 {
    reg = <0x20000000 DT_SIZE_K(128)>;
};
/ { chosen { zephyr,sram = &sram0; }; };
```

### リンカスクリプト

**テンプレートベース自動生成**。DeviceTree → `gen_defines.py` → C マクロ → C プリプロセッサでテンプレートを展開 → `linker.cmd` 生成。手書き不要。

### デバッグプローブ

`board.cmake` で複数プローブを並列定義。`boards/common/` に共通ランナー (openocd, jlink, pyocd 等)。

### 評価

| 軸 | 評価 | 理由 |
|----|------|------|
| 真実の単一源泉 | ○ | DeviceTree が唯一のハードウェア記述 |
| 継承メカニズム | ◎ | DTS include チェーンで段階的詳細化 |
| リンカスクリプト | ◎ | テンプレートから自動生成 |
| 選択的包含 | ○ | Out-of-Tree ボード対応 (BOARD_ROOT, Module) |
| 学習コスト | △ | DeviceTree + Kconfig + CMake の三重設定 |
| ファイル数 | △ | 1ボード最低5ファイル |

---

## 2. ESP-IDF

### 階層構造

```
components/
├── soc/<chip>/include/soc/        SoC 定義層
│   ├── soc_caps.h                 ペリフェラル能力マクロ（唯一の源泉）
│   ├── Kconfig.soc_caps.in        soc_caps.h から自動生成
│   ├── xxx_reg.h                  レジスタ定義
│   └── xxx_struct.h               レジスタ構造体
├── hal/<chip>/include/hal/        HAL/LL 層（static inline 強制）
├── esp_hw_support/port/<chip>/    ハードウェアサポート層
└── esp_system/ld/<chip>/          リンカスクリプトテンプレート
    ├── memory.ld.in               メモリ領域定義
    └── sections.ld.in             セクション配置
```

### メモリ定義

`components/esp_system/ld/<chip>/memory.ld.in` にチップ固有のメモリ領域を定義。

### リンカスクリプト

**ldgen ツール + フラグメント (.lf) 方式**。各コンポーネントが `.lf` ファイルでメモリ配置を宣言。ldgen が全フラグメントを統合し、`sections.ld.in` のマーカーを置換して最終スクリプトを生成。

```
[mapping:wifi]
archive: libwifi.a
entries:
    * (noflash)     ← WiFi ライブラリ全体を IRAM に配置
```

### soc_caps.h → Kconfig 自動生成

`gen_soc_caps_kconfig.py` が `soc_caps.h` の `#define` マクロを解析し、`Kconfig.soc_caps.in` を自動生成。pre-commit フックで同期。

### 評価

| 軸 | 評価 | 理由 |
|----|------|------|
| 真実の単一源泉 | ◎ | soc_caps.h が唯一の源泉、Kconfig は自動生成 |
| 継承メカニズム | △ | ファイルパス (`${target}/`) による暗黙的切り替え |
| リンカスクリプト | ◎ | ldgen + フラグメントで分散定義・自動統合 |
| 選択的包含 | △ | IDF_TARGET 全体の切り替えのみ |
| 学習コスト | △ | Kconfig + CMake + soc_caps.h + ldgen の四重構成 |
| 独自ツール依存 | △ | ldgen が独自 Python ツール |

---

## 3. PlatformIO

### 階層構造

```
platforms/ststm32/
├── platform.json                  プラットフォームマニフェスト
├── platform.py                    動的パッケージ選択ロジック（Python）
├── boards/                        ボード定義群（291ファイル）
│   └── nucleo_f446re.json         ボードマニフェスト
└── builder/frameworks/            フレームワーク別ビルダー
    ├── arduino.py
    └── stm32cube.py
```

### board.json の構造

```json
{
  "build": {
    "cpu": "cortex-m4",
    "mcu": "stm32f446ret6",
    "extra_flags": "-DSTM32F4 -DSTM32F446xx"
  },
  "debug": {
    "default_tools": ["stlink"],
    "openocd_target": "stm32f4x",
    "svd_path": "STM32F446x.svd"
  },
  "upload": {
    "maximum_ram_size": 131072,
    "maximum_size": 524288
  }
}
```

### リンカスクリプト

**テンプレートから自動生成**。`upload.maximum_ram_size` / `maximum_size` → `linker.tpl` に変数置換。ただし FLASH + RAM の 2 リージョンのみ対応。CCM-RAM 等は非対応。

### 評価

| 軸 | 評価 | 理由 |
|----|------|------|
| 真実の単一源泉 | △ | board.json にデータ集約だが、継承なし → 大量重複 |
| 継承メカニズム | ✕ | **board.json に継承なし**。同一 MCU のボード間でコピペ |
| リンカスクリプト | △ | テンプレート生成可だが 2 リージョン限定 |
| 選択的包含 | ○ | パッケージの optional フラグ + 動的選択 |
| カスタムボード追加 | ◎ | JSON 1ファイルのみ |
| デバッグ統合 | ◎ | 300+ ボードで即座にデバッグ可能 |

---

## 4. CMSIS-Pack (ARM 業界標準)

### 階層構造

PDSC XML で 4 階層を定義:

```
family (Dfamily, Dvendor)
  └── subFamily (DsubFamily)
      └── device (Dname)
          └── variant (Dvariant)
```

### メモリ定義

```xml
<memory name="Flash" access="rx" start="0x08000000"
        size="0x80000" startup="1" default="1"/>
<memory name="SRAM"  access="rwx" start="0x20000000"
        size="0x20000" default="1"/>
```

属性 `access` で `r`, `w`, `x`, `s` (セキュア), `n` (ノンセキュア) を指定可能。

### 評価

| 軸 | 評価 | 理由 |
|----|------|------|
| 真実の単一源泉 | ◎ | ベンダー公式 DFP がデバイスの正規定義 |
| 継承メカニズム | ◎ | 4 階層の属性累積、下位で上書き可能 |
| リンカスクリプト | ○ | Pack 内にツールチェーン別で提供 |
| 選択的包含 | ○ | Pack 単位でインストール |
| 人間可読性 | △ | XML が冗長 |
| ツール統合 | ◎ | Keil, IAR, pyOCD, probe-rs 等が対応 |

---

## 5. Mbed OS

### 継承メカニズム（最も体系的な JSON 継承）

```json
"MCU_STM32F4": {
    "inherits": ["MCU_STM32"],
    "device_has_add": ["SERIAL_ASYNCH", "FLASH", "MPU"]
},
"NUCLEO_F411RE": {
    "inherits": ["MCU_STM32F411xE"],
    "detect_code": ["0740"]
}
```

`_add` / `_remove` 構文でプロパティの差分管理。Linter で継承パターンを自動検証。

### メモリ定義

targets.json 内に直接定義:
```json
"mbed_rom_start": "0x08000000",
"mbed_rom_size": "0x80000",
"mbed_ram_start": "0x20000000",
"mbed_ram_size": "0x20000"
```

### 評価

| 軸 | 評価 | 理由 |
|----|------|------|
| 継承メカニズム | ◎ | `inherits` + `_add`/`_remove` が直感的 |
| 真実の単一源泉 | ○ | targets.json 1ファイルだが巨大 |
| メモリ定義 | △ | JSON 内の値とリンカのプリプロセッサマクロの二重管理 |
| 保守性 | ✕ | 巨大モノリシック JSON。2026年7月 EOL |

---

## 6. Arduino

### 階層構造

```properties
# boards.txt — フラットなキーバリュー
uno.build.mcu=atmega328p
uno.upload.maximum_size=32256
# メニューによるバリアント選択
mega.menu.cpu.atmega2560.build.mcu=atmega2560
```

```properties
# platform.txt — ビルドレシピテンプレート
recipe.c.o.pattern="{compiler.path}{compiler.c.cmd}" -mmcu={build.mcu} ...
```

### 評価

| 軸 | 評価 | 理由 |
|----|------|------|
| シンプルさ | ◎ | プロパティファイルで直感的 |
| 継承メカニズム | ✕ | **継承なし**。各ボード独立 |
| Boards Manager 配布 | ◎ | ワンクリックインストール |
| 構造化データ | ✕ | フラット、配列やネストオブジェクト不可 |

---

## 7. Rust Embedded

### 層分離（最も明確）

```
Target Triple (thumbv7em-none-eabihf)    ← コンパイラレベル
  └── PAC (stm32f407)                    ← SVD から自動生成
      └── HAL (stm32f4xx-hal)            ← ファミリ HAL
          └── BSP (stm32f4-discovery)    ← ボード固有
```

### memory.x（最もシンプルなメモリ定義）

```
MEMORY {
    FLASH : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   : ORIGIN = 0x20000000, LENGTH = 128K
}
```

`cortex-m-rt` の `link.x` がセクション配置を標準化。ユーザーはメモリ領域だけ書けばよい。

### probe-rs の chip.yaml

CMSIS-Pack からデバイスデータを自動抽出。メモリマップ、フラッシュアルゴリズム、デバッグ設定を YAML で記述。

### 評価

| 軸 | 評価 | 理由 |
|----|------|------|
| 層分離 | ◎ | PAC/HAL/BSP が独立クレート |
| メモリ定義 | ◎ | memory.x が極めてシンプル |
| 型安全性 | ◎ | コンパイル時レジスタアクセス検証 |
| エントリーバリア | △ | 層の多さが初学者に困難 |

---

## 8. NuttX RTOS

### 3層ディレクトリ

```
boards/arm/stm32/stm32f4discovery/
├── configs/nsh/defconfig          構成バリアント
├── scripts/ld.script              リンカスクリプト（手書き）
└── src/                           ボード初期化コード
```

### 評価

| 軸 | 評価 | 理由 |
|----|------|------|
| ディレクトリ構造 | ◎ | arch/chip/board が物理ディレクトリに直接対応 |
| 構成バリアント | ◎ | 同一ボードで nsh, usbnsh 等の複数構成 |
| リンカスクリプト | △ | ボードごとに手書き、テンプレート生成なし |

---

## 9. modm（最も参考になるシステム）

### アーキテクチャ

```
modm-devices/          デバイスデータベース（4,557 デバイス）
  ↓ lbuild クエリ
modm/                  モジュール群（Jinja2 テンプレート）
  ↓ テンプレート展開
generated/             デバイス固有の生成コード
  ├── startup.c        スタートアップ（生成）
  ├── vectors.c        ベクタテーブル（生成）
  └── linker.ld        リンカスクリプト（生成）
```

### デバイスデータベース

- CubeMX / Atmel Target Description Files から**自動抽出**
- カスタムツリーベース構造でロスレスオーバーレイ
- `DevicesCache` クラスでパフォーマンス最適化

### lbuild モジュールシステム

```python
def prepare(module, options):
    device = options[":target"]
    if not device.has_driver("uart:stm32"):
        return False  # このモジュールは対象外
    return True

def build(env):
    env.template("uart.hpp.in", "uart.hpp")  # テンプレートからコード生成
```

**UART だけ使いたければ、UART のモジュールと依存関係のコードだけが含まれる。**

### リンカスクリプト・スタートアップ

**完全生成**。テンプレート (`ram.ld.in`, `startup_platform.c.in`) + デバイスデータ → デバイス固有コード。手書き不要。

### ボード抽象化

`project.xml` で `<extends>modm:nucleo-f429zi</extends>` と書くだけでボードの全設定を継承。

### 評価

| 軸 | 評価 | 理由 |
|----|------|------|
| 真実の単一源泉 | ★ | modm-devices が 4,557 デバイスの唯一のソース |
| 選択的包含 | ★ | モジュール単位で必要なもののみ生成 |
| リンカスクリプト | ★ | テンプレートから完全生成 |
| スタートアップ | ★ | テンプレートから完全生成 |
| 継承メカニズム | ◎ | ボード extends + デバイスデータの階層的オーバーレイ |
| vendor/family/device/board | ◎ | 4階層が明確に分離 |
| 独自ツール依存 | △ | lbuild (Python3) + Jinja2 が必要 |

---

## 10. libopencm3

### devices.data（パターンマッチング型データベース）

```
# パターン          親          データ
stm32f407?g*     stm32f407    ROM=1024K RAM=128K
stm32f407??      stm32f40x
stm32f40x        stm32f4
stm32f4          stm32
stm32            END          ROM_OFF=0x08000000 RAM_OFF=0x20000000
```

### リンカスクリプト生成

`genlink.py` が `devices.data` をトラバース → C プリプロセッサで `linker.ld.S` テンプレートを展開。

### ペリフェラル IP バージョンに基づく共有

```
lib/stm32/common/
├── usart_common_all.c       全ファミリ共通
├── gpio_common_f0234.c      F0,F2,F3,F4 で GPIO IP 共通
└── i2c_common_v1.c          I2C v1 IP を持つファミリ群で共有
```

**ファミリではなくペリフェラル IP バージョンで共有単位を決定。**

### 評価

| 軸 | 評価 | 理由 |
|----|------|------|
| 真実の単一源泉 | ◎ | devices.data がメモリ定義の唯一源泉 |
| リンカスクリプト | ◎ | テンプレート + genlink で生成 |
| IP ベース共有 | ★ | ペリフェラル IP バージョンで共有する設計が秀逸 |
| ボード抽象化 | ✕ | ボード概念が存在しない |

---

## 横断比較表

### ハードウェア記述方式

| システム | 形式 | データ継承 | メモリ定義の場所 | リンカスクリプト |
|---------|------|----------|-----------------|---------------|
| Zephyr | DTS + YAML + Kconfig | DTS 8階層 include + Kconfig select チェーン | DTS `<memory>` ノード | テンプレート生成 |
| ESP-IDF | C ヘッダ + Kconfig | ファイルパス切り替え | `memory.ld.in` | ldgen 生成 |
| PlatformIO | JSON | **board.json 間は継承なし**（フレームワーク側は各自の継承を持つ） | board.json `upload.*` | テンプレート生成 (2リージョン限定) |
| CMSIS-Pack | XML (PDSC) | 4階層属性累積 | `<memory>` 要素 | Pack 内提供 |
| Mbed OS | JSON | `inherits` + `_add`/`_remove` チェーン | targets.json | テンプレート + マクロ |
| Arduino | Properties | **なし** | boards.txt `upload.*` | variant ディレクトリ |
| Rust | JSON + YAML + TOML | 層ごと独立クレート | `memory.x` | `cortex-m-rt` 提供 |
| NuttX | Kconfig + Make | Kconfig 依存関係 | defconfig `CONFIG_RAM_*` | 手書き |
| **modm** | **独自 XML DB** | **マージ + `device-*` 属性フィルタ**（共通ノード=暗黙の継承） | **DB から生成** | **テンプレート生成** |
| libopencm3 | テキスト DB | パターンマッチ継承 | devices.data | テンプレート生成 |

### 設計品質の軸別評価

| システム | 単一源泉 | 選択的包含 | 生成 vs 手書き | 層分離 | カスタムボード追加 |
|---------|---------|-----------|--------------|--------|-----------------|
| Zephyr | ○ | ○ | 生成 | ◎ | 中（5ファイル） |
| ESP-IDF | ◎ | △ | 生成 | ◎ | 高（多数ファイル） |
| PlatformIO | △ | ○ | 生成(限定的) | △ | 低（JSON 1つ） |
| CMSIS-Pack | ◎ | ○ | 混在 | ◎ | 高（PDSC XML） |
| Mbed OS | ○ | △ | 混在 | ○ | 中（JSON追加） |
| Arduino | △ | ✕ | 手書き | △ | 低（行追加） |
| Rust | ○ | ○ | 混在 | ★ | 中（memory.x） |
| NuttX | △ | △ | 手書き | ○ | 中（ディレクトリコピー） |
| **modm** | **★** | **★** | **完全生成** | **◎** | 中（lbuild 必要） |
| libopencm3 | ◎ | △ | 生成 | ○ | 低（DB 1行） |

---

## UMI への示唆

### 最重要の教訓

#### 1. メモリ定義は唯一の源泉から生成すべき

modm, libopencm3, Zephyr, ESP-IDF の全てが「メモリ定義の唯一源泉」を持ち、リンカスクリプトを**生成**している。手書きリンカスクリプトとデータベースの二重管理は、全てのシステムが回避している問題。

**結論**: umiport の Lua データベースをメモリの唯一源泉とし、`memory.ld` を生成する。

#### 2. vendor 階層の扱い

| システム | vendor 階層の扱い |
|---------|-----------------|
| Zephyr | ディレクトリのグループ化のみ。データ継承なし |
| CMSIS-Pack | `Dvendor` 属性として記録。継承に影響しない |
| modm | データ抽出ソースの切り分けに使用。ランタイムで意味なし |
| PlatformIO | `vendor` フィールド。表示用のみ |
| Rust | クレート名の慣習的プレフィックス (stm32-rs) |

**結論**: vendor は**分類用ラベル**であり、継承元としての実体を持つべきではない。ディレクトリのグループ化程度にとどめる。

#### 3. データ継承 vs コード生成（最重要の設計判断）

全てのシステムを「データの継承方式」と「生成の有無」の2軸で分類する。これらは独立した軸であり、混同してはならない。

**データ継承の軸:**

| 方式 | 採用システム | 特徴 |
|------|------------|------|
| **明示的継承** (inherits) | Mbed OS | `_add`/`_remove` で差分管理。チェーンが深くなると追跡困難 |
| **include チェーン** | Zephyr (DeviceTree) | `.dtsi` の `#include` で8階層のデータ継承。下位が上位のノードを追加・オーバーライド |
| **マージ + フィルタ** | modm | 類似デバイスを1ファイルにマージ。`device-*` 属性のないノードが共通データ（=暗黙の継承）、差分のみ属性で修飾 |
| **dofile() チェーン** | （UMI 現設計） | Lua テーブル上書きによる2段継承 |
| **フラットデータ** | PlatformIO (board.json), Arduino | board.json 間に継承なし。同一 MCU のボード間でデータ重複（291ボード） |

> **注意**: PlatformIO 自体は board.json に継承メカニズムを持たないが、Mbed フレームワーク使用時は Mbed の targets.json 継承構造を `variants_remap.json` 経由で部分的に参照する。フレームワーク側の継承と PlatformIO 側のフラットデータは独立しており、結果として二重管理が発生している。

**コード生成の軸:**

| 方式 | 採用システム | 生成対象 |
|------|------------|---------|
| **テンプレートから全生成** | modm | startup, vectors, linker script, レジスタアクセスコード |
| **テンプレートからリンカ生成** | Zephyr, ESP-IDF, libopencm3 | リンカスクリプト（セクション配置） |
| **テンプレートからメモリ部分生成** | PlatformIO | FLASH + RAM の2リージョン限定 |
| **手書き** | NuttX, Arduino | ボードごとに個別のリンカスクリプト |

**重要な発見**: modm と Zephyr は「データに継承がある」かつ「コード生成もする」。この2つは排他的ではない。modm のデバイスデータベース内部には `device-*` 属性によるデータ共有（=暗黙の継承）が存在し、そのデータからテンプレートで全てを生成する。Zephyr の DeviceTree は8階層の include チェーンでデータを継承し、そのデータからリンカスクリプトを生成する。

**結論**: UMI は「Lua dofile() によるデータ継承」+「memory.ld のテンプレート生成」のハイブリッドが最適。modm/Zephyr と同じく、データ継承とコード生成の両方を活用する。

#### 4. ボードとデバイスの分離

| システム | デバイス定義 | ボード定義 | 関係 |
|---------|------------|-----------|------|
| Zephyr | soc/ + dts/ | boards/ | ボードが SoC を `select` |
| Rust | PAC (クレート) | BSP (クレート) | 独立パッケージ |
| CMSIS-Pack | DFP | BSP | 独立パック |
| modm | modm-devices | board extends | ボードがデバイスを拡張 |
| NuttX | arch/chip/ | boards/ | 物理ディレクトリで分離 |
| **UMI 現設計** | database/family+mcu | board/ | **同一ディレクトリに混在** |

全ての成熟したシステムがデバイスとボードを明確に分離している。

**結論**: デバイス定義（MCU のメモリ・コア・ペリフェラル）とボード定義（ピン設定・外部部品・デバッグプローブ）は分離すべき。

#### 5. 選択的包含（必要なものだけ取得）

| システム | 粒度 | 仕組み |
|---------|------|--------|
| modm | モジュール単位 | lbuild が依存解決し必要なものだけ生成 |
| Zephyr | ボード単位 | BOARD_ROOT で外部ボード定義 |
| PlatformIO | パッケージ単位 | optional フラグ + 動的選択 |
| Yocto | レイヤー単位 | BBLAYERS で必要なレイヤーのみ |
| Arduino | BSP 単位 | Boards Manager でインストール |

**結論**: xmake パッケージとしてボード固有ファイルを配布し、プロジェクトが必要なボードのみ取得する仕組みが理想。

#### 6. デバッグプローブはボード階層に

全システムでデバッグプローブ設定はボード（基板）レベルに配置されている:

| システム | デバッグプローブ設定の場所 |
|---------|------------------------|
| Zephyr | `boards/<board>/board.cmake` |
| PlatformIO | `board.json` の `debug` セクション |
| NuttX | 外部ツール設定 |
| Rust | probe-rs の chip.yaml（チップレベル対応一覧）+ ユーザー選択 |

**結論**: デバッグプローブはボード定義で設定。対応プローブの一覧はデバイスレベルで持ち、デフォルト選択はボードレベルで行う。

---

## 理想的なアーキテクチャの方向性

調査結果から導かれる UMI の理想像:

### modm + Rust Embedded + libopencm3 のハイブリッド

```
                    ┌─────────────────────────────────────┐
                    │  arm-embedded パッケージ              │
                    │  (コア仕様 → コンパイラフラグ)         │
                    │  cortex-m.json は普遍的知識          │
                    └─────────────┬───────────────────────┘
                                  │ embedded.core = "cortex-m4f"
                    ┌─────────────▼───────────────────────┐
                    │  umiport                             │
                    │  ┌─────────────────────────────────┐ │
                    │  │  Lua データベース (唯一の源泉)    │ │
                    │  │  family → mcu の dofile() 継承   │ │
                    │  │  メモリ、コア、ペリフェラル       │ │
                    │  └────────────┬────────────────────┘ │
                    │               │ 生成                  │
                    │  ┌────────────▼────────────────────┐ │
                    │  │  memory.ld (生成)                │ │
                    │  │  sections.ld (ファミリ共通手書き) │ │
                    │  │  startup.cc (手書き or 生成)     │ │
                    │  └─────────────────────────────────┘ │
                    │  ┌─────────────────────────────────┐ │
                    │  │  ボード定義                      │ │
                    │  │  platform.hh, board.hh           │ │
                    │  │  デバッグプローブ設定             │ │
                    │  │  Renode .repl                    │ │
                    │  └─────────────────────────────────┘ │
                    └─────────────────────────────────────┘
```

### 各システムから採用すべき要素

| 要素 | 参考元 | UMI での実現 |
|------|--------|-------------|
| メモリ定義の唯一源泉 | modm, libopencm3 | Lua DB → memory.ld 生成 |
| memory.x のシンプルさ | Rust Embedded | 生成される memory.ld が同等の簡潔さ |
| sections.ld の分離 | ESP-IDF, Zephyr | ファミリ共通 sections.ld を手書きで維持 |
| IP ベースの共有 | libopencm3 | common/ に IP バージョン別ソースを配置 |
| デバッグプローブ | Zephyr board.cmake | ボード定義内に runner 設定 |
| 選択的パッケージ | PlatformIO, Yocto | xmake パッケージとしてボード配布 |
| vendor は分類のみ | Zephyr, CMSIS | ディレクトリのグループ化に使用 |

---

## 参照ソース

### Zephyr
- [Board Porting Guide](https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html)
- [SoC Porting Guide](https://docs.zephyrproject.org/latest/hardware/porting/soc_porting.html)
- [Devicetree HOWTOs](https://docs.zephyrproject.org/latest/build/dts/howtos.html)

### ESP-IDF
- [Hardware Abstraction](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/hardware-abstraction.html)
- [Linker Script Generation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/linker-script-generation.html)

### PlatformIO
- [Custom Embedded Boards](https://docs.platformio.org/en/latest/platforms/creating_board.html)
- [platform-ststm32 (GitHub)](https://github.com/platformio/platform-ststm32)

### CMSIS-Pack
- [Open-CMSIS-Pack Spec](https://open-cmsis-pack.github.io/Open-CMSIS-Pack-Spec/main/html/pdsc_family_pg.html)
- [Memfault: Peeking Inside CMSIS-Packs](https://interrupt.memfault.com/blog/cmsis-packs)

### Mbed OS
- [Adding and Configuring Targets](https://os.mbed.com/docs/mbed-os/v6.16/program-setup/adding-and-configuring-targets.html)

### Arduino
- [Platform Specification](https://arduino.github.io/arduino-cli/0.27/platform-specification/)

### Rust Embedded
- [Embedded Rust Book](https://docs.rust-embedded.org/book/portability/)
- [probe-rs](https://probe.rs/)

### NuttX
- [Custom Boards](https://nuttx.apache.org/docs/latest/guides/customboards.html)

### modm
- [How modm Works](https://modm.io/how-modm-works/)
- [modm-devices (GitHub)](https://github.com/modm-io/modm-devices)
- [lbuild (GitHub)](https://github.com/modm-io/lbuild)

### libopencm3
- [libopencm3 ld/README](https://github.com/libopencm3/libopencm3/blob/master/ld/README)
- [devices.data](https://github.com/libopencm3/libopencm3/blob/master/ld/devices.data)

### Yocto
- [BSP Developer's Guide](https://docs.yoctoproject.org/bsp-guide/bsp.html)

### Bazel
- [Platforms](https://bazel.build/concepts/platforms)
- [Toolchains](https://bazel.build/extending/toolchains)

### Meson
- [Cross-compilation](https://mesonbuild.com/Cross-compilation.html)
