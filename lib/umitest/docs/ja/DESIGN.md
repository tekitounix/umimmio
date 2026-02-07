# umitest 設計

[ドキュメント一覧](INDEX.md) | [English](../DESIGN.md)

## 1. ビジョン

`umitest` は C++23 向けのマクロゼロ、ヘッダオンリーテストフレームワークです。

1. テストコードは `bool` を返す通常の C++ 関数として記述する
2. プリプロセッサマクロ不使用 — `std::source_location` が `__FILE__`/`__LINE__` を置き換える
3. 外部ビルド依存なし — インクルードして使うだけ
4. host、WASM、組み込みターゲットで修正なしに動作する
5. 出力は CI ログに適した人間可読なカラーターミナルテキスト

---

## 2. 譲れない要件

### 2.1 マクロゼロ

全てのアサーションは通常の関数呼び出しです。`ASSERT_EQ` や `TEST_CASE` マクロは存在しません。
ソース位置は `std::source_location::current()` のデフォルト引数で取得されます。

### 2.2 ヘッダオンリー

フレームワーク全体が `include/umitest/` 配下のヘッダファイルで構成されます。
静的ライブラリ、リンク時登録、コード生成は一切ありません。

### 2.3 ヒープ割り当てなし

全ての内部状態はスタックまたは静的ストレージを使用します。
動的割り当てが利用できないベアメタル環境との互換性を確保しています。

### 2.4 例外なし

アサーションはスローしません。テスト関数は `bool` を返します。
TestContext は `mark_failed()` を通じて内部的に失敗状態を追跡します。

### 2.5 依存境界

レイヤリングは厳格です:

1. `umitest` は C++23 標準ライブラリヘッダにのみ依存
2. 他の umi ライブラリには依存しない
3. 他の umi ライブラリはテスト時のみ `umitest` に依存

依存グラフ:

```text
umibench/tests -> umitest
umimmio/tests  -> umitest
umirtm/tests   -> umitest
umitest/tests  -> umitest (セルフテスト)
```

---

## 3. 現在のレイアウト

```text
lib/umitest/
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
│   ├── assertions.cc
│   └── check_style.cc
├── include/umitest/
│   ├── test.hh          # アンブレラヘッダ
│   ├── suite.hh         # Suite クラス + TestContext 実装
│   ├── context.hh       # TestContext 宣言
│   └── format.hh        # 診断出力用 format_value
└── tests/
    ├── test_main.cc
    ├── test_assertions.cc
    ├── test_format.cc
    ├── test_suite_workflow.cc
    └── xmake.lua
```

---

## 4. 将来のレイアウト

```text
lib/umitest/
├── include/umitest/
│   ├── test.hh
│   ├── suite.hh
│   ├── context.hh
│   ├── format.hh
│   └── matchers.hh       # 将来: 合成可能なマッチャー (contains, starts_with)
├── examples/
│   ├── minimal.cc
│   ├── assertions.cc
│   ├── check_style.cc
│   └── matchers.cc        # 将来: マッチャー使用デモ
└── tests/
    ├── test_main.cc
    ├── test_*.cc
    └── xmake.lua
```

注記:

1. 公開ヘッダは `include/umitest/` 配下に置く
2. 将来のマッチャーは別ヘッダでオプトイン — 最小利用時に肥大化しない
3. Suite と TestContext のみがユーザー向け型として維持される

---

## 5. プログラミングモデル

### 5.1 最小フロー

必要な最小手順:

1. `Suite` を構築
2. `TestContext&` を取り `bool` を返すテスト関数を定義
3. `suite.run("name", fn)` を呼ぶ
4. `main` から `suite.summary()` を返す

### 5.2 2つのテストスタイル

**構造化スタイル** (`TestContext` 使用):

```cpp
bool test_foo(TestContext& t) {
    t.assert_eq(1 + 1, 2);
    return true;
}

Suite s("foo");
s.run("test_foo", test_foo);
```

**インラインスタイル** (Suite 上で直接 `check_*`):

```cpp
Suite s("bar");
s.section("arithmetic");
s.check_eq(1 + 1, 2);
s.check_ne(1, 2);
return s.summary();
```

### 5.3 応用

応用的な使い方:

1. `section()` による出力内の論理グループ化
2. `check_near()` / `assert_near()` による浮動小数点比較
3. ユーザー型向けのカスタム `format_value` 特殊化
4. 独立した統計のための複数 Suite の単一テストバイナリ内使用

---

## 6. アサーションセマンティクス

### 6.1 TestContext アサーション

TestContext の全 `assert_*` メソッド:

1. `bool` を返す — アサーション成功時は `true`、失敗時は `false`
2. 失敗時に `mark_failed()` を呼び、コンテキストの失敗フラグを設定
3. 失敗時にソース位置と比較値を stdout に出力
4. throw、abort、longjmp は行わない。テスト実行は継続する

### 6.2 Suite インラインチェック

Suite の全 `check_*` メソッド:

1. `bool` を返す — アサーションと同じセマンティクス
2. `passed` または `failed` カウンタを直接インクリメント
3. TestContext は関与しない。簡易チェックに適する

### 6.3 値フォーマット

`format_value<T>` は失敗メッセージ用に値を人間可読な文字列に変換します。
対応型: 整数、浮動小数点、bool、char、const char*、std::string_view、std::nullptr_t、ポインタ型。

---

## 7. 出力モデル

### 7.1 人間可読レポート

ANSI カラーコード付きターミナル出力:

1. セクションヘッダはシアン
2. 成功結果は緑 (`OK`)
3. 失敗結果は赤 (`FAIL`) + ソース位置
4. 合格/不合格の合計を含むサマリー行

### 7.2 終了コード規約

`summary()` は全テスト合格時に `0`、いずれか失敗時に `1` を返します。
CI パイプラインおよび `xmake test` と互換です。

---

## 8. テスト戦略

1. umitest はセルフテスト: `tests/` は umitest 自身を使って動作を検証する
2. テストファイルは責務ごとに分割: アサーション、フォーマット、Suiteワークフロー
3. 全テストは `xmake test` でホスト上実行
4. テストはセマンティクスの正当性に焦点を当て、タイミングには依存しない
5. CI は全サポートプラットフォームでホストテストを実行

---

## 9. サンプル戦略

サンプルは学習段階を表す:

1. `minimal`: 最短の完全なテスト
2. `assertions`: 全アサーションメソッドのデモ
3. `check_style`: セクションとインラインチェックスタイル

---

## 10. 短期改善計画

1. 文字列/コンテナチェック用の合成可能マッチャーを追加
2. 該当する場合、読み取り専用アサーション用の compile-fail テストを追加
3. ADL カスタマイズポイントによるユーザー定義型向け `format_value` を拡張
4. ベンチマーク統合サンプルを追加（umitest + umibench 連携）

---

## 11. 設計原則

1. マクロゼロ — 全機能は通常の C++ 関数で実現
2. ヘッダオンリー — インクルードして使うだけ、ビルドステップ不要
3. 組み込み安全 — ヒープ・例外・RTTI 不使用
4. 明示的な失敗位置 — 全アサーションで `std::source_location`
5. 2スタイル、1フレームワーク — 構造化 (TestContext) とインライン (Suite checks) が共存
