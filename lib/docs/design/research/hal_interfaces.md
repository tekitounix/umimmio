# HAL インターフェース設計パターン

**概要:** 10 以上の主要 HAL システムを横断調査し、抽出された 6 つの根源的設計原理。組み込み HAL インターフェースの「変わらない真理」として、umihal の設計指針となる。

---

## 調査対象 HAL

| HAL | 言語 | 多態方式 | 成熟度 |
|-----|------|----------|--------|
| Rust embedded-hal 1.0 | Rust | trait (静的ディスパッチ) | ★★★★★ (1.0 安定版) |
| Mbed OS 6 | C++ | 仮想関数 (vtable) | ★★★★★ (EOL) |
| Zephyr RTOS | C | 関数ポインタテーブル (device_api) | ★★★★★ |
| CMSIS-Driver | C | Access Struct (vtable 的) | ★★★★ |
| ESP-IDF | C | ハンドルベース | ★★★★ |
| Arduino | C++ | クラス + グローバル関数 | ★★★ |
| Linux kernel | C | subsystem ops 構造体 | ★★★★★ |
| NuttX | C | VFS + ioctl | ★★★★ |

---

## 6 つの根源的設計原理

### 原理 1: I/O と設定の分離

> 初期化・ピン割り当ては HAL のスコープ外。HAL は「すでに初期化されたペリフェラルをどう使うか」だけを定義する。

- **embedded-hal**: 設定を HAL に含めない。最も忠実な実装
- **Zephyr**: DeviceTree で設定を分離
- **CMSIS-Driver**: `Initialize` / `PowerControl` / 操作 を明確に分離

**根拠:** 設定はハードウェア固有の知識（ピンマッピング、PLL 設定等）を必要とし、抽象化すると漏れが生じる。一方、I/O 操作は「何バイト送るか」「何を読むか」だけで、完全に抽象化できる。

### 原理 2: 方向は型で表現する

> GPIO の入力と出力を型レベルで区別するのは、Rust embedded-hal と Mbed OS の両方が独立に到達した結論。

- **embedded-hal**: `InputPin` / `OutputPin` 別 trait
- **Mbed**: `DigitalIn` / `DigitalOut` 別クラス
- **Zephyr/CMSIS**: ランタイムフラグ（型安全性なし）

**根拠:** コンパイル時に「出力ピンを読む」「入力ピンに書く」というバグを防げる。C++20 concepts でもこの分離は `GpioInput` / `GpioOutput` concept として表現可能。

### 原理 3: 実行モデルは別インターフェイスに分離する

> 全成熟 HAL が到達した結論: ブロッキング / 非同期 / DMA は同一インターフェイスに混ぜない。

| HAL | 分離方法 |
|-----|---------|
| embedded-hal | **別クレート** (embedded-hal / embedded-hal-async / embedded-hal-nb) |
| Zephyr | **別 API 関数群** (Polling / IRQ-driven / Async DMA) |
| CMSIS-Driver | **コールバック方式** (SignalEvent で統一) |
| Mbed | **別クラス** (BufferedSerial / UnbufferedSerial) |

**根拠:**
- ブロッキングと非同期を同一インターフェイスに混ぜると、「この関数はブロックするのか？」が型から読めない
- 全メソッドを 1 つの concept に入れると、単純なポーリング実装でも非同期メソッドのスタブが必要
- Zephyr は「IRQ-driven と Async API の同時使用禁止」を文書で警告 -- 共存は危険

**UMI での適用:** `UartBasic` / `UartAsync` のような concept 階層化。`try_` prefix で非ブロッキング操作を示す命名規則。

### 原理 4: トランザクションが基本単位

> embedded-hal 1.0 の I2C と SPI は `transaction()` を唯一の必須メソッドとし、`write()`, `read()` はデフォルト実装とした。

```rust
trait I2c: ErrorType {
    fn transaction(&mut self, address: A, operations: &mut [Operation<'_>])
        -> Result<(), Self::Error>;
    // write(), read(), write_read() は transaction() のデフォルト実装
}
```

**根拠:** I2C の `write_read` は START -> WRITE -> REPEATED START -> READ -> STOP という一連のトランザクション。これを `write()` + `read()` に分解すると、間に STOP が入ってしまう。トランザクションを基本単位にすれば、任意の操作シーケンスを正しく表現できる。

### 原理 5: エラーは常に返す、だが分類は緩くする

| HAL | エラー表現 |
|-----|----------|
| embedded-hal | `ErrorType` associated type + `ErrorKind` enum |
| CMSIS-Driver | `ARM_DRIVER_ERROR_xxx` 定数 |
| Zephyr | 負の int (POSIX errno) |
| ESP-IDF | `esp_err_t` |

**根拠:** 全メソッドが失敗し得るため、フォールブル設計は必須。ただし、エラーの詳細分類はハードウェア依存のため、粗い分類 (ErrorKind) を共通化し、詳細は実装固有にする。

### 原理 6: 初期化/破棄のライフサイクルを明確に

