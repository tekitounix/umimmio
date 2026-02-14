# Rust Embedded

**分類:** 言語エコシステム
**概要:** Rust の組み込み開発エコシステム。PAC/HAL/BSP の 3 層クレート分離と embedded-hal 1.0 トレイト標準が特徴。Embassy プロジェクトによる "no BSP" アプローチが近年のトレンド。

---

## アーキテクチャ概要

### PAC/HAL/BSP クレート分離

Rust Embedded エコシステムは最も明確な層分離を持つ。各層が独立したクレート (パッケージ) として管理される。

```
Target Triple (thumbv7em-none-eabihf)    ← コンパイラレベル
  └── PAC (stm32f4)                      ← SVD から自動生成
      └── HAL (stm32f4xx-hal)            ← ファミリ HAL
          └── BSP (stm32f4-discovery)    ← ボード固有
```

| 層 | 責務 | 生成方法 | 例 |
|---|------|---------|---|
| Target Triple | コンパイラターゲット指定 | rustc 組み込み / JSON 定義 | `thumbv7em-none-eabihf` |
| PAC | レジスタ定義 (型付き安全アクセス) | SVD → svd2rust で自動生成 | `stm32f4` crate |
| HAL | ペリフェラル抽象 (embedded-hal trait 実装) | 手書き | `stm32f4xx-hal` crate |
| BSP | ボード固有ピン設定・外部デバイス | 手書き | `stm32f4-discovery` crate |

ドライバクレート（例: センサドライバ）は embedded-hal トレイトにのみ依存し、特定の HAL 実装に依存しない。

---

## 主要メカニズム

### embedded-hal 1.0 トレイト

GPIO、I2C、SPI 等のペリフェラルインターフェースを trait (型クラス) として標準化。

**GPIO — 入力と出力を別トレイトに分離:**

```rust
trait InputPin: ErrorType {
    fn is_high(&mut self) -> Result<bool, Self::Error>;
    fn is_low(&mut self) -> Result<bool, Self::Error>;
}
trait OutputPin: ErrorType {
    fn set_high(&mut self) -> Result<(), Self::Error>;
    fn set_low(&mut self) -> Result<(), Self::Error>;
}
```

**I2C — transaction() が唯一の必須メソッド:**

```rust
trait I2c<A: AddressMode = SevenBitAddress>: ErrorType {
    fn transaction(&mut self, address: A, operations: &mut [Operation<'_>])
        -> Result<(), Self::Error>;
    // write(), read(), write_read() は transaction() のデフォルト実装
}
```

**SPI — Bus/Device の明確な分離:**

```rust
trait SpiBus: ErrorType {          // 排他所有権、CS 管理なし
    fn transfer(&mut self, read: &mut [u8], write: &[u8]) -> Result<(), Self::Error>;
    fn flush(&mut self) -> Result<(), Self::Error>;
}
trait SpiDevice: ErrorType {       // CS 管理付き共有可能
    fn transaction(&mut self, operations: &mut [Operation<'_, u8>]) -> Result<(), Self::Error>;
}
```

**UART — embedded-io に委譲:**

UART トレイトは embedded-hal に存在しない。バイトストリーム抽象として `embedded-io` クレートに委譲されている。

**実行モデルの完全分離:**

```
embedded-hal        blocking (同期)
embedded-hal-async  async (非同期)
embedded-hal-nb     nb (ポーリング / non-blocking)
```

3 つの独立したクレートで実行モデルを型レベルで完全に分離する。

### Embassy の "no-BSP" アプローチ

Embassy は従来の BSP クレートパターンを廃止し、HAL クレートを直接使用する。

```rust
#[embassy_executor::main]
async fn main(_spawner: embassy_executor::Spawner) {
    let p = embassy_stm32::init(Default::default());
    let mut led = Output::new(p.PD12, Level::Low, Speed::Low);
    loop {
        led.toggle();
        Timer::after_millis(500).await;
    }
}
```

理由:
- BSP クレートはボードごとに作成・保守が必要で更新が滞りやすい
- ユーザーは結局 BSP の中身を理解する必要がある（「80% Done 問題」）
- HAL クレートを直接使う方が柔軟性が高い

### memory.x + cortex-m-rt リンカフレームワーク

ユーザーが記述する唯一のリンカ関連ファイル:

```
MEMORY {
    FLASH : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM   : ORIGIN = 0x20000000, LENGTH = 128K
}
```

`cortex-m-rt` クレートの `link.x` がセクション配置を標準化。ユーザーはメモリ領域だけ書けばよい。

### probe-rs YAML チップ定義

CMSIS-Pack からデバイスデータを自動抽出し、YAML で管理する。

```yaml
name: STM32F407VGTx
memory_map:
  - Ram: { range: { start: 0x20000000, end: 0x20020000 }, cores: [main] }
  - Nvm: { range: { start: 0x08000000, end: 0x08100000 }, is_boot_memory: true }
  - Ram: { range: { start: 0x10000000, end: 0x10010000 }, cores: [main] }
```

---

## 長所と短所

| 観点 | 評価 | 詳細 |
|------|------|------|
| 層分離 | ◎ | PAC/HAL/BSP が独立クレート。責務が明確 |
| 型安全性 | ◎ | コンパイル時レジスタアクセス検証。GPIO 方向も型で保証 |
| メモリ定義 | ◎ | memory.x が極めてシンプル |
| リンカ標準化 | ◎ | cortex-m-rt が提供。手書き不要 |
| 実行モデル分離 | ◎ | blocking/async/nb が型レベルで完全分離 |
| Embassy DX | ○ | BSP 不要の直接 HAL 使用 |
| エントリーバリア | △ | 層の多さが初学者に困難 |
| C++ 連携 | △ | FFI のオーバーヘッドと複雑さ |

---

## UMI への示唆

1. **embedded-hal 1.0 のトレイト設計** は umihal の C++20 concepts 設計と直接対応する。GPIO の入出力分離、I2C の transaction 中心設計は UMI でも採用すべき
2. **実行モデルの別クレート分離** は UMI の concept 階層化 (`UartBasic` / `UartAsync`) と同じ方向。Rust が別クレートで実現するものを C++ concepts の refinement で実現する
3. **memory.x のシンプルさ** は UMI のリンカ生成でも目指すべき水準。Lua データベースから生成される memory.ld が同等の簡潔さを持つべき
4. **Embassy の "no-BSP" アプローチ** は「80% Done 問題」への解答。UMI のボード定義も最小限にとどめ、ユーザーが直接 HAL を使える設計が望ましい
5. **SVD → PAC の自動生成** パイプラインは UMI のレジスタ定義にも応用可能

---

## 参照

- [Embedded Rust Book](https://docs.rust-embedded.org/book/)
- [embedded-hal 1.0](https://docs.rs/embedded-hal/1.0.0/embedded_hal/)
- [Embassy](https://embassy.dev/)
- [probe-rs](https://probe.rs/)
- [cortex-m-rt](https://docs.rs/cortex-m-rt/latest/cortex_m_rt/)
- [Tock OS](https://www.tockos.org/)
