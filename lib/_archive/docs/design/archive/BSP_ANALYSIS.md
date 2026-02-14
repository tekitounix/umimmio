# BSP アーキテクチャ分析: パターン・問題・設計判断

**ステータス:** 分析完了
**作成日:** 2026-02-09
**前提文書:** PLATFORM_COMPARISON.md, ARCHITECTURE_FINAL.md, HARDWARE_DATA_CONSOLIDATION.md
**目的:** 調査結果を横断的に分析し、UMI の BSP アーキテクチャ設計に必要なパターン・問題・洞察を体系化する

---

## 設計原則

> **完全に理想的な美しい方法のみを考えるべき。開発コストはいくらでもかけられる。**

この原則に基づき、本分析では「現実的な妥協案」ではなく「理論的に最適な構造」を追求する。
対象とする 3 つのユースケース:

1. **既存ボードパッケージ (BSP) を使った開発** -- 最も一般的な利用形態
2. **既存 BSP を継承・拡張するカスタムボード** -- 開発ボードを組み込んだカスタム基板
3. **完全に新規のボード開発** -- ユーザーがゼロから BSP を作成

---

## 1. 既存アプローチの分類と評価

調査した 13 以上のシステムは、以下の 5 つのアーキタイプに分類できる。

### 1.1 テンプレート/コード生成型 (Template / Code-generation)

**代表:** STM32CubeMX, modm (lbuild + Jinja2)

**原理:** デバイスデータベースを入力とし、テンプレートエンジンがターゲット固有のソースコード（startup, vectors, linker script, レジスタアクセスコード）を生成する。

```
Device Database (XML/JSON/YAML)
    |
    v  テンプレートエンジン (Jinja2, Mako, etc.)
    |
    v
Generated Source (startup.c, vectors.c, linker.ld, HAL code)
```

**3 ユースケースへの対応:**

| UC | 評価 | 理由 |
|----|:----:|------|
| 1. 既存 BSP 使用 | ◎ | デバイスを選ぶだけで全コードが生成される |
| 2. BSP 継承/拡張 | ○ | modm: `extends` で既存ボードを継承。CubeMX: GUI で再生成（コード差分管理が困難） |
| 3. 新規 BSP 作成 | △ | データベースにデバイスが存在すれば容易。未登録デバイスでは DB エントリ作成が必要 |

**強み:**
- データの唯一源泉 (Single Source of Truth) を最も徹底
- コード品質が均一（人手によるばらつきがない）
- modm は 4,557 デバイスをカバーする巨大 DB を持つ

**弱み:**
- 生成ツール自体の開発・保守コストが高い
- 生成されたコードのデバッグが困難（テンプレートとデータの二重追跡）
- CubeMX のように再生成でユーザー変更が消える問題

**UMI との関連:** xmake Lua スクリプトが Jinja2 の代替として機能できる。memory.ld の生成は既にこのパターンを部分的に採用している（HARDWARE_DATA_CONSOLIDATION.md Section 8）。

---

### 1.2 オーバーレイ/デルタ型 (Overlay / Delta)

**代表:** Zephyr (DeviceTree overlay), Nix (nixpkgs overlay)

**原理:** ベースとなる定義の上に「差分 (delta)」を重ねる。下位が上位のノードを追加・オーバーライドする。

```
Base DTS (stm32f4.dtsi)
    |  #include
    v
SoC DTS (stm32f411.dtsi)
    |  #include
    v
Board DTS (nucleo_f411re.dts)
    |  overlay
    v
Application Overlay (app.overlay)   -- ユーザーが追加変更
```

**3 ユースケースへの対応:**

| UC | 評価 | 理由 |
|----|:----:|------|
| 1. 既存 BSP 使用 | ◎ | ボード DTS を指定するだけ |
| 2. BSP 継承/拡張 | ◎ | overlay ファイルで差分のみ記述。ベースを変更せずに拡張可能 |
| 3. 新規 BSP 作成 | △ | DTS + Kconfig + board.cmake + defconfig の最低 5 ファイルが必要 |

**強み:**
- 差分管理が非常にエレガント（ベースを壊さない）
- 8 階層の include チェーンによる段階的詳細化
- Out-of-Tree ボード定義 (BOARD_ROOT) をサポート

**弱み:**
- DeviceTree + Kconfig + CMake の三重構成は学習コストが高い
- overlay の副作用が追跡困難（どの overlay がどのプロパティを変更したか）
- 1 ボード最低 5 ファイルの複雑さ

**UMI との関連:** Lua テーブルの dofile() + 上書きが DeviceTree overlay に類似。xmake の Lua 継承でこのパターンを実現できる。ただし DeviceTree の構文的複雑さは避けるべき。

---

### 1.3 継承型 (Inheritance)

**代表:** Mbed OS (targets.json inherits), Zephyr HWMv2 (extend)

**原理:** オブジェクト指向の継承に類似。ベースターゲットのプロパティを子が引き継ぎ、差分のみ追加・削除する。

```json
"MCU_STM32":     { "device_has": ["SERIAL"] }
  |  inherits
  v
"MCU_STM32F4":   { "inherits": ["MCU_STM32"], "device_has_add": ["FLASH", "MPU"] }
  |  inherits
  v
"NUCLEO_F411RE": { "inherits": ["MCU_STM32F411xE"], "detect_code": ["0740"] }
```

**3 ユースケースへの対応:**

| UC | 評価 | 理由 |
|----|:----:|------|
| 1. 既存 BSP 使用 | ◎ | ターゲット名を指定するだけ |
| 2. BSP 継承/拡張 | ◎ | `inherits` + `_add`/`_remove` で差分管理。最も直感的 |
| 3. 新規 BSP 作成 | ○ | 親を選んで差分を書くだけ。ただし深い継承チェーンは追跡困難 |

**強み:**
- `_add`/`_remove` 構文が直感的
- 継承チェーンにより共通設定の重複を排除
- Mbed OS は Linter で継承パターンを自動検証

**弱み:**
- 巨大モノリシック JSON（Mbed の targets.json は 1 万行超）
- 深い継承チェーンの追跡が困難（「このプロパティはどこで定義されたか？」）
- ダイヤモンド継承問題（複数の親から矛盾するプロパティ）

**UMI との関連:** Lua dofile() による 2 段継承（family → mcu）は既にこのパターン。ただし Mbed のようなモノリシック JSON は避けるべき。ファイル分割 + dofile() の方が保守性が高い。

---

### 1.4 宣言的フラットファイル型 (Declarative Flat-file)

**代表:** PlatformIO (board.json), Arduino (boards.txt)

**原理:** 各ボードを 1 つのフラットなデータファイルで記述。継承メカニズムを持たない。