- **CMSIS-Driver**: `Initialize -> PowerControl(FULL) -> ... -> PowerControl(OFF) -> Uninitialize`
- **ESP-IDF**: `xxx_new -> ... -> xxx_del`
- **embedded-hal**: ライフサイクルはスコープ外（Rust の所有権システムに委ねる）
- **Zephyr**: DeviceTree で自動初期化

**根拠:** ペリフェラルのライフサイクル管理は、電源管理・リソース共有・エラーリカバリの全てに関わる。

---

## ペリフェラル別横断比較

### GPIO

| 側面 | embedded-hal | Mbed | Zephyr | CMSIS | ESP-IDF |
|------|-------------|------|--------|-------|---------|
| 入出力分離 | **別 trait** | **別クラス** | 統合 (flags) | 統合 | 統合 |
| 割り込み | なし | **別クラス** | 同一 API | なし | 別 API |
| エラー戻り値 | `Result<T,E>` | なし | `int` | `int32_t` | `esp_err_t` |

### I2C

| 側面 | embedded-hal | Mbed | Zephyr | CMSIS | ESP-IDF |
|------|-------------|------|--------|-------|---------|
| 基本操作 | **transaction()** | write/read | transfer_msg | Send/Receive | transmit/receive |
| Bus/Device 分離 | **明確** | なし | なし | なし | **あり** |
| 非同期 | 別クレート | なし | なし | コールバック | なし |

### SPI

| 側面 | embedded-hal | Mbed | Zephyr | CMSIS | ESP-IDF |
|------|-------------|------|--------|-------|---------|
| Bus/Device 分離 | **SpiBus + SpiDevice** | なし | なし | なし | Master/Slave |
| CS 管理 | Device 側 | 手動 | API 内 | 手動 | Master 管理 |
| flush | **あり** | なし | なし | なし | なし |

### UART

| 側面 | embedded-hal | Mbed | Zephyr | CMSIS |
|------|-------------|------|--------|-------|
| 抽象化 | **embedded-io** | クラス | API | USART |
| 実行モデル | 別クレート | 別クラス | **3 層 API** | コールバック |

---

## CMSIS-Driver Control() の補足

CMSIS-Driver の `Control()` 関数は ioctl のようなビットフィールドベースの設定インターフェースである。「バッチコマンド」ではなく、ビットフィールドの OR 結合で設定項目を指定する方式。

```c
// 例: USART の設定
driver->Control(ARM_USART_MODE_ASYNCHRONOUS |
                ARM_USART_DATA_BITS_8 |
                ARM_USART_PARITY_NONE |
                ARM_USART_STOP_BITS_1 |
                ARM_USART_FLOW_CONTROL_NONE,
                115200);  // baudrate
```

統一された `Control()` 関数で全設定を行うため、API は簡潔だが、ビットフィールドの意味はペリフェラル固有であり、型安全性は低い。

---

## 命名規則の比較

| 操作 | embedded-hal | Mbed | Zephyr | CMSIS |
|------|-------------|------|--------|-------|
| 送信 | `write` | `write` | `uart_poll_out` / `uart_tx` | `Send` |
| 受信 | `read` | `read` | `uart_poll_in` / `uart_rx` | `Receive` |
| 双方向 | `transfer` | -- | `spi_transceive` | `Transfer` |
| GPIO HIGH | `set_high` | `write(1)` | `gpio_pin_set(1)` | `SetOutput` |
| GPIO 読み | `is_high` | `read` | `gpio_pin_get` | `GetInput` |

推奨命名原則:
1. 動詞は操作の意味: `write`, `read`, `transfer`
2. 状態問い合わせ: `is_` + 形容詞
3. 設定: `set_` + 名詞
4. 非ブロッキング: `try_` prefix (`try_write`, `try_read`)

---

## UMI への示唆

1. **concept 階層化** を全ペリフェラルに適用。`CodecBasic -> CodecWithVolume -> AudioCodec` のパターンを GPIO、UART、I2C、SPI、Timer に横展開
2. **GPIO の入出力分離** は型安全性の根本問題。`GpioInput` / `GpioOutput` concept に分離
3. **実行モデルの分離** を `try_` 命名規則と concept 分離で実現。blocking / async / poll を型で区別
4. **I2C は transaction ベース** に設計。`write()`, `read()` はデフォルト実装
5. **初期化は HAL スコープ外** とし、C++ のコンストラクタ/デストラクタで RAII 管理
6. **巨大 concept の分割** -- 18 メソッドの `Uart` concept は embedded-hal の 1-3 メソッド設計を参考に分割

---

## 参照

- [embedded-hal 1.0](https://docs.rs/embedded-hal/1.0.0/embedded_hal/)
- [embedded-hal-async](https://docs.rs/embedded-hal-async/latest/)
- [Mbed OS 6 Drivers](https://os.mbed.com/docs/mbed-os/v6.16/apis/drivers.html)
- [Zephyr Peripherals](https://docs.zephyrproject.org/latest/hardware/peripherals/)
- [CMSIS-Driver 2.11.0](https://arm-software.github.io/CMSIS_6/latest/Driver/)
- [ESP-IDF Peripherals](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/)
- [Arduino Language Reference](https://docs.arduino.cc/language-reference/)
