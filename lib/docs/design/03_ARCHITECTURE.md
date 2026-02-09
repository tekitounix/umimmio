# UMI ハードウェア抽象化アーキテクチャ

**ステータス:** 確定版  **策定日:** 2026-02-09
**根拠:** ARCHITECTURE_FINAL.md + BSP_ARCHITECTURE_PROPOSAL.md + 7 設計文書の統合

---

## 1. 設計原則

以下の 5 原則は不変であり、全ての設計判断の土台となる。

| # | 原則 | 具体制約 |
|---|------|----------|
| 1 | **ソースにハード依存を漏らさない** | `#ifdef STM32F4` 禁止。MCU 差異は `constexpr` + `if constexpr`。フルパスインクルードはアプリ層禁止（`platform.hh` 内のみ例外） |
| 2 | **ライブラリは HW を知らない** | umirtm, umibench, umimmio, umitest は HW 依存ゼロ。出力はリンク時注入 |
| 3 | **統合は board 層のみで行う** | MCU × 外部デバイスの結合は `board/platform.hh` が唯一の統合点 |
| 4 | **ゼロオーバーヘッド** | vtable 不使用。C++ Concepts と静的ポリモーフィズムで全抽象をコンパイル時解消 |
| 5 | **パッケージは単独で意味を持つ単位** | 分離の判断軸:「独立して使えるか」+「知識の帰属先が異なるか」 |

---

## 2. パッケージ構成

7 パッケージで構成され、各パッケージが単独で意味を持つ。

| パッケージ | 役割 | HW 依存 |
|-----------|------|---------|
| umihal | HAL Concept 契約（実装なし） | なし（0 依存） |
| umimmio | 型安全 MMIO レジスタ抽象化 | なし |
| umiport | MCU/Arch/Board ポーティング（統合点） | **MCU 固有** |
| umidevice | 外部 IC ドライバ（CS43L22, WM8731 等） | なし（Transport 経由） |
| umirtm | 軽量 printf/monitor | なし |
| umibench | ベンチマーク基盤 | なし |
| umitest | テスト基盤 | なし |

### 依存関係グラフ

```
              umihal (0 deps)        umimmio (0 deps)
          ┌── Concept 定義 ──┐         │
     umiport (MCU固有)    umidevice (MCU非依存)
          └──────┬───────────┘
           platform.hh  ← board/ 内で MCU × Device を結合
                 │
           application / tests / examples
     umirtm, umibench, umitest ── 全て 0 HW 依存
```

umiport と umidevice は **同格の Hardware Driver Layer**。両者は互いを知らず、`board/platform.hh` で初めて出会う。umiport は MCU シリーズ固有、umidevice は全 MCU 共通（Mock Transport でホストテスト可能）。

---

## 3. umiport ディレクトリ構造

```
lib/umiport/
├── include/umiport/
│   ├── arm/cortex-m/          # アーキテクチャ共通（DWT, CoreDebug, NVIC）
│   ├── mcu/stm32f4/           # MCU 固有レジスタ操作（rcc, gpio, uart, i2c）
│   └── board/                 # ボード定義
│       ├── stm32f4-disco/     # platform.hh + board.hh
│       ├── stm32f4-renode/
│       ├── host/
│       └── wasm/
├── database/                  # MCU データベース（Lua）
│   ├── index.lua              # MCU 名 → ファイルパス索引
│   ├── family/stm32f4.lua     # ファミリ共通定義
│   └── mcu/stm32f4/stm32f407vg.lua
├── boards/                    # ボード定義（Lua 側）
│   ├── stm32f4-disco/board.lua
│   └── stm32f4-renode/board.lua
├── src/
│   ├── stm32f4/               # startup.cc, syscalls.cc, sections.ld
│   ├── host/write.cc          # write_bytes() → stdout
│   └── wasm/write.cc          # write_bytes() → fwrite
├── rules/
│   ├── board.lua              # umiport.board xmake ルール
│   ├── board_loader.lua       # ボード読み込み・継承解決
│   └── memory_ld.lua          # memory.ld 生成
└── xmake.lua
```

