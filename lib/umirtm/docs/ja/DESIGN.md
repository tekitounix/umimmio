# umirtm 設計

[ドキュメント一覧](INDEX.md) | [English](../DESIGN.md)

## 1. ビジョン

`umirtm` は C++23 向けのヘッダオンリー Real-Time Monitor ライブラリです。

1. リングバッファレイアウトは SEGGER RTT プロトコルとバイナリ互換
2. デバッグ出力はベアメタル、WASM、ホストターゲットで修正なしに動作する
3. printf は自己完結型の組み込み実装 — libc 依存なし
4. `print()` は printf の軽量代替として `{}` プレースホルダ構文を提供する
5. ホスト側ブリッジユーティリティがリングバッファを stdout と共有メモリに接続してデスクトップテストを支援

---

## 2. 譲れない要件

### 2.1 RTT プロトコル互換性

コントロールブロックレイアウト (`rtm_control_block_t`) は SEGGER J-Link、pyOCD、OpenOCD の RTT リーダーで
発見可能でなければなりません。マジック ID 文字列、バッファディスクリプタ配列、オフセットフィールドは
RTT 仕様と一致する必要があります。

### 2.2 ヘッダオンリー

全コンポーネントは `include/umirtm/` 配下のヘッダファイルです。
静的ライブラリ、生成コード、リンク時登録は一切ありません。

### 2.3 ヒープ割り当てなし

全リングバッファストレージは `Monitor` クラステンプレート内で静的に割り当てられます。
`new`、`malloc`、`std::vector` は不使用です。

### 2.4 例外なし

write/read 操作は `noexcept` です。
バッファオーバーフロー処理は `Mode`（skip、trim、block）で制御されます。

### 2.5 依存境界

レイヤリングは厳格です:

1. `umirtm` は C++23 標準ライブラリヘッダにのみ依存（ホスト printf 用に `<unistd.h>` を追加）
2. `rtm_host.hh` は追加で `<iostream>`, `<thread>`, `<chrono>` を使用（ホスト専用）
3. `tests/` はアサーションに `umitest` を利用

依存グラフ:

```text
application    -> umirtm (rt::Monitor, rt::printf, rt::print)
umirtm/tests   -> umitest
```

### 2.6 初期化境界

`Monitor::init(id)` は write/read の前に呼ばれなければなりません。
コントロールブロックのマジック ID を設定し、全バッファオフセットをリセットします。

---

## 3. 現在のレイアウト

```text
lib/umirtm/
├── README.md
├── xmake.lua
├── docs/
│   ├── INDEX.md
│   ├── DESIGN.md
│   ├── GETTING_STARTED.md
│   ├── USAGE.md
│   ├── EXAMPLES.md
│   ├── TESTING.md
│   └── ja/
├── examples/
│   ├── minimal.cc
│   ├── printf_demo.cc
│   └── print_demo.cc
├── include/umirtm/
│   ├── rtm.hh          # Monitor クラス + ターミナルカラー
│   ├── printf.hh       # 組み込み printf/snprintf 実装
│   ├── print.hh        # {} プレースホルダ print ヘルパー
│   └── rtm_host.hh     # ホスト側ブリッジ (stdout, 共有メモリ, TCP)
└── tests/
    ├── test_main.cc
    ├── test_monitor.cc
    ├── test_printf.cc
    ├── test_print.cc
    └── xmake.lua
```

---

## 4. 将来のレイアウト

```text
lib/umirtm/
├── include/umirtm/
│   ├── rtm.hh
│   ├── printf.hh
│   ├── print.hh
│   ├── rtm_host.hh
│   └── format.hh        # 将来: コンパイル時フォーマット文字列検証
├── examples/
│   ├── minimal.cc
│   ├── printf_demo.cc
│   ├── print_demo.cc
│   └── host_bridge.cc   # 将来: ホスト側ブリッジ使用デモ
└── tests/
    ├── test_main.cc
    ├── test_*.cc
    └── xmake.lua
```

注記:

1. 公開ヘッダは `include/umirtm/` 配下に置く
2. `rtm_host.hh` はホスト専用（`<thread>`、POSIX API を使用）
3. `printf.hh` は `rtm.hh` なしでスタンドアロンの組み込み printf 代替として使用可能
4. 将来のフォーマット文字列検証は `consteval` によるコンパイル時安全性を活用

---

## 5. プログラミングモデル

### 5.1 最小フロー

必要な最小手順:

1. `rtm::init("MY_RTM")` を呼ぶ
2. `rtm::write<0>("message")` または `rtm::log<0>("message")` を呼ぶ
3. ホストデバッガが up バッファを読み取る