```json
// PlatformIO board.json -- 1 ファイルで完結
{
  "build": { "mcu": "stm32f446ret6", "cpu": "cortex-m4" },
  "upload": { "maximum_ram_size": 131072, "maximum_size": 524288 },
  "debug": { "default_tools": ["stlink"] }
}
```

**3 ユースケースへの対応:**

| UC | 評価 | 理由 |
|----|:----:|------|
| 1. 既存 BSP 使用 | ◎ | JSON 1 つ選ぶだけ。291 ボード対応済み |
| 2. BSP 継承/拡張 | ✕ | 継承なし。ベース JSON をコピーして全フィールドを手動編集 |
| 3. 新規 BSP 作成 | ◎ | JSON 1 ファイルを書くだけ。最も低い参入障壁 |

**強み:**
- 極めてシンプル（JSON 1 ファイル）
- 参入障壁が最も低い
- 独立性が高い（他のボード定義に依存しない）

**弱み:**
- 同一 MCU のボード間でデータが大量重複（291 ボードの board.json 間にコピペ）
- MCU レベルの変更を全ボードに反映する手段がない
- メモリ領域が 2 リージョン (FLASH + RAM) に限定

**UMI との関連:** 新規ボード追加の容易さは見習うべき。ただし継承なしのフラットファイルは、MCU 追加時のデータ重複が深刻。UMI は Lua の dofile() 継承でこの問題を回避している。

---

### 1.5 トレイト/コンセプト型 (Trait / Concept)

**代表:** Rust embedded-hal, Embassy

**原理:** ハードウェア抽象をコンパイル時の型制約（trait / concept）で定義。BSP は trait を実装する具体型を提供する。

```rust
// embedded-hal trait
pub trait OutputPin {
    fn set_high(&mut self) -> Result<(), Error>;
    fn set_low(&mut self) -> Result<(), Error>;
}

// BSP が具体型を提供
pub struct LedPin { /* ... */ }
impl OutputPin for LedPin { /* ... */ }
```

**3 ユースケースへの対応:**

| UC | 評価 | 理由 |
|----|:----:|------|
| 1. 既存 BSP 使用 | ○ | BSP クレートを依存に追加。ただし BSP が存在しないボードも多い |
| 2. BSP 継承/拡張 | ○ | Rust は継承より合成。BSP をラップして拡張する |
| 3. 新規 BSP 作成 | ○ | PAC + HAL の上に BSP を作る。layer が多いが各層が明確 |

**強み:**
- コンパイル時に型安全性が保証される（vtable オーバーヘッドなし）
- 層分離が最も明確: PAC → HAL → BSP
- 「BSP なし」でも HAL レベルで直接操作可能（Embassy はこの方向）

**弱み:**
- 層の多さが初学者に困難
- BSP クレートのカバレッジが低い（多くのボードに BSP がない）
- ビルドシステムとの統合が弱い（memory.x は手書き）

**UMI との関連:** C++23 concept が Rust trait に直接対応する。UMI は既にこのパターンを採用（umihal の Platform, OutputDevice, UartBasic 等の concept）。static_assert でコンパイル時検証も実施済み。

---

### 1.6 アーキタイプ横断比較

```
                    生成の度合い
                    高 ─────────────────────── 低
                    |                           |
           modm    |  CubeMX                    |
        (完全生成) |  (GUI 生成)                |
                    |                           |
    データ    Zephyr |                           |  Arduino
    継承の    (DTS   |                           |  (継承なし)
    深さ      8階層) |                           |
                    |        Mbed               |  PlatformIO
              Nix   |      (inherits)           |  (JSON フラット)
                    |                           |
                    |  ESP-IDF                  |
                    |  (soc_caps.h)             |
                    |                           |
                    |        Rust embedded-hal  |
                    |        (型レベル)          |
                    |                           |
                    低 ─────────────────────── 低
```

---

## 2. 3 つのユースケースに対する各システムの対応能力

### 2.1 総合比較表

| # | システム | アーキタイプ | UC1: 既存 BSP 使用 | UC2: BSP 継承/拡張 | UC3: 新規 BSP 作成 | 備考 |
|---|---------|------------|:--:|:--:|:--:|------|
| 1 | Zephyr RTOS | Overlay | ◎ | ◎ | △ | overlay が強力だが 5+ ファイル必要 |
| 2 | ESP-IDF | 生成 | ◎ | △ | △ | IDF_TARGET 全体の切り替えのみ。ボード概念が弱い |
| 3 | PlatformIO | フラット | ◎ | ✕ | ◎ | 継承なし。新規は JSON 1 つで最も簡単 |
| 4 | CMSIS-Pack | 継承 | ◎ | ○ | △ | 4 階層の属性累積。PDSC XML が冗長 |
| 5 | Mbed OS | 継承 | ◎ | ◎ | ○ | `_add`/`_remove` が直感的。EOL |
| 6 | Arduino | フラット | ◎ | ✕ | ○ | 行追加で最もシンプル。構造化不可 |
| 7 | Rust Embedded | Trait | ○ | ○ | ○ | BSP カバレッジが低い。各層は明確 |
| 8 | NuttX | -- | ○ | △ | △ | ディレクトリコピーベース |
| 9 | modm | 生成 | ◎ | ◎ | △ | DB に未登録なら困難。登録済みなら最良 |
| 10 | libopencm3 | 生成 | ○ | ✕ | △ | ボード概念なし。デバイスレベルのみ |
| 11 | Yocto | Overlay | ◎ | ◎ | △ | BSP レイヤー合成が強力。学習コスト高 |
| 12 | Bazel/Buck2 | -- | ○ | ○ | ○ | constraint + select() は汎用的 |
| 13 | Meson | -- | ○ | ○ | ○ | クロスファイル方式。シンプル |

### 2.2 ユースケース別の最良システム

| ユースケース | 最良 | 理由 |
|-------------|------|------|
| UC1: 既存 BSP 使用 | PlatformIO / Zephyr | PlatformIO: 291 ボード即利用可。Zephyr: 広範なボードサポート |
| UC2: BSP 継承/拡張 | Mbed OS / Zephyr | Mbed: `_add`/`_remove` が直感的。Zephyr: DT overlay が強力 |
| UC3: 新規 BSP 作成 | PlatformIO / Arduino | JSON 1 ファイル / 行追加で完了 |

### 2.3 全 UC で ○ 以上を達成するシステム

3 つ全てのユースケースで ○ 以上を達成するのは **modm**, **Mbed OS**, **Zephyr** のみ。共通する特徴:

- データの階層的継承メカニズムを持つ
- デバイス/ボードの分離が明確
- 何らかの生成機構を持つ

