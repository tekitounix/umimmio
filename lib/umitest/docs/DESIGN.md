# umitest 設計ドキュメント

## 目的

マクロ不使用の軽量テストフレームワークを提供する。

## 設計思想

### マクロゼロ

- プリプロセッサマクロを一切使用しない
- すべての機能は型安全なC++コードで実現
- `std::source_location` でファイル名・行番号を自動取得

### ヘッダオンリー

- ビルド設定不要
- インクルードするだけで使用可能
- リンク時のオーバーヘッドなし

## 主要コンポーネント

| コンポーネント | 説明 | ファイル |
|--------------|------|---------|
| Suite | テストスイート管理 | `umitest.hh` |
| TestContext | アサーション群 | `umitest.hh` |

## API設計

### Suite

テストスイートはテストケースの集まり。統計情報を管理し、結果を出力する。

```cpp
Suite s("test_name");
s.run("test_case_name", test_function);
return s.summary();  // 0 = 成功, 1 = 失敗
```

### TestContext

テスト関数内でアサーションを行うためのコンテキスト。

```cpp
bool test_add(TestContext& t) {
    t.assert_eq(1 + 1, 2);
    t.assert_true(condition);
    return true;  // 成功時はtrueを返す
}
```

## 依存関係

- C++23標準ライブラリのみ
  - `<cstdio>` - 出力
  - `<source_location>` - ソース位置情報
  - `<type_traits>` - 型特性

## 制約

- 例外を使用しない（組み込み環境向け）
- ヒープ割り当てを行わない

## 命名規約

| 要素 | 命名規則 | 例 |
|------|---------|-----|
| クラス | CamelCase | `Suite`, `TestContext` |
| 関数 | snake_case | `assert_eq`, `assert_true` |
| 変数 | snake_case | `passed`, `failed` |