- `arm/cortex-m/` -- 全 Cortex-M 共通。MCU/ボード追加は `mcu/` と `board/` にディレクトリを足すだけ。
- `src/<family>/` -- startup/syscalls/sections.ld は同一ファミリのボード間で共有。

---

## 4. HAL Concept 設計

### 設計方針

- Concept は **Basic -> Extended -> Full** に階層化。`NOT_SUPPORTED` 逃げ道は禁止。
- sync/async/DMA は別インターフェイスとして分離（embedded-hal 1.0 準拠）。
- GPIO は InputPin/OutputPin に分離（embedded-hal + Mbed OS が独立に到達した結論）。
- Transaction が基本単位。エラーモデル: `Result<T> = std::expected<T, ErrorCode>`。

### エラー型と代表的 Concept

```cpp
namespace umi::hal {
// --- エラー型 ---
enum class ErrorCode : uint8_t {
    OK = 0, TIMEOUT, NACK, BUS_ERROR, OVERRUN, NOT_READY, INVALID_CONFIG,
};
template <typename T> using Result = std::expected<T, ErrorCode>;

// --- Codec: 階層化の模範例 ---
template <typename T>
concept CodecBasic = requires(T& c) { { c.init() } -> std::convertible_to<bool>; };
template <typename T>
concept CodecWithVolume = CodecBasic<T> && requires(T& c, int db) {
    { c.set_volume(db) } -> std::same_as<void>;
};
template <typename T>
concept AudioCodec = CodecWithVolume<T> && requires(T& c, bool m) {
    { c.power_on() } -> std::same_as<void>;
    { c.mute(m) } -> std::same_as<void>;
};

// --- GPIO: 入出力分離 ---
template <typename T>
concept GpioInput = requires(const T& pin) { { pin.is_high() } -> std::convertible_to<bool>; };
template <typename T>
concept GpioOutput = requires(T& pin) {
    { pin.set_high() } -> std::same_as<void>; { pin.set_low() } -> std::same_as<void>;
};
template <typename T>
concept GpioStatefulOutput = GpioOutput<T> && requires(T& pin) {
    { pin.is_set_high() } -> std::convertible_to<bool>; { pin.toggle() } -> std::same_as<void>;
};

// --- UART: 実行モデル分離 ---
template <typename T>
concept UartBasic = requires(T& u, const uart::Config& cfg, uint8_t byte) {
    { u.init(cfg) } -> std::same_as<Result<void>>;
    { u.write_byte(byte) } -> std::same_as<Result<void>>;
    { u.read_byte() } -> std::same_as<Result<uint8_t>>;
};
template <typename T>
concept UartAsync = UartBasic<T> && requires(T& u, std::span<const uint8_t> data) {
    { u.write_async(data) } -> std::same_as<Result<void>>;
};

// --- Transport: umidevice が使用 ---
template <typename T>
concept I2cTransport = requires(T& t, uint8_t addr, uint8_t reg,
                                std::span<const uint8_t> tx, std::span<uint8_t> rx) {
    { t.write(addr, reg, tx) } -> std::same_as<Result<void>>;
    { t.read(addr, reg, rx) } -> std::same_as<Result<void>>;
};

// --- Platform ---
template <typename T>
concept OutputDevice = requires(char c) {
    { T::init() } -> std::same_as<void>; { T::putc(c) } -> std::same_as<void>;
};
template <typename T>
concept Platform = requires {
    requires OutputDevice<typename T::Output>; { T::init() } -> std::same_as<void>;
};
template <typename T>
concept PlatformWithTimer = Platform<T> && requires { typename T::Timer; };
} // namespace umi::hal
```

---

## 5. ボード定義の二重構造

ボード定義は **Lua 側**（ビルド構成）と **C++ 側**（型定義）の二重構造を持つ。Lua はコンパイル前に作用し（フラグ, リンカスクリプト）、C++ はコンパイル時に作用する（型検証）。