UMI が目指すべきは、この 3 システムの良い部分を取り入れつつ、それぞれの弱点（modm の独自ツール依存、Mbed のモノリシック JSON、Zephyr の三重構成）を回避する設計である。

---

## 3. 共通する問題パターン

調査した全システムに横断的に現れる問題パターンを分析する。

### 3.1 「80% 完成」問題 (The "80% Done" Problem)

**症状:** 開発ボードでは即座に動作するが、カスタムボード（量産基板）に移行すると残りの 20% で想定外の工数が発生する。

**発生メカニズム:**

```
開発ボード (Nucleo, Discovery, Daisy Seed)
  → BSP 提供済み、デバッグプローブ内蔵、標準クロック設定
  → 即座に動作 ✓

カスタムボード (量産基板)
  → HSE 周波数が異なる ← ここで問題発生
  → デバッグプローブが異なる ← ここでも問題
  → 外部デバイスの組み合わせが異なる ← ここでも
  → ピン割り当てが異なる ← ここでも
```

**各システムでの発現:**

| システム | 80% 問題の深刻度 | 理由 |
|---------|:---:|------|
| PlatformIO | 高 | board.json に継承なし。カスタムボードは全フィールドをコピーして手動変更 |
| Arduino | 高 | boards.txt にバリアント概念があるが、制約が多い |
| Zephyr | 中 | DT overlay で差分記述可能だが、5 ファイル最低必要 |
| Mbed OS | 低 | `inherits` で既存ターゲットを継承し差分のみ指定 |
| modm | 低 | `extends` で既存ボードを継承 |
| Rust | 中 | memory.x の手書きと BSP の再構築が必要 |

**UMI への示唆:** UC2（既存 BSP の継承/拡張）を第一級市民として設計すべき。「開発ボードから量産基板へ」の移行は最も頻繁に発生するユースケースであり、ここで摩擦が生じるとフレームワーク全体の価値が毀損される。

---

### 3.2 Fork-and-Modify アンチパターン

**症状:** カスタムボードを作る際、既存 BSP をフォーク（コピー）して改変する。以後、上流の改善を取り込めなくなる。

**発生条件:** 継承メカニズムが存在しない、または不十分な場合に必然的に発生する。

```
既存 BSP v1.0
    |  コピー
    v
カスタム BSP v1.0-fork  ← 以後、上流と乖離
    |
    v  独自変更を追加
カスタム BSP v1.0-fork-modified
    |
    :  上流が v2.0 にアップデート → 取り込めない / 手動マージ地獄
```

**影響を受けるシステム:**

| システム | Fork 回避能力 | メカニズム |
|---------|:---:|------|
| PlatformIO | ✕ | board.json コピーが唯一の方法 |
| Arduino | ✕ | variant コピーが標準手法 |
| NuttX | ✕ | `boards/` ディレクトリをコピーして改変 |
| Zephyr | ○ | DT overlay + BOARD_ROOT で上流を変更せずに拡張 |
| Mbed OS | ◎ | `inherits` で上流の変更を自動的に継承 |
| modm | ◎ | `extends` + DB の階層的共有 |

**UMI への示唆:** Lua dofile() 継承により、Fork-and-Modify を構造的に回避できる。board.hh の constexpr 定数 + platform.hh の型定義という分離が、「変わる部分」を明確にし、コピーの必要性を排除する。

---

### 3.3 設定-コードギャップ (Configuration-Code Gap)

**症状:** 回路図（ハードウェア設計）からファームウェア設定への変換が手動であり、不整合が生じやすい。

**具体例:**

```
回路図: HSE = 25 MHz、PA9 = UART1_TX
    |  手動変換（エラーの源泉）
    v
board.hh:  hse_frequency = 8'000'000  ← 間違い！25 MHz のはず
platform.hh:  UART1_TX = PA9  ← 正しい
```

**各システムの対応:**

| システム | ギャップの大きさ | 対処法 |
|---------|:---:|------|
| CubeMX | 小 | GUI で回路図に近い視覚的設定 → コード自動生成 |
| Zephyr | 中 | DTS が回路図に比較的近い表現力を持つ |
| modm | 中 | lbuild オプションで主要設定を宣言的に記述 |
| PlatformIO | 大 | board.json とソースコードが独立 |
| Rust | 大 | BSP コードに直接記述。宣言的な設定ファイルなし |
| UMI (現在) | 中 | board.hh の constexpr が回路図の数値を反映。ただし手動 |

**UMI への示唆:** board.hh の constexpr 定数パターンは、Configuration-Code Gap を最小化する良い設計。回路図の物理定数（クロック周波数、ピン番号、メモリサイズ）が C++ の型システムで検証可能になる。`static_assert` によるコンパイル時検証を積極的に活用すべき。

---

### 3.4 三重管理負担 (Triple Maintenance Burden)

**症状:** 1 つのハードウェア事実を 3 つ以上の場所に記述する必要があり、不整合が発生する。

**Zephyr での具体例:**

```
1 つの事実: 「STM32F407VG は 1M Flash を持つ」

記述箇所 1: DTS
    sram0: memory@20000000 { reg = <0x20000000 DT_SIZE_K(128)>; };

記述箇所 2: Kconfig
    config SRAM_SIZE
        default 128

記述箇所 3: CMake
    set(CONFIG_FLASH_SIZE 1024)
```

**各システムの多重管理度:**

| システム | 管理箇所数 | 箇所 |
|---------|:---:|------|
| Zephyr | 3 | DTS + Kconfig + CMake |
| ESP-IDF | 4 | soc_caps.h + Kconfig + CMake + ldgen |
| UMI (旧) | 2 | mcu-database.json (arm-embedded) + linker.ld (umiport) |
| UMI (新設計) | 1 | Lua DB のみ（memory.ld は自動生成） |
| modm | 1 | modm-devices DB のみ（全て自動生成） |
| Rust | 2 | memory.x (手書き) + BSP コード内の定数 |

**UMI への示唆:** HARDWARE_DATA_CONSOLIDATION.md で設計した「Lua DB が唯一の源泉、memory.ld は自動生成」は、三重管理を 1 箇所に集約する理想的な解。この方針を堅持すべき。

---

### 3.5 フラットファイル爆発 vs モノリシックファイル複雑性

**症状:** フラットファイルを採用すると件数が爆発し、モノリシックファイルを採用すると 1 ファイルが肥大化する。

**二律背反:**

