# UMI ライブラリ構成リファクタリング提案

本文書は `lib/` 以下のライブラリ構成をフラット化し、依存関係を明確化するためのリファクタリング計画を定義する。

---

## 背景と目的

### 現状の問題

1. **umios が巨大すぎる** — core/kernel/backend/adapter/crypto/app/platform が混在
2. **責務の境界が曖昧** — Application層とOS層のコードが同一ライブラリに存在
3. **循環依存のリスク** — 内部サブディレクトリ間の依存が複雑

### 目標

- **フラットなフォルダ構成** — lib/ 直下に全ライブラリを配置（umios のようなネスト構造を解消）
- **循環依存の排除** — 依存グラフが DAG（有向非巡回グラフ）
- **明確な責務分離** — アーキテクチャ仕様（docs/umios-architecture）の3層モデルに対応
- **ターゲット非依存の最大化** — アプリコードの移植性向上

---

## アーキテクチャ仕様との対応

docs/umios-architecture で定義された3層モデル:

```
┌─────────────────────────────────────────────┐
│              Application 層                  │
│  Processor: process(AudioContext&)          │
│  Controller: main() + wait_event()          │
├─────────────────────────────────────────────┤
│              Runtime 層                      │
│  EventRouter, ParamState, AudioEngine       │
├─────────────────────────────────────────────┤
│           Backend Adapter 層                 │
│  組み込み: DMA + ISR + SVC                   │
│  WASM: AudioWorklet                         │
│  Plugin: DAW processBlock()                 │
└─────────────────────────────────────────────┘
```

各層に対応するライブラリを明確に分離する。

---

## 現状のライブラリ構成（16ライブラリ）

| ライブラリ | 現在の役割 | 問題点 |
|-----------|-----------|--------|
| umi | 統合ファサード（xmake.luaのみ） | 実体なし、依存宣言のみ |
| **umios** | OSコア全体 | **巨大すぎ、要分解** |
| umidi | MIDI処理 | ✅ 独立性高い |
| umidsp | DSP処理 | ✅ 独立性高い |
| umiport | HAL | ✅ 構造良好 |
| umiboot | ブートローダー | umios/cryptoと境界曖昧 |
| umishell | シェルプリミティブ | ✅ 独立性高い |
| umisynth | シンセ実装 | ✅ umidspに依存（適切） |
| umiusb | USBスタック | umidspに依存（要検討） |
| umifs | ファイルシステム | ✅ 独立性高い |
| umimmio | MMIO抽象化 | ✅ 独立性高い |
| umimock | ライブラリ構造規約のリファレンス実装 | lib/ に維持 |
| umitest | テストフレームワーク | テスト専用 |
| umigui | グラフィック描画（Canvas, FrameBuffer） | ✅ 独立性高い |
| umiui | UI状態管理（HW入力→イベント変換） | ✅ 独立性高い |

---

## 提案: umios の分解

### 分解前の umios 構造

```
lib/umios/
├── core/           # AudioContext, Event, Processor
├── kernel/         # Scheduler, Syscall, Loader
├── backend/        # cm/, wasm/
├── adapter/        # embedded_adapter, umim_adapter
├── crypto/         # ed25519, sha256, sha512
├── app/            # crt0, syscall stub, linker script
└── platform/       # (README.mdのみ)
```

### 分解後の構成

| 新ライブラリ | 元の場所 | 責務 | 層 |
|-------------|---------|------|-----|
| **umicore** | umios/core/ (基本型) | AudioContext, Processor, Event, types, shared_state | Application境界 |
| **umirt** | umios/core/ (ルーティング) | EventRouter, ParamMapping, RouteTable, irq | Runtime |
| **umikernel** | umios/kernel/ | Scheduler, Syscall, MPU, Loader, modules/ | Backend (組み込み) |
| **umiadapt** | umios/adapter/, umios/backend/ | embedded_adapter, umim_adapter, web_sim | Backend Adapter |
| **umiapp** | umios/app/ | crt0, syscall stub, linker script | Application (バイナリ) |
| **umicrypto** | umios/crypto/ | ed25519, sha256, sha512 | 共通ユーティリティ |

**注記**: `umios/backend/cm/` は現在空。Cortex-M 固有コードは `umikernel` に統合。

### 各ライブラリの詳細

#### umicore — Application層の基本型

アプリ開発者が使う **ターゲット非依存** の基本型。

```
lib/umicore/
├── README.md
├── xmake.lua
├── include/umicore/
│   ├── audio_context.hh    # AudioContext構造体
│   ├── processor.hh        # ProcessorLike concept
│   ├── event.hh            # Event, EventQueue
│   ├── types.hh            # sample_t, port_id_t等
│   ├── error.hh            # Result<T>, Error
│   ├── shared_state.hh     # SharedParamState, SharedChannelState
│   ├── time.hh             # TimeSpec
│   └── triple_buffer.hh    # TripleBuffer
└── test/
```

