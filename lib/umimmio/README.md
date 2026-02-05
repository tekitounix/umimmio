# umimmio - UMI Memory-mapped I/O library

組み込み向けMMIO（Memory-mapped I/O）抽象化ライブラリ。

## 特徴

- ヘッダオンリー
- レジスタアクセスの型安全な抽象化
- ビットフィールド操作
- 複数トランスポート対応（Direct, I2C, SPI）

## 依存関係

なし

## 主要API

- `Register<T>` — 型付きレジスタアクセス
- `BitField` — ビットフィールド操作
- `MMIO` — メモリマップドI/O抽象化

## クイックスタート

```cpp
#include <umimmio/mmio.hh>

// レジスタ定義
using ControlReg = umimmio::Register<uint32_t, 0x40000000>;

// 読み書き
auto value = ControlReg::read();
ControlReg::write(value | 0x1);
```

## ビルド・テスト

```bash
xmake build umimmio
xmake build test_umimmio
xmake run test_umimmio
```

## ライセンス

MIT