### 5.2 3つの出力レイヤー

**レイヤー 1: 生リングバッファ** (`rtm.hh`):

```cpp
rtm::init("MY_RTM");
rtm::log<0>("hello\n");
```

**レイヤー 2: Printf** (`printf.hh`):

```cpp
rt::printf("value = %d\n", 42);
```

**レイヤー 3: Print** (`print.hh`):

```cpp
rt::println("value = {}", 42);
```

### 5.3 Printf 設定

Printf の動作は `PrintConfig` テンプレートで制御:

```cpp
using Minimal = rt::PrintConfig<false, false, false, false, false, false, false, false>;
using Full    = rt::PrintConfig<true, true, true, true, true, true, false, true>;
```

個別に機能を無効にして組み込みターゲットのコードサイズを削減できます。

### 5.4 応用

応用的な使い方:

1. 別々のログストリーム用の複数 up/down チャンネル
2. ホストからターゲットへのコマンド用 down バッファ読み取り
3. `read_line()` による行指向入力
4. デスクトップテスト用ホスト側ブリッジ (`HostMonitor`)
5. macOS での RTT ビューア統合用共有メモリエクスポート

---

## 6. リングバッファアーキテクチャ

### 6.1 コントロールブロック

コントロールブロックの内容:

1. 16バイトのマジック ID 文字列（NUL 終端、最大15文字使用可能）
2. Up バッファディスクリプタ（ターゲット → ホスト）
3. Down バッファディスクリプタ（ホスト → ターゲット）

各ディスクリプタは名前ポインタ、データポインタ、サイズ、書き込みオフセット、読み取りオフセット、フラグを保持します。

### 6.2 メモリオーダリング

- `write_up_buffer`: データを先に書き込み、`release` フェンス、その後 `write_offset` を更新
- `read_down_buffer`: データ読み取り前に `acquire` フェンスで `write_offset` を読む
- シングルプロデューサ/シングルコンシューマモデル — mutex 不要

### 6.3 オーバーフローモード

| モード | 動作 |
|-------|------|
| `NoBlockSkip` | バッファ満杯時に書き込み全体をドロップ（デフォルト） |
| `NoBlockTrim` | 収まる分だけ書き込み、超過分を破棄 |
| `BlockIfFifoFull` | 空きが出るまでスピン |

---

## 7. Printf 仕様

### 7.1 対応フォーマット指定子

`%d`, `%i`, `%u`, `%x`, `%X`, `%o`, `%c`, `%s`, `%p`, `%f`, `%e`, `%g`, `%a`, `%%`。

オプション（設定依存）: `%b`/`%B`（バイナリ）、`%n`（ライトバック）。

### 7.2 長さ修飾子

`h`, `hh`, `l`, `ll`（`use_large` 時）、`j`, `z`, `t`, `L`。

### 7.3 出力先

- `rt::printf()` — `::write(1, ...)` で stdout に書き込み
- `rt::snprintf()` — 呼び出し側提供バッファに書き込み
- `rt::vsnprintf()` — va_list 版

---

## 8. テスト戦略

1. テストは責務ごとに分割: monitor、printf、print
2. Monitor テストは write/read セマンティクス、容量制限、バッファラッピングを検証
3. Printf テストはフォーマット指定子を期待出力文字列に対して検証
4. Print テストは `{}` プレースホルダ変換と出力を検証
5. 全テストは `xmake test` でホスト上実行
6. CI は全サポートプラットフォームでホストテストを実行

---

## 9. サンプル戦略

サンプルは学習段階を表す:

1. `minimal`: Monitor の初期化と書き込み
2. `printf_demo`: 全 printf フォーマット指定子のデモ
3. `print_demo`: `{}` プレースホルダ print/println

---

## 10. 短期改善計画

1. J-Link RTT over TCP 用の TCP サーバーを `rtm_host.hh` に実装
2. `consteval` によるコンパイル時フォーマット文字列検証を追加
3. リングバッファへのゼロコピーフォーマット用 `fmt::format_to` スタイル API を追加
4. Linux 共有メモリサポートを追加（現在 macOS のみ）

---

## 11. 設計原則

1. RTT 互換 — 既存デバッグインフラで動作
2. ヘッダオンリー — インクルードして使うだけ、ビルドステップ不要
3. 組み込み安全 — ヒープ・例外・RTTI 不使用
4. レイヤード出力 — raw → printf → print、適切なレベルを選択
5. 設定可能なフットプリント — 未使用の printf 機能を無効にしてコードサイズを節約