```
フラットファイル方式                  モノリシック方式
(PlatformIO: 291 board.json)         (Mbed: targets.json 10,000+ 行)
  |                                    |
  + 各ファイルが独立・小さい           + 1 ファイルで全体が見える
  + 新規追加が容易                     + 継承関係が 1 箇所で追跡可能
  - 共通変更の一括反映が不可能         - 1 ファイルの肥大化
  - 同一 MCU のデータが重複            - Git の conflict が頻発
  - 一覧性がない                       - エディタの負荷
```

**UMI の解法:** Lua dofile() 継承 + ファイル分割で両方の問題を回避:

```
database/
├── index.lua              ← カタログ（一覧性）
├── family/stm32f4.lua     ← 共通データ（重複排除）
└── mcu/stm32f4/
    ├── stm32f401re.lua    ← 差分のみ（小さいファイル）
    ├── stm32f407vg.lua    ← 差分のみ
    └── stm32f446re.lua    ← 差分のみ
```

この構造は modm の modm-devices (4,557 デバイスを階層的に管理) と libopencm3 の devices.data (パターンマッチ継承) の利点を組み合わせている。

---

## 4. ボード間で実際に変わるもの

ハードウェアの差異を精密に分類し、それぞれがソフトウェアのどの層に影響するかを分析する。

### 4.1 同一 MCU・異なるボード

同じ STM32F407VG を搭載するボードでも、以下が異なる:

| 変更要素 | 例 | 変更頻度 | 影響を受けるソフトウェア層 |
|---------|-----|:---:|------|
| **HSE 水晶周波数** | 8 MHz / 25 MHz | 高 | クロック設定 (PLL 分周/逓倍比) |
| **ピン割り当て** | UART1_TX: PA9 / PB6 | 高 | GPIO 初期化、AF マッピング |
| **外部デバイス** | CS43L22 / WM8731 / AK4556 | 高 | デバイスドライバ選択、I2C/SPI 設定 |
| **デバッグプローブ** | ST-Link / J-Link / CMSIS-DAP | 高 | デバッガ設定 (OpenOCD/pyOCD) |
| **コンソール出力経路** | UART / RTT / SWO | 高 | Output 実装、platform.hh |
| **外部メモリ** | なし / SDRAM 64M / QSPI 8M | 中 | リンカスクリプト、初期化コード |
| **電源電圧** | 3.3V / 1.8V | 低 | Flash ウェイトステート設定 |
| **ブートモード** | Flash / RAM / System | 低 | ベクタテーブルオフセット |

**重要な観察:** 同一 MCU のボード間では、MCU レジスタ操作コード (`mcu/stm32f4/*.hh`) は完全に共有できる。変わるのは「何を、どのピンに、どう繋ぐか」という**接続情報**のみ。

---

### 4.2 同一 MCU ファミリ・異なるバリアント

STM32F4 ファミリ内でのバリアント差異:

| 変更要素 | 例 | ソフトウェアへの影響 |
|---------|-----|------|
| **Flash サイズ** | 256K (F401) / 1M (F407) / 2M (F429) | リンカスクリプトの MEMORY 定義 |
| **RAM サイズ** | 64K (F401) / 128K (F407) / 256K (F429) | リンカスクリプトの MEMORY 定義 |
| **CCM-RAM の有無** | なし (F401) / 64K (F407) | リンカスクリプト + スタック配置 |
| **最大クロック** | 84 MHz (F401) / 168 MHz (F407) / 180 MHz (F446) | PLL 設定の上限 |
| **ペリフェラルの有無** | USB OTG (F407) / SAI (F446) | 利用可能なドライバの制限 |
| **パッケージ** | LQFP64 (F401RE) / LQFP100 (F407VG) | 利用可能なピン数 |

**重要な観察:** ファミリ内バリアント間の差異は、ほとんどが**数値の差**（サイズ、クロック、ピン数）である。レジスタ操作コードのアーキテクチャは共通。これが family.lua → mcu.lua の 2 段継承で自然に表現できる理由。

---

### 4.3 異なる MCU ファミリ

| 変更要素 | STM32F4 → STM32H7 | STM32 → nRF52 |
|---------|-------------------|---------------|
| **CPU コア** | Cortex-M4 → M7 | Cortex-M4 → M4 (同じ) |
| **メモリアーキテクチャ** | Flash+SRAM+CCM → Flash+DTCM+AXI+SRAM1/2/4+ITCM | 全く異なる構成 |
| **クロックツリー** | PLL 構成が異なる | 全く異なる |
| **ペリフェラル IP** | I2S → SAI | I2S → I2S (実装が異なる) |
| **レジスタマップ** | 一部共通 (GPIO) | 完全に異なる |
| **割り込みベクタ** | 完全に異なる | 完全に異なる |
| **startup コード** | 異なる | 異なる |
| **リンカスクリプト** | sections.ld が異なる | 完全に異なる |

**重要な観察:** ファミリが異なると、共有できるのはアーキテクチャレベル（Cortex-M 共通: DWT, SCB, NVIC, SysTick）のみ。これが umiport の `arm/cortex-m/` ディレクトリの存在意義。

---

### 4.4 変更の影響度マトリクス

```
                      board.hh   platform.hh   mcu/*.hh   sections.ld   startup.cc
                      (定数)     (型+統合)     (レジスタ)  (リンカ)      (ベクタ)
同一MCU別ボード         ◎変更      ◎変更        -          -             -
同一Family別バリアント   ◎変更      ○一部変更    -          -             -
異なるFamily            ◎変更      ◎変更        ◎変更      ◎変更         ◎変更

◎: 必ず変更  ○: 場合により変更  -: 変更不要
```

