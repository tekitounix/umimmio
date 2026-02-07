# はじめに

[ドキュメント一覧](INDEX.md) | [English](../GETTING_STARTED.md)

## 前提条件

- C++23 コンパイラ（`clang++` または `g++`）
- `xmake`

## 1. ヘッダをインクルード

```cpp
#include <umimmio/mmio.hh>        // オールインワン
// 個別ヘッダ:
#include <umimmio/register.hh>     // Region, Field, Value, Block
#include <umimmio/transport/i2c.hh> // I2cTransport
```

## 2. レジスタを定義

```cpp
using StatusReg = umi::mmio::Region<0x4000'0000, std::uint8_t>;
using Ready     = umi::mmio::Field<StatusReg, 0, 1>;  // ビット0
using Mode      = umi::mmio::Field<StatusReg, 4, 2>;  // ビット4-5
using ModeIdle  = umi::mmio::Value<Mode, 0>;
```

## 3. トランスポートで使用

```cpp
// ダイレクトメモリマップドI/O（ベアメタル）
umi::mmio::DirectTransport<> io;
io.write(Ready{}, 1);       // ビット0をセット
auto val = io.read(Mode{});  // ビット4-5を読み取り
```

## 4. 続きを読む

- 使い方の詳細: [`USAGE.md`](USAGE.md)
- サンプルファイル: [`EXAMPLES.md`](EXAMPLES.md)
- 設計: [`DESIGN.md`](DESIGN.md)
- テスト: [`TESTING.md`](TESTING.md)
