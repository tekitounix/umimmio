# UMI ライブラリ構成 設計仕様書

**バージョン:** 1.2.0
**作成日:** 2026-02-14
**最終監査日:** 2026-02-14

本文書は UMI の理想的なライブラリ構成を定義する**設計仕様書**である。現状の問題分析や移行手順は [MIGRATION_PLAN.md](MIGRATION_PLAN.md) を参照。

---

## 1. 概要

### 1.1 設計思想

**「少数の高品質なライブラリ」 > 「多数の細分化されたモジュール」**

全ライブラリが LIBRARY_SPEC v2.0.0 + UMI Strict Profile（§8.1 参照）準拠のスタンドアロン構成として lib/ 直下に配置される。

```
lib/
├── umicore/      (Core Types & Concepts)
├── umihal/       (HAL Concepts)
├── umimmio/      (MMIO Register Access)
├── umiport/      (Platform Ports)
├── umidevice/    (Device Drivers)
├── umidsp/       (DSP Algorithms)
├── umidi/        (MIDI Protocol)
├── umiusb/       (USB Device Stack)
├── umios/        (OS Kernel + Services)
├── umitest/      (Test Framework)
├── umibench/     (Benchmark Framework)
├── umirtm/       (Real-Time Monitor)
├── umi/          (Bundle definitions only)
└── docs/         (共通標準・ガイド)
```

**ライブラリ総数: 12**

### 1.2 ライブラリ分割の基準

ライブラリは以下の4条件を**すべて**満たす単位で分割する:

| 条件 | 説明 |
|------|------|
| **独立デプロイ可能** | 他のUMIライブラリなしでも単独ビルド・テスト可能 |
| **明確な責務境界** | 「このライブラリは何をするか」を1文で説明できる |
| **安定したAPI** | 公開APIが頻繁に変わらない（内部実装の変更は自由） |
| **再利用価値** | UMI以外のプロジェクトでも利用できる汎用性 |

**分割しない判断基準:**
- 常に一緒に変更される2つのモジュール → 1ライブラリに統合
- 単独では意味をなさないモジュール → 親ライブラリの内部モジュールに
- ファイル数が極端に少ない (3ファイル以下) → 関連ライブラリに統合

---

## 2. レイヤーモデル

```
L5  Application        ← Processor 実装、アプリケーション
L4  System             ← umios (カーネル + サービス統合)
L3  Domain             ← umidsp, umidi, umiusb (ドメイン固有)
L2  Platform           ← umiport, umidevice (ハードウェア抽象化)
L1  Foundation         ← umicore, umihal, umimmio (型・概念・基盤)
L0  Infrastructure     ← umitest, umibench, umirtm (開発支援)
```

**レイヤー規則:**
- 依存は**同一レイヤーまたは下位レイヤーのみ**許可
- L0 は特殊: テスト・ベンチマーク・デバッグ用で、**プロダクションコードからは依存禁止**
- 上位レイヤーへの依存は**コンパイルエラー**で防止（将来的にCI検証）

---

## 3. 名前空間規約

全ライブラリは `umi::` ルート名前空間配下に統一する。

| ライブラリ | 名前空間 |
|-----------|---------|
| umicore | `umi::core`, `umi` (トップレベル Concept) |
| umihal | `umi::hal` |
| umimmio | `umi::mmio` |
| umiport | `umi::port`, `umi::board` (ボード固有) |
| umidevice | `umi::device` |
| umirtm | `umi::rt` |
| umitest | `umi::test` |
| umibench | `umi::bench` |
| umidsp | `umi::dsp` |
| umidi | `umi::midi` |
| umiusb | `umi::usb` |
| umios | `umi::os`, `umi::os::service`, `umi::os::ipc` |

---

## 4. 依存関係

### 4.1 全体依存関係図

```
                         umios ─────────────────────────┐
                         (kernel, service, runtime)     │  L4
                         deps: umicore + umiport        │
                                                        │
                                                        │
     umidsp          umidi          umiusb              │  L3
     (filter,synth)  (parser,       (audio,midi)        │  (umios とは独立)
     deps: umicore   protocol)     deps: umicore        │
                     deps: umicore       + umidsp       │
         │              │              │                │
         │              │              └─── umidsp (ASRC)
         │              │                               │
         └──────────────┼───────────────────────────────┘
                        │                               │
                        │                               │
     umiport ───────────┼───────────────────────────────┘  L2
     (arch,mcu,board)   │
     deps: umihal       │    umidevice
           + umimmio    │    (codecs, drivers)
                        │    deps: umihal + umimmio
         │              │         │
         └──────────────┼─────────┘
                        │
     umicore        umihal        umimmio                  L1
     (types,event,  (concepts)    (register,
      audio,error)                 transport)
     deps: なし     deps: なし    deps: なし


     umitest        umibench      umirtm                   L0 (テスト/デバッグ専用)
     deps: なし     deps: なし    deps: なし
```