**設計への帰結:** board.hh と platform.hh は全ケースで変更が必要な最外層。mcu/*.hh, sections.ld, startup.cc はファミリ内で共有可能な内部層。この 2 層構造が ARCHITECTURE_FINAL.md の設計を裏付ける。

---

## 5. 設計判断の軸

### 5.1 A. 継承 vs 合成 (Inheritance vs Composition)

BSP の構造を定義する最も根本的な設計判断。

#### 継承 (IS-A)

「MyBoard IS-A Nucleo の変種」

```
Nucleo_F411RE
    |  extends / inherits
    v
MyCustomBoard
    HSE = 25MHz (override)
    UART_TX = PB6 (override)
    + CS43L22 codec (add)
```

**採用システム:** Mbed OS (`inherits`), Zephyr HWMv2 (`extend`), modm (`extends`)

**利点:**
- 差分記述が最小
- 上流の変更を自動的に継承

**問題:**
- 深い継承チェーンの追跡困難
- 「Nucleo の変種」が意味的に正しくない場合がある
- ダイヤモンド継承

#### 合成 (HAS-A)

「MyBoard HAS-A STM32F411 + PinConfig + AudioCodec」

```cpp
struct MyBoard {
    using Mcu = stm32f4::STM32F411;      // MCU を持つ
    using Pins = my_pins::PinConfig;      // ピン設定を持つ
    using Codec = CS43L22<I2C1>;          // コーデックを持つ
};
```

**採用システム:** Rust BSP (合成的にモジュールを組み合わせ), Terraform (リソースの合成)

**利点:**
- 関係が明示的（何を使っているか一目瞭然）
- 組み合わせの自由度が高い
- テスト時にモック差し替えが容易

**問題:**
- 共通パターンの抽出が難しい（同じ組み合わせを何度も書く）
- ボイラープレートが多い

#### Mixin/Overlay

「MyBoard = Base + Delta1 + Delta2」

```
stm32f4.dtsi + stm32f411.dtsi + nucleo.dts + app.overlay
```

**採用システム:** Zephyr (DT overlay), Nix (overlay), Yocto (BSP layer)

**利点:**
- 複数の独立した差分を重ね合わせ可能
- 各 delta が独立してテスト可能
- 非侵入的（ベースを変更せずに拡張）

**問題:**
- 適用順序の制御が必要
- 副作用の追跡が困難

#### UMI にとっての最適解

**C++ 層: 合成 (Composition)**

UMI の C++23 concept は合成パターンと自然に整合する:

```cpp
// platform.hh は合成パターン
struct Platform {
    using Output = stm32f4::UartOutput;       // HAS-A Output
    using Timer  = cortex_m::DwtTimer;        // HAS-A Timer
    using Codec  = CS43L22Driver<I2C1>;       // HAS-A Codec
    static void init() { Output::init(); }
};
static_assert(umi::hal::Platform<Platform>);  // コンパイル時検証
```

concept は「何を満たすべきか」を定義し、具体的な実装の選択はボード層の合成に委ねる。これは Rust embedded-hal の trait パターンと等価であり、vtable オーバーヘッドなしで静的ポリモーフィズムを実現する。

**xmake/Lua 層: 継承 + overlay のハイブリッド**

Lua dofile() による 2 段継承が自然:

```lua
-- family/stm32f4.lua (ベース)
-- mcu/stm32f4/stm32f407vg.lua (family を dofile で継承 + MCU 固有値を上書き)
```

ボードレベルでは overlay 的な差分追加が xmake の `set_values()` で実現される:

```lua
-- ボード選択 = overlay 的な差分指定
set_values("umiport.board", "stm32f4-disco")
set_values("embedded.mcu", "stm32f407vg")
```

---

### 5.2 B. ビルドシステム vs 型システム (Build-system level vs Type-system level)

#### ビルドシステムレベル (xmake Lua)

ビルド時に設定を解決する層。

**責務:**
- MCU データベースからコンパイラフラグを導出
- リンカスクリプト (memory.ld) の生成
- startup.cc / syscalls.cc の選択と追加
- includedirs の設定（ボード固有ヘッダの解決）
- デバッグプローブ / フラッシュ設定の提供
- ELF → HEX/BIN 変換、メモリ使用量表示

**表現力:** Lua スクリプトによる条件分岐、ファイル走査、テンプレート展開が可能。JSON/YAML より遥かに強力。

#### 型システムレベル (C++23 concept)

コンパイル時に制約を検証する層。

**責務:**
- Platform, OutputDevice, UartBasic 等の concept による契約定義
- `static_assert` によるコンパイル時検証
- `if constexpr` による MCU 差異の吸収
- テンプレートパラメータによる Transport 注入

**表現力:** C++23 concept + constexpr + `if constexpr` により、マクロ (`#ifdef`) に頼らずにゼロオーバーヘッドの静的ポリモーフィズムを実現。

#### 両者の責務分離

```
┌─────────────────────────────────────────────────────────────┐
│ xmake Lua 層 (ビルドシステム)                                 │
│                                                             │
│ ・MCU DB → コンパイラフラグ        「何をビルドするか」       │
│ ・memory.ld 生成                                             │
│ ・includedirs 設定                                           │
│ ・startup/syscalls 選択                                      │
│ ・デバッグ/フラッシュ設定                                     │
├─────────────────────────────────────────────────────────────┤
│ C++23 型システム層                                           │
│                                                             │
│ ・concept による契約定義            「正しくビルドされるか」   │
│ ・static_assert による検証                                   │
│ ・constexpr + if constexpr                                  │
│ ・テンプレートによる注入                                     │
└─────────────────────────────────────────────────────────────┘
```

**重要:** この 2 層は独立しているが補完的。xmake が「何をビルドするか」を決定し、C++ 型システムが「正しくビルドされるか」を検証する。どちらか一方では不十分。

---

### 5.3 C. データの配置 (Where to put hardware data)

ハードウェアデータをどこに配置するかは、保守性と使いやすさに直結する。

| 配置方式 | 採用システム | 利点 | 欠点 |
|---------|------------|------|------|
| **xmake Lua** | UMI (設計) | dofile() 継承、条件分岐、コメント可 | xmake 固有 |
| **JSON** | PlatformIO, CMSIS-Pack | ツール非依存、広く解析可能 | 継承不可、コメント不可 |
| **DeviceTree** | Zephyr | 構造的で表現力が高い | 独自構文、学習コスト高 |
| **C++ ヘッダ** | UMI (board.hh) | コンパイル時検証可能、型安全 | ビルド時のみ利用可能 |
| **YAML** | probe-rs (chip.yaml) | 人間可読、構造的 | 継承が標準ではない |
| **カスタム DB** | modm (XML), libopencm3 (text) | 高い柔軟性 | 独自パーサーが必要 |

**UMI の最適解: Lua + C++ ヘッダの二層構造**

```
Lua DB (database/*.lua)         C++ ヘッダ (board.hh)
  |                               |
  | ビルド時に使用                 | コンパイル時に使用
  |                               |
  v                               v
・memory.ld 生成                  ・constexpr 定数
・コンパイラフラグ導出             ・static_assert 検証
・デバッグ/フラッシュ設定          ・if constexpr 分岐
・メモリ使用量表示                 ・テンプレートパラメータ
```

**なぜ 2 箇所にデータを持つのか?** Lua DB と board.hh の間に一部データの重複（メモリサイズ、クロック周波数）が生じうる。これは意図的なトレードオフ:

- Lua DB: ビルドシステムが消費するデータ（リンカスクリプト生成、フラッシュコマンド構築）
- board.hh: ランタイムコードが消費するデータ（クロック設定、ピン初期化）

メモリサイズは Lua DB が正（memory.ld 生成）、board.hh は必要な場合のみ持つ（例: ランタイムでのヒープサイズ計算）。HSE 周波数は board.hh が正（PLL 設定のコンパイル時計算に必要）。

---

### 5.4 D. プロジェクトローカルなボード定義

ユーザーが自分のプロジェクト内でカスタムボードを定義する方法。UC2 と UC3 に直結する。

| システム | 方式 | 評価 |
|---------|------|:---:|
| PlatformIO | プロジェクト内 `boards/` ディレクトリに JSON を置く | ◎ |
| Zephyr | `BOARD_ROOT` 変数でプロジェクト内ボードを参照 | △ |
| NuttX | Out-of-tree: 既存 board をコピーして改変 | ✕ |
| Mbed | targets.json を直接編集（推奨外）またはカスタム targets.json を追加 | ○ |
| modm | `project.xml` に `<extends>` で宣言 | ◎ |
| Arduino | `hardware/` ディレクトリにパッケージを配置 | ○ |

**UMI の理想:**

```
ユーザープロジェクト/
├── xmake.lua
├── src/
│   └── main.cc
└── board/                         ← プロジェクトローカルなボード定義
    └── my-custom-board/
        ├── platform.hh            ← Platform 型定義
        ├── board.hh               ← 定数
        └── board.lua              ← Lua DB エントリ（MCU 選択 + オーバーライド）
```

xmake.lua からの使用:

```lua
target("my_app")
    add_rules("embedded", "umiport.board")
    set_values("embedded.mcu", "stm32f407vg")
    set_values("umiport.board", "my-custom-board")
    set_values("umiport.board_root", "board")  -- プロジェクトローカルな board を検索
    add_deps("umiport", "umidevice")
    add_files("src/*.cc")
```

**PlatformIO の `boards/` ディレクトリの簡潔さ** と **Zephyr の BOARD_ROOT の柔軟性** を組み合わせる。`umiport.board_root` により、umiport 内蔵ボード以外の検索パスを追加できる。

---

## 6. UMI 固有の制約と機会

### 6.1 C++23 Concept = Rust embedded-hal Trait

UMI の C++23 concept は、Rust の embedded-hal trait と機能的に等価:

| 特性 | Rust embedded-hal | UMI umihal concept |
|------|-------------------|-------------------|
| 型制約の定義 | `trait OutputPin` | `concept OutputDevice` |
| コンパイル時検証 | `impl OutputPin for Pin` | `static_assert(Platform<T>)` |
| 静的ディスパッチ | 単形化 (monomorphization) | テンプレートインスタンス化 |
| vtable オーバーヘッド | なし | なし |
| 階層化 | `trait AsyncSerial: Serial` | `concept UartAsync = UartBasic<T> && ...` |

**機会:** Rust embedded-hal エコシステムの設計判断（PAC/HAL/BSP 分離、「BSP なし」アプローチ）を C++ 世界に持ち込める。UMI の umihal (concept) / umiport (MCU 実装) / umidevice (デバイス実装) は Rust の HAL/PAC/BSP に直接対応する。

---

### 6.2 xmake Lua スクリプティング

xmake の Lua スクリプティングは JSON/YAML/Kconfig/CMake より遥かに強力:

| 能力 | JSON | YAML | Kconfig | CMake | **xmake Lua** |
|------|:----:|:----:|:-------:|:-----:|:------------:|
| 条件分岐 | ✕ | ✕ | ○ | ○ | ◎ |
| 継承/合成 | ✕ | ✕ | △ | ✕ | ◎ (`dofile`) |
| 関数定義 | ✕ | ✕ | ✕ | △ | ◎ |
| コメント | ✕ | ○ | ○ | ○ | ◎ |
| 文字列操作 | ✕ | ✕ | ✕ | ○ | ◎ |
| ファイル I/O | ✕ | ✕ | ✕ | △ | ◎ |
| テーブル/構造体 | ✕ (辞書のみ) | ○ | ✕ | ✕ | ◎ |

**機会:** Lua の表現力により、Zephyr の「DTS + Kconfig + CMake 三重構成」を **Lua 単一言語** に集約できる。これは学習コストの劇的な削減をもたらす。

---

### 6.3 One-Source Multi-Target

UMI の最大の差別化要因: 同一 C++ ソースコードが Cortex-M, WASM, デスクトップで動作する。

**制約:**
- ソースコードに `#ifdef STM32F4` を含められない
- プラットフォーム差異は platform.hh の型選択で吸収する
- `umi::rt::detail::write_bytes()` の link-time 注入で出力経路を切り替える

**BSP 設計への影響:**
- platform.hh は「このプラットフォームでの具体的な型の選択」を行う唯一の場所
- ライブラリ（umirtm, umibench）は platform.hh の存在を知らない
- link-time 注入により、ライブラリのバイナリインターフェースはプラットフォーム非依存

---

### 6.4 リアルタイムオーディオ制約

**ハード制約:**
- ヒープ割り当て禁止 (`new`, `malloc`)
- ブロッキング同期禁止 (`mutex`, `semaphore`)
- 例外禁止 (`throw`)
- stdio 禁止 (`printf`, `cout`)

**BSP 設計への影響:**
- BSP の初期化コードはリアルタイムパス外（起動時に 1 回のみ実行）で例外が許容される場合もあるが、UMI は全面禁止を選択
- concept の制約式 (`requires`) にリアルタイム安全性を型レベルで表現できる可能性
- `constexpr` によるコンパイル時計算を最大限に活用し、ランタイムコストを排除

---

### 6.5 既存アーキテクチャの構成要素

現在の UMI アーキテクチャの構成要素と、BSP 設計における役割:

```
┌──────────────────────────────────────────────────────┐
│ umihal (concept 定義)                                  │
│   Platform, OutputDevice, UartBasic, ClockTree,        │
│   AudioCodec, I2cTransport, etc.                       │
│   → BSP の「何を実装すべきか」を定義                    │
├──────────────────────────────────────────────────────┤
│ umiport (MCU + ボード統合)                              │
│   arm/cortex-m/ : DWT, SCB (アーキ共通)                │
│   mcu/stm32f4/  : GPIO, UART, I2S (MCU 固有)          │
│   board/        : platform.hh + board.hh (ボード固有)  │
│   src/          : startup.cc, syscalls.cc, linker.ld   │
│   database/     : Lua DB (MCU メモリ等)                │
│   rules/        : umiport.board ルール                  │
│   → BSP の「どう実装するか」を提供                      │
├──────────────────────────────────────────────────────┤
│ umidevice (外部デバイスドライバ)                        │
│   audio/cs43l22/, audio/wm8731/, etc.                  │
│   → MCU 非依存のデバイスドライバ                        │
├──────────────────────────────────────────────────────┤
│ arm-embedded パッケージ (ビルドメカニズム)               │
│   cortex-m.json : コア → コンパイラフラグ               │
│   embedded ルール : フラグ適用、ELF 生成                │
│   → 「コア名」を受け取り、ビルドを構成                  │
└──────────────────────────────────────────────────────┘
```

**embedded.core インターフェース:** arm-embedded パッケージと umiport の間の契約。umiport が MCU DB からコア名を解決し、arm-embedded がコア名からコンパイラフラグを導出する。この単一のインターフェースポイントにより、両者の責務が明確に分離される。

---

## 7. 各フレームワークの教訓 (Lessons Learned)

### 7.1 modm: デバイスデータベースの分離とモジュール粒度

**教訓 1: デバイス DB はコードとは独立した資産**

modm-devices は 4,557 デバイスをカバーする独立リポジトリ。CubeMX / Atmel Target Description Files から自動抽出され、ロスレスオーバーレイでマージされる。

→ UMI: Lua DB (`database/`) をコードベースの中核資産として位置づける。MCU メモリ、コア、ペリフェラル情報の唯一源泉とする。

**教訓 2: モジュール粒度の選択的包含**

modm の lbuild は「UART だけ使いたければ、UART モジュールと依存関係のコードだけ」を生成する。

→ UMI: ヘッダオンリーライブラリのため、使わないヘッダはビルドに影響しない。C++ のテンプレートインスタンス化が自然な選択的包含を実現する。

**教訓 3: テンプレートベースの生成**

startup, vectors, linker script が全てテンプレートから生成される。手書きゼロ。

→ UMI: memory.ld は Lua DB から生成。sections.ld はファミリ共通で手書き維持（セクション配置がファミリのメモリアーキテクチャに依存するため完全生成は困難だが、memory 部分の生成で最大の恩恵を得る）。

---

### 7.2 Rust/Embassy: Trait ベースの No-BSP アプローチ

**教訓 1: 明確な層分離 (PAC → HAL → BSP)**

各層が独立した crate (パッケージ) であり、依存方向が一方向。BSP がなくても HAL レベルで直接操作可能。

→ UMI: umihal (concept) → umiport (実装) → board (統合) の層分離。umihal concept を満たす限り、umiport の特定実装に依存せずにアプリケーションを書ける。

**教訓 2: memory.x の極限的シンプルさ**

Rust の memory.x は MEMORY 定義のみ。セクション配置は cortex-m-rt が標準提供。

→ UMI: 生成される memory.ld が同等のシンプルさを実現。sections.ld がファミリ共通として cortex-m-rt の役割を果たす。

**教訓 3: Embassy の「BSP は必要ない」哲学**

Embassy は BSP クレートを推奨せず、HAL を直接使うことを推奨。ボード固有の情報はアプリケーション側に持つ。

→ UMI: 完全に同意はしないが、platform.hh を最小限に保つ設計指針として採用。platform.hh は「型の選択と結合」のみを行い、ロジックは含めない。

---

### 7.3 Zephyr: 三重構成の回避と DT Overlay の学習

**教訓 1: 三重構成 (DTS + Kconfig + CMake) の負担を避ける**

Zephyr の 1 ボード = 最低 5 ファイルは、新規ボード追加の高い障壁。

→ UMI: 同一 MCU の新ボードなら platform.hh + board.hh の 2 ファイルで完了。xmake.lua での 1 行で完結。

**教訓 2: DT Overlay の非侵入性**

ベースを変更せずに差分を重ねるパターンは強力。

→ UMI: xmake の `set_values()` による overlay 的な設定差し替え。`umiport.board_root` でプロジェクトローカルなボードを非侵入的に追加。

**教訓 3: HWMv2 (Hardware Model v2) の教訓**

Zephyr は v3.7 で HWMv1 から HWMv2 へ移行。soc.yml で SoC 階層を明示化、ボード variant 機能を追加。大規模な破壊的変更。

→ UMI: 最初から正しい階層構造を設計することの重要性。ARCHITECTURE_FINAL.md の設計は、Zephyr の HWMv2 と同等の階層性を初期設計に組み込んでいる。

---

### 7.4 ESP-IDF: soc_caps.h の宣言的パターンと LL の static inline

**教訓 1: soc_caps.h -- 能力の宣言的定義**

```c
#define SOC_I2S_NUM              2
#define SOC_I2S_SUPPORTS_PDM     1
#define SOC_ADC_MAX_CHANNEL_NUM  10
```

→ UMI: board.hh の constexpr 定数が同等の役割。`static constexpr bool has_fmc = true;` のように MCU 能力を宣言的に定義する。`if constexpr` で分岐すればゼロオーバーヘッド。

**教訓 2: LL (Low-Level) API の static inline 強制**

ESP-IDF の LL 層は全て `static inline` で、ゼロオーバーヘッドを保証。

→ UMI: ヘッダオンリーの constexpr/static メンバ関数により同等の効果を達成。

---

### 7.5 PlatformIO: board.json の簡潔さとリンカテンプレート生成

**教訓 1: JSON 1 ファイルでボード追加**

参入障壁が最も低い。291 ボード対応はこの簡潔さの結果。

→ UMI: platform.hh + board.hh の 2 ファイルは PlatformIO より 1 ファイル多いが、型安全性の恩恵がこの追加コストを正当化する。

**教訓 2: リンカテンプレート生成の限界 (2 リージョン限定)**

PlatformIO のリンカ生成は FLASH + RAM の 2 リージョンのみ。CCM-RAM、複数 SRAM を持つ MCU に対応できない。

→ UMI: Lua DB の memory テーブルは任意のリージョン数をサポート。STM32H7 の 8 リージョンも同じ生成関数で処理できる。

---

### 7.6 libopencm3: devices.data のパターンマッチと IP バージョン共有

**教訓 1: devices.data のパターンマッチ継承**

```
stm32f407?g*     stm32f407    ROM=1024K RAM=128K
stm32f407??      stm32f40x
stm32f40x        stm32f4
```

→ UMI: index.lua + dofile() 継承が同等の役割を果たす。パターンマッチより明示的だが、型番の命名規則がベンダーごとに異なる問題を回避。

**教訓 2: ペリフェラル IP バージョンに基づく共有**

```
gpio_common_f0234.c   -- F0, F2, F3, F4 で GPIO IP 共通
i2c_common_v1.c       -- I2C v1 IP を持つファミリ群で共有
```

→ UMI: `mcu/stm32f4/` 内のレジスタ定義が IP バージョン単位で共有可能。例えば STM32F4 と STM32F2 で GPIO IP が共通なら、`mcu/common/gpio_v2.hh` のような共有ヘッダを導入できる。

**教訓 3: ボード概念の不在**

libopencm3 にはボードレベルの抽象がない。デバイスレベルのみ。

→ UMI: これは反面教師。ボード層 (platform.hh) の存在が、ライブラリの HW 非依存性を保証する鍵。

---

### 7.7 Mbed OS: `_add`/`_remove` プロパティ修飾子

**教訓 1: `_add`/`_remove` の直感的な差分管理**

```json
"MCU_STM32F4": {
    "device_has_add": ["SERIAL_ASYNCH", "FLASH", "MPU"]
}
```

→ UMI: Lua テーブルの操作で同等の表現が可能:

```lua
family.peripherals = table.merge(family.peripherals, {"SERIAL_ASYNCH", "FLASH", "MPU"})
```

ただし UMI では能力の表現を Lua ではなく C++ constexpr に委ねるため、Mbed パターンの直接的な採用はない。

**教訓 2: モノリシック JSON の教訓**

targets.json が 10,000 行を超え、保守性が著しく低下した。

→ UMI: ファイル分割 (family/*.lua, mcu/*/*.lua) で回避済み。index.lua がカタログとして機能し、全体の一覧性を維持。