| 側 | ファイル | 役割 | 消費者 |
|----|---------|------|--------|
| Lua | `boards/<name>/board.lua` | MCU 選択, クロック, ピン, デバッグ | xmake ルール |
| C++ | `board/<name>/platform.hh` | HAL Concept 充足型, Output/Timer 結合 | コンパイラ |
| C++ | `board/<name>/board.hh` | constexpr 定数（ピン, クロック, メモリ） | platform.hh |

### platform.hh の具体例（stm32f4-disco）

```cpp
#pragma once
#include <umihal/concept/platform.hh>
#include <umiport/mcu/stm32f4/uart_output.hh>
#include <umiport/mcu/stm32f4/i2c.hh>
#include <umiport/arm/cortex-m/dwt.hh>
#include <umidevice/audio/cs43l22/cs43l22.hh>
#include <umiport/board/stm32f4-disco/board.hh>

namespace umi::port {
struct Platform {
    using Output = stm32f4::UartOutput;
    using Timer  = cortex_m::DwtTimer;
    using I2C    = stm32f4::I2C1;
    using Codec  = umi::device::CS43L22Driver<I2C>;

    static constexpr const char* name() { return "stm32f4-disco"; }
    static void init() { Output::init(); }
};

static_assert(umi::hal::Platform<Platform>);
static_assert(umi::hal::PlatformWithTimer<Platform>);
static_assert(umi::hal::I2cTransport<Platform::I2C>);
} // namespace umi::port
```

### board.hh の具体例

```cpp
#pragma once
namespace umi::board {
struct Stm32f4Disco {
    static constexpr uint32_t hse_frequency = 8'000'000;
    static constexpr uint32_t system_clock  = 168'000'000;
    static constexpr uint32_t apb1_clock    = 42'000'000;
    struct Pin { static constexpr uint32_t console_tx = 9, console_rx = 10; };
    struct Memory {
        static constexpr uint32_t flash_base = 0x08000000, flash_size = 1024 * 1024;
        static constexpr uint32_t ram_base   = 0x20000000, ram_size   = 128 * 1024;
    };
};
} // namespace umi::board
```

---

## 6. ボード継承メカニズム

board.lua は `extends` で差分のみ記述できる。フォーク不要のカスタマイズ。

```lua
-- project/boards/my-custom-board/board.lua
return {
    extends = "stm32f4-disco",
    clock = { hse = 25000000 },          -- 水晶だけ変更
    pins = { audio_i2s_sck = "PB13" },   -- 1 ピンだけ変更
}
```

### deep_merge アルゴリズム

親テーブルの全フィールドを深いコピーし、子のフィールドで上書きする。テーブル同士は再帰マージ、それ以外は完全置換。

| 親の値 | 子の値 | 結果 |
|--------|--------|------|
| スカラー | スカラー | 子で上書き |
| `table` | `table` | 再帰マージ |
| `table` | 指定なし | 親を維持 |
| 任意 | `false` | 明示的に無効化 |

解決例: 親 `{ clock = { hse = 8M, sysclk = 168M } }` + 子 `{ clock = { hse = 25M } }` = `{ clock = { hse = 25M, sysclk = 168M } }`

ボード定義の検索順序:
1. `<project>/boards/<name>/board.lua` (プロジェクトローカル、最優先)
2. `umiport/boards/<name>/board.lua` (umiport 同梱)

---

## 7. MCU データベースと memory.ld 生成

### データベース構造

MCU データは umiport 内に Lua 形式で一元管理。arm-embedded パッケージとのインターフェイスは **`embedded.core`（コア名）1 つ** に集約。

```lua
-- database/family/stm32f4.lua
return {
    core = "cortex-m4f",  vendor = "st",  src_dir = "stm32f4",
    openocd_target = "stm32f4x",  flash_tool = "pyocd",
    memory = {
        FLASH = { attr = "rx",  origin = 0x08000000 },
        SRAM  = { attr = "rwx", origin = 0x20000000 },
        CCM   = { attr = "rwx", origin = 0x10000000 },
    },
}
-- database/mcu/stm32f4/stm32f407vg.lua
local family = dofile(path.join(os.scriptdir(), "../../family/stm32f4.lua"))
family.device_name = "STM32F407VG"
family.memory.FLASH.length = "1M"
family.memory.SRAM.length  = "192K"
family.memory.CCM.length   = "64K"
return family
```

