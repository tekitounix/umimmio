# umirtm ドキュメント

[English](../INDEX.md)

このページは、GitHub と Doxygen の両方で使うドキュメントの入口です。

## 推奨の読む順序

1. [はじめに](GETTING_STARTED.md)
2. [使い方](USAGE.md)
3. [サンプル](EXAMPLES.md)
4. [テスト](TESTING.md)
5. [設計](DESIGN.md)

## APIリファレンスマップ

- Monitor（リングバッファトランスポート）:
  - `include/umirtm/rtm.hh` — Monitor クラス、Mode 列挙、ターミナルカラー
- Printf（組み込み printf）:
  - `include/umirtm/printf.hh` — PrintConfig, vsnprintf, snprintf, printf
- Print（`{}` プレースホルダ）:
  - `include/umirtm/print.hh` — print, println, FormatConverter
- ホストブリッジ（デスクトップユーティリティ）:
  - `include/umirtm/rtm_host.hh` — HostMonitor (stdout, 共有メモリ, TCP)

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

- ワークフローファイル: `.github/workflows/umirtm-ci.yml`
- Pull Request: ホストテスト実行
- `main` ブランチpush: CI検証
