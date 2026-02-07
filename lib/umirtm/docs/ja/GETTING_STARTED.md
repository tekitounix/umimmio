# はじめに

[ドキュメント一覧](INDEX.md) | [English](../GETTING_STARTED.md)

## 前提条件

- C++23 コンパイラ（`clang++` または `g++`）
- `xmake`

## 1. ヘッダをインクルード

```cpp
#include <umirtm/rtm.hh>       // モニター（リングバッファ）
#include <umirtm/printf.hh>    // rt::printf / rt::snprintf
#include <umirtm/print.hh>     // rt::print / rt::println（{} フォーマット）
```

## 2. 最小限の使用例

```cpp
#include <umirtm/rtm.hh>

int main() {
    rtm::init("MY_APP");          // デフォルトモニターを初期化
    rtm::log("Hello, world!\n");  // アップバッファ0に書き込み
    return 0;
}
```

## 3. Printf

```cpp
#include <umirtm/printf.hh>

char buf[64];
rt::snprintf(buf, sizeof(buf), "x = %d, y = %.2f\n", 42, 3.14);
```

## 4. Print（`{}` スタイル）

```cpp
#include <umirtm/print.hh>

rt::println("value: {}, hex: {0:x}", 255);
```

## 5. 続きを読む

- 使い方の詳細: [`USAGE.md`](USAGE.md)
- サンプルファイル: [`EXAMPLES.md`](EXAMPLES.md)
