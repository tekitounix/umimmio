# umios 構造分析

本文書は `lib/umios/` 内のファイル構成、依存関係、使用状況を網羅的に分析し、理想的な分類を提案する。

---

## 目次

1. [現在のディレクトリ構成](#現在のディレクトリ構成)
2. [ファイル一覧と分類](#ファイル一覧と分類)
3. [依存関係マップ](#依存関係マップ)
4. [使用状況分析](#使用状況分析)
5. [問題点](#問題点)
6. [理想的な分類案](#理想的な分類案)

---

## 現在のディレクトリ構成

```
lib/umios/
├── core/           # 基本型・ルーティング（混在）
│   ├── ui/         # UIコントローラ（未使用の可能性）
├── kernel/         # OS + System Service（混在）
│   ├── syscall/    # syscall番号定義
│   └── modules/    # オーディオモジュール（未使用の可能性）
├── adapter/        # バックエンドアダプタ
│   └── web/        # Web用アダプタ
├── backend/        # バックエンド実装
│   ├── cm/         # Cortex-M用（空）
│   └── wasm/       # WASM用シミュレータ
├── crypto/         # 暗号ライブラリ
├── app/            # アプリSDK
└── platform/       # プラットフォーム（README.mdのみ）
```

---

## ファイル一覧と分類

### core/ — 基本型・ルーティング

| ファイル | 責務 | ターゲット依存 | 実際の使用先 | 分類 |
|---------|------|--------------|-------------|------|
| `audio_context.hh` | AudioContext構造体 | なし | examples/*, tests/* | **Application境界** |
| `processor.hh` | ProcessorLike concept | なし | examples/*, tests/* | **Application境界** |
| `event.hh` | Event, EventQueue | なし | examples/*, adapter/* | **Application境界** |
| `types.hh` | sample_t, param_id_t等 | なし | 多数 | **Application境界** |
| `error.hh` | Result<T>, Error | なし | audio_context.hh | **Application境界** |
| `shared_state.hh` | SharedParamState等 | なし | audio_context.hh, kernel/* | **Application境界** |
| `time.hh` | TimeSpec | なし | ? | **Application境界** |
| `triple_buffer.hh` | TripleBuffer | なし | stm32f4_kernel | **共通ユーティリティ** |
| `event_router.hh` | EventRouter | なし | stm32f4_kernel, synth_app | **Runtime** |
| `param_mapping.hh` | ParamMapping, RouteTable参照 | なし | stm32f4_kernel, synth_app | **Runtime** |
| `route_table.hh` | RouteTable, RouteFlags | なし | event_router.hh | **Runtime** |
| `irq.hh` | IRQハンドラインターフェース | 組み込み | port/*, kernel/* | **→umiportへ移動** |
| `syscall_nr.hh` | Syscall番号定義 | なし | app/syscall.hh, kernel/* | **共通定義** |
| `fs_types.hh` | OpenFlags, FsInfo等 | なし | kernel/storage_service.hh | **共通定義** |
| `app.hh` | AppEvent, AppEventType | なし | **参照なし** | **削除候補** |

#### core/ui/ — UIコントローラ

| ファイル | 責務 | 使用状況 |
|---------|------|---------|
| `ui_controller.hh` | UIコントローラテンプレート | **外部参照なし** |
| `ui_map.hh` | UIマッピング | ui_controller.hhのみ |
| `ui_view.hh` | UIビュー | **外部参照なし** |

**注記**: `core/ui/` は外部から参照されておらず、未使用の可能性が高い。

---

### kernel/ — OS + System Service

| ファイル | 責務 | 分類案 | 実際の使用先 |
|---------|------|-------|-------------|
| `umi_kernel.hh` | RTOSカーネル本体（タスク、スケジューラ） | **Kernel** | stm32f4_kernel, daisy_pod_kernel, tests/* |
| `syscall_handler.hh` | SVCハンドラ | **Kernel** | カーネル実装 |
| `mpu_config.hh` | MPU設定 | **Kernel** | カーネル実装 |
| `protection.hh` | メモリ保護 | **Kernel** | カーネル実装 |
| `fault_handler.hh` | HardFault処理 | **Kernel** | カーネル実装 |
| `fpu_policy.hh` | FPUコンテキスト退避 | **Kernel** | stm32f4_kernel, daisy_pod_kernel |
| `loader.hh/cc` | .umiaローダー | **Kernel** | カーネル実装, synth_app |
| `app_header.hh` | アプリヘッダ定義 | **Kernel** | loader.hh |
| `umi_shell.hh` | シェルサービス | **System Service** | stm32f4_kernel |
| `shell_commands.hh` | シェルコマンド定義 | **System Service** | stm32f4_kernel |
| `umi_audio.hh` | オーディオサービス | **System Service** | tests/test_audio.cc |
| `umi_midi.hh` | MIDIサービス | **System Service** | tests/test_midi.cc |
| `umi_monitor.hh` | モニタサービス | **System Service** | **外部参照なし** |
| `umi_startup.hh` | スタートアップ | **System Service** | **外部参照なし** |
| `storage_service.hh` | ストレージサービス | **System Service** | syscall_handler.hh |
| `driver.hh` | ドライバインターフェース | **System Service** | カーネル実装 |
| `block_device.hh` | ブロックデバイスconcept | **System Service** | storage_service.hh |
| `coro.hh` | コルーチンランタイム | **共通ユーティリティ** | synth_app, embedded/example_app |
| `log.hh` | ログハンドラ | **共通ユーティリティ** | ? |
| `assert.hh` | アサートマクロ | **共通ユーティリティ** | ? |
| `metrics.hh` | KernelMetrics | **共通ユーティリティ** | カーネル実装 |
| `embedded_state_provider.hh` | 状態プロバイダ | **?** | **外部参照なし** |

#### kernel/syscall/ — Syscall番号

| ファイル | 責務 | 備考 |
|---------|------|------|
| `syscall_numbers.hh` | syscall番号定義 | core/syscall_nr.hh と重複？ |

#### kernel/modules/ — オーディオモジュール

| ファイル | 責務 | 使用状況 |
|---------|------|---------|
| `audio_module.hh` | AudioRingBuffer, IAudioProcessor | **外部参照なし（umiusbと重複）** |
| `usb_audio_module.hh` | USB Audioタスク | **外部参照なし** |

**注記**: `kernel/modules/` は umiusb ライブラリと機能が重複しており、未使用の可能性が高い。

---

### adapter/ — バックエンドアダプタ

| ファイル | 責務 | ターゲット | 使用状況 |
|---------|------|-----------|---------|
| `embedded_adapter.hh` | 組み込み用アダプタ | Cortex-M | **直接使用なし**（テンプレート） |
| `umim_adapter.hh` | WASM用アダプタ | WASM | README.md, headless_webhost? |
| `embedded.hh` | 組み込み共通定義 | Cortex-M | embedded_adapter.hh |

#### adapter/web/ — Web用アダプタ

| ファイル | 責務 | 使用状況 |
|---------|------|---------|
| `web_adapter.hh` | Webアダプタ | **要確認** |
| `umi.js` | JS統合 | **要確認** |
| `umi-worklet.js` | AudioWorklet | **要確認** |
| `worklet-processor.js` | Workletプロセッサ | **要確認** |

---

### backend/ — **umiportと重複（削除候補）**

#### backend/cm/ — Cortex-M用

**空ディレクトリ** — 削除すべき。

#### backend/wasm/ — WASM用

| ファイル | 責務 | 使用状況 |
|---------|------|---------|
| `web_sim.js` | Webシミュレータ | headless_webhost? |
| `web_sim_worklet.js` | Workletシミュレータ | headless_webhost? |

**注記**: `umiport/platform/wasm/` に `web_sim.hh`, `web_hal.hh` が既にある。
JS ファイルは `umiport/platform/wasm/` または `examples/headless_webhost/` に移動すべき。

---

### crypto/ — 暗号ライブラリ

| ファイル | 責務 | 使用状況 |
|---------|------|---------|
| `ed25519.hh/cc` | Ed25519署名 | tests/test_signature.cc |
| `sha256.hh/cc` | SHA-256 | ? |
| `sha512.hh/cc` | SHA-512 | tests/test_signature.cc, ed25519.cc |
| `public_key.hh` | 公開鍵定義 | loader.hh? |

---

### app/ — アプリSDK

| ファイル | 責務 | 使用状況 |
|---------|------|---------|
| `syscall.hh` | syscallラッパー | synth_app, stm32f4_kernel, daisy_pod_synth_h7 |
| `umi_app.hh` | アプリ用ヘッダ | synth_app |
| `crt0.cc` | C runtime startup | アプリビルド時 |
| `app.ld` | リンカスクリプト | アプリビルド時 |
| `app_sections.ld` | セクション定義 | app.ld |
| `xmake.lua` | ビルド設定 | — |

---

### platform/ — プラットフォーム

| ファイル | 責務 | 使用状況 |
|---------|------|---------|
| `README.md` | 説明のみ | — |

**注記**: 実質的に空ディレクトリ。

---

## 依存関係マップ

```
core/types.hh ─────────────────────────────────────────────────┐
     │                                                          │
     ▼                                                          │
core/error.hh                                                   │
     │                                                          │
     ▼                                                          │
core/shared_state.hh                                            │
     │                                                          │
     ▼                                                          │
core/event.hh ◀───────────────────────┐                        │
     │                                 │                        │
     ▼                                 │                        │
core/audio_context.hh                  │                        │
     │                                 │                        │
     ▼                                 │                        │
core/processor.hh                      │                        │
                                       │                        │
core/route_table.hh ──────────────────┼────────────────────────┘
     │                                 │
     ▼                                 │
core/param_mapping.hh                  │
     │                                 │
     ▼                                 │
core/event_router.hh ─────────────────┘

core/syscall_nr.hh ────────────────────────────────────────────┐
     │                                                          │
     ├──────────────────────────────▶ app/syscall.hh            │
     │                                                          │
     └──────────────────────────────▶ kernel/storage_service.hh │
                                           │                    │
                                           ▼                    │
                                      kernel/syscall_handler.hh │
                                           │                    │
                                           ▼                    │
                                      kernel/loader.hh ◀────────┘
                                           │
                                           ▼
                                      kernel/umi_kernel.hh
```

---

## 使用状況分析

### 頻繁に使用されるファイル

| ファイル | 参照元 | 重要度 |
|---------|--------|-------|
| `core/audio_context.hh` | examples/*, tests/* | **高** |
| `core/processor.hh` | examples/*, tests/* | **高** |
| `core/event.hh` | examples/*, adapter/* | **高** |
| `core/types.hh` | 多数 | **高** |
| `kernel/umi_kernel.hh` | stm32f4_kernel, daisy_pod_kernel, tests/* | **高** |
| `kernel/fpu_policy.hh` | stm32f4_kernel, daisy_pod_kernel | **高** |
| `kernel/loader.hh` | カーネル実装, synth_app | **高** |
| `kernel/coro.hh` | synth_app, embedded/* | **中** |
| `app/syscall.hh` | アプリ実装 | **高** |
| `crypto/ed25519.hh` | tests/* | **中** |

### 外部参照状況の詳細調査結果

| ファイル | 調査結果 | 有用性 | 判定 |
|---------|----------|-------|------|
| `core/ui/ui_controller.hh` | **コード参照なし**。PROJECT_STRUCTURE.mdに記載のみ | 高機能UIフレームワーク（MIDI Learn, Undo/Redo, プリセット管理）。完成度高いが未使用 | **保留** |
| `core/ui/ui_map.hh` | ui_controller.hhからのみ参照 | UIController依存 | **保留** |
| `core/ui/ui_view.hh` | ui_controller.hhからのみ参照 | UIController依存 | **保留** |
| `core/app.hh` | **コード参照なし**。app/syscall.hhと役割重複 | AppEvent型定義、register_processor()。app/umi_app.hhと重複・競合 | **削除候補** |
| `kernel/umi_monitor.hh` | **コード参照なし**。EXPANSION_PLAN.mdに将来計画として記載 | StackMonitor/HeapMonitor実装済み。デバッグ・プロファイリングに有用 | **将来使用** |
| `kernel/umi_startup.hh` | **コード参照なし**。PROJECT_STRUCTURE.mdに記載のみ | Bootstrap/LinkerSymbols。汎用スタートアップテンプレート | **将来使用** |
| `kernel/embedded_state_provider.hh` | **コード参照なし** | ShellCommands用状態プロバイダ。shell_commands.hhと連携 | **将来使用** |
| `kernel/modules/audio_module.hh` | **コード参照なし**。umiusbと機能重複（volatile版 vs atomic版） | 旧設計の名残。umiusbのAudioRingBufferが優れている | **削除候補** |
| `kernel/modules/usb_audio_module.hh` | **コード参照なし**。UMIUSB_REFERENCE.mdで「未使用の可能性」と記載 | 旧設計の名残 | **削除候補** |
| `adapter/embedded_adapter.hh` | **直接使用なし**だがドキュメント参照あり | テンプレート提供。実カーネルはkernel.ccに直接実装 | **参考実装** |

### 重複・冗長

| 項目 | 場所1 | 場所2 | 備考 |
|------|-------|-------|------|
| syscall番号 | `core/syscall_nr.hh` | `kernel/syscall/syscall_numbers.hh` | 要統合 |
| AudioRingBuffer | `kernel/modules/audio_module.hh` | `lib/umiusb/` | umiusbを使用すべき |

---

## 問題点

### 1. 責務の混在

**core/** には2種類のファイルが混在：
- Application層の基本型（audio_context, processor, event, types）
- Runtime層のルーティング（event_router, param_mapping, route_table）

### 2. kernel/ の肥大化

**kernel/** には4種類のコードが混在：
- Kernel本体（umi_kernel, syscall_handler, mpu_config）
- System Service（umi_shell, umi_audio, umi_midi, storage_service）
- 共通ユーティリティ（coro, log, assert, metrics）
- 未使用コード（umi_monitor, umi_startup, modules/）

### 3. 空・冗長なディレクトリ

- `backend/cm/` — 空
- `platform/` — README.mdのみ
- `kernel/modules/` — 未使用（umiusbと重複）

### 4. 未使用コードの存在

- `core/ui/` — 外部参照なし
- `core/app.hh` — 実コード参照なし
- `kernel/umi_monitor.hh` — 外部参照なし
- `kernel/umi_startup.hh` — 外部参照なし
- `kernel/embedded_state_provider.hh` — 外部参照なし

---

## 理想的な分類案

### アーキテクチャ仕様との対応

| 層 | 責務 | 対応ファイル |
|---|------|-------------|
| **Application境界** | アプリ開発者が使う型 | audio_context, processor, event, types, error, shared_state |
| **Runtime** | イベントルーティング、パラメータ管理 | event_router, param_mapping, route_table |
| **Kernel** | タスク、スケジューラ、メモリ保護 | umi_kernel, syscall_handler, mpu_config, protection, fault_handler, fpu_policy, loader |
| **System Service** | OS提供サービス | umi_shell, umi_audio, umi_midi, storage_service, driver |
| **Backend Adapter** | ターゲット固有ブリッジ | embedded_adapter, umim_adapter, web_adapter |
| **共通** | ターゲット非依存ユーティリティ | coro, log, assert, metrics, crypto/* |

### 推奨ディレクトリ構成

```
lib/umios/
├── core/               # Application境界（変更なし、ui/削除）
│   ├── audio_context.hh
│   ├── processor.hh
│   ├── event.hh
│   ├── types.hh
│   ├── error.hh
│   ├── shared_state.hh
│   ├── time.hh
│   └── triple_buffer.hh
│
├── runtime/            # Runtime層（新設、core/から移動）
│   ├── event_router.hh
│   ├── param_mapping.hh
│   └── route_table.hh
│
├── sys/                # Kernel + System Service（kernel/をリネーム）
│   ├── kernel/         # Kernel本体
│   │   ├── umi_kernel.hh
│   │   ├── syscall_handler.hh
│   │   ├── mpu_config.hh
│   │   ├── protection.hh
│   │   ├── fault_handler.hh
│   │   ├── fpu_policy.hh
│   │   ├── loader.hh/cc
│   │   └── app_header.hh
│   │
│   └── service/        # System Service
│       ├── umi_shell.hh
│       ├── shell_commands.hh
│       ├── umi_audio.hh
│       ├── umi_midi.hh
│       ├── storage_service.hh
│       ├── driver.hh
│       └── block_device.hh
│
├── adapter/            # Backend Adapter（変更なし）
│   ├── embedded_adapter.hh
│   ├── umim_adapter.hh
│   └── web/
│
├── crypto/             # 暗号（変更なし）
│
├── app/                # アプリSDK（変更なし）
│
├── util/               # 共通ユーティリティ（新設）
│   ├── coro.hh
│   ├── log.hh
│   ├── assert.hh
│   └── metrics.hh
│
└── shared/             # OS/App共有定義（新設）
    ├── syscall_nr.hh   # core/から移動
    ├── fs_types.hh     # core/から移動
    └── irq.hh          # core/から移動
```

### 削除候補

| パス | 理由 | 判定 |
|------|------|------|
| `core/app.hh` | app/syscall.hh, app/umi_app.hhと役割重複。実コード参照なし | **削除** |
| `kernel/modules/audio_module.hh` | umiusbと重複、未使用。旧設計の名残 | **削除** |
| `kernel/modules/usb_audio_module.hh` | umiusbと重複、未使用。旧設計の名残 | **削除** |
| `kernel/syscall/syscall_numbers.hh` | core/syscall_nr.hhと重複 | **削除** |
| `backend/` | **umiportと責務重複** | **umiportへ移動** |
| `adapter/` | **umiportと責務重複** | **umiportへ移動** |
| `crypto/` | 独立した暗号ライブラリ | **umicryptoへ分離** |
| `platform/` | README.mdのみ | **削除** |
| `core/irq.hh` | HALの責務 | **umiportへ移動** |

### 保留（将来使用予定または参考実装）

| パス | 理由 | 判定 |
|------|------|------|
| `core/ui/` | 完成度の高いUIフレームワーク。現在未使用だが将来有用 | **保留** |
| `kernel/umi_monitor.hh` | StackMonitor/HeapMonitor。将来のプロファイリングで使用 | **保留** |
| `kernel/umi_startup.hh` | 汎用Bootstrapテンプレート。新カーネル作成時に参考 | **保留** |
| `kernel/embedded_state_provider.hh` | ShellCommands連携用。シェル実装時に使用 | **保留** |
| `adapter/embedded_adapter.hh` | 参考実装テンプレート | **保留** |

### 移行の優先度

1. **高**: 未使用ファイルの削除確認
2. **高**: syscall番号の統合（重複解消）
3. **中**: runtime/ への分離（core/から）
4. **中**: sys/kernel/ と sys/service/ への分離
5. **低**: util/ への移動
6. **低**: shared/ への移動

---

## 次のステップ

1. 未使用ファイルの削除前に、本当に使われていないか最終確認
2. syscall_nr.hh と syscall_numbers.hh の統合
3. lib/docs/LIBRARY_REFACTORING.md との整合性確認
4. 実際のリファクタリング実施

---

## OS本体（examples/\*_kernel）の構造分析

`examples/stm32f4_kernel` と `examples/daisy_pod_kernel` は「kernel」と命名されているが、
実際は**OS本体**（kernel + OSフレーム + サービス群のコンポジションルート）である。

### 現状の構成

```
examples/stm32f4_kernel/src/
├── main.cc     # ベクタテーブル、フォルトハンドラ、起動シーケンス
├── kernel.cc   # OSロジック（タスク、syscall、audio処理）
├── arch.cc/hh  # CM4コンテキストスイッチ
├── mcu.cc/hh   # MCU初期化（クロック、GPIO、DMA）
└── bsp.hh      # ボード固有定数（LED、オーディオ設定）

examples/daisy_pod_kernel/src/
├── main.cc     # ベクタテーブル、DMA IRQ、起動シーケンス
├── kernel.cc   # OSロジック
├── arch.cc/hh  # CM7コンテキストスイッチ
├── synth.hh    # フォールバックシンセ
└── loader_stub.cc
```

### 共通化可能な外郭（OSフレーム）

以下は両実装で重複しており、lib/へ共通化できる：

| 要素 | 説明 |
|------|------|
| Kernel<N, M, HW>インスタンス管理 | タスク数、キュー数の設定 |
| タスクスタック/TCB構造 | AUDIO/SYSTEM/CONTROL/IDLEパターン |
| FPU policy選択ロジック | TaskFpuDecl → resolve_fpu_policy |
| SpscQueue（DMA→Audio）パターン | g_audio_ready_queue |
| EventRouter + TripleBuffer管理 | g_event_router, g_app_config_buf |
| SharedMemory初期化 | g_shared, init_shared_memory() |
| SysEx/Shell基盤 | ShellCommands, StateProvider |
| タスク登録・起動シーケンス | create_task, start_scheduler |
| ButtonDebouncer | UIイベント生成 |

### ボード固有（examples/に残るべき）

| 要素 | 説明 |
|------|------|
| HW policy構造体 | Stm32F4Hw, Stm32H7Hw（critical section実装） |
| BSP定数 | LED pin、オーディオ設定、メモリレイアウト |
| MCU初期化 | クロック、GPIO、DMA設定 |
| DMAバッファ配置 | section属性（.sram_d2_bss等） |
| USB device info | VID/PID、文字列記述子 |
| ベクタテーブル | IRQハンドラ登録 |

### 提案：OSフレーム層の導入

```
lib/umios/
├── core/           # Application境界型（Processor, Event, AudioContext）
├── runtime/        # EventRouter, TripleBuffer, ParamMapping
├── kernel/         # スケジューラ、タスク管理（umi_kernel.hh）
├── frame/          # ★NEW: OSフレーム（共通外郭）
│   ├── os_frame.hh      # タスク構成テンプレート
│   ├── audio_task.hh    # Audioタスクパターン
│   ├── system_task.hh   # Systemタスクパターン
│   ├── shell_frame.hh   # SysExシェル統合
│   └── state_provider.hh # StateProvider基底
└── app/            # アプリSDK

examples/stm32f4_kernel/
├── main.cc         # ベクタテーブル、BSP初期化
├── os.cc           # OsFrame<Stm32F4Hw, Stm32F4Bsp>の特殊化
├── bsp.hh          # ボード固有定数
└── hw.hh           # HW policy
```

### 検討事項

1. **frame/ vs 現状のkernel/の境界**
   - umi_kernel.hh（スケジューラ）は kernel/ に残す
   - タスク構成パターンは frame/ に移動

2. **テンプレート vs 具象クラス**
   - OSフレームはCRTPまたはポリシーテンプレートで実装
   - コンパイル時に特殊化される

3. **core/app/kernelの関係**
   - core/ = 全プラットフォーム共通の型定義
   - kernel/ = 組み込みOS実装
   - app/ = アプリSDK
   - frame/ = kernel + サービスの組み立てパターン

---

## 最終提案: `lib/umi/` 統合構造

### 背景

現状の `lib/umiXXX/` 構造（Boost風）は以下の問題がある：
- namespaceとパスが不一致（`umidsp::Osc` vs `#include <umidsp/osc.hh>`）
- ライブラリ境界が不明確

### 推奨構造

```
lib/
└── umi/                          # namespace root
    ├── core/                     # umi::core:: — Application境界型
    │   ├── processor.hh          # ProcessorLike concept
    │   ├── audio_context.hh      # AudioContext
    │   ├── event.hh              # Event, EventQueue
    │   ├── types.hh              # sample_t, param_id_t
    │   ├── error.hh              # Result<T>, Error
    │   └── shared_state.hh       # SharedParamState
    │
    ├── runtime/                  # umi::runtime:: — イベントルーティング
    │   ├── event_router.hh
    │   ├── param_mapping.hh
    │   └── route_table.hh
    │
    ├── kernel/                   # umi::kernel:: — RTOSカーネル
    │   ├── scheduler.hh          # スケジューラ
    │   ├── task.hh               # タスク管理
    │   ├── syscall.hh            # syscallハンドラ
    │   ├── mpu.hh                # メモリ保護
    │   └── loader.hh             # .umiaローダー
    │
    ├── shell/                    # umi::shell:: — シェルサービス
    │   ├── shell.hh
    │   └── commands.hh
    │
    ├── storage/                  # umi::storage:: — ストレージサービス
    │   ├── service.hh
    │   └── block_device.hh
    │
    ├── dsp/                      # umi::dsp:: — DSPコンポーネント
    │   ├── oscillator/
    │   ├── filter/
    │   └── envelope/
    │
    ├── midi/                     # umi::midi:: — MIDI処理
    │   ├── ump.hh
    │   ├── message.hh
    │   └── sysex.hh
    │
    ├── usb/                      # umi::usb:: — USB実装
    │   ├── device.hh
    │   └── audio_class.hh
    │
    ├── port/                     # umi::port:: — HAL/BSP
    │   ├── hal/                  # ハードウェア抽象化
    │   └── platform/             # プラットフォーム固有
    │       ├── stm32f4/
    │       ├── stm32h7/
    │       └── wasm/
    │
    ├── crypto/                   # umi::crypto:: — 暗号ライブラリ
    │   ├── ed25519.hh
    │   ├── sha256.hh
    │   └── sha512.hh
    │
    ├── app/                      # umi::app:: — アプリSDK
    │   ├── syscall.hh            # syscallラッパー
    │   ├── crt0.cc
    │   └── app.ld
    │
    └── util/                     # umi::util:: — 共通ユーティリティ
        ├── coro.hh
        ├── triple_buffer.hh
        └── ring_buffer.hh
```

### namespace対応表

| ディレクトリ | namespace | include path | 責務 |
|-------------|-----------|--------------|------|
| `lib/umi/core/` | `umi::core` | `<umi/core/processor.hh>` | OS-App契約（API仕様） |
| `lib/umi/runtime/` | `umi::runtime` | `<umi/runtime/event_router.hh>` | イベントルーティング |
| `lib/umi/kernel/` | `umi::kernel` | `<umi/kernel/scheduler.hh>` | RTOSカーネル |
| `lib/umi/shell/` | `umi::shell` | `<umi/shell/shell.hh>` | シェルサービス |
| `lib/umi/storage/` | `umi::storage` | `<umi/storage/service.hh>` | ストレージサービス |
| `lib/umi/dsp/` | `umi::dsp` | `<umi/dsp/oscillator/saw.hh>` | DSPコンポーネント |
| `lib/umi/midi/` | `umi::midi` | `<umi/midi/ump.hh>` | MIDI処理 |
| `lib/umi/usb/` | `umi::usb` | `<umi/usb/device.hh>` | USB実装 |
| `lib/umi/port/` | `umi::port` | `<umi/port/hal/gpio.hh>` | HAL/BSP |
| `lib/umi/crypto/` | `umi::crypto` | `<umi/crypto/ed25519.hh>` | 暗号ライブラリ |
| `lib/umi/app/` | `umi::app` | `<umi/app/syscall.hh>` | アプリSDK |
| `lib/umi/util/` | `umi::util` | `<umi/util/coro.hh>` | 共通ユーティリティ |

### 他プロジェクトとの比較

| プロジェクト | 構造 | 例 |
|-------------|------|-----|
| **Boost** | `lib/boost_xxx/` | `#include <boost/asio.hpp>` |
| **Abseil** | `lib/absl/xxx/` | `#include <absl/strings/str_cat.h>` |
| **Folly** | `lib/folly/xxx/` | `#include <folly/futures/Future.h>` |
| **Zephyr** | `lib/zephyr/xxx/` | `#include <zephyr/kernel.h>` |
| **UMI（提案）** | `lib/umi/xxx/` | `#include <umi/core/processor.hh>` |

### 旧→新マッピング

| 旧ライブラリ | 新パス | 備考 |
|-------------|--------|------|
| `lib/umios/core/` | `lib/umi/core/` | Application境界型 |
| `lib/umios/core/event_router.hh` 等 | `lib/umi/runtime/` | Runtime層を分離 |
| `lib/umios/kernel/` | `lib/umi/kernel/` | カーネル本体のみ |
| `lib/umios/kernel/umi_shell.hh` 等 | `lib/umi/shell/` | サービスを分離 |
| `lib/umios/kernel/storage_service.hh` | `lib/umi/storage/` | サービスを分離 |
| `lib/umios/crypto/` | `lib/umi/crypto/` | 独立ライブラリ化 |
| `lib/umios/app/` | `lib/umi/app/` | そのまま移動 |
| `lib/umios/adapter/`, `backend/` | `lib/umi/port/` | HALに統合 |
| `lib/umidsp/` | `lib/umi/dsp/` | そのまま移動 |
| `lib/umidi/` | `lib/umi/midi/` | そのまま移動 |
| `lib/umiusb/` | `lib/umi/usb/` | そのまま移動 |
| `lib/umiport/` | `lib/umi/port/` | 統合 |
| `lib/umishell/` | `lib/umi/shell/` | kernel側と統合 |
| `lib/umisynth/` | `lib/umi/synth/` | そのまま移動 |
| `lib/umiboot/` | `lib/umi/boot/` | そのまま移動 |
| `lib/umifs/` | `lib/umi/fs/` | そのまま移動 |
| `lib/umigui/` | `lib/umi/gui/` | そのまま移動 |
| `lib/umiui/` | `lib/umi/ui/` | そのまま移動 |
| `lib/umimmio/` | `lib/umi/mmio/` | そのまま移動 |
| `lib/umimock/` | `lib/umi/mock/` | そのまま移動 |
| `lib/umitest/` | `lib/umi/test/` | そのまま移動 |

### 移行計画

#### Phase 1: 準備（破壊的変更なし）
1. `lib/umi/` ディレクトリ作成
2. 新規コードは `lib/umi/xxx/` に配置
3. 旧パスからのシンボリックリンクまたはinclude転送ヘッダ

#### Phase 2: 段階的移行
1. 依存の少ないライブラリから移行（`crypto`, `util`）
2. コアライブラリ移行（`core`, `runtime`）
3. サービス移行（`shell`, `storage`）
4. カーネル移行（`kernel`）

#### Phase 3: クリーンアップ
1. 旧ディレクトリ削除
2. 転送ヘッダ削除
3. ドキュメント更新

### 利点

1. **namespace = path**: `umi::dsp::Osc` → `#include <umi/dsp/osc.hh>`
2. **明確な境界**: 各サブディレクトリが独立したモジュール
3. **検索性**: すべてが `umi/` 下にある
4. **拡張性**: 新モジュール追加が容易
5. **業界標準**: Abseil, Folly, Zephyrと同じパターン

---

## 参考資料

- [docs/umios-architecture/00-overview.md](../../../docs/umios-architecture/00-overview.md) — 3層モデル定義
- [lib/docs/LIBRARY_REFACTORING.md](../../docs/LIBRARY_REFACTORING.md) — ライブラリ全体のリファクタリング計画