**教訓 3: Linter による継承パターンの自動検証**

Mbed は CI で継承チェーンの整合性を自動検証していた。

→ UMI: `static_assert(umi::hal::Platform<Platform>)` がコンパイラレベルでこれを実現。Lua DB の整合性は xmake テストで検証可能。

---

## 8. 結論

### 8.1 UMI に最適なアーキタイプ

単一のアーキタイプでは 3 ユースケース全てを満たせない。UMI に最適なのは**ハイブリッドアーキテクチャ**:

| 層 | アーキタイプ | 理由 |
|----|------------|------|
| C++ 型システム | **Trait/Concept 型** | C++23 concept = Rust embedded-hal trait。ゼロオーバーヘッド静的ポリモーフィズム |
| xmake ビルドシステム | **継承 + Overlay 型** | Lua dofile() 継承 + set_values() overlay。単一言語で三重構成を回避 |
| リンカスクリプト | **テンプレート/生成型** | Lua DB → memory.ld 自動生成。唯一源泉の原則 |
| ボード定義 | **合成型** | platform.hh が MCU + Device + Config を合成 |

### 8.2 推奨ハイブリッドアーキテクチャ

```
┌────────────────────────────────────────────────────────┐
│                    C++ Concept 層                       │
│  umihal: Platform, OutputDevice, UartBasic, AudioCodec │
│  → 「何を満たすべきか」の契約定義                       │
│  → コンパイル時に static_assert で検証                  │
├────────────────────────────────────────────────────────┤
│                    合成 (Composition) 層                │
│  platform.hh:                                          │
│    using Output = stm32f4::UartOutput;    ← umiport    │
│    using Codec  = CS43L22Driver<I2C>;     ← umidevice  │
│  → MCU + Device + Config の唯一の統合点                │
├────────────────────────────────────────────────────────┤
│              Lua 継承 + Overlay 層                      │
│  family/stm32f4.lua → mcu/stm32f4/stm32f407vg.lua     │
│  → dofile() 継承でデータ共有                            │
│  → set_values("umiport.board", "...") で overlay       │
├────────────────────────────────────────────────────────┤
│                テンプレート生成層                        │
│  Lua DB → memory.ld (自動生成)                         │
│  sections.ld (ファミリ共通手書き)                       │
│  → メモリ定義の唯一源泉                                │
├────────────────────────────────────────────────────────┤
│            ビルドメカニズム層 (arm-embedded)             │
│  embedded.core → コンパイラフラグ                       │
│  → アーキテクチャの普遍的知識                           │
└────────────────────────────────────────────────────────┘
```

