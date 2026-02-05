# umitest - マクロゼロ軽量テストフレームワーク

C++23 `std::source_location` ベース。マクロ不使用、ヘッダオンリー、組み込み対応。

## 依存関係

なし (`<cstdio>`, `<source_location>`, `<type_traits>` のみ)

## 主要API

- `Suite` — テストスイート (統計管理 + 出力)
- `TestContext` — テスト関数内で使うアサーション群

## クイックスタート

```cpp
#include <umitest/umitest.hh>
using namespace umitest;

bool test_add(TestContext& t) {
    t.assert_eq(1 + 1, 2);
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
xmake build umitest
xmake build test_umitest
xmake run test_umitest
```

## ライセンス

MIT
