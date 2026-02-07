# umimmio

[English](../../README.md)

C++23 向けの型安全、ゼロコストメモリマップド I/O ライブラリです。
レジスタマップをコンパイル時に定義し、同一の API で Direct MMIO、I2C、SPI トランスポート経由でアクセスできます。

## 特徴

- 型安全レジスタ — コンパイル時検証アクセスポリシー (RW/RO/WO)
- ゼロコストビットフィールド操作 — 全ディスパッチはコンパイル時に解決
- 複数トランスポート — Direct MMIO、I2C、SPI、ビットバング対応
- ポリシーベースエラーハンドリング — assert、trap、ignore、カスタムコールバック
- compile-fail ガード — 不正アクセスはコンパイル時に拒否

## クイックスタート

```cpp
#include <umimmio/mmio.hh>
using namespace umi::mmio;

struct MyDevice : Device<RW> {
    static constexpr Addr base_address = 0x4000'0000;
};

using CTRL = Register<MyDevice, 0x00, 32>;
using EN   = Field<CTRL, 0, 1>;

DirectTransport<> io;
io.write(EN::Set{});          // ビット0をセット
auto val = io.read(EN{});     // ビット0を読み取り
io.flip(EN{});                // ビット0をトグル
```

## ビルドとテスト

```bash
xmake test
```

## ドキュメント

- [設計 & API](../DESIGN.md)
- [共通ガイド](../../docs/INDEX.md)

## ライセンス

MIT — [LICENSE](../../../LICENSE) を参照
