# umihal インターフェイス設計解析 — 既存HAL徹底比較

> 作成日: 2026-02-09
> 目的: umihal の設計を「不変の基盤」として確定する前に、世界の主要HALインターフェイスを俯瞰し、根源的設計原理を抽出する

---

## 目次

1. [調査対象HAL一覧](#1-調査対象hal一覧)
2. [各HALの設計思想と構造](#2-各halの設計思想と構造)
3. [横断比較表 — ペリフェラル別](#3-横断比較表--ペリフェラル別)
4. [根源的設計原理の抽出](#4-根源的設計原理の抽出)
5. [実行モデルの隠蔽 vs 分離](#5-実行モデルの隠蔽-vs-分離)
6. [現在の umihal 解析](#6-現在の-umihal-解析)
7. [課題と改善提案](#7-課題と改善提案)
8. [命名規則の比較と推奨](#8-命名規則の比較と推奨)
9. [最終判定](#9-最終判定)

---

## 1. 調査対象HAL一覧

| HAL | 言語 | 対象 | 多態方式 | 成熟度 |
|-----|------|------|----------|--------|
| **Rust embedded-hal 1.0** | Rust | Cortex-M, RISC-V, AVR 等 | trait (静的ディスパッチ) | ★★★★★ (1.0安定版) |
| **Rust embedded-hal-async** | Rust | 同上 | async trait | ★★★★ |
| **Rust embedded-hal-nb** | Rust | 同上 | nb (ポーリング) | ★★★★ |
| **Mbed OS 6** | C++ | Cortex-M | 仮想関数 (vtable) | ★★★★★ (終了済) |
| **Zephyr RTOS** | C | Cortex-M, RISC-V, x86 等 | 関数ポインタテーブル (device_api) | ★★★★★ |
| **CMSIS-Driver** | C | Cortex-M | Access Struct (vtable的) | ★★★★ |
| **ESP-IDF** | C | ESP32系 | ハンドルベース | ★★★★ |
| **Arduino** | C++ | AVR, ARM, ESP等 | クラス + グローバル関数 | ★★★ |
| **Linux kernel** | C | 全般 | subsystem ops構造体 | ★★★★★ |
| **NuttX** | C | POSIX互換RTOS | VFS + ioctl | ★★★★ |

---

## 2. 各HALの設計思想と構造

### 2.1 Rust embedded-hal 1.0 — **最も教義的**

**設計原理:**
- **I/Oのみ**: 初期化・設定はスコープ外。HALはデータ転送のみを抽象化
- **最小トレイト**: 各ペリフェラルに1-3個のメソッドのみ
- **フォールブル**: 全メソッドが `Result<T, E>` を返す
- **実行モデル別crate分離**: blocking / async / nb を完全に別crateに分離

**GPIO:**
```rust
// 入力と出力を別トレイトに分離
trait InputPin: ErrorType {
    fn is_high(&mut self) -> Result<bool, Self::Error>;
    fn is_low(&mut self) -> Result<bool, Self::Error>;
}
trait OutputPin: ErrorType {
    fn set_high(&mut self) -> Result<(), Self::Error>;
    fn set_low(&mut self) -> Result<(), Self::Error>;
}
trait StatefulOutputPin: OutputPin {
    fn is_set_high(&mut self) -> Result<bool, Self::Error>;
    fn is_set_low(&mut self) -> Result<bool, Self::Error>;
    fn toggle(&mut self) -> Result<(), Self::Error>;
}
```

**I2C:**
```rust
trait I2c<A: AddressMode = SevenBitAddress>: ErrorType {
    fn transaction(&mut self, address: A, operations: &mut [Operation<'_>])
        -> Result<(), Self::Error>;
    // write(), read(), write_read() はtransaction()のデフォルト実装
}
enum Operation<'a> {
    Read(&'a mut [u8]),
    Write(&'a [u8]),
}
```

**SPI:**
```rust
// Bus = 排他所有権、Device = CS管理付き共有可能
trait SpiBus: ErrorType {
    fn read(&mut self, words: &mut [u8]) -> Result<(), Self::Error>;
    fn write(&mut self, words: &[u8]) -> Result<(), Self::Error>;
    fn transfer(&mut self, read: &mut [u8], write: &[u8]) -> Result<(), Self::Error>;
    fn transfer_in_place(&mut self, words: &mut [u8]) -> Result<(), Self::Error>;
    fn flush(&mut self) -> Result<(), Self::Error>;
}
trait SpiDevice: ErrorType {
    fn transaction(&mut self, operations: &mut [Operation<'_, u8>]) -> Result<(), Self::Error>;
}
```

**特筆事項:**
- UARTトレイトは存在しない → `embedded-io` crateに委譲（バイトストリーム抽象）
- 割り込み設定、ピン設定は一切HALに含まない
- エラー型は `ErrorKind` を強制しない — `Error` トレイトで `kind()` メソッドを提供

### 2.2 Mbed OS 6 — **OOP古典派**

**設計原理:**
- **方向別クラス分離**: `DigitalIn`, `DigitalOut`, `DigitalInOut`
- **割り込みは別クラス**: `InterruptIn`
- **バッファリング方針別分離**: `BufferedSerial`, `UnbufferedSerial`
- **コンストラクタで初期化**: ピン指定はオブジェクト生成時

**GPIO:**
```cpp
class DigitalIn {
    DigitalIn(PinName pin);
    int read();
    void mode(PinMode pull);
    operator int();
};
class DigitalOut {
    DigitalOut(PinName pin);
    void write(int value);
    int read();  // 現在の出力状態
    operator= (int value);
};
class InterruptIn {
    InterruptIn(PinName pin);
    void rise(Callback<void()> func);
    void fall(Callback<void()> func);
};
```

**特筆事項:**
- バスクラス: `BusIn`, `BusOut` (複数ピンを1値として操作)
- SPI: Master/Slaveを別クラス
- I2C: `I2C` (Master), `I2CSlave` の2クラス
- 仮想関数ベースで動的ディスパッチ

### 2.3 Zephyr RTOS — **最も実用的な3層分離**

**設計原理:**
- **同一ペリフェラルに3つのAPI層**: Polling / Interrupt-driven / Async(DMA)
- **Devicetree統合**: ハードウェア記述はDevicetreeで分離
- **Kconfigで機能選択**: 不要なAPIはコンパイルから除外可能
- **関数ポインタテーブル (driver_api)**: C言語での多態

**UART - 3層構造:**
```c
// Layer 1: Polling (基本、常に利用可能)
int uart_poll_in(const struct device *dev, unsigned char *p_char);
void uart_poll_out(const struct device *dev, unsigned char out_char);

// Layer 2: Interrupt-driven (CONFIG_UART_INTERRUPT_DRIVEN有効時)
void uart_irq_tx_enable(const struct device *dev);
int  uart_fifo_fill(const struct device *dev, const uint8_t *tx_data, int size);
void uart_irq_callback_set(const struct device *dev, uart_irq_callback_user_data_t cb, void *user_data);

// Layer 3: Async/DMA (CONFIG_UART_ASYNC_API有効時)
int uart_tx(const struct device *dev, const uint8_t *buf, size_t len, int32_t timeout);
int uart_rx_enable(const struct device *dev, uint8_t *buf, size_t len, int32_t timeout);
int uart_callback_set(const struct device *dev, uart_callback_t callback, void *user_data);
```

**GPIO:**
```c
int gpio_pin_configure(const struct device *port, gpio_pin_t pin, gpio_flags_t flags);
int gpio_pin_set(const struct device *port, gpio_pin_t pin, int value);
int gpio_pin_get(const struct device *port, gpio_pin_t pin);
int gpio_pin_toggle(const struct device *port, gpio_pin_t pin);
int gpio_pin_interrupt_configure(const struct device *port, gpio_pin_t pin, gpio_flags_t flags);

// Devicetree版
int gpio_pin_configure_dt(const struct gpio_dt_spec *spec, gpio_flags_t extra_flags);
```

**特筆事項:**
- Interrupt-drivenとAsync APIは排他使用（同時使用不可）
- `device *` ハンドルによる多態 — 実装は `device_api` 内の関数テーブル
- 全戻り値は `int` (0: 成功, 負: エラー) — POSIX慣習

### 2.4 CMSIS-Driver — **最も構造化された関数テーブル**

**設計原理:**
- **統一ライフサイクル**: 全ドライバに共通の `Initialize` → `PowerControl` → 操作 → `PowerControl(OFF)` → `Uninitialize`
- **Access Struct**: 関数ポインタ構造体によるインスタンス化
- **コールバック (SignalEvent)**: 非同期完了通知
- **Capabilities (GetCapabilities)**: 実行時に機能問い合わせ

**共通パターン:**
```c
typedef struct {
    ARM_DRIVER_VERSION (*GetVersion)(void);
    ARM_xxx_CAPABILITIES (*GetCapabilities)(void);
    int32_t (*Initialize)(ARM_xxx_SignalEvent_t cb_event);
    int32_t (*Uninitialize)(void);
    int32_t (*PowerControl)(ARM_POWER_STATE state);
    int32_t (*Send)(const void *data, uint32_t num);
    int32_t (*Receive)(void *data, uint32_t num);
    int32_t (*Transfer)(const void *out, void *in, uint32_t num);
    int32_t (*Control)(uint32_t control, uint32_t arg);
    ARM_xxx_STATUS (*GetStatus)(void);
    uint32_t (*GetCount)(void);
} ARM_DRIVER_xxx;
```

**特筆事項:**
- `Send / Receive / Transfer` の命名統一
- 電源管理を明示的にAPIに含む（`FULL`, `LOW`, `OFF`）
- Control関数で設定を一括管理（バッチコマンド方式）
- データ転送は非ブロッキングが基本、コールバックで完了通知

### 2.5 ESP-IDF — **ハンドルベース、機能豊富**

**設計原理:**
- **ハンドルベースAPI**: `xxx_new_xxx()` でハンドルを作成、以後ハンドルで操作
- **Config構造体**: 初期化時に全設定を構造体で渡す
- **マスター/スレーブ明確分離**: I2C Master と I2C Slave は別API
- **ISRコールバック**: ISR-safeなコールバック登録

**I2C:**
```c
// ハンドル作成
i2c_master_bus_handle_t bus;
i2c_new_master_bus(&bus_cfg, &bus);

// デバイス追加
i2c_master_dev_handle_t dev;
i2c_master_bus_add_device(bus, &dev_cfg, &dev);

// 転送
i2c_master_transmit(dev, data, len, timeout);
i2c_master_receive(dev, data, len, timeout);
i2c_master_transmit_receive(dev, tx, tx_len, rx, rx_len, timeout);
```

### 2.6 Linux kernel — **最も抽象的**

**設計原理:**
- **subsystemごとのops構造体**: `struct gpio_chip`, `struct i2c_algorithm`, `struct spi_controller`
- **完全なバス/デバイスモデル分離**: バスドライバとデバイスドライバが分離
- **VFS統合**: デバイスファイルを透過的に操作
- **アプリからは `ioctl()` + `read()` / `write()`**

### 2.7 Arduino — **最も簡素**

```cpp
// GPIO - グローバル関数
pinMode(pin, INPUT/OUTPUT/INPUT_PULLUP);
digitalWrite(pin, HIGH/LOW);
int val = digitalRead(pin);

// SPI
SPI.begin();
SPI.transfer(data);

// I2C
Wire.begin();
Wire.beginTransmission(address);
Wire.write(data);
Wire.endTransmission();
```

---

## 3. 横断比較表 — ペリフェラル別

### 3.1 GPIO

| 側面 | embedded-hal | Mbed | Zephyr | CMSIS | ESP-IDF | umihal |
|------|-------------|------|--------|-------|---------|--------|
| 入出力分離 | **別trait** | **別クラス** | 統合(flags) | 統合 | 統合 | **統合concept** |
| 割り込み | 無し | **別クラス** | 同一API | 無し | 別API | **同一concept** |
| エラー戻り値 | `Result<T,E>` | 無し | `int` | `int32_t` | `esp_err_t` | `Result<T>` |
| ピン/ポート分離 | ピンのみ | 両方 | 両方 | 両方 | ピンのみ | **両方** |
| 設定 | HALスコープ外 | コンストラクタ | configure() | Control() | config構造体 | set_xxx() |

### 3.2 UART/Serial

| 側面 | embedded-hal | Mbed | Zephyr | CMSIS | ESP-IDF | umihal |
|------|-------------|------|--------|-------|---------|--------|
| 抽象化 | **embedded-io** | クラス | API | USART | API | Uart concept |
| 実行モデル | 別crate | 別クラス | **3層API** | コールバック | mixed | **混在** |
| init/deinit | 無し | ctor/dtor | devicetree | Init/Uninit | handle | init/deinit |
| バイト/バッファ | バッファ | 両方 | 両方 | バッファ | バッファ | **両方** |

### 3.3 I2C

| 側面 | embedded-hal | Mbed | Zephyr | CMSIS | ESP-IDF | umihal |
|------|-------------|------|--------|-------|---------|--------|
| 基本操作 | **transaction()** | write/read | transfer_msg | Send/Receive | transmit/receive | write/read/write_read |
| アドレスモード | trait param | 7bit | 両方 | Control | config | **無し** |
| Bus/Device分離 | **明確** | 無し | 無し | 無し | **有り** | 無し |
| 非同期 | 別crate | 無し | 無し | コールバック | 無し | **同一concept** |

### 3.4 SPI

| 側面 | embedded-hal | Mbed | Zephyr | CMSIS | ESP-IDF | umihal |
|------|-------------|------|--------|-------|---------|--------|
| Bus/Device分離 | **SpiBus + SpiDevice** | 無し | 無し | 無し | Master/Slave | **SpiTransport** |
| CS管理 | Device側 | 手動 | API内 | 手動 | Master管理 | select/deselect |
| flush | **有り** | 無し | 無し | 無し | 無し | 無し |

### 3.5 Timer/Delay

| 側面 | embedded-hal | Mbed | Zephyr | CMSIS | ESP-IDF | umihal |
|------|-------------|------|--------|-------|---------|--------|
| Delay | **trait** | wait_us | k_sleep | 無し | esp_delay | **DelayTimer** |
| PWM | **trait** | PwmOut | pwm API | 無し | ledc/mcpwm | **PwmTimer** |
| Timer | 無し | Timer/Ticker | counter API | 無し | gptimer | **Timer** |
| 分離度 | 完全分離 | 機能別クラス | API別 | — | — | **3 concept** |

---

## 4. 根源的設計原理の抽出

全HALを横断して発見された、**変わらない真理**:

### 原理1: 「I/Oは I/O、設定は設定」— 関心の分離

> **embedded-hal が最も忠実**: 初期化・ピン割り当てはHALのスコープ外。HALは「すでに初期化されたペリフェラルをどう使うか」だけを定義する。

- 📐 embedded-hal: 設定をHALに含めない
- 📐 Zephyr: Devicetreeで設定を分離
- 📐 CMSIS: `Initialize` / `PowerControl` / 操作 を明確に分離

**根拠**: 設定はハードウェア固有の知識（ピンマッピング、PLL設定等）を必要とし、抽象化すると漏れが生じる。一方、I/O操作は「何バイト送るか」「何を読むか」だけで、完全に抽象化できる。

### 原理2: 「方向は型で表現する」— 型安全性

> GPIO の入力と出力を型レベルで区別するのは、Rust embedded-hal と Mbed OS の両方が独立に到達した結論。

- 📐 embedded-hal: `InputPin` / `OutputPin` 別trait
- 📐 Mbed: `DigitalIn` / `DigitalOut` 別クラス
- 📐 Zephyr/CMSIS: ランタイムフラグ（型安全性なし）

**根拠**: コンパイル時に「出力ピンを読む」「入力ピンに書く」というバグを防げる。C++20 conceptsでもこの分離は表現可能。

### 原理3: 「実行モデルは別インターフェイスに分離する」

> **全成熟HALが到達した結論**: ブロッキング/非同期/DMAは同一インターフェイスに混ぜない。

| HAL | 分離方法 |
|-----|---------|
| embedded-hal | **別crate** (embedded-hal / embedded-hal-async / embedded-hal-nb) |
| Zephyr | **別API関数群** (Polling / IRQ-driven / Async DMA) |
| CMSIS | **コールバック方式** (SignalEventで統一) |
| Mbed | **別クラス** (BufferedSerial / UnbufferedSerial) |

**根拠**: 
- ブロッキングと非同期を同一インターフェイスに混ぜると、「この関数はブロックするのか？」が型から読めなくなる
- 全メソッドを1つのconceptに入れると、単純なポーリング実装でも非同期メソッドのスタブが必要になる
- Zephyrは「IRQ-drivenとAsync APIの同時使用禁止」を文書で警告している — つまり共存は危険

### 原理4: 「トランザクションが基本単位」

> embedded-hal 1.0 のI2CとSPIは `transaction()` を唯一の必須メソッドとし、`write()`, `read()` はデフォルト実装にした。

**根拠**: I2Cの `write_read` はSTART → WRITE → REPEATED START → READ → STOP という一連のトランザクション。これを `write()` + `read()` に分解すると、間にSTOPが入ってしまう。トランザクションを基本単位にすれば、任意の操作シーケンスを正しく表現できる。

### 原理5: 「エラーは常に返す、だが分類は緩くする」

| HAL | エラー表現 |
|-----|----------|
| embedded-hal | `ErrorType` associated type + `ErrorKind` enum |
| CMSIS | `ARM_DRIVER_ERROR_xxx` 定数 |
| Zephyr | 負のint (POSIX errno) |
| ESP-IDF | `esp_err_t` |

**根拠**: 全メソッドが失敗し得るため、フォールブル設計は必須。ただし、エラーの詳細分類はハードウェア依存のため、粗い分類（ErrorKind）を共通化し、詳細はimpl固有にする。

### 原理6: 「初期化/破棄のライフサイクルを明確に」

- CMSIS: `Initialize → PowerControl(FULL) → ... → PowerControl(OFF) → Uninitialize`
- ESP-IDF: `xxx_new → ... → xxx_del`
- embedded-hal: ライフサイクルはスコープ外（Rustの所有権システムに委ねる）
- Zephyr: Devicetreeで自動初期化

**根拠**: ペリフェラルのライフサイクル管理は、電源管理・リソース共有・エラーリカバリの全てに関わる。

---

## 5. 実行モデルの隠蔽 vs 分離

### ❌ 混在式（現在のumihal）

```cpp
concept Uart = requires(T& uart, ...) {
    // ブロッキング
    { uart.write(tx_data) } -> std::same_as<Result<void>>;
    { uart.read(rx_data) } -> std::same_as<Result<std::size_t>>;
    // タイムアウト付き
    { uart.write_with_timeout(tx_data, timeout_ms) } -> std::same_as<Result<std::size_t>>;
    // 非同期
    { uart.write_async(tx_data, tx_cb) } -> std::same_as<Result<void>>;
    { uart.read_async(rx_data, rx_cb) } -> std::same_as<Result<void>>;
};
```

**問題点:**
- ポーリング実装でも非同期メソッドの実装が必要
- 「NOT_SUPPORTEDを返してもよい」は型安全性を損なう
- ドライバ作者が「このUartは非同期をサポートするか？」を型で判別できない

### ✅ 分離式（推奨）

**方式A: Rust方式 — 別concept群に完全分離**
```cpp
// 基本（最小限）
concept UartBasic = requires(T& u) {
    { u.write_byte(byte) } -> same_as<Result<void>>;
    { u.read_byte() } -> same_as<Result<uint8_t>>;
};

// バッファ（ブロッキング）
concept UartBuffered = UartBasic<T> && requires(T& u) {
    { u.write(data) } -> same_as<Result<void>>;
    { u.read(buf) } -> same_as<Result<size_t>>;
};

// 非同期
concept UartAsync = UartBasic<T> && requires(T& u) {
    { u.write_async(data, callback) } -> same_as<Result<void>>;
};
```

**方式B: Zephyr方式 — 名前空間 / 条件付きconcept**
```cpp
namespace uart::polling { concept Uart = ...; }
namespace uart::async   { concept Uart = ...; }
```

**方式C: CMSIS方式 — Capabilitiesで実行時判定**
```cpp
concept UartBase = requires(T& u) {
    { u.capabilities() } -> same_as<UartCapabilities>;
    // ... 統合APIだが、capabilitiesで非対応機能を明示
};
```

**推奨: 方式A（concept階層化）**
- C++20 conceptsの refinement（制約の精緻化）は型安全
- ドライバ作者は `UartBasic` のみを要求できる
- 既にumihalの `concept/codec.hh` で `CodecBasic` → `CodecWithVolume` → `AudioCodec` の階層を使用中 — これが正しいパターン

---

## 6. 現在の umihal 解析

### 6.1 構造の整理

```
umihal/
├── include/umihal/
│   ├── result.hh          # Result<T> = std::expected<T, ErrorCode>
│   ├── board.hh           # BoardSpec, McuInit
│   ├── arch.hh            # CacheOps, FpuOps, ContextSwitch, ArchTraits
│   ├── fault.hh           # FaultReport
│   ├── interrupt.hh       # InterruptController, CriticalSection
│   ├── gpio.hh            # GpioPin, GpioPort
│   ├── uart.hh            # Uart (巨大 — ブロッキング+非同期混在)
│   ├── i2c.hh             # I2cMaster
│   ├── i2s.hh             # I2sMaster
│   ├── timer.hh           # Timer, DelayTimer, PwmTimer
│   ├── audio.hh           # AudioDevice, CallbackAudioDevice, BlockingAudioDevice
│   ├── codec.hh           # → concept/codec.hh へ転送
│   └── concept/
│       ├── codec.hh       # CodecBasic → CodecWithVolume → AudioCodec (★模範的)
│       ├── clock.hh       # ClockSource, ClockTree
│       ├── platform.hh    # OutputDevice, Platform, PlatformWithTimer
│       ├── transport.hh   # I2cTransport, SpiTransport
│       └── uart.hh        # UartBasic, UartAsync (★模範的)
```

### 6.2 良い点 ✅

1. **C++20 concepts による静的多態**: vtableオーバーヘッドゼロ — embedded-hal と同等の設計判断
2. **`Result<T>` = `std::expected<T, ErrorCode>`**: モダンなエラーハンドリング
3. **Codec階層 (`CodecBasic` → `CodecWithVolume` → `AudioCodec`)**: 能力に応じた段階的concept refinement — 最も美しい設計パターン
4. **Audio の3分割 (`AudioDevice` / `CallbackAudioDevice` / `BlockingAudioDevice`)**: 実行モデルの概念的分離を正しく実施
5. **Transport概念 (`I2cTransport`, `SpiTransport`)**: ドライバ視点のバストランスポート抽象 — embedded-halの driver-oriented 思想に一致
6. **`concept/uart.hh` の `UartBasic` / `UartAsync`**: 段階的concept refinement を実践済み
7. **`CriticalSection` RAII**: 割り込み管理の安全パターン
8. **ヘッダオンリーライブラリ**: 依存ゼロ、コンパイルタイム検証

### 6.3 問題点と懸念 ⚠️

#### 問題1: 二重定義 — `uart.hh` と `concept/uart.hh` の役割重複

`uart.hh` に巨大な `Uart` concept（ブロッキング+タイムアウト+非同期を全部含む）が存在し、同時に `concept/uart.hh` に正しく階層化された `UartBasic` / `UartAsync` がある。

**判定**: `concept/uart.hh` が正解。`uart.hh` の巨大concept は廃止すべき。

#### 問題2: 一部conceptが「巨大すぎる」— 1 concept に全機能を詰め込み

- **`Uart`** (uart.hh): 18メソッド — init/deinit/write_byte/read_byte/write/read/write_with_timeout/read_with_timeout/write_async/read_async/is_readable/is_writable/flush_tx/flush_rx/get_error/clear_error
- **`Timer`**: 18メソッド — start/stop/reset/set_period/set_mode/set_callback/get_counter/set_counter/set_clock_source/set_prescaler/set_direction/enable_capture/disable_capture/get_capture_value/is_running/get_error
- **`GpioPin`**: 9メソッド（割り込みを含む）

**対比**: embedded-hal の `I2c` は1メソッド (`transaction`)。`InputPin` は2メソッド。

**判定**: 巨大conceptは実装負荷が高く、「NOT_SUPPORTEDを返してもよい」という逃げ道は型安全性の欠陥。

#### 問題3: GPIO — 入出力が未分離

`GpioPin` が `set_direction(Direction)` + `write(State)` + `read()` を全て含む。

**判定**: embedded-hal, Mbed の両方が「入力と出力は別型」に到達している。`GpioInput` / `GpioOutput` に分離すべき。

#### 問題4: I2C — トランザクション概念の欠如

`I2cMaster` は `write()`, `read()`, `write_read()` を個別メソッドとして持つ。

**判定**: embedded-hal のように `transaction(address, Operation[])` を基本にし、`write/read/write_read` をデフォルト実装にするのが正しい。これにより任意の複合トランザクションを表現できる。

#### 問題5: SPI concept の不在

`SpiTransport` は存在するが、SPIバス自体のconcept（`SpiBus` 相当）がない。

**判定**: `SpiTransport` はドライバ向け視点で正しいが、HALとしてのSPIバスconcept （`transfer`, `flush` 等）も必要。

#### 問題6: `concept/` への移行が不完全

古い巨大conceptファイル（`uart.hh`, `gpio.hh` 等）と、新しい階層的concept（`concept/codec.hh`, `concept/uart.hh`）が混在。

**判定**: `concept/` ディレクトリの設計パターンが正解。全conceptを `concept/` に移行し、旧ファイルはdeprecated転送ヘッダにする。

#### 問題7: 初期化/ライフサイクルの一貫性欠如

- `I2cMaster`: `init(speed)` / `deinit()`
- `I2sMaster`: `init(config)` / `deinit()`
- `Uart`: `init(config)` / `deinit()`
- `AudioDevice`: `configure(config)`（deinit無し）
- `Timer`: init無し（set_xxx で設定）
- `GpioPin`: init無し（set_direction で設定）
- `CodecBasic`: `init()` → bool 

**判定**: 初期化パターンが統一されていない。CMSIS-Driverのように `init` / `deinit` を統一するか、embedded-halのように明示的に「HALスコープ外」と宣言するか、どちらかを選ぶべき。

#### 問題8: 日本語コメント

HAL定義内のコメントが日本語。ライブラリのインターフェイス定義は英語にすべき（Doxygen互換性、コントリビュータアクセス）。

---

## 7. 課題と改善提案

### 提案1: concept階層の全面統一

`concept/codec.hh` のパターンを全ペリフェラルに適用:

```
concept/
├── error.hh            # ErrorCode, Result<T>
├── gpio.hh             # GpioInput, GpioOutput, GpioInterruptible
├── uart.hh             # UartBasic, UartBuffered, UartAsync
├── i2c.hh              # I2cBasic, I2cTransactional, I2cAsync
├── spi.hh              # SpiBus, SpiDevice, SpiAsync
├── i2s.hh              # I2sBasic, I2sContinuous
├── timer.hh            # TimerBasic, TimerCapture, PwmOutput, Delay
├── codec.hh            # CodecBasic, CodecWithVolume, AudioCodec (現状維持)
├── clock.hh            # ClockSource, ClockTree (現状維持)
├── platform.hh         # OutputDevice, Platform (現状維持)
├── transport.hh        # I2cTransport, SpiTransport (現状維持)
├── interrupt.hh        # InterruptController (現状維持)
├── arch.hh             # CacheOps, FpuOps, ArchTraits (現状維持)
└── audio.hh            # AudioStream, AudioCallback, AudioBlocking
```

### 提案2: GPIO分離

```cpp
/// 入力ピン — 読み取り専用
template <typename T>
concept GpioInput = requires(const T& pin) {
    { pin.is_high() } -> std::convertible_to<bool>;
    { pin.is_low() } -> std::convertible_to<bool>;
};

/// 出力ピン — 書き込み可能
template <typename T>
concept GpioOutput = requires(T& pin) {
    { pin.set_high() } -> std::same_as<void>;
    { pin.set_low() } -> std::same_as<void>;
};

/// 出力状態確認可能な出力ピン
template <typename T>
concept GpioStatefulOutput = GpioOutput<T> && requires(T& pin) {
    { pin.is_set_high() } -> std::convertible_to<bool>;
    { pin.toggle() } -> std::same_as<void>;
};

/// 割り込み対応入力ピン
template <typename T>
concept GpioInterruptible = GpioInput<T> && requires(T& pin, ...) {
    { pin.set_interrupt(trigger, callback) } -> std::same_as<Result<void>>;
    { pin.enable_interrupt() } -> std::same_as<Result<void>>;
    { pin.disable_interrupt() } -> std::same_as<Result<void>>;
};
```

### 提案3: I2C トランザクション化

```cpp
enum class I2cOperation { WRITE, READ };

struct I2cTransfer {
    I2cOperation op;
    std::span<std::uint8_t> data;  // readならmutable, writeならconst
};

template <typename T>
concept I2cBasic = requires(T& i2c, uint16_t addr,
                            std::span<const uint8_t> tx,
                            std::span<uint8_t> rx) {
    { i2c.write(addr, tx) } -> std::same_as<Result<void>>;
    { i2c.read(addr, rx) } -> std::same_as<Result<void>>;
    { i2c.write_read(addr, tx, rx) } -> std::same_as<Result<void>>;
};

// 注: C++ではRustのOperation enumのようなtransaction()メソッドは
// ライフタイム管理の複雑さから、write/read/write_read の3メソッドが
// 実用的にはより適切。
```

### 提案4: Timer の分割

```cpp
/// 基本タイマー（周期的/ワンショット）
template <typename T>
concept TimerBasic = requires(T& t, uint32_t period_us, timer::Callback cb) {
    { t.start() } -> std::same_as<Result<void>>;
    { t.stop() } -> std::same_as<Result<void>>;
    { t.set_period_us(period_us) } -> std::same_as<Result<void>>;
    { t.set_callback(cb) } -> std::same_as<Result<void>>;
    { t.is_running() } -> std::convertible_to<bool>;
};

/// キャプチャ対応タイマー
template <typename T>
concept TimerCapture = TimerBasic<T> && requires(T& t, ...) {
    { t.enable_capture(event, cb) } -> std::same_as<Result<void>>;
    { t.get_capture_value() } -> std::same_as<Result<uint32_t>>;
};

/// PWM出力
template <typename T>
concept PwmOutput = requires(T& t, uint32_t freq, uint8_t duty) {
    { t.set_frequency(freq) } -> std::same_as<Result<void>>;
    { t.set_duty_cycle(duty) } -> std::same_as<Result<void>>;
    { t.start() } -> std::same_as<Result<void>>;
    { t.stop() } -> std::same_as<Result<void>>;
};

/// ブロッキング遅延（現状維持、これは良い）
template <typename T>
concept Delay = requires(T& d, uint32_t us, uint32_t ms) {
    { d.delay_us(us) } -> std::same_as<void>;
    { d.delay_ms(ms) } -> std::same_as<void>;
};
```

### 提案5: 初期化方針の明確化

**推奨**: embedded-hal方式 — **初期化はHALスコープ外**

```cpp
// conceptは「すでに初期化済みの」デバイスの操作だけを定義する
// init/deinit は concept に含めない
// → ボード固有のコンストラクタ/ファクトリで初期化する
// → RAII で deinit する
```

**理由**:
1. 初期化パラメータはペリフェラル毎に全く異なる（UARTのbaudrate, I2Cの速度, SPIのモード...）
2. ピン割り当ては完全にハードウェア固有
3. C++では コンストラクタ / デストラクタで RAII 的に管理する方が自然
4. conceptに `init()` を含めると、「2回initされた場合」「deinitされた後にwriteされた場合」の状態管理が型安全でなくなる

---

## 8. 命名規則の比較と推奨

### 各HALの動詞選択

| 操作 | embedded-hal | Mbed | Zephyr | CMSIS | umihal |
|------|-------------|------|--------|-------|--------|
| 送信 | `write` | `write` | `uart_poll_out` / `uart_tx` | `Send` | `write` |
| 受信 | `read` | `read` | `uart_poll_in` / `uart_rx` | `Receive` | `read` |
| 双方向 | `transfer` | — | `spi_transceive` | `Transfer` | `transfer` |
| GPIO HIGH | `set_high` | `write(1)` | `gpio_pin_set(1)` | `SetOutput` | `write(HIGH)` |
| GPIO LOW | `set_low` | `write(0)` | `gpio_pin_set(0)` | `SetOutput` | `write(LOW)` |
| GPIO 読み | `is_high` | `read` | `gpio_pin_get` | `GetInput` | `read` |
| 連続I2Sストリーミング | — | — | `i2s_trigger(START)` | — | `start_continuous_transmit` |

### 推奨命名原則

1. **動詞は操作の意味を表す**: `write`, `read`, `transfer` — POSIX / embedded-hal 共通
2. **状態問い合わせは `is_` + 形容詞**: `is_busy()`, `is_running()`, `is_high()`
3. **設定は `set_` + 名詞**: `set_frequency()`, `set_callback()`
4. **取得は引数なし名詞、または `get_` + 名詞**: `frequency()` or `get_frequency()`
5. **ブール戻り値の関数名は疑問文にしない**: `is_high()` ✓, `is_pin_high()` ✗

---

## 9. 最終判定

### 現在の umihal の評価

| 側面 | 評価 | 補足 |
|------|------|------|
| **全体アーキテクチャ** | ★★★★☆ | concepts + Result<T> は世界最高水準の判断 |
| **concept粒度** | ★★☆☆☆ | 巨大conceptが多い。concept/配下の階層型が正解 |
| **GPIO設計** | ★★☆☆☆ | 入出力未分離は明確な設計ミス |
| **UART設計** | ★★★☆☆ | concept/uart.hh は良い。旧uart.hh は廃止すべき |
| **I2C設計** | ★★★☆☆ | 基本は正しいが、非同期混入が問題 |
| **I2S設計** | ★★★☆☆ | 連続ストリーミング概念は良い。非同期分離が必要 |
| **Codec設計** | ★★★★★ | 模範的。この階層パターンを全体に展開すべき |
| **Transport設計** | ★★★★★ | ドライバ向け抽象として完璧 |
| **Timer設計** | ★★☆☆☆ | 巨大すぎる。Timer/PWM/Delay は良い分離だがTimerが肥大 |
| **Audio設計** | ★★★★☆ | 3概念分離は正しい。名前改善の余地あり |
| **Clock設計** | ★★★★☆ | static methodsベースで軽量。良い |
| **Platform設計** | ★★★★☆ | OutputDevice + Platform の階層は正しい |
| **エラー型** | ★★★★☆ | std::expected ベースは最適。ErrorCode はやや少ない |
| **命名規則** | ★★★☆☆ | 概ね良いが不統一箇所あり |
| **ライフサイクル** | ★★☆☆☆ | init/deinit の有無が不統一 |
| **コメント言語** | ★★☆☆☆ | 日本語混在 |

### 結論: 骨格は正しい、精緻化が必要

umihal の **根本的な設計判断** — C++20 concepts による静的多態、`Result<T>`、ヘッダオンリー — は正しく、embedded-hal 1.0 と同等の設計品質に達している。

しかし `concept/` ディレクトリ内の新設計パターンと、旧来のフラットなconcept定義が混在しており、**concept/ が暗黙の「正解」であるにもかかわらず、全面展開されていない**。

**最優先の改善:**

1. **GPIO の入出力分離** — 型安全性の根本問題
2. **全conceptを `concept/` に移行し、階層化する** — Codec パターンの横展開
3. **実行モデル（sync/async）の分離** — `UartBasic` / `UartAsync` パターンの全面適用
4. **初期化の方針統一** — HALスコープ外とするか、統一パターンを決めるか
5. **巨大concept の分割** — Timer, Uart, GPIO それぞれを段階的concept refinement に

これらは破壊的変更になるため、今（安定化前）が最後の機会である。

---

## 付録 A: 参照文献

| 資料 | URL |
|------|-----|
| Rust embedded-hal 1.0 | https://docs.rs/embedded-hal/1.0.0/embedded_hal/ |
| Rust embedded-hal-async | https://docs.rs/embedded-hal-async/latest/ |
| Rust embedded-hal設計原理 | https://github.com/rust-embedded/embedded-hal/issues |
| Mbed OS 6 Drivers | https://os.mbed.com/docs/mbed-os/v6.16/apis/drivers.html |
| Zephyr RTOS Peripherals | https://docs.zephyrproject.org/latest/hardware/peripherals/ |
| Zephyr GPIO | https://docs.zephyrproject.org/latest/hardware/peripherals/gpio.html |
| Zephyr UART 3層設計 | https://docs.zephyrproject.org/latest/hardware/peripherals/uart.html |
| CMSIS-Driver 2.11.0 | https://arm-software.github.io/CMSIS_6/latest/Driver/ |
| CMSIS Theory of Operation | https://arm-software.github.io/CMSIS_6/latest/Driver/theoryOperation.html |
| ESP-IDF Peripherals | https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/ |
| Arduino Language Reference | https://docs.arduino.cc/language-reference/ |
