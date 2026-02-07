# umitest ドキュメント

[English](../INDEX.md)

このページは、GitHub と Doxygen の両方で使うドキュメントの入口です。

## 推奨の読む順序

1. [はじめに](GETTING_STARTED.md)
2. [使い方](USAGE.md)
3. [サンプル](EXAMPLES.md)
4. [テスト](TESTING.md)
5. [設計](DESIGN.md)

## APIリファレンスマップ

- 公開エントリポイント: `include/umitest/test.hh`
- コアコンポーネント:
  - `include/umitest/suite.hh` — Suite クラス、インラインチェック、TestContext 実装
  - `include/umitest/context.hh` — TestContext 宣言 (assert_* メソッド)
  - `include/umitest/format.hh` — 診断出力用 format_value

## ローカル生成

```bash
xmake doxygen -P . -o build/doxygen .
```

生成先:

- `build/doxygen/html/index.html`

## リリース情報

- バージョンファイル: `VERSION`
- 変更履歴: `CHANGELOG.md`
- リリース方針: `RELEASE.md`

GitHub自動化:

- ワークフローファイル: `.github/workflows/umitest-ci.yml`
- Pull Request: ホストテスト実行
- `main` ブランチpush: CI検証
