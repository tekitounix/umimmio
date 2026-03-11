# umimmio

[English](../../README.md)

> [!CAUTION]
> **実験的公開 — このリポジトリに依存しないでください。**
>
> 本ライブラリは git subtree により実験的に公開されています。
> API・構成・内容は予告なく変更される可能性があり、将来的に非公開化またはリポジトリ自体の削除もあり得ます。
> プロジェクトの依存先としてこのリポジトリを追加しないでください。
>
> また、テストの依存ライブラリの一部が公開されていないため、このリポジトリ単体ではテストの完全なビルド・実行ができません。

C++23 向けの型安全、ゼロコストメモリマップド I/O ライブラリです。
レジスタマップをコンパイル時に定義し、同一の API で Direct MMIO、I2C、SPI トランスポート経由でアクセスできます。

## 課題

従来の C/C++ ベンダーヘッダー（CMSIS、ESP-IDF、Pico SDK等）はレジスタを `uint32_t` ポインタとビットマスクマクロで公開しています。以下のようなバグがコンパイルを通過します：

```c
USART1->SR |= USART_CR1_UE;        // 間違ったレジスタ — コンパイル通過
GPIOA->ODR |= USART_CR1_UE;        // 間違ったペリフェラル — コンパイル通過
USART1->SR = 0;                     // RO ビットへの書き込み — コンパイル通過
```

## umimmio の解決策

| 安全チェック | ベンダー CMSIS | umimmio |
|-------------|:----------:|:-------:|
| クロスレジスタのビットマスク誤用 | ❌ | ✅ 型で防止 |
| クロスペリフェラルのビットマスク誤用 | ❌ | ✅ 型で防止 |
| 読み出し専用レジスタへの書き込み | ❌ | ✅ コンパイルエラー |
| 書き込み専用レジスタからの読み出し | ❌ | ✅ コンパイルエラー |
| フィールド幅のレンジチェック | ❌ | ✅ `if consteval` + ランタイムポリシー |
| 名前付き値の型安全性 | ❌ (マクロ) | ✅ NTTP `Value<F, V>` |
| W1C (Write-1-to-Clear) 安全性 | ❌ | ✅ フィールドレベル `W1C` ポリシー |

## 特徴

- **デフォルトで安全** — フィールドは名前付き `Value<>` 型のみ受け付け、raw 数値アクセスは `Numeric` トレイトでオプトイン
- **型安全レジスタ** — コンパイル時検証アクセスポリシー (RW/RO/WO/W1C)
- **ゼロコスト** — 全ディスパッチはコンパイル時に解決、vtable なし、ヒープなし
- **複数トランスポート** — 同一レジスタマップを Direct MMIO、I2C、SPI で共有
- **ポリシーベースエラーハンドリング** — `AssertOnError`、`TrapOnError`、`IgnoreError`、`CustomErrorHandler`
- **compile-fail ガード** — 15 個の compile-fail テストで不正アクセスの拒否を検証
- **RegionValue** — 1 回のバス読み出しで複数フィールドを `read(Reg{}).get(Field{})` で抽出
- **パターンマッチ** — `read_variant()` で `std::variant` + `std::visit` による網羅的分岐
- **並行性** — トランスポート操作は非アトミック、呼び出し側がプラットフォーム固有のクリティカルセクションでアクセスを直列化
- **C++23** — deducing this (CRTP 不要)、`if consteval`、`std::byteswap`

## クイックスタート

```cpp
#include <umimmio/mmio.hh>
using namespace umi::mmio;

// 階層型レジスタマップ — Device > Register > Field > Value の
// ネスト構造がデバイスの物理構造をそのまま表現する。
struct MyDevice : Device<RW> {
    static constexpr Addr base_address = 0x4000'0000;

    struct CTRL : Register<MyDevice, 0x00, 32> {
        // 1 ビットフィールド — Set/Reset 自動生成
        struct EN : Field<CTRL, 0, 1> {};

        // 2 ビットフィールド（名前付き値）— デフォルトで安全
        struct MODE : Field<CTRL, 1, 2> {
            using Output  = Value<MODE, 0b01>;
            using AltFunc = Value<MODE, 0b10>;
        };

        // 9 ビット数値フィールド — Numeric トレイトで raw value() 有効
        struct PLLN : Field<CTRL, 6, 9, Numeric> {};
    };
};

using CTRL = MyDevice::CTRL;  // 簡潔さのためエイリアス

DirectTransport<> io;
io.write(CTRL::EN::Set{});            // ビット 0 をセット
io.write(CTRL::MODE::Output{});       // 名前付き値を書き込み
io.write(CTRL::PLLN::value(336));     // raw 数値書き込み (Numeric のみ)
io.modify(CTRL::EN::Set{});           // read-modify-write (他フィールド保持)

// 読み出し
auto val = io.read(CTRL::EN{});       // → RegionValue<EN>
auto raw = val.bits();                // raw 値のエスケープハッチ
io.flip(CTRL::EN{});                  // 1 ビットフィールドのトグル

// RegionValue — 1 回のバスアクセスで複数フィールド取得
auto cfg = io.read(CTRL{});           // → RegionValue<CTRL>
auto en  = cfg.get(CTRL::EN{});       // フィールド抽出（追加バスアクセスなし）
```

## フィールド型安全性

| フィールド種別 | `value()` (書込) | `Value<>` 型 | `read()` 戻り値 |
|-----------|:---------:|:---------------:|:----------------:|
| デフォルト（安全） | ブロック | Yes | `RegionValue<F>` |
| `Numeric` トレイト | Yes（符号なしのみ） | Yes | `RegionValue<F>` |
| 1 ビット RW | — | `Set` / `Reset` 自動 | `RegionValue<F>` |
| 1 ビット W1C | — | `Clear` 自動 | `RegionValue<F>` |

`RegionValue<F>` は `Value<F,V>` および `DynamicValue<F,T>` との `==` のみサポート — 整数との直接比較はコンパイルエラー。`.bits()` で raw 値を取得。

## ビルドとテスト

```bash
xmake test
```

## パブリック API

- エントリポイント: `include/umimmio/mmio.hh`
- コア: `Device`, `Register`, `Field`, `Value`, `DynamicValue`, `RegionValue`, `Numeric`
- 操作: `read()`, `write()`, `modify()`, `is()`, `flip()`, `clear()`, `reset()`, `read_variant()`
- トランスポート: `DirectTransport`, `I2cTransport`, `SpiTransport`
- 並行性: 提供しない — 呼び出し側がプラットフォーム固有のロックを使用（設計 §9.4 参照）
- エラーポリシー: `AssertOnError`, `TrapOnError`, `IgnoreError`, `CustomErrorHandler`

## ドキュメント

- [設計 & API](DESIGN.md)
- [テスト](TESTING.md)

## ライセンス

MIT — [LICENSE](../../LICENSE) を参照
