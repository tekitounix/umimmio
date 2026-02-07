# UMI-OS Kernel ドキュメント

このディレクトリはUMI-OSカーネルの設計・実装に関するドキュメント集です。

## 学習パス

### 初めての方

1. [OVERVIEW.md](OVERVIEW.md) — カーネル全体像の把握
2. [CONTEXT_API.md](CONTEXT_API.md) — アプリ開発者向けAPI理解

### カーネル開発者

1. [ARCHITECTURE.md](ARCHITECTURE.md) — 詳細アーキテクチャ
2. [DESIGN_DECISIONS.md](DESIGN_DECISIONS.md) — 設計背景理解
3. [STM32F4_IMPLEMENTATION.md](STM32F4_IMPLEMENTATION.md) — 実装詳細

### コードベース理解

1. [LIBRARY_CONTENTS.md](LIBRARY_CONTENTS.md) — lib/umi/ の分類と依存関係

## 各ドキュメントの役割

| ドキュメント | 内容 | 対象読者 |
|-------------|------|---------|
| [OVERVIEW.md](OVERVIEW.md) | カーネル概要、4タスク構成、メモリレイアウト | 初学者 |
| [ARCHITECTURE.md](ARCHITECTURE.md) | RTOS Core、Syscall、SharedMemory、Web UI、シェル | カーネル開発者 |
| [DESIGN_DECISIONS.md](DESIGN_DECISIONS.md) | 設計判断の根拠（ADR集） | カーネル開発者 |
| [STM32F4_IMPLEMENTATION.md](STM32F4_IMPLEMENTATION.md) | 起動フロー、ISR、DMA、ペリフェラル詳細 | カーネル移植者 |
| [CONTEXT_API.md](CONTEXT_API.md) | AudioContext/Processor/Controller API | アプリ開発者 |
| [LIBRARY_CONTENTS.md](LIBRARY_CONTENTS.md) | lib/umi/の分類と依存関係 | コントリビューター |

## 関連ドキュメント

- [NOMENCLATURE.md](../NOMENCLATURE.md) — プロジェクト全体の用語体系
- [UMI_STATUS_PROTOCOL.md](../UMI_STATUS_PROTOCOL.md) — SysExプロトコル設計
- [UMIOS_STORAGE.md](../UMIOS_STORAGE.md) — ストレージ設計（将来）
- [PLAN_APP_MEMORY_AND_FAULT.md](../PLAN_APP_MEMORY_AND_FAULT.md) — メモリ/Fault設計（将来）