**図の読み方:**
- `deps:` は直接依存 (`add_deps`) を表す。
- umios (L4) は umicore (L1) と umiport (L2) に直接依存するが、L3 には依存しない。
- L3 ライブラリ (umidsp, umidi, umiusb) は全て umicore (L1) に直接依存する。L2 には依存しない。umios とも独立しており、Application (L5) で初めて結合される。
- L0 は開発支援ツールであり、プロダクション依存グラフには参加しない。テスト時に各ライブラリの `tests/xmake.lua` から参照されるが、ライブラリ本体からの依存は禁止される。レイヤー番号の「0」は「最も基盤的」ではなく「プロダクション外」を意味する。

**L3 内の依存方向:** umiusb → umidsp（ASRC のため）。umidsp と umidi は互いに独立。

### 4.2 依存関係マトリクス（プロダクション依存のみ）

L0 (umitest, umibench, umirtm) はプロダクション依存グラフに参加しないため本マトリクスから除外する。

```
              core  hal  mmio  port  device  dsp  midi  usb  os
umicore        —    ×     ×     ×      ×      ×    ×     ×    ×
umihal         ×    —     ×     ×      ×      ×    ×     ×    ×
umimmio        ×    ×     —     ×      ×      ×    ×     ×    ×
umiport        ×    ○     ○     —      ×      ×    ×     ×    ×
umidevice      ×    ○     ○     ×      —      ×    ×     ×    ×
umidsp         ○    ×     ×     ×      ×      —    ×     ×    ×
umidi          ○    ×     ×     ×      ×      ×    —     ×    ×
umiusb         ○    ×     ×     ×      ×      ○    ×     —    ×
umios          ○    ×     ×     ○      ×      ×    ×     ×    —

○ = 直接依存あり   × = 依存なし
```

**テスト依存許可表（L0）:**

| L0 ライブラリ | プロダクション依存 | テスト時に依存されるライブラリ |
|--------------|:-----------------:|---------------------------|
| umitest | なし | 各ライブラリの `tests/xmake.lua` から参照 |
| umibench | なし | 各ライブラリの `tests/xmake.lua` から参照 |
| umirtm | なし | 各ライブラリの `tests/xmake.lua` から参照 |

> L0 への `add_deps` はテストターゲット (`tests/xmake.lua`) 内に限定し、ライブラリ本体の `xmake.lua` から参照してはならない。

**注記:**
- **間接依存:** umios は umiport 経由で umihal, umimmio に間接依存するが、直接 `add_deps` はしない。同様に umiusb は umidsp 経由で umicore に間接依存する。マトリクスは `add_deps` に記載される直接依存のみを表す。
- **L3 内依存:** umiusb → umidsp は同一レイヤー内の許可された依存（ASRC 機能のため）。umidsp と umidi は互いに独立。

### 4.3 依存ルール

1. **L0 (test, bench, rtm) はプロダクション依存禁止** — `add_deps` は `{private = true}` + テストターゲット限定
2. **循環依存は絶対禁止** — グラフは DAG でなければならない
3. **umicore は依存ゼロ** — すべての基盤となる唯一の依存元
4. **umihal は依存ゼロ** — Concept 定義のみ。実装は umiport が持つ
5. **umimmio は依存ゼロ** — レジスタアクセス基盤
6. **umios は umicore + umiport に依存するが、L3 (umidsp/umidi/umiusb) には依存しない** — L3 と L4 は独立であり、Application 層 (L5) で初めて結合する

---

## 5. ライブラリ詳細設計

### L0: Infrastructure (開発支援ツール)

#### umitest — テストフレームワーク

```
lib/umitest/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/
│   ├── DESIGN.md
│   ├── INDEX.md
│   ├── TESTING.md
│   └── ja/
├── include/umitest/
│   ├── test.hh              # umbrella header
│   ├── suite.hh             # Suite クラス
│   ├── context.hh           # TestContext (assert)
│   └── format.hh            # 値フォーマット
├── tests/
├── examples/
└── platforms/
    ├── host/
    └── wasm/
```

