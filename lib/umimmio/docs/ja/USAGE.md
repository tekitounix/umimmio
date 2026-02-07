# 使い方

[ドキュメント一覧](INDEX.md) | [English](../USAGE.md)

## コアコンセプト

| 型 | 用途 |
|----|------|
| `Region<addr, T>` | 固定アドレスにストレージ型Tのレジスタ。 |
| `Field<Region, offset, width>` | Region内のビットフィールド。 |
| `Value<Field, val>` | Fieldの名前付き定数。 |
| `DynamicValue<Field>` | Fieldの実行時値。 |
| `Block<addr, Regions...>` | 連続レジスタのグループ。 |

## トランスポート型

| トランスポート | 用途 |
|--------------|------|
| `DirectTransport` | メモリマップドI/O（volatileポインタアクセス）。 |
| `I2cTransport` | HAL互換I2Cペリフェラルドライバ。 |
| `SpiTransport` | HAL互換SPIペリフェラルドライバ。 |
| `BitBangI2cTransport` | GPIO経由のソフトウェアI2C。 |
| `BitBangSpiTransport` | GPIO経由のソフトウェアSPI。 |

## アクセスポリシー

フィールドにはコンパイル時アクセスポリシーがあります:

| ポリシー | `read()` | `write()` | `modify()` |
|---------|:--------:|:---------:|:----------:|
| `RW` | 可 | 可 | 可 |
| `RO` | 可 | 不可 | 不可 |
| `WO` | 不可 | 可 | 不可 |

## リード・モディファイ・ライト

```cpp
// 単一フィールドへの書き込み
transport.write(Enable{}, 1);

// フィールドの読み取り
auto val = transport.read(Mode{});

// 値の比較
bool idle = transport.is(ModeIdle{});

// フィールドのトグル
transport.flip(Enable{});
```

## レジスタマップの構造化

`Block` でレジスタをグループ化:

```cpp
using SPI_Block = umi::mmio::Block<0x4001'3000,
    CR1, CR2, SR, DR>;
```

## 静的検証

Region、Field、Valueのプロパティはすべて `constexpr`:

```cpp
static_assert(Ready::mask == 0x01);
static_assert(Mode::offset == 4);
static_assert(ModeIdle::shifted_value == 0);
```
