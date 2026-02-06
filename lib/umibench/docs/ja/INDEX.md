# umibench ドキュメント

[English](../INDEX.md)

このページは、GitHub と Doxygen の両方で使うドキュメントの入口です。

## 推奨の読む順序

1. [はじめに](GETTING_STARTED.md)
2. [使い方](USAGE.md)
3. [プラットフォーム](PLATFORMS.md)
4. [サンプル](EXAMPLES.md)
5. [テスト](TESTING.md)
6. [設計](DESIGN.md)

## APIリファレンスマップ

- 公開エントリポイント: `include/umibench/bench.hh`
- コア計測API:
  - `include/umibench/core/measure.hh`
  - `include/umibench/core/runner.hh`
  - `include/umibench/core/stats.hh`
- コンセプト:
  - `include/umibench/timer/concept.hh`
  - `include/umibench/output/concept.hh`

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

- ワークフローファイル: `.github/workflows/umibench-doxygen.yml`
- Pull Request: HTML成果物をArtifactとして保存
- `main` ブランチpush: GitHub Pagesへ公開