- **責務:** ベアメタル・WASM 対応の軽量テストフレームワーク
- **名前空間:** `umi::test`
- **依存:** なし

#### umibench — ベンチマークフレームワーク

- **責務:** サイクル精度のベンチマーク測定
- **名前空間:** `umi::bench`
- **依存:** なし

#### umirtm — リアルタイムモニタ

- **責務:** RTT/SWO ベースのデバッグ出力
- **名前空間:** `umi::rt`
- **依存:** なし

---

### L1: Foundation (型・概念・基盤)

#### umicore — コア型・概念定義

```
lib/umicore/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/
│   ├── DESIGN.md
│   ├── INDEX.md
│   ├── TESTING.md
│   └── ja/
├── include/umicore/
│   ├── core.hh              # umbrella header
│   ├── types.hh             # sample_t, port_id_t, constants
│   ├── error.hh             # Result<T>, Error enum
│   ├── event.hh             # Event, EventQueue<Cap>
│   ├── audio_context.hh     # AudioContext, StreamConfig
│   ├── processor.hh         # ProcessorLike concept
│   ├── shared_state.hh      # SharedParamState, etc.
│   ├── time.hh              # 時間変換ユーティリティ
│   ├── irq.hh               # 割り込み抽象 (backend-agnostic)
│   └── shell.hh             # シェル基盤ユーティリティ
├── tests/
└── examples/
```

- **責務:** 全プラットフォーム共通の型定義、Concept定義、エラー型
- **名前空間:** `umi::core`, `umi` (ProcessorLike 等のトップレベルConcept)
- **依存:** なし
- **設計判断:**
  - `AudioContext` はここに配置 — DSP や OS に依存せず、型定義のみ
  - `ProcessorLike` concept もここ — 実装は各ライブラリが提供
  - `Event` / `EventQueue` もここ — ルーティングロジックは umios に
  - `irq.hh` は既存 `lib/umi/core/irq.hh` を **移設** する。現行ファイルは backend-agnostic なインターフェース定義（`umi::irq::Handler`, `init()`, `set_handler()` 等）を持つ。移設時に API 差分を整理し、MCU 固有の実装（`lib/umi/port/common/irq.cc`）は umiport に残す

#### umihal — HAL Concept 定義

```
lib/umihal/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/
│   ├── DESIGN.md
│   ├── INDEX.md
│   ├── TESTING.md
│   └── ja/
├── include/umihal/
│   ├── hal.hh                # umbrella header
│   ├── arch.hh
│   ├── audio.hh
│   ├── board.hh
│   ├── codec.hh
│   ├── fault.hh
│   ├── gpio.hh
│   ├── i2c.hh
│   ├── i2s.hh
│   ├── interrupt.hh
│   ├── result.hh
│   ├── timer.hh
│   ├── uart.hh
│   └── concept/
│       ├── clock.hh
│       ├── codec.hh
│       ├── platform.hh
│       ├── transport.hh
│       ├── uart.hh
│       └── usb.hh            # USB HAL Concept (UsbDeviceLike 等)
├── tests/
│   ├── test_concepts.cc
│   └── compile_fail/
└── examples/
```

- **責務:** ハードウェア抽象化の Concept 定義のみ（実装なし）
- **名前空間:** `umi::hal`
- **依存:** なし
- **設計判断:** USB HAL Concept (`UsbDeviceLike`, `UsbEndpointLike` 等) もここに定義する。MCU 固有の USB 実装（STM32 OTG FS 等）は umiport が提供し、umiusb（L3）はプロトコル層のみを担当する。

#### umimmio — MMIO レジスタアクセス

- **責務:** メモリマップドI/O レジスタへの型安全なアクセス
- **名前空間:** `umi::mmio`
- **依存:** なし

---

### L2: Platform (ハードウェア抽象化)

#### umiport — プラットフォームポート

