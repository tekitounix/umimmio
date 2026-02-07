# umitest

[English](../../README.md) | 日本語

`umitest` は C++23 向けのマクロゼロ、ヘッダオンリーテストフレームワークです。
`std::source_location` による自動ソース位置取得で、テスト関数を通常の C++ コードとして記述できます。

## リリース状況

- 現在のバージョン: `0.1.0`
- 安定性: 初回リリース
- バージョニング方針: [`RELEASE.md`](../../RELEASE.md)
- 変更履歴: [`CHANGELOG.md`](../../CHANGELOG.md)

## なぜ umitest か

- マクロゼロ — 全アサーションは通常の関数呼び出し
- ヘッダオンリー — `#include <umitest/test.hh>` だけで使用可能
- 組み込み対応 — 例外・ヒープ・RTTI 不使用
- 2つのテストスタイル — 構造化 (`TestContext`) とインライン (`Suite::check_*`)
- セルフテスト — umitest は自身をテストし、フレームワークのリグレッションを即座に検出

## クイックスタート

```cpp
#include <umitest/test.hh>
using namespace umi::test;

bool test_add(TestContext& t) {
    t.assert_eq(1 + 1, 2);
    t.assert_true(true);
    return true;
}

int main() {
    Suite s("example");
    s.run("add", test_add);
    return s.summary();
}
```

## ビルド・テスト

```bash
xmake build test_umitest
xmake test "test_umitest/*"
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