### memory.ld 自動生成

Lua DB から MEMORY 定義を自動生成。任意のメモリ領域数に対応（PlatformIO の 2 リージョン制限なし）。`sections.ld`（手書き、ファミリ共通）が `INCLUDE memory.ld` で取り込む。

| 合成パターン | MEMORY | SECTIONS | 用途 |
|-------------|--------|----------|------|
| A: 全自動 | DB から生成 | ファミリ共通 | ライブラリテスト |
| B: セクション追加 | DB から生成 | 標準 + `-T extra.ld` | 中規模アプリ |
| C: 完全カスタム | アプリ所有 | アプリ所有 | kernel, bootloader |

---

## 8. 出力経路

ライブラリは HW を知らない。出力先の解決は **link-time 注入** で行う。

```cpp
// umirtm/include/umirtm/detail/write.hh -- umirtm はこのシンボルの存在のみを要求
namespace umi::rt::detail {
extern void write_bytes(std::span<const std::byte> data);
}
```

```
rt::println("Hello {}", 42)
  -> umi::rt::detail::write_bytes(data)     <-- link-time 注入点
       +-- Cortex-M: Platform::Output::putc(c)  (USART or RTT)
       +-- Host:     ::write(1, data, size)      (stdout)
       +-- WASM:     std::fwrite(data, 1, size, stdout)
```

Cortex-M 実装では `write_bytes()` + `_write()` syscall が共存。`_write()` は Cortex-M + newlib の一実装に格下げ。WASM/ESP-IDF では不要。

```cpp
// umiport/src/stm32f4/syscalls.cc -- Cortex-M 側の実装例
namespace umi::rt::detail {
void write_bytes(std::span<const std::byte> data) {
    for (auto byte : data) umi::port::Platform::Output::putc(static_cast<char>(byte));
}
}
```

---

## 9. xmake ルール設計

`on_load` で MCU データベースを解決し `embedded.core` を設定。`on_config` でボード固有設定を適用する二段構成。

```lua
rule("umiport.board")
    on_load(function(target)   -- board.lua 読込 + extends 継承解決 + MCU DB -> embedded.core 設定
    end)
    on_config(function(target) -- includedirs, startup.cc, memory.ld 生成, ldflags フィルタ
    end)
```

使用側は 1 行で完結:

```lua
target("umirtm_stm32f4_renode")
    set_kind("binary")
    add_rules("embedded", "umiport.board")
    set_values("umiport.board", "stm32f4-renode")
    add_deps("umirtm", "umiport")
    add_files("test_*.cc")
```

**ライフサイクル制約:** xmake の on_load は on_config より先に全ターゲットで完了する。`umiport.board` の on_load で設定した `embedded.core` が `embedded` ルールの on_load に遡及しない場合がある（ルール宣言順序依存）。回避策: `set_values("embedded.core", "cortex-m4f")` で順序非依存にする。ldflags フィルタ（`-T` の除去と再追加）は on_config 内で動作確認済み。

---

## 10. 論点と解決策

### 10.1 ボードデータの Single Source of Truth

**問題:** ハードウェア定数（HSE 周波数、ピン番号、メモリマップ等）が複数箇所に散在し、手動同期が必要。

**解決策:** ボード定義ファイル（board.lua）を唯一のソースとし、board.hh・memory.ld・clock_config.hh・platform.hh を全て生成する。MCU層とボード層を分離し、設定の統一と局所化を実現する。

詳細は **[04_BOARD_CONFIG.md](04_BOARD_CONFIG.md)** を参照。

### 10.2 Platform Concept 完全仕様

**問題:** 現在の `Platform` concept は `Output` + `init()` のみ。ClockTree 等の検証がコンパイル時に行われない。

**解決策:** 既に `umihal/concept/clock.hh` に `ClockSource` / `ClockTree` concept が定義済み。これを Platform に段階的に統合する。