```
lib/umiport/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/
│   ├── DESIGN.md
│   ├── INDEX.md
│   ├── TESTING.md
│   └── ja/
├── include/umiport/
│   ├── port.hh              # umbrella header
│   ├── arch/
│   │   ├── cm4/             # Cortex-M4 (context switch, handlers)
│   │   └── cm7/             # Cortex-M7 (cache, context)
│   ├── mcu/
│   │   ├── stm32f4/         # STM32F4 ペリフェラル (RCC, GPIO, I2S, etc.)
│   │   └── stm32h7/         # STM32H7 ペリフェラル
│   ├── board/
│   │   ├── host/            # ホスト環境
│   │   ├── wasm/            # WASM 環境
│   │   ├── stm32f4_disco/   # STM32F4 Discovery
│   │   ├── stm32f4_renode/  # Renode シミュレータ
│   │   ├── daisy_seed/      # Daisy Seed
│   │   └── daisy_pod/       # Daisy Pod
│   ├── platform/
│   │   ├── embedded/        # 組込みカーネル環境 (syscall, protection)
│   │   ├── wasm/            # WASM 実行環境
│   │   └── renode/          # Renode 環境
│   └── common/              # 共通ユーティリティ (NVIC, SCB, DWT, IRQ)
├── src/
│   ├── arch/
│   │   ├── cm4/handlers.cc
│   │   └── cm7/handlers.cc
│   ├── common/irq.cc
│   ├── stm32f4/
│   │   ├── syscalls.cc
│   │   └── usb_otg.cc       # STM32 OTG FS USB HAL 実装
│   ├── host/write.cc
│   └── wasm/write.cc
├── rules/
│   └── board.lua            # ボード選択ルール
└── tests/
```

- **責務:** 全プラットフォームのハードウェア抽象化実装
- **名前空間:** `umi::port` (アーキテクチャ), `umi::board` (ボード固有)
- **依存:** umihal (L1), umimmio (L1, optional)
- **4層構造:** arch (CPU) → mcu (SoC) → board (基板) → platform (実行環境)

#### umidevice — デバイスドライバ

- **責務:** オーディオコーデック等の外部デバイスドライバ
- **名前空間:** `umi::device`
- **依存:** umihal (L1), umimmio (L1)

---

### L3: Domain (ドメイン固有ライブラリ)

#### umidsp — DSP アルゴリズム

```
lib/umidsp/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/
│   ├── DESIGN.md
│   ├── INDEX.md
│   ├── TESTING.md
│   └── ja/
├── include/umidsp/
│   ├── dsp.hh               # umbrella header
│   ├── constants.hh         # DSP 定数
│   ├── core/
│   │   ├── phase.hh         # 位相管理
│   │   └── interpolate.hh   # 補間
│   ├── filter/
│   │   ├── biquad.hh        # Biquad (multi-stage)
│   │   ├── svf.hh           # State Variable Filter
│   │   ├── moog.hh          # Moog ラダーフィルタ
│   │   ├── k35.hh           # K35 フィルタ
│   │   └── moving_average.hh
│   ├── synth/
│   │   ├── oscillator.hh    # オシレータ群
│   │   └── envelope.hh      # ADSR エンベロープ
│   └── audio/
│       └── rate/
│           ├── asrc.hh      # 非同期サンプルレート変換
│           └── pi_controller.hh
├── tests/
├── examples/
└── filter_ref/               # リファレンス実装
```

- **責務:** オーディオ信号処理アルゴリズム（フィルタ、シンセシス、レート変換）
- **名前空間:** `umi::dsp`
- **依存:** umicore (L1)

#### umidi — MIDI プロトコル

```
lib/umidi/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/
│   ├── DESIGN.md
│   ├── INDEX.md
│   ├── TESTING.md
│   └── ja/
├── include/umidi/
│   ├── midi.hh              # umbrella header
│   ├── core/
│   │   ├── ump.hh           # UMP32 型、エンコード/デコード
│   │   ├── parser.hh        # インクリメンタルパーサ
│   │   ├── sysex_assembler.hh
│   │   └── timestamp.hh
│   ├── messages/
│   │   ├── channel_voice.hh
│   │   ├── system.hh
│   │   └── sysex.hh
│   ├── cc/
│   │   ├── types.hh         # CC 列挙型
│   │   ├── standards.hh     # 標準 CC 定義
│   │   └── decoder.hh
│   ├── codec/
│   │   └── decoder.hh       # テンプレートベース静的デコーダ
│   └── protocol/
│       ├── umi_sysex.hh     # UMI SysEx プロトコル
│       ├── umi_auth.hh
│       ├── umi_bootloader.hh
│       ├── umi_firmware.hh
│       ├── umi_session.hh
│       ├── umi_state.hh
│       ├── umi_object.hh
│       └── umi_transport.hh
├── tests/
└── examples/
```

