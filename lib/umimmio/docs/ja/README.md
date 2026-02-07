# umimmio

[English](../../README.md) | 日本語

`umimmio` は C++23 向けの型安全、ゼロコストメモリマップド I/O ライブラリです。
レジスタマップをコンパイル時に定義し、同一の API で Direct MMIO、I2C、SPI トランスポート経由でアクセスできます。

## リリース状況

- 現在のバージョン: `0.1.0`
- 安定性: 初回リリース
- バージョニング方針: [`RELEASE.md`](../../RELEASE.md)
- 変更履歴: [`CHANGELOG.md`](../../CHANGELOG.md)

## なぜ umimmio か

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

## ビルド・テスト

```bash
xmake build test_umimmio
xmake test "test_umimmio/*"
```

## ドキュメント

- ドキュメント一覧（推奨エントリ）: [`docs/ja/INDEX.md`](INDEX.md)
- はじめに: [`docs/ja/GETTING_STARTED.md`](GETTING_STARTED.md)
- 使い方: [`docs/ja/USAGE.md`](USAGE.md)
- テストと品質ゲート: [`docs/ja/TESTING.md`](TESTING.md)
- サンプルガイド: [`docs/ja/EXAMPLES.md`](EXAMPLES.md)
- 設計ノート: [`docs/ja/DESIGN.md`](DESIGN.md)

英語版は [`docs/`](../INDEX.md) にあります。

Doxygen HTML をローカル生成:

```bash
xmake doxygen -P . -o build/doxygen .
```

## ライセンス

MIT (`LICENSE`)
