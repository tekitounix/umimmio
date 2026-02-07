# umirtm

[English](../../README.md) | 日本語

`umirtm` は C++23 向けのヘッダオンリー Real-Time Monitor ライブラリです。
SEGGER RTT 互換リングバッファ、組み込み printf、`{}` プレースホルダ print を提供します。全てヒープ割り当て不要です。

## リリース状況

- 現在のバージョン: `0.1.0`
- 安定性: 初回リリース
- バージョニング方針: [`RELEASE.md`](../../RELEASE.md)
- 変更履歴: [`CHANGELOG.md`](../../CHANGELOG.md)

## なぜ umirtm か

- RTT 互換 — 既存 RTT ビューア (J-Link, pyOCD, OpenOCD) で動作
- 3つの出力レイヤー — 生リングバッファ、printf、`{}` フォーマット print
- 軽量 printf — ヒープ不使用、コードサイズ制御のための設定可能な機能セット
- ヘッダオンリー — ビルド依存なし
- ホストテスト可能 — ユニットテストと共有メモリエクスポート用ホスト側ブリッジ付き

## クイックスタート

```cpp
#include <umirtm/rtm.hh>
#include <umirtm/print.hh>

int main() {
    rtm::init("MY_RTM");
    rtm::log<0>("hello\n");
    rt::println("value = {}", 42);
    return 0;
}
```

## 公開ヘッダ

- `umirtm/rtm.hh` — RTT モニターコア (Monitor, Mode, ターミナルカラー)
- `umirtm/printf.hh` — 軽量 printf/snprintf (PrintConfig, フォーマットエンジン)
- `umirtm/print.hh` — `{}` フォーマット print/println ヘルパー
- `umirtm/rtm_host.hh` — ホスト側ブリッジ (stdout, 共有メモリ, TCP)

## ビルド・テスト

```bash
xmake build test_umirtm
xmake test "test_umirtm/*"
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