- **責務:** MIDI 1.0/2.0 プロトコル処理（パース、エンコード、UMI拡張プロトコル）
- **名前空間:** `umi::midi`
- **依存:** umicore (L1)

#### umiusb — USB デバイススタック

```
lib/umiusb/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/
│   ├── DESIGN.md
│   ├── INDEX.md
│   ├── TESTING.md
│   └── ja/
├── include/umiusb/
│   ├── usb.hh               # umbrella header
│   ├── core/
│   │   ├── types.hh
│   │   ├── device.hh
│   │   └── descriptor.hh
│   ├── audio/
│   │   ├── audio_types.hh
│   │   ├── audio_interface.hh
│   │   └── audio_device.hh
│   └── midi/
│       ├── usb_midi_class.hh
│       └── umidi_adapter.hh
├── tests/
└── examples/
```

- **責務:** USB Audio/MIDI クラスデバイスのプロトコル層実装（HAL 非依存）
- **名前空間:** `umi::usb`
- **依存:** umicore (L1), umidsp (L3 — ASRC のため)
- **設計判断:** USB HAL Concept は umihal に、MCU 固有の USB ドライバ実装（STM32 OTG FS 等）は umiport に配置する。umiusb はプロトコル層のみを担当し、プラットフォーム固有コードを含まない。これにより L3 にプラットフォーム依存が侵入することを防ぎ、他の HAL 実装（GPIO, I2C 等）と同一のパターンで USB 対応を拡張できる。

---

### L4: System (OS / カーネル統合)

#### umios — OS カーネル + サービス

```
lib/umios/
├── README.md
├── Doxyfile
├── xmake.lua
├── docs/
│   ├── DESIGN.md
│   ├── INDEX.md
│   ├── TESTING.md
│   └── ja/
├── include/umios/
│   ├── os.hh                # umbrella header
│   │
│   ├── kernel/              # カーネルコア
│   │   ├── kernel.hh        # Kernel<MaxTasks,MaxTimers,HW,MaxCores>
│   │   ├── startup.hh       # Bootstrap, LinkerSymbols
│   │   ├── monitor.hh       # StackMonitor, TaskProfiler
│   │   ├── metrics.hh       # KernelMetrics (opt-in)
│   │   ├── fault.hh         # FaultLog, FaultHandler
│   │   ├── driver.hh        # ドライバフレームワーク
│   │   ├── app_header.hh    # .umia バイナリフォーマット
│   │   ├── syscall.hh       # Syscall 番号・ハンドラ
│   │   └── concepts.hh      # AppLoaderLike 等の内部 Concept
│   │
│   ├── runtime/             # イベントルーティング
│   │   ├── event_router.hh  # EventRouter
│   │   ├── route_table.hh   # RouteTable
│   │   └── param_mapping.hh # ParamMapping, AppConfig
│   │
│   ├── service/             # カーネルサービス
│   │   ├── audio.hh         # AudioService
│   │   ├── midi.hh          # MidiService
│   │   ├── shell.hh         # Shell<HW,Kernel>
│   │   ├── loader.hh        # AppLoader
│   │   └── storage.hh       # StorageService
│   │
│   ├── ipc/                 # プロセス間通信
│   │   ├── spsc_queue.hh    # SpscQueue<T,Cap>
│   │   ├── triple_buffer.hh # TripleBuffer
│   │   └── notification.hh  # Notification<MaxTasks>
│   │
│   ├── app/                 # アプリケーション層
│   │   ├── syscall.hh       # ユーザー空間 syscall API
│   │   └── crt0.hh          # アプリケーション CRT0
│   │
│   └── adapter/             # プラットフォームアダプタ
│       ├── embedded.hh      # 組込みアダプタ
│       ├── wasm.hh          # WASM アダプタ
│       └── umim.hh          # UMIM プラグインアダプタ
│
├── src/
│   ├── service/
│   │   └── loader.cc
│   └── crypto/              # 暗号 (カーネル内部)
│       ├── sha256.cc
│       ├── sha512.cc
│       └── ed25519.cc
│
├── tests/
└── examples/
```

- **責務:** リアルタイムカーネル、サービス、IPC、アプリケーションフレームワーク
- **名前空間:** `umi::os` (kernel), `umi::os::service`, `umi::os::ipc`
- **依存:** umicore (L1), umiport (L2)

##### umios 内部アーキテクチャ