```cpp
// 現状維持: Platform は最小限のまま
template <typename T>
concept Platform = requires {
    requires OutputDevice<typename T::Output>;
    { T::init() } -> std::same_as<void>;
};

// 段階的拡張: 既存 concept を組み合わせた上位 concept を追加
template <typename T>
concept PlatformWithClock = Platform<T> && requires {
    requires ClockTree<typename T::Clock>;
};

template <typename T>
concept PlatformFull = PlatformWithClock<T> && requires {
    typename T::Timer;
    { T::name() } -> std::convertible_to<const char*>;
};
```

platform.hh での検証:

```cpp
struct Platform {
    using Output = stm32f4::UartOutput;
    using Timer  = cortex_m::DwtTimer;
    using Clock  = stm32f4::ClockTreeImpl;  // ClockTree concept を充足
    // ...
};

static_assert(umi::hal::Platform<Platform>);           // 最小
static_assert(umi::hal::PlatformWithClock<Platform>);  // クロック検証
static_assert(umi::hal::PlatformFull<Platform>);       // フル検証
```

ホスト/WASM は `Platform` のみ充足し、ARM ボードは `PlatformFull` を充足する。Concept 階層化により既存コードを壊さずに段階的に要件を追加できる。

### 10.3 Transport Concept 詳細設計

**問題:** I2C 10-bit アドレス、SPI `flush()`、DMA 転送の扱いが未決定。

**解決策:** 既存の umimmio I2cTransport / SpiTransport 実装を分析すると、AddressType テンプレートパラメータで 8/16-bit アドレスを既にサポートしている。HAL concept もこのパターンに合わせる。

```cpp
// I2C: 7-bit は基本、10-bit は拡張 concept
template <typename T>
concept I2cTransport = requires(T& t, uint8_t addr, uint8_t reg,
                                std::span<const uint8_t> tx, std::span<uint8_t> rx) {
    { t.write(addr, reg, tx) } -> std::same_as<Result<void>>;
    { t.read(addr, reg, rx) } -> std::same_as<Result<void>>;
};

template <typename T>
concept I2cTransport10Bit = I2cTransport<T> && requires(T& t, uint16_t addr10) {
    { t.set_10bit_addressing(true) } -> std::same_as<void>;
};

// SPI: flush は SpiBus concept に追加（embedded-hal 1.0 準拠）
template <typename T>
concept SpiTransport = requires(T& t, std::span<const uint8_t> tx, std::span<uint8_t> rx) {
    { t.transfer(tx, rx) } -> std::same_as<Result<void>>;
    { t.select() } -> std::same_as<void>;
    { t.deselect() } -> std::same_as<void>;
};

template <typename T>
concept SpiBus = SpiTransport<T> && requires(T& t) {
    { t.flush() } -> std::same_as<Result<void>>;
};

// DMA: 別 concept（実行モデル分離の原則に従う）
template <typename T>
concept I2cTransportDma = I2cTransport<T> && requires(T& t, uint8_t addr, uint8_t reg,
                                                       std::span<const uint8_t> tx,
                                                       DmaCallback cb) {
    { t.write_dma(addr, reg, tx, cb) } -> std::same_as<Result<void>>;
};
```

10-bit アドレスは `I2cTransport10Bit` として分離することで、対応しない MCU で不要なメソッドの実装を強制しない。DMA は全く異なる実行コンテキスト（ISR コールバック）であるため別 concept とする。

#### HAL Transport と mmio Transport の関係

`umi::hal::I2cTransport`（concept）と `umi::mmio::I2cTransport`（class）は名前が同じだが、異なる抽象レベルに位置する:

| 名前 | 名前空間 | 種別 | 抽象レベル |
|------|---------|------|-----------|
| `I2cTransport` | `umi::hal` | concept | バスレベル: バイト列の送受信能力 |
| `I2cTransport` | `umi::mmio` | class | レジスタレベル: アドレスフレーミング + エンディアン変換 |

mmio Transport は HAL concept を **消費する** 側であり、HAL concept 準拠の型をテンプレートパラメータとして受け取る:

```
umi::hal::I2cTransport (concept)
        ↑ satisfies
umiport::Stm32I2c (実装)
        ↓ template inject
umi::mmio::I2cTransport<Stm32I2c> (レジスタR/Wブリッジ)
        ↓ template inject
umi::device::CS43L22<mmio::I2cTransport<Stm32I2c>> (デバイスドライバ)
```

**未解決の課題:**

1. **名前衝突**: 同名の `I2cTransport` が 2 つの名前空間に存在する。mmio 側を `I2cRegisterBus` 等にリネームすることで解消可能。
2. **concept 制約の欠如**: 現在の `mmio::I2cTransport<I2C>` はテンプレートパラメータ `I2C` に対して `umi::hal::I2cTransport` concept を要求していない（duck typing）。明示的な `requires` 節の追加が望ましい。
3. **シグネチャの不一致**: HAL concept は `write(addr, reg, tx)` を要求するが、mmio Transport が実際に呼ぶのは `write(addr, payload)`（reg 引数なし）。concept のシグネチャ設計を整理する必要がある。

詳細は `umimmio/docs/DESIGN.md` セクション 10 を参照。

### 10.4 デュアルコア MCU

**問題:** STM32H755（CM7 + CM4）のようなデュアルコア MCU の扱い。

**解決策:** デュアルコアはビルドターゲットとして分離し、各コアに独立した Platform を定義する。コア間共有は shared memory セクションで行う。

```cpp
// board/stm32h755-disco/platform_cm7.hh
namespace umi::port {
struct PlatformCM7 {
    using Output = stm32h7::UartOutput;
    using Timer  = cortex_m::DwtTimer;
    using Clock  = stm32h7::ClockTreeImpl;
    static constexpr const char* name() { return "stm32h755-cm7"; }
    static void init() { Output::init(); }
};
static_assert(umi::hal::PlatformFull<PlatformCM7>);
}

// board/stm32h755-disco/platform_cm4.hh
namespace umi::port {
struct PlatformCM4 {
    using Output = stm32h7::UartOutputCM4;
    using Timer  = cortex_m::DwtTimer;
    static constexpr const char* name() { return "stm32h755-cm4"; }
    static void init() { Output::init(); }
};
static_assert(umi::hal::Platform<PlatformCM4>);
}
```

```lua
-- boards/stm32h755-disco/board.lua
return {
    mcu = "stm32h755zi",
    cores = {
        cm7 = { platform = "platform_cm7.hh", entry = "_start_cm7" },
        cm4 = { platform = "platform_cm4.hh", entry = "_start_cm4" },
    },
    -- shared memory for IPC
    memory_extra = {
        SHARED = { attr = "rw", origin = 0x38000000, length = "64K" },
    },
}
```

```lua
-- xmake.lua での使用
target("app_cm7")
    add_rules("embedded", "umiport.board")
    set_values("umiport.board", "stm32h755-disco")
    set_values("umiport.core", "cm7")  -- コア選択

target("app_cm4")
    add_rules("embedded", "umiport.board")
    set_values("umiport.board", "stm32h755-disco")
    set_values("umiport.core", "cm4")
```

各コアは独立したビルドターゲット（独立した ELF）として扱う。`umiport.core` で platform.hh と startup の切り替えを行う。コア間通信は `SHARED` メモリセクションに配置するデータ構造で行い、HAL concept のスコープ外とする。

### 10.5 非 ARM アーキテクチャ

**問題:** RISC-V (ESP32-C3)、Xtensa (ESP32) は startup/linker パターンが ARM Cortex-M と根本的に異なる。

**解決策:** アーキテクチャごとにパッケージを分離し、MCU DB の `core` フィールドで対応パッケージを自動選択する。

```
xmake-repo/synthernet/packages/
├── a/arm-embedded/       # 既存: Cortex-M 用
├── r/riscv-embedded/     # 新規: RISC-V 用
└── x/xtensa-embedded/    # 新規: Xtensa 用（ESP-IDF 統合）
```

