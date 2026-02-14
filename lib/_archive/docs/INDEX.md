# UMI Library Development Guide

このディレクトリは、UMI エコシステムのライブラリに共通する標準・ガイド・リファレンスを集約しています。

---

## For New Contributors

1. [Getting Started](guides/GETTING_STARTED.md) — 新規ライブラリ段階的作成ガイド
2. [Coding Rule](standards/CODING_RULE.md) — コードの書き方
3. [API Comment Rule](standards/API_COMMENT_RULE.md) — コメントの書き方

## Standards & Rules

- [Library Spec](standards/LIBRARY_SPEC.md) — 全ライブラリ共通の構造・規約・著作権表記
- [Coding Rule](standards/CODING_RULE.md) — C++23 コーディング規約
- [API Comment Rule](standards/API_COMMENT_RULE.md) — API コメント規約

## Development Guides

- [Getting Started](guides/GETTING_STARTED.md) — 段階的ライブラリ作成（Phase 1-4）
- [API Docs](guides/API_DOCS_GUIDE.md) — API ドキュメント生成・運用
- [Testing](guides/TESTING_GUIDE.md) — テスト戦略
- [Build](guides/BUILD_GUIDE.md) — ビルド・テスト・デプロイ
- [Release](guides/RELEASE_GUIDE.md) — リリース・パッケージ配布
- [Code Quality](guides/CODE_QUALITY_GUIDE.md) — コード品質ツール設定
- [Debugging](guides/DEBUGGING_GUIDE.md) — デバッグ手法 (pyOCD, GDB, RTT)

## Reference Implementation

- [umibench](../umibench/) — 完全準拠リファレンス（すべての標準を満たすモデル実装）

## Standard-Compliant Libraries

| Library | Phase | Description |
|---------|-------|-------------|
| [umibench](../umibench/) | Phase 4 | Cross-target microbenchmark (reference implementation) |
| [umitest](../umitest/) | Phase 3 | Zero-macro test framework |
| [umimmio](../umimmio/) | Phase 3 | MMIO register abstractions |
| [umirtm](../umirtm/) | Phase 3 | RTT-compatible debug monitor |
| [umiport](../umiport/) | WIP | Shared platform infrastructure (STM32F4 startup, linker, UART) |