```
umios 内部の依存階層:

kernel/ (コア: スケジューラ, MPU, 割込み)
    ↑
    │  kernel は上位に依存しない (最下層)
    │
ipc/ (共有データ構造: SpscQueue, TripleBuffer)
    ↑
    │  ipc は kernel/service の両方から利用される中立層
    │
runtime/ (イベントルーティング: EventRouter, RouteTable)
    ↑
    │  runtime は kernel API を使用
    │
service/ (各サービス: Audio, MIDI, Shell, Loader, Storage)
    ↑
    │  service は kernel + runtime + ipc を使用
    │
adapter/ (プラットフォームアダプタ: embedded, wasm, umim)
    │  adapter は全層を結合し、エントリポイントを提供
```

kernel は service に依存しない。service が kernel を使用する。この方向を維持するため:
- SyscallHandler はテンプレートパラメータで StorageType, LoaderType を注入
- AppLoaderLike 等の Concept を kernel/concepts.hh に定義し、実装は service/ に配置

##### umios xmake ターゲット

```lua
-- lib/umios/xmake.lua
target("umios")
    set_kind("headeronly")
    add_deps("umicore", "umiport")
    add_headerfiles("include/(umios/**.hh)")
    add_includedirs("include", { public = true })
target_end()

-- サービスローダー (static: .cc ファイルを含む)
target("umios.service")
    set_kind("static")
    add_deps("umios")
    add_files("src/service/loader.cc")
target_end()

-- 暗号ライブラリ (static: .cc ファイルを含む)
target("umios.crypto")
    set_kind("static")
    add_deps("umios")
    add_files("src/crypto/sha256.cc", "src/crypto/sha512.cc", "src/crypto/ed25519.cc")
target_end()
```

> **設計判断:** `loader.cc` はリンク時にアプリケーションターゲットから参照される実体を含むため、ヘッダオンリーではなく static ターゲットとして分離する。アプリケーションは `add_deps("umios.service")` で明示的にリンクする。

---

## 6. プラットフォーム抽象化パターン

### 6.1 標準ディレクトリ構造

プラットフォーム固有のコードを持つライブラリは、以下の統一パターンに従う:

```
lib/<libname>/
├── include/<libname>/        # プラットフォーム非依存ヘッダ
├── platforms/
│   ├── common/               # 共通基底・トレイト
│   │   └── platform_base.hh
│   ├── host/
│   │   └── <libname>/
│   │       └── platform.hh
│   ├── wasm/
│   │   └── <libname>/
│   │       └── platform.hh
│   └── embedded/
│       └── <libname>/
│           └── platform.hh
└── xmake.lua                 # プラットフォーム選択ロジック
```

> **標準仕様との差分:** `lib/docs/standards/LIBRARY_SPEC.md` v2.0.0 では `platforms/arm/cortex-m/<board>/` を使用しているが、これは umiport 固有の 4 層構造（§6.3）に対応した表記である。一般ライブラリでは上記の `platforms/embedded/` を使用する。umiport のみが `arch/mcu/board/platform` の 4 層構造を持つ。

### 6.2 プラットフォーム選択メカニズム

```lua
-- xmake.lua での標準パターン
if is_plat("wasm") then
    add_includedirs("platforms/wasm", { public = true })
elseif is_plat("cross") then
    add_includedirs("platforms/embedded", { public = true })
else
    add_includedirs("platforms/host", { public = true })
end
```

### 6.3 umiport の特殊な 4 層構造

umiport のみ、ハードウェアの階層に対応した特殊構造を持つ:

```
arch/     → CPU アーキテクチャ (cm4, cm7)
mcu/      → SoC 固有 (stm32f4, stm32h7)
board/    → ボード固有 (stm32f4_disco, daisy_seed)
platform/ → 実行環境 (embedded, wasm, renode)
```

この 4 層は xmake の `board.lua` ルールで動的に選択される:

```lua
-- board.lua (簡略版)
function on_load(target)
    local board = target:values("umiport.board")
    target:add("includedirs", "include/umiport/board/" .. board)
    -- MCU/arch は board 設定から自動推論
end
```

---

## 7. xmake ビルドシステム

### 7.1 ルート xmake.lua