**依存**: なし（完全独立）

**インクルード例**:
```cpp
#include <umicore/audio_context.hh>
#include <umicore/processor.hh>
```

#### umirt — Runtime層

EventRouter とパラメータ管理。OS/バックエンドが使用。

```
lib/umirt/
├── README.md
├── xmake.lua
├── include/umirt/
│   ├── event_router.hh     # EventRouter
│   ├── param_mapping.hh    # ParamMapping
│   ├── route_table.hh      # RouteTable
│   └── irq.hh              # IRQ utilities
└── test/
```

**依存**: umicore

#### umikernel — 組み込みOS

RTOS カーネル、スケジューラ、syscall 実装。**組み込みターゲット専用**。

```
lib/umikernel/
├── README.md
├── xmake.lua
├── include/umikernel/
│   ├── scheduler.hh        # タスクスケジューラ
│   ├── syscall_handler.hh  # SVC ハンドラ
│   ├── syscall/            # 各syscall実装
│   ├── loader.hh           # .umia ローダー
│   ├── mpu_config.hh       # MPU設定
│   ├── protection.hh       # メモリ保護
│   ├── metrics.hh          # KernelMetrics
│   ├── fault_handler.hh    # HardFault処理
│   └── modules/            # Shell, Updater等
├── src/
│   └── loader.cc
└── test/
```

**依存**: umirt, umicore, umiport

#### umiadapt — バックエンドアダプタ

各ターゲット向けの AudioContext 構築とイベント変換ブリッジ。

```
lib/umiadapt/
├── README.md
├── xmake.lua
├── include/umiadapt/
│   ├── embedded_adapter.hh  # 組み込み用アダプタ
│   ├── umim_adapter.hh      # WASM用アダプタ
│   ├── embedded.hh          # 組み込み共通定義
│   └── web/                 # Web用ブリッジ
│       ├── web_sim.js
│       └── web_sim_worklet.js
└── test/
```

**依存**: umicore, umikernel (組み込み時)

**役割**:
- `embedded_adapter`: DMA ISR → AudioContext 構築 → process() 呼び出し
- `umim_adapter`: WASM ↔ JS ブリッジ、Web MIDI API 統合

#### umiapp — アプリバイナリ用

.umia バイナリのスタートアップと syscall スタブ。

```
lib/umiapp/
├── README.md
├── xmake.lua
├── crt0.cc                 # C runtime startup
├── syscall.hh              # syscall wrapper
├── umi_app.hh              # アプリ用ヘッダ
├── app.ld                  # リンカスクリプト
└── app_sections.ld         # セクション定義
```

**依存**: umicore（型定義のみ）

#### umicrypto — 暗号ライブラリ

署名検証とハッシュ。組み込み/ホスト両対応。

```
lib/umicrypto/
├── README.md
├── xmake.lua
├── include/umicrypto/
│   ├── ed25519.hh
│   ├── sha256.hh
│   ├── sha512.hh
│   └── public_key.hh
├── src/
│   ├── ed25519.cc
│   ├── sha256.cc
│   └── sha512.cc
└── test/
```

**依存**: なし

---

## 提案: ライブラリのリネーム

紛らわしい名前を明確化するためのリネーム提案。

| 現在 | 提案 | 理由 |
|------|------|------|
| `umigui` | `umigfx` | "graphics" の略。描画ライブラリであることが明確 |
| `umiui` | `umihid` | Human Interface Device。HW入力→イベント変換の役割を示す |
| `umimock` | `umiref` | リファレンス実装。モックではない |

---

## 最終的なライブラリ構成（18ライブラリ）

| ライブラリ | 役割 | 依存 | 層 |
|-----------|------|-----|-----|
| **umicore** | AudioContext, Processor, Event, shared_state | なし | Application境界 |
| **umirt** | EventRouter, ParamMapping, RouteTable | umicore | Runtime |
| **umikernel** | RTOS, Scheduler, Syscall, modules | umirt, umiport | Backend (組み込み) |
| **umiadapt** | embedded_adapter, umim_adapter, web_sim | umicore, (umikernel) | Backend Adapter |
| **umiapp** | crt0, syscall stub | umicore | Application |
| **umicrypto** | ed25519, sha256/512 | なし | 共通 |
| **umiport** | HAL (arch/mcu/board/platform) | umimmio | Backend |
| **umimmio** | MMIO抽象化 | なし | Backend |
| **umidi** | MIDI処理 | なし | 共通 |
| **umidsp** | DSP処理 | なし | 共通 |
| **umisynth** | シンセ実装 | umidsp | Application |
| **umiboot** | ブートローダー | umicrypto | Backend |
| **umifs** | ファイルシステム | なし | 共通 |
| **umiusb** | USBスタック | umidsp | Backend |
| **umishell** | シェルプリミティブ | なし | 共通 |
| **umigfx** | グラフィック描画（Canvas, FrameBuffer, Skin） | なし | Application |
| **umihid** | UI状態管理（HW入力→イベント変換） | なし | Application |
| **umiref** | ライブラリ構造規約のリファレンス | なし | 開発参考 |
| **umitest** | テストフレームワーク | なし | テスト |