```lua
-- database/family/esp32c3.lua
return {
    core = "riscv32-imc",  -- arm-embedded ではなく riscv-embedded が処理
    vendor = "espressif",
    toolchain = "riscv-none-elf",
    src_dir = "esp32c3",
    -- ESP-IDF は独自のリンカスクリプトとスタートアップを持つ
    startup = "esp-idf",   -- umiport の startup.cc を使わない
    linker = "esp-idf",    -- umiport の memory.ld/sections.ld を使わない
}
```

```lua
-- rules/board.lua の on_load 内
local core = mcu_config.core
if core:match("^cortex") then
    target:add("packages", "arm-embedded")
elseif core:match("^riscv") then
    target:add("packages", "riscv-embedded")
elseif core:match("^xtensa") then
    target:add("packages", "xtensa-embedded")
end
target:set("values", "embedded.core", core)
```

umiport 内部の変更:

```
lib/umiport/
├── include/umiport/
│   ├── arm/cortex-m/         # 既存
│   ├── riscv/                # 新規: RISC-V 共通コード
│   └── mcu/
│       ├── stm32f4/          # 既存
│       └── esp32c3/          # 新規
├── src/
│   ├── stm32f4/              # 既存: startup.cc + syscalls.cc + sections.ld
│   ├── esp32c3/              # 新規: write.cc のみ（startup/linker は ESP-IDF 管理）
│   └── host/                 # 既存
```

重要な設計判断:
- ESP-IDF 統合の場合、startup/linker は ESP-IDF が管理する。umiport は `write_bytes()` の実装のみ提供。`_write()` syscall は不要（newlib を使わない）
- `write_bytes()` の link-time 注入パターンは全アーキテクチャで統一。これが唯一のアーキテクチャ横断インターフェイス
- umihal concept は全アーキテクチャ共通。`GpioOutput` を満たす型が ARM でも RISC-V でも同じ concept で検証される

### 10.6 ボード定義バージョニング

**問題:** umiport のボード定義フォーマットが変更された場合、`extends` で拡張しているプロジェクトローカルボードが影響を受ける。

**解決策:** board.lua に `schema_version` フィールドを導入し、互換性チェックを board_loader.lua で行う。

```lua
-- boards/stm32f4-disco/board.lua
return {
    schema_version = 1,  -- 現行フォーマット
    mcu = "stm32f407vg",
    clock = { hse = 8000000, sysclk = 168000000 },
    -- ...
}
```

```lua
-- rules/board_loader.lua
local CURRENT_SCHEMA = 1

local function load_board(umiport_dir, board_name, target)
    local board = dofile(board_path)

    -- スキーマバージョンチェック
    local version = board.schema_version or 0
    if version > CURRENT_SCHEMA then
        raise("board '%s' requires schema_version %d, but umiport supports up to %d. "
              .. "Update umiport to use this board.", board_name, version, CURRENT_SCHEMA)
    end
    if version < CURRENT_SCHEMA then
        -- 将来: マイグレーション関数を呼ぶ
        cprint("${yellow}warning: board '%s' uses schema_version %d (current: %d). "
               .. "Consider updating the board definition.${reset}", board_name, version, CURRENT_SCHEMA)
    end

    -- extends 解決
    if board.extends then
        local parent = load_board(umiport_dir, board.extends, target)
        board = deep_merge(parent, board)
    end
    return board
end
```

バージョニングポリシー:
- **schema_version 未指定 = v0**: 旧フォーマット。警告表示のみ、動作継続
- **マイナー変更** (フィールド追加): version 据え置き。新フィールドにはデフォルト値を設定
- **破壊的変更** (フィールド改名/削除): version をインクリメント。マイグレーション関数を提供
- **UMI が 1.0 に到達するまで**: schema_version = 1 のまま運用し、破壊的変更はリリースノートで通知

```lua
-- マイグレーション例（将来）
local migrations = {
    [0] = function(board)
        -- v0 -> v1: "uart_tx" を "pins.console_tx" に移動
        if board.uart_tx then
            board.pins = board.pins or {}
            board.pins.console_tx = board.uart_tx
            board.uart_tx = nil
        end
        return board
    end,
}
```