```lua
set_project("umi")
set_version("0.3.0")
set_xmakever("2.8.0")

-- L0: Infrastructure
includes("lib/umitest")
includes("lib/umibench")
includes("lib/umirtm")

-- L1: Foundation
includes("lib/umicore")
includes("lib/umihal")
includes("lib/umimmio")

-- L2: Platform
includes("lib/umiport")
includes("lib/umidevice")

-- L3: Domain
includes("lib/umidsp")
includes("lib/umidi")
includes("lib/umiusb")

-- L4: System
includes("lib/umios")

-- Bundles
includes("lib/umi")

-- Applications
includes("examples/stm32f4_kernel")
includes("examples/headless_webhost")

-- Tools
includes("tools/release.lua")
includes("tools/dev-sync.lua")
```

### 7.2 各ライブラリの xmake.lua パターン

```lua
-- 標準パターン (headeronly)
target("<libname>")
    set_kind("headeronly")
    add_headerfiles("include/(<libname>/**.hh)")
    add_includedirs("include", { public = true })
    add_deps("<dependency1>", "<dependency2>")  -- 必要な場合のみ
target_end()

-- テスト (別ファイル推奨)
includes("tests")
```

### 7.3 便利バンドル

```lua
-- lib/umi/xmake.lua
target("umi.base")
    set_kind("headeronly")
    add_deps("umicore", "umidsp", "umidi")
target_end()

target("umi.wasm.full")
    set_kind("headeronly")
    add_deps("umicore", "umidsp", "umidi")
target_end()

target("umi.embedded.full")
    set_kind("headeronly")
    add_deps("umicore", "umidsp", "umidi", "umiusb", "umios")
target_end()
```

---

## 8. 品質基準

### 8.1 全ライブラリ共通（UMI Strict Profile）

本プロジェクトは LIBRARY_SPEC v2.0.0 を基盤としつつ、以下の **UMI Strict Profile** を上乗せする。
v2.0.0 で任意とされている項目のうち、UMI では必須に格上げしたものを明示する。

| 項目 | v2.0.0 | UMI Strict Profile | 差分理由 |
|------|:------:|:-----------------:|---------|
| README.md | 必須 | 必須 | — |
| DESIGN.md | 必須 | 必須（11セクション） | UMI 標準テンプレートに拡張 |
| INDEX.md | — | **必須** | API リファレンスマップとして全ライブラリに要求 |
| TESTING.md | — | **必須** | テスト戦略・品質ゲートの明文化 |
| docs/ja/ | **任意** | **必須** | 日英バイリンガル開発チーム対応 |
| Doxyfile | 必須 | 必須 | — |
| compile_fail/ | **任意** | **条件付き必須** | Concept 定義ライブラリのみ必須（§8.2 参照） |
| examples/ | 任意 | **必須** | 最低1つの minimal example |

> **正本関係:** `lib/docs/standards/LIBRARY_SPEC.md` が v2.0.0 の正本。本文書の UMI Strict Profile は v2.0.0 を拡張するものであり、矛盾する場合は本文書（UMI Strict Profile）が優先する。

### 8.2 テストレベル

| レベル | 対象 | 要件 |
|--------|------|------|
| **ユニットテスト** | 全ライブラリ | 公開 API の主要パスをカバー |
| **compile-fail テスト** | Concept を定義するライブラリ (umihal, umimmio, umitest 等) | 不正な型がコンパイルエラーになることを検証 |
| **統合テスト** | L2 以上のライブラリ (umiport, umidevice, umios) | レイヤーを跨ぐ最低1つの統合シナリオ |

**統合テスト例:**
- umiport + umidevice: I2C 経由でのオーディオコーデック初期化シーケンス
- umios + umiport: カーネル起動 → SysTick → タスクスケジュール
- umiusb + umidsp: USB Audio ストリーム + ASRC パイプライン

### 8.3 目標スコア

全ライブラリが **★★★★☆ (4/5) 以上** を達成すること。

---

## 9. 検証基準

### 9.1 構造的正当性

- [ ] 全ライブラリが lib/ 直下にスタンドアロンとして存在する
- [ ] lib/umi/ にはバンドル定義のみ残る
- [ ] 依存関係グラフが DAG である（循環なし）
- [ ] レイヤー規則（下位のみ依存）が守られている
- [ ] 全ライブラリが `xmake test` で独立テスト可能

### 9.2 ドキュメント完成度

- [ ] 全ライブラリに README.md, DESIGN.md, INDEX.md, TESTING.md が存在する
- [ ] docs/ja/ が全ライブラリに存在する
- [ ] 全ライブラリに Doxyfile が存在する

### 9.3 テスト完成度

- [ ] 全ライブラリにユニットテストが存在する
- [ ] Concept 定義ライブラリ (umihal, umimmio, umitest) に compile-fail テストが存在する
- [ ] L2 以上のライブラリに最低1つの統合テストシナリオが存在する

