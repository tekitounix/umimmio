# 使い方

[ドキュメント一覧](INDEX.md) | [English](../USAGE.md)

## コアAPI

- `umi::test::Suite` — テストランナー兼統計管理。
- `umi::test::TestContext` — 構造化テスト用アサーションコンテキスト。
- `umi::test::format_value()` — snprintfベースの値フォーマッタ。

## 2つのテストスタイル

### スタイル1: 構造化テスト（run + TestContext）

```cpp
suite.run("test_name", [](umi::test::TestContext& ctx) {
    ctx.assert_eq(a, b);
    ctx.assert_true(cond, "message");
    ctx.assert_near(1.0, 1.001, 0.01);
    return true; // false = 明示的な失敗
});
```

### スタイル2: インラインチェック（コンテキスト不要）

```cpp
suite.check_eq(a, b);
suite.check_ne(a, b);
suite.check_lt(a, b);
suite.check(cond, "message");
suite.check_near(a, b, eps);
```

## 利用可能なアサーション

| メソッド | 検証内容 |
|---------|---------|
| `assert_eq` / `check_eq` | `a == b` |
| `assert_ne` / `check_ne` | `a != b` |
| `assert_lt` / `check_lt` | `a < b` |
| `assert_le` / `check_le` | `a <= b` |
| `assert_gt` / `check_gt` | `a > b` |
| `assert_ge` / `check_ge` | `a >= b` |
| `assert_near` / `check_near` | `\|a - b\| < eps` |
| `assert_true` / `check` | 真偽値条件 |

## セクション

```cpp
umi::test::Suite::section("Group Name");
```

テスト出力をグループ化するセクションヘッダを表示します。

## サマリー

```cpp
return suite.summary(); // 0 = 全成功, 1 = 失敗あり
```

## カラー出力

ヘッダをインクルードする前に `UMI_TEST_NO_COLOR` を定義すると、ANSIカラーを無効化できます。
