# バックエンド切り替えアーキテクチャ

異なるボード・MCU・アーキテクチャの実装を、**マクロ（`#ifdef`）を一切使わず**、
xmakeのインクルードディレクトリ切り替えだけで選択する仕組みを定義する。

最終目標は Daisy Pod カーネルの実装であり、
そのためにUMIプロジェクト全体のHAL / Driver / Middleware構造を根本からリファクタリングする。

## ドキュメント一覧

| # | ドキュメント | 内容 |
|---|-------------|------|
| 00 | [実装計画](design/00-implementation-plan.md) | Phase 0〜5の実装ロードマップ、リスク分析 |
| 01 | [基本原則](design/01-principles.md) | マクロ排除、同名ヘッダ、xmake制御の3ルール |
| 02 | [port/ アーキテクチャ](design/02-port-architecture.md) | 5レイヤー構成、HAL/Driver/Middleware関係、派生ボード、ディレクトリ配置 |
| 03 | [Concept 契約](design/03-concept-contracts.md) | arch/mcu/board 各レイヤーのConcept定義とstatic_assert検証 |
| 04 | [HW 分離原則](design/04-hw-separation.md) | カーネル・ミドルウェアからのHW漏出排除 |
| 05 | [移行マッピング](design/05-migration.md) | 現行ファイル→新構成の対応表と移行手順 |

## 設計要約

| 原則 | 内容 |
|------|------|
| **マクロ排除** | `#ifdef` による切り替えは一切使わない |
| **同名ヘッダ** | `<mcu/rcc.hh>` 等の同名ヘッダで実装を差し替える |
| **xmake制御** | インクルードディレクトリの設定のみで切り替え完了 |
| **5レイヤー** | common / arch / mcu / board / platform |
| **Concept契約** | 各レイヤーの契約をConceptで形式定義、static_assertで検証 |
| **局所性** | 1つの関心事 = 1つのディレクトリ。変更箇所が最小限に |
| **上位→下位のみ** | 依存方向は target → board → mcu → arch → common。逆は禁止 |
| **HW分離** | カーネル/ドライバはConceptのみ使用。HW実装はport/に局所化 |
| **port/ に統合** | backend/ + kernel/port/ の散在を解消 |
| **mcu/=HAL, board/=Driver** | HALはレジスタ操作、DriverはHALを束ねてConceptを満たす |
| **arch/=PAL** | CPUコア固有機能。HAL/Driverとは直交する軸 |
| **ミドルウェア** | umiusb, kernel等はHW非依存のプロトコルスタック。Concept経由でDriverを注入 |
| **派生ボード** | xmakeインクルードパスの積み重ねでベースボードを拡張 |