### 9.4 レイヤー規則検証

- [ ] 各ライブラリの `add_deps` が §4.2 マトリクスと一致する
- [ ] L0 ライブラリへの `add_deps` がテストターゲット外に存在しない
- [ ] L3 内の依存方向が umiusb → umidsp のみ（逆方向なし）

---

## 10. 設計判断の根拠

§1.2 の分割基準 4 条件に対する各設計判断の照合:

| 判断 | 独立デプロイ | 責務境界 | 安定API | 再利用性 | 結論 |
|------|:----------:|:-------:|:------:|:-------:|------|
| synth を dsp に吸収 | ✗ (2ファイル、単独では不足) | △ (DSP と密結合) | ○ | △ | **吸収**: 分割しない基準「3ファイル以下」に該当 |
| crypto を umios 内部に | ✗ (カーネル署名検証専用) | ○ | ○ | △ (汎用だが UMI 固有用途) | **内部**: 常にカーネルと一緒に変更される |
| fs を umios 内部に | ✗ (StorageService と密結合) | ○ | △ | △ | **内部**: 単独では意味をなさない。ただし WASM 環境で非カーネルファイルシステム利用が必要になった場合は再分離を検討 |
| shell を umicore に吸収 | ✗ (2ファイル) | △ (基盤ユーティリティ) | ○ | △ | **吸収**: 分割しない基準「3ファイル以下」に該当 |
| umiport に umi::board 名前空間 | — | ○ (arch/mcu と board の責務分離) | ○ | — | **採用**: ボード固有定義は port 層の一部だが名前空間で区別 |

### 10.1 ADR: umios → umiport 直接依存

**背景:** 初期調査（INVESTIGATION.md, ANALYSIS.md）では「kernel は umicore のみに依存」を高評価した。本仕様では umios が umiport に直接依存する設計を採用している。

**検討した案:**

| 案 | 構造 | 利点 | 欠点 |
|---|------|------|------|
| **A: 独立性優先** | umios.kernel → umicore のみ、umios.adapter → umiport | kernel 単体テストが容易、port 差し替えが明示的 | xmake ターゲットが増加、adapter↔kernel の接合が複雑化 |
| **B: 統合優先（採用）** | umios → umicore + umiport | ターゲット構成が単純、adapter が port API を直接利用可能 | kernel テスト時に port のスタブが必要 |

**採用理由:**
1. umios の adapter 層は umiport の HAL 実装を直接呼び出す（SysTick, GPIO, UART 等）。これは kernel ではなく adapter の責務であり、umios 全体として umiport に依存するのは自然
2. kernel 内部は umiport に依存しない設計を維持する（§5 の umios 内部アーキテクチャ参照）。依存方向の制約は xmake ターゲット分割ではなくコードレビューで担保する
3. 案 A の分割は将来的に必要になった時点で実施可能（後方互換を壊さない）

---

## 11. 将来拡張候補

以下のモジュールは現時点では12ライブラリ構成に含めないが、将来的にスタンドアロンライブラリへの昇格を検討する。

| モジュール | 現行パス | 候補名 | 想定レイヤー | 昇格条件 |
|-----------|---------|--------|:----------:|---------|
| コルーチン | lib/umi/coro/ | — | — | 使用実績を確認。未使用ならアーカイブ。使用中なら umicore に吸収（1ファイル510行） |
| グラフィックス | lib/umi/gfx/ + lib/umi/ui/ | umigfx | L3 (Domain) | UI/描画の需要が確定し、12ファイル1,159行を超える規模に成長した時点 |

**移行期間中の扱い:**
- coro: lib/umi/coro/ に保持。MIGRATION_PLAN Phase M5 で判断
- gfx/ui: lib/umi/gfx/ および lib/umi/ui/ に保持。移行対象外

---

## 用語集

| 用語 | 定義 |
|------|------|
| **モジュール** | lib/umi/ 配下のディレクトリ単位 (例: lib/umi/dsp/) |
| **ライブラリ** | lib/ 直下のスタンドアロン単位。LIBRARY_SPEC v2.0.0 に準拠する (例: lib/umidsp/) |
| **バンドル** | 複数ライブラリをまとめて依存追加する便利ターゲット (例: umi.base) |
| **DAG** | 有向非巡回グラフ。ライブラリ依存関係の循環を禁止する制約 |
