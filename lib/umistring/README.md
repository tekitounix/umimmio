# umistring - 文字列ユーティリティライブラリ

組み込み向けの軽量文字列処理ライブラリ。

## 特徴

- ヘッダオンリー
- ゼロオーバーヘッド
- constexpr 対応
- ヒープ割り当てなし（リアルタイム安全性）

## 依存関係

- `umicore` - 基本型（オプション）
- `umitest` - テスト（開発時のみ）

## 主要API

- `string_view` — 文字列ビュー（組み込み std::string_view の代替）
- `to_string` — 整数→文字列変換
- `from_string` — 文字列→整数変換
- `split` — 文字列分割
- `trim` — 前後の空白削除

## クイックスタート

```cpp
#include <umistring/string.hh>
#include <umistring/convert.hh>

// 文字列ビュー
constexpr auto sv = umistring::string_view{"Hello"};

// 整数→文字列
char buf[16];
umistring::to_string(42, buf);

// 文字列分割
auto parts = umistring::split("a,b,c", ',');
```

## ビルド・テスト

```bash
xmake build umistring
xmake build test_umistring
xmake run test_umistring
```

## ライセンス

MIT