### 削除されるライブラリ

- `umi/` — ファサード不要（各ライブラリを直接使用）
- `umios/` — 分解後に削除

### リネームされるライブラリ

- `umimock/` → `umiref/`（ライブラリ構造規約のリファレンス）
- `umigui/` → `umigfx/`
- `umiui/` → `umihid/`

### lib/ に残すライブラリ

- `umitest/` — テストフレームワーク（lib/ 内で他ライブラリから参照されるため）
- `umiref/` — ライブラリ構造規約のリファレンス実装

---

## 依存グラフ

```
                    ┌─────────┐
                    │ umicore │ ← Application層の基本型・境界インターフェース
                    └────┬────┘
                         │
         ┌───────────────┼───────────────┬───────────────┐
         │               │               │               │
         ▼               ▼               ▼               ▼
    ┌─────────┐    ┌──────────┐    ┌─────────┐    ┌──────────┐
    │  umirt  │    │  umiapp  │    │umisynth │    │ umiadapt │
    │(Runtime)│    │  (App)   │    │  (App)  │    │(Adapter) │
    └────┬────┘    └──────────┘    └────┬────┘    └────┬─────┘
         │                              │               │
         ▼                              ▼               │
    ┌───────────┐                  ┌─────────┐         │
    │ umikernel │◀─────────────────┤ umidsp  │         │
    │(Backend)  │                  └─────────┘         │
    └─────┬─────┘                                      │
          │                                            │
          ▼                                            │
    ┌──────────┐     ┌──────────┐                      │
    │ umiport  │────▶│ umimmio  │◀─────────────────────┘
    └──────────┘     └──────────┘


独立ライブラリ（依存なし）:
  umidi, umidsp, umifs, umishell, umicrypto, umimmio,
  umigfx, umihid, umiref, umitest

派生依存:
  umiboot ← umicrypto
  umiusb ← umidsp
  umisynth ← umidsp
```

### umigfx と umihid の役割分担

| ライブラリ | 責務 | 例 |
|-----------|------|-----|
| **umigfx** | グラフィック描画 | Canvas2D, FrameBuffer, Skin, Layout |
| **umihid** | UI状態管理 | Knob/Slider/Button状態、HW入力→Event変換 |

```
HW入力 (GPIO, ADC, Encoder)
    │
    ▼
umihid (状態管理・イベント変換)
    │ ControlEvent
    ▼
EventRouter (umirt)
    │
    ├── AudioEventQueue → process()
    └── ControlEventQueue → Controller

umigfx は Controller が umihid の状態を画面に描画する際に使用
```

---

## 移行計画

### フェーズ 1: umios の分解（優先度: 高）

1. `umicore/` 作成 — umios/core/ の基本型（audio_context, processor, event, types, shared_state, error）を移動
2. `umirt/` 作成 — umios/core/ のルーティング系（event_router, param_mapping, route_table, irq）を移動
3. `umicrypto/` 作成 — umios/crypto/ を移動
4. `umiapp/` 作成 — umios/app/ を移動
5. `umiadapt/` 作成 — umios/adapter/, umios/backend/wasm/ を移動
6. `umikernel/` 作成 — umios/kernel/ を移動
7. `umios/` 削除

**見積もり**: 3-4日

### フェーズ 2: リネーム整理（優先度: 低）

1. `umimock/` → `umiref/` にリネーム（ライブラリ構造規約のリファレンス）
2. テスト用モック機能を `umitest/` に統合

**見積もり**: 半日

### フェーズ 3: umi ファサード削除（優先度: 低）

1. 各 example/target の依存を直接指定に変更
2. `lib/umi/` 削除

**見積もり**: 1日

---

## 互換性への配慮

### インクルードパス

移行期間中は互換性ヘッダを提供:

```cpp
// lib/umios/core/audio_context.hh (互換性用)
#pragma once
#warning "umios/core/audio_context.hh is deprecated, use umicore/audio_context.hh"
#include <umicore/audio_context.hh>
```

---

## 検証基準

移行完了の判定基準:

1. **ビルド成功**: 全ターゲット（host/embedded/wasm）がビルド可能
2. **テスト通過**: `xmake test` が全て PASS
3. **循環依存なし**: 依存グラフが DAG（有向非巡回グラフ）
4. **ドキュメント更新**: CLAUDE.md, README.md の更新完了

---

## 参考資料

- [docs/umios-architecture/00-overview.md](../../docs/umios-architecture/00-overview.md) — システム全体像
- [docs/umios-architecture/08-backend-adapters.md](../../docs/umios-architecture/08-backend-adapters.md) — バックエンド設計
- [lib/docs/LIBRARY_STRUCTURE.md](LIBRARY_STRUCTURE.md) — ライブラリ構造規約
