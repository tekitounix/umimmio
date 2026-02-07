# 使い方

[ドキュメント一覧](INDEX.md) | [English](../USAGE.md)

## モニターAPI（`rtm.hh`）

`rtm` は `rt::Monitor<>` のデフォルトパラメータ版の型エイリアスです。

| メソッド | 説明 |
|---------|------|
| `rtm::init(id)` | コントロールブロックID文字列で初期化。 |
| `rtm::write(str)` | アップバッファに文字列を書き込み。書き込みバイト数を返す。 |
| `rtm::log(str)` | 文字列を書き込み、戻り値を破棄。 |
| `rtm::read(span)` | ダウンバッファから読み取り。読み取りバイト数を返す。 |
| `rtm::read_byte()` | 1バイト読み取り（空の場合 -1）。 |
| `rtm::read_line(buf, len)` | ダウンバッファから1行読み取り。 |
| `rtm::get_available()` | アップバッファの保留バイト数。 |
| `rtm::get_free_space()` | アップバッファの空きバイト数。 |

### カスタム設定

```cpp
using MyMonitor = rt::Monitor<
    2,    // UpBuffers
    1,    // DownBuffers
    4096, // UpBufferSize
    64,   // DownBufferSize
    rt::Mode::NoBlockTrim  // オーバーフローモード
>;
```

### バッファモード

| モード | 動作 |
|--------|------|
| `NoBlockSkip` | バッファが満杯の場合、書き込み全体を破棄。 |
| `NoBlockTrim` | 収まる分だけ書き込み、超過分を破棄。 |
| `BlockIfFifoFull` | 空きができるまでブロック。 |

## Printf API（`printf.hh`）

| 関数 | 説明 |
|------|------|
| `rt::snprintf(buf, sz, fmt, ...)` | バッファにフォーマット。 |
| `rt::vsnprintf(buf, sz, fmt, va)` | va_list版。 |
| `rt::printf(fmt, ...)` | stdoutにフォーマット。 |

### 対応フォーマット指定子

`%d`, `%u`, `%x`, `%X`, `%o`, `%c`, `%s`, `%p`, `%f`, `%e`, `%g`, `%%`

フラグ: `0`, `-`, `+`, ` `, `#`、幅、精度。

### コンフィグレーション

```cpp
rt::snprintf<rt::MinimalConfig>(buf, sz, "%d", 42); // 最小フットプリント
rt::snprintf<rt::FullConfig>(buf, sz, "%lld", 42LL); // フルC99サポート
```

## Print API（`print.hh`）

| 関数 | 説明 |
|------|------|
| `rt::print(fmt, args...)` | `{}` プレースホルダでstdoutに出力。 |
| `rt::println(fmt, args...)` | 同上 + 改行。 |
| `rt::println()` | 改行のみ。 |

### プレースホルダ構文

- `{}` — 型に応じたフォーマット指定子を自動検出。
- `{0:x}` — 明示的なフォーマット指定。
- `{{` / `}}` — リテラルブレース。

## ターミナル色（`rtm.hh`）

```cpp
rt::terminal::text::red      // "\x1B[2;31m"
rt::terminal::text::green    // "\x1B[2;32m"
rt::terminal::reset          // "\x1B[0m"
```
