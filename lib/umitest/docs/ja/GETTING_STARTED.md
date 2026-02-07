# はじめに

[ドキュメント一覧](INDEX.md) | [English](../GETTING_STARTED.md)

## 前提条件

- C++23 コンパイラ（`clang++` または `g++`）
- `xmake`

## 1. ヘッダをインクルード

```cpp
#include <umitest/test.hh>
```

## 2. 最初のテストを書く

```cpp
#include <umitest/test.hh>

int main() {
    umi::test::Suite suite("my_first_test");

    suite.run("addition works", [](umi::test::TestContext& ctx) {
        ctx.assert_eq(1 + 1, 2);
        return true;
    });

    return suite.summary();
}
```

## 3. 実行

```bash
xmake test
```

## 4. 続きを読む

- 使い方の詳細: [`USAGE.md`](USAGE.md)
- サンプルファイル: [`EXAMPLES.md`](EXAMPLES.md)
- 設計: [`DESIGN.md`](DESIGN.md)
- テスト戦略: [`TESTING.md`](TESTING.md)