### 8.3 各ユースケースでの動作

**UC1: 既存 BSP 使用**

```lua
-- xmake.lua (2 行で完結)
add_rules("embedded", "umiport.board")
set_values("embedded.mcu", "stm32f407vg")
set_values("umiport.board", "stm32f4-disco")
```

ユーザーが書くのは上記のみ。umiport.board ルールが全てを解決する。

**UC2: BSP 継承/拡張**

```
my_project/
├── board/my-board/
│   ├── platform.hh    ← stm32f4-disco を参考に差分のみ変更
│   └── board.hh       ← HSE, ピン, デバッグプローブを変更
└── xmake.lua
    set_values("umiport.board", "my-board")
    set_values("umiport.board_root", "board")
```

startup.cc, syscalls.cc, linker.ld は同一 MCU なら共有。platform.hh + board.hh の 2 ファイル作成のみ。

**UC3: 新規 BSP 作成**

```
1. database/family/<family>.lua を作成 (新ファミリの場合)
2. database/mcu/<family>/<mcu>.lua を作成
3. database/index.lua に 1 行追加
4. src/<family>/ に startup.cc, syscalls.cc, sections.ld を作成
5. board/<name>/ に platform.hh, board.hh を作成
```

全て umiport 内で完結。`dev-sync` 不要。`static_assert` により「何が不足しているか」がコンパイラエラーで明確になる。

### 8.4 成功の鍵

1. **唯一源泉の徹底:** メモリ定義は Lua DB のみ。C++ 型契約は umihal concept のみ。二重管理を構造的に排除する。

2. **合成による統合:** platform.hh が MCU ドライバ (umiport) と外部デバイスドライバ (umidevice) を結合する唯一の場所。両者は互いを知らない。

3. **コンパイル時検証:** `static_assert(umi::hal::Platform<Platform>)` により、新ボード追加時に「何を実装すべきか」がコンパイラエラーで即座に判明する。

4. **Lua 単一言語:** DTS + Kconfig + CMake の三重構成を Lua に集約。学習コストの劇的な削減。

5. **プロジェクトローカルなボード定義:** `umiport.board_root` によりユーザーのプロジェクト内にカスタムボードを非侵入的に定義可能。Fork-and-Modify を構造的に回避。

6. **段階的詳細化:** family → mcu → board の 3 層で、各層が適切な粒度の情報を持つ。フラットファイル爆発もモノリシックファイル肥大化も回避。

---

## 参照文書

| 文書 | 内容 |
|------|------|
| PLATFORM_COMPARISON.md | 13 システムの詳細調査結果 |
| ARCHITECTURE_FINAL.md | UMI の確定アーキテクチャ設計 |
| HARDWARE_DATA_CONSOLIDATION.md | ハードウェアデータの統合設計 |
| LINKER_DESIGN.md | リンカスクリプト設計 |
| XMAKE_RULE_ORDERING.md | xmake ルール順序問題の分析 |
| MIGRATION_PLAN.md | 移行計画 |
