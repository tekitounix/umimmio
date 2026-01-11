# UMI-OS と MOS-STM32 の設計比較

## 概要

**UMI-OS** は電子楽器のためのミニマルRTOSである。
汎用OSの全機能を実装するのではなく、**オーディオ処理に必要十分な機能のみ**を提供する。

**MOS-STM32** (https://github.com/Eplankton/mos-stm32) は STM32F4xx 向けの汎用 C++ RTOS で、
豊富な同期プリミティブ（Barrier, CondVar, Mutex等）を実装している。

### 設計方針の違い

| 観点 | MOS-STM32 | UMI-OS |
|------|-----------|--------|
| 目的 | 汎用RTOS | 電子楽器特化 |
| 同期プリミティブ | 豊富（5種類以上） | 最小限（3種類） |
| IPC | MPSC, タイムアウト対応 | SPSC のみ |
| シェル | あり | あり（デバッグ用、MIDI SysEx経由出力も可）|
| コードサイズ | 大 | 小（ヘッダオンリー）|

本文書では両者を比較し、UMI-OS に**本当に必要な機能のみ**を見極める。

---

## 1. アーキテクチャ比較

### MOS-STM32 のディレクトリ構造
```
USR/src/
├── core/
│   ├── kernel/          # アーキテクチャ非依存のカーネル層
│   │   ├── macro.hpp    # 定数マクロ
│   │   ├── type.hpp     # 基本型
│   │   ├── concepts.hpp # 型制約
│   │   ├── data_type.hpp # データ構造
│   │   ├── alloc.hpp    # メモリ管理
│   │   ├── global.hpp   # グローバル変数
│   │   ├── task.hpp     # タスク制御
│   │   ├── sync.hpp     # 同期プリミティブ
│   │   ├── scheduler.hpp # スケジューラ
│   │   └── ipc.hpp      # IPC (MsgQueue)
│   ├── kernel.hpp       # カーネルモジュール統合
│   └── shell.hpp        # コマンドラインシェル
├── drivers/
│   └── stm32f4xx/       # HAL ラッパー
│       ├── hal.hpp
│       ├── gpio.hpp
│       ├── uart.hpp
│       ├── spi.hpp
│       └── systick.hpp
├── arch/                # アーキテクチャ依存コード (別ディレクトリ想定)
└── user/                # ユーザーアプリケーション
```

### UMI-OS の現在の構造
```
core/
├── umi_kernel.hh        # 全機能統合 (971行)
├── umi_audio.hh
├── umi_midi.hh
├── umi_coro.hh
├── umi_app.hh
├── umi_app_types.hh
├── umi_hw_interface.hh  # syscalls用HWインターフェース
└── umi_startup.hh
platform/
├── monolithic/
│   └── umi_hw.hh        # HW実装
├── microkernel/
│   └── umi_hw.hh
├── startup.cc
├── syscalls.cc
└── stm32f4.ld
```

### 評価

| 観点 | MOS-STM32 | UMI-OS | 改善案 |
|------|-----------|--------|--------|
| モジュール分離 | ✅ task/sync/scheduler/ipc 分離 | ⚠️ umi_kernel.hh に全統合 | カーネルを複数ファイルに分割検討 |
| アーキテクチャ層 | ✅ arch/ で明確に分離 | ⚠️ platform/ に混在 | arch/cortex-m/, arch/riscv/ 追加 |
| ドライバ層 | ✅ HAL ラッパー充実 | ❌ なし（ヘッダオンリー方針） | ユーザー領域で提供する想定でOK |

---

## 2. タスク管理

### MOS-STM32
```cpp
// タスク作成 (任意の引数型サポート)
Task::create(led_init, &leds, 2, "led/init");

// 泛型関数シグネチャ対応
Task::create([](auto arg) { /* ... */ }, arg, pri, "name");

// タスク終了時の自動リソース回収
Task::terminate(); // exit時に暗黙呼び出し

// 優先度取得・タスク名取得
auto pri = Task::current()->get_pri();
auto name = Task::current()->get_name();
```

### UMI-OS
```cpp
// タスク作成
kernel.create_task(TaskConfig{
    .entry = my_task,
    .arg = &data,
    .prio = Priority::User,
});

// TaskId による管理
TaskId id = kernel.create_task(config);
kernel.notify(id, Event::AudioReady);
```

### 取り入れるべき点 → ✅ 実装済
1. **タスク名サポート**: `TaskConfig::name` フィールド追加済
2. **タスク状態取得**: `get_task_name()`, `get_task_state_str()`, `for_each_task()`

### 実装済みコード
```cpp
// core/umi_kernel.hh - TaskConfig
struct TaskConfig {
    void (*entry)(void*) {nullptr};
    void* arg {nullptr};
    Priority prio {Priority::Idle};
    std::uint8_t core_affinity {static_cast<std::uint8_t>(Core::Any)};
    bool uses_fpu {false};
    const char* name {"<unnamed>"};  // ✅ 実装済
};

// カーネルAPIに追加されたメソッド
const char* get_task_name(TaskId id) const;
Priority get_task_priority(TaskId id) const;
const char* get_task_state_str(TaskId id) const;

template<typename Fn>
void for_each_task(Fn&& fn) const;  // シェルのps用
```

---

## 3. 同期プリミティブ（電子楽器向け評価）

### MOS-STM32
```cpp
// 豊富な同期プリミティブ
Sync::Sema_t sema;         // セマフォ
Sync::Lock_t lock;         // スピンロック
Sync::Mutex_t<T> mutex;    // データ保護付きMutex
Sync::CondVar_t cv;        // 条件変数
Sync::Barrier_t bar{2};    // バリア
```

### UMI-OS（現状）
```cpp
// 電子楽器に必要十分なプリミティブ
SpscQueue<T, N> queue;      // MIDI/パラメータ伝送
Notification<MaxTasks> n;   // イベント通知
MaskedCritical<HW> cs;      // 割り込み禁止区間
```

### 電子楽器における必要性評価

| プリミティブ | MOS | UMI | 電子楽器での必要性 |
|-------------|-----|-----|-------------------|
| **SpscQueue** | ✅ | ✅ | ✅ 必須: MIDI, パラメータ, ログ |
| **Notification** | ✅ | ✅ | ✅ 必須: DMA完了, VSync |
| **CriticalSection** | ✅ | ✅ | ✅ 必須: 共有リソース保護 |
| Semaphore | ✅ | ❌ | ❌ 不要: SpscQueue で代替可 |
| Mutex | ✅ | ❌ | ❌ 不要: CriticalSection で十分 |
| CondVar | ✅ | ❌ | ❌ 不要: Notification で代替可 |
| Barrier | ✅ | ❌ | ❌ 不要: 電子楽器に複雑な起動同期なし |

### 結論
**UMI-OS の現状で十分。追加プリミティブは不要。**

電子楽器の典型的なデータフロー:
```
[MIDI ISR] --SpscQueue--> [Audio Task] --DMA--> [DAC]
                               ^
                               |
                          Notification (buffer complete)
```

このパターンは `SpscQueue` + `Notification` で完全にカバーされる。

---

## 3.5. C++20/23 コルーチン比較

### MOS-STM32
```cpp
// 実験的 (Experimental) - README に記載
namespace Async {
    template <typename T>
    struct Future_t;
    
    // async/await パターン
    Future_t<int> async_operation() {
        co_await some_awaitable;
        co_return result;
    }
}
```

- **状態**: 実験的 (Experimental)
- **実装**: スタックレスコルーチン
- **ドキュメント**: README に `Async::{Future_t, async/await}` として記載

### UMI-OS
```cpp
// core/umi_coro.hh - 完全実装
namespace umi::coro {
    template <typename T = void>
    struct Task {
        struct promise_type {
            Task get_return_object();
            std::suspend_always initial_suspend() noexcept;
            std::suspend_always final_suspend() noexcept;
            void return_value(T value);
            void unhandled_exception();
        };
        
        // Awaitable interface
        bool await_ready() const noexcept;
        void await_suspend(std::coroutine_handle<> h);
        T await_resume();
    };
    
    // Task<void> 特殊化も完備
    
    struct Scheduler {
        void schedule(std::coroutine_handle<> h);
        void run();  // イベントループ
    };
}
```

- **状態**: 本番利用可能
- **実装**: `<coroutine>` ヘッダ使用、完全な promise_type
- **特徴**: `Task<T>` / `Task<void>` 両対応、Scheduler クラス

### 実装詳細比較

| 観点 | MOS-STM32 `Async::` | UMI-OS `umi::coro::` |
|------|---------------------|---------------------|
| 成熟度 | 実験的 | 本番対応 |
| 戻り値型 | `Future_t<T>` | `Task<T>` |
| void 特殊化 | 不明 | ✅ `Task<void>` |
| スケジューラ統合 | 不明 | ✅ `Scheduler` クラス |
| promise_type | 不明 | ✅ 完全実装 |
| 例外処理 | 不明 | `unhandled_exception()` |

### 結論
UMI-OS のコルーチン実装は MOS-STM32 より成熟している。
MOS から学ぶ点は少なく、むしろ UMI の設計を維持すべき。

---

## 4. IPC（電子楽器向け評価）

### MOS-STM32
```cpp
// 汎用メッセージキュー (MPSC)
using MsgQ_t = IPC::MsgQueue_t<int, 3>;
auto [status, msg] = msg_q.recv(200_ms);
```

### UMI-OS（現状）
```cpp
// SPSC キュー (wait-free)
SpscQueue<midi::Event, 64> midi_queue;
SpscQueue<ParamChange, 32> param_queue;
```

### 電子楽器におけるIPC

典型的なデータフロー:

```
[MIDI ISR] ──SpscQueue──> [Audio Task]
                              │
[Encoder ISR] ──SpscQueue────┘
                              │
                          [Audio Output]
```

**特徴**:
- Producer は常に1つ（ISR or 特定タスク）
- Consumer は常に1つ（オーディオタスク）
- MPSC は不要

### 評価

| 機能 | MOS | UMI | 電子楽器での必要性 |
|------|-----|-----|-------------------|
| SPSC Queue | ✅ | ✅ | ✅ 必須 |
| MPSC Queue | ✅ | ❌ | ❌ 不要（複数producerなし）|
| タイムアウト recv | ✅ | ❌ | 🔶 有用だがなくても可 |

### 結論
**UMI-OS の SPSC キューで十分。MPSC は追加しない。**

タイムアウト recv が必要な場合は、`wait()` と組み合わせる:
```cpp
// タイムアウト待機パターン（既存機能で実現可能）
if (kernel.wait(Event::MidiReady, 1000_usec)) {
    if (auto ev = midi_queue.try_pop()) {
        process(*ev);
    }
}
```

---

## 5. スケジューラ（電子楽器向け評価）

### MOS-STM32
```cpp
// PreemptPri + RoundRobin (同一優先度)
Scheduler::Policy::PreemptPri

// 遅延
Task::delay(250_ms);
```

### UMI-OS（現状）
```cpp
// 優先度ベース + 協調的 yield
kernel.yield();
kernel.wait(Event::AudioReady, timeout);

// プリエンプション: SysTick / PendSV
```

### 電子楽器におけるスケジューリング

電子楽器のタスク構成は固定的:

| 優先度 | タスク例 | 同一優先度の複数タスク？ |
|--------|---------|----------------------|
| Realtime | オーディオ ISR | ❌ 1つのみ |
| Server | MIDI, USB, I2C | 🔶 稀（イベント駆動） |
| User | UI, LED, エンコーダ | 🔶 稀（協調的） |
| Idle | スリープ | ❌ 1つのみ |

**ラウンドロビンが必要な場面がほぼない。**

### 追加する機能

```cpp
// ✅ delay() のみ追加
void Kernel::delay(usec duration) {
    usec deadline = HW::monotonic_time_usecs() + duration;
    call_later(deadline, [this] { 
        // wake current task 
    });
    block_current_task();
}
```

用途:
- LED点滅: `delay(500000)` (500ms)
- デバウンス: `delay(20000)` (20ms)
- 起動シーケンス: ハードウェア安定待ち

### 追加しない機能

- **ラウンドロビン**: 電子楽器では不要
- **スケジューラ停止/再開**: `MaskedCritical` で十分

---

## 6. Renode 対応 ✅ 完了

### UMI-OS の対応状況
- `renode/stm32f4.repl` - プラットフォーム定義
- `renode/run.resc` - 起動スクリプト
- ✅ **解決済み**: C++ ベクターテーブル (`startup.cc` 内の `.isr_vector` セクション)

### 修正内容
1. **ベクターテーブル追加** (`platform/startup.cc`):
   - `.isr_vector` セクションに完全なベクターテーブルを配置
   - 初期SP、Reset_Handler、NMI、HardFault 等のコアハンドラ
   - STM32F4の外部割り込み (IRQ0-81)
   
2. **ブートエイリアス設定** (`renode/run.resc`):
   - ARM Cortex-Mはアドレス0x0からベクターテーブルを読む
   - STM32はFLASH (0x08000000) を0x0にマッピング
   - Renodeスクリプトでベクターテーブルの先頭2ワードをコピー

```cpp
// startup.cc - C++でベクターテーブルを定義
__attribute__((section(".isr_vector"), used))
const std::uintptr_t g_vector_table[] = {
    reinterpret_cast<std::uintptr_t>(&_estack),  // 初期SP
    reinterpret_cast<std::uintptr_t>(&_start),   // Reset Handler
    // ... 以下続く
};
```

### アセンブリ vs C++ の選択

| 観点 | アセンブリ | C++ |
|------|-----------|-----|
| 可読性 | △ アセンブリ知識必要 | ◎ C++で統一 |
| 保守性 | △ 別ファイル管理 | ◎ startup.cc に統合 |
| 柔軟性 | ◎ 完全制御 | ○ 十分な制御可能 |
| 移植性 | △ アーキテクチャ毎に必要 | ◎ constexprで抽象化可能 |

**結論**: UMI-OSはC++ベクターテーブルで十分に動作。アセンブリは不要。
    
    // .data コピー
    ldr r0, =_sdata
    ldr r1, =_edata
    ldr r2, =_sidata
    ...
    
    // .bss ゼロ化
    ldr r2, =_sbss
    ldr r4, =_ebss
    ...
    
    // 静的初期化
    bl __libc_init_array
    
    // main 呼び出し
    bl main
    b .
```

---

## 7. デバッグ機能（電子楽器向け）

### MOS-STM32
```cpp
// 対話式シェル
Shell::launch(&stdio.buf);
```

### UMI-OS のアプローチ
電子楽器でもデバッグシェルは有用。出力先を柔軟に:

1. **UART**: 開発時のシリアルコンソール
2. **MIDI SysEx**: 製品版でもデバッグ可能（USBホスト経由）
3. **USB CDC**: 仮想COMポート

### 実装済みデバッグ機能 (`core/umi_shell.hh`)

```cpp
// シェル出力関数（UART/SysEx/USB CDCを差し替え可能）
using ShellWriteFn = void (*)(const char* str);

// MIDI SysEx経由でprint
void sysex_write(const char* str) {
    // 0xF0 <manufacturer> <data...> 0xF7
    midi_send_sysex(str, strlen(str));
}

// シェル初期化
umi::Shell<HW, Kernel> shell(kernel, sysex_write);  // or uart_write
```

コマンド: `ps`, `mem`, `load`, `reboot`, `help`

---

## 8. エラーハンドリング（電子楽器向け）

### 方針
電子楽器では「音が止まらない」ことが最優先。
複雑なエラー型よりも、シンプルなフォールバックを重視。

### 既存の対応
```cpp
// std::optional で十分
std::optional<T> try_pop();
std::optional<TaskId> create_task(...);

// クリティカルエラーは即座にミュート
[[noreturn]] void HardFault_Handler() {
    HW::mute_audio_dma();  // 音を止める
    // CrashDump 保存
    while (true) {}
}
```

### 追加しない理由
- `Result<T, E>`: `std::optional` で十分
- `Option<T>`: C++17 以降は `std::optional` が標準

**現状維持。追加機能なし。**
};
```

---

## 9. ビルドシステム / テスト

### MOS-STM32
- Keil / GCC 両対応
- STM32Cube HAL 互換

### UMI-OS
- xmake ベース
- ホストテスト + Renode テスト (WIP)

### 改善ポイント
- Renode CI テスト自動化
- `xmake test` でホスト + エミュレータ両方実行

---

## 10. 実装優先順位（電子楽器向け再評価 v2）

### 設計方針
UMI-OSは**電子楽器のためのミニマルRTOS**である。
汎用OSの機能を全て取り込むのではなく、以下の基準で取捨選択する:

| 基準 | 説明 |
|------|------|
| ✅ 採用 | 電子楽器の実装に有用で、設計に合致 |
| 🔶 検討 | 有用だが、追加コストとのバランス |
| ❌ 不採用 | 汎用OS向け、または設計方針に反する |

### MOS-STM32 機能の再評価（v2）

| MOS機能 | 評価 | 理由 |
|---------|------|------|
| `Task::delay()` (ブロッキング) | ❌ 不採用 | ブロッキングAPIは避ける。コルーチンで `co_await sleep(ms)` を使うべき |
| `co_await sleep()` | ✅ 採用 | 非ブロッキング遅延。`umi_coro.hh` に awaiter 追加 |
| タスク名 | ✅ 採用 | デバッグ用。`const char*` 1フィールド |
| シェル | ✅ 採用 | 状態確認・動作テストに有用。最小実装で |
| ラウンドロビン | ✅ 採用 | User/Idle 優先度でバックグラウンドタスク公平化 |
| `Result<T,E>` | 🔶 検討 | Rust風エラーハンドリング。便利だが `std::expected` (C++23) で代替可 |
| Barrier | ❌ 不採用 | 電子楽器では不要 |
| CondVar | ❌ 不採用 | `SpscQueue` + `Notification` で代替可 |
| MPSC キュー | ❌ 不採用 | 電子楽器のデータフローはSPSCで完結 |

### 各機能の詳細検討

#### 1. `co_await sleep(usec)` ✅ 採用
```cpp
// ブロッキング delay() は使わない
// kernel.delay(1000);  // NG: タスク全体がブロック

// 代わりにコルーチンで非ブロッキング待機
co_await sleep(1000_usec);  // OK: 他のコルーチンに譲る
```

✅ 実装済 (`core/umi_coro.hh`)
```cpp
// SleepAwaiter - 非ブロッキングスリープ
template<std::size_t MaxCoros>
class SleepAwaiter {
    Scheduler<MaxCoros>& sched_;
    usec deadline_;
public:
    explicit SleepAwaiter(Scheduler<MaxCoros>& s, std::chrono::microseconds dur);
    bool await_ready() const noexcept;
    void await_suspend(std::coroutine_handle<> h) noexcept;
    void await_resume() noexcept {}
};

// chrono literals サポート
namespace literals {
    inline auto operator""_ms(unsigned long long v);
    inline auto operator""_us(unsigned long long v);
}

// 使用例
using namespace umi::coro::literals;
co_await ctx.sleep(100ms);  // SchedulerContext経由
co_await 500ms;             // operator co_await (ルーチン内から)
```

#### 2. タスク名 ✅ 採用
```cpp
struct TaskConfig {
    // ... existing fields ...
    const char* name {"<anon>"};  // デバッグ・シェル表示用
};
```

追加コスト: `sizeof(const char*)` = 4バイト/タスク

#### 3. シェル ✅ 実装済 (`core/umi_shell.hh`)
```cpp
// 実装済みコマンドセット
// ps     - タスク一覧表示 (ID, 名前, 優先度, 状態)
// mem    - メモリ使用量 (共有リージョン、スタック/ヒープ)
// load   - CPU負荷/稼働時間、アクティブタスク数
// reboot - システムリセット
// help   - ヘルプ表示
```

用途:
- 開発時の状態確認
- 工場テスト
- フィールドデバッグ（UART/USB経由）

使用例:
```cpp
#include "umi_shell.hh"

void uart_write(const char* str) { /* UART送信 */ }
umi::Shell<HW, decltype(kernel)> shell(kernel, uart_write);

// UART受信割り込みで呼び出し
void USART2_IRQHandler() {
    char c = USART2->DR;
    shell.process_char(c);
}

// 出力例:
umi> ps
ID  Name            Prio     State
--  --------------  -------  -------
 0  audio           RT       Blocked
 1  ui              User     Ready
 2  idle            Idle     Ready
```

#### 4. ラウンドロビン ✅ 採用
```
優先度構成:
  Realtime (0): オーディオISR - 1タスク固定
  Server   (1): MIDI, USB    - イベント駆動、ラウンドロビン不要
  User     (2): UI, シェル   - ★ ラウンドロビン対象
  Idle     (3): スリープ     - 1タスク固定
```

User優先度に複数タスクがある場合:
- UIタスク
- シェルタスク  
- ユーザー定義バックグラウンドタスク

これらを公平に実行するためラウンドロビンは有用。

✅ 実装済 (`core/umi_kernel.hh` - `get_next_task()`)
```cpp
// last_user_idx ベースのラウンドロビン
std::optional<std::uint16_t> get_next_task() {
    auto start_idx = (last_user_idx + 1) % tasks.size();
    
    for (std::uint16_t count = 0; count < tasks.size(); ++count) {
        std::uint16_t i = (start_idx + count) % tasks.size();
        auto& t = tasks[i];
        
        // 高優先度 (Realtime) は常に優先
        if (t.cfg.prio < Priority::User) {
            // 最高優先度タスクを選択
        }
        // User優先度: 最初に見つかったものを選択 (開始位置がローテート)
        else if (t.cfg.prio == Priority::User && !first_user.has_value()) {
            first_user = TaskId{i};
        }
    }
    
    if (first_user.has_value()) {
        last_user_idx = first_user->value;  // 次回のため記録
        return first_user->value;
    }
    // ...
}
```

この設計により:
- Realtime タスクは常に最優先で実行
- User 優先度タスクはラウンドロビンで公平に実行
- Idle タスクは他に Ready タスクがない場合のみ実行

#### 5. `Result<T, E>` ✅ 実装済 (`core/umi_expected.hh`)

C++23 の `std::expected` をベースにしたRust風エラーハンドリング。

```cpp
#include "umi_expected.hh"

// 型エイリアス
using umi::Result;    // = std::expected<T, Error>
using umi::Error;     // 共通エラーコード

// 使用例
Result<TaskId> try_create_task(const TaskConfig& cfg) {
    if (!cfg.entry) return umi::Err(Error::NullPointer);
    auto id = allocate_tcb();
    if (!id.valid()) return umi::Err(Error::OutOfTasks);
    return umi::Ok(id);
}

// 呼び出し側
auto result = kernel.try_create_task(cfg);
if (result) {
    auto id = *result;
    // 成功
} else {
    // result.error() でエラーコード取得
    const char* msg = umi::error_to_string(result.error());
}

// TRY マクロ（Rustの ? 演算子相当）
Result<void> init_audio() {
    auto task = UMI_TRY(kernel.try_create_task(audio_cfg));
    auto timer = UMI_TRY(kernel.try_create_timer(timer_cfg));
    return {};  // Ok(void)
}
```

共通エラーコード:
- `OutOfMemory`, `OutOfTasks`, `OutOfTimers`
- `InvalidTask`, `InvalidState`, `InvalidParam`
- `Timeout`, `WouldBlock`
- `BufferOverrun`, `BufferUnderrun` (オーディオ)
- `MidiParseError`, `MidiBufferFull` (MIDI)

### UMI-OS に既にある電子楽器向け機能

| 機能 | 状態 | 用途 |
|------|------|------|
| `SpscQueue<T,N>` | ✅ 実装済 | MIDI イベント、パラメータ変更 |
| `Notification` | ✅ 実装済 | オーディオ完了通知、VSync |
| `LoadMonitor` | ✅ 実装済 | DSP負荷監視、オーバーラン検出 |
| `TimerQueue` | ✅ 実装済 | エンベロープ、LFO、アルペジエータ |
| `AudioEngine` | ✅ 実装済 | DMAダブルバッファ、低レイテンシ |
| `midi::Event` | ✅ 実装済 | 完全なMIDI 1.0サポート |
| `Priority::Realtime` | ✅ 実装済 | オーディオISR最優先 |
| コルーチン | ✅ 実装済 | 非同期UI、シーケンサ |
| `StackMonitor` | ✅ 実装済 | スタック使用量監視、オーバーフロー検出 |
| `HeapMonitor` | ✅ 実装済 | ヒープ使用量追跡、リーク検出 |
| `TaskProfiler` | ✅ 実装済 | タスク実行時間計測、CPU使用率 |
| `IrqLatencyMonitor` | ✅ 実装済 | 割り込みレイテンシ監視 |

### 実装計画

#### Phase 1: ✅ 完了
1. [x] ベクターテーブル (`platform/startup.cc`)
2. [x] Renode動作確認

#### Phase 2: ✅ 完了 - カーネル機能強化
3. [x] **タスク名** - `TaskConfig::name`、`get_task_name()`、`for_each_task()`
4. [x] **ラウンドロビン** - User優先度で `last_user_idx` ベースのラウンドロビン
5. [x] **`co_await sleep()`** - `SleepAwaiter`、chrono literals (`co_await 100ms`)

#### Phase 3: ✅ 完了 - デバッグ・開発支援
6. [x] **シェル** (`core/umi_shell.hh`) - ps, mem, load, reboot, help
7. [x] **リソースモニター** (`core/umi_monitor.hh`) - スタック/ヒープ/タスク/IRQ監視

#### Phase 4: ✅ 完了 - エラーハンドリング
8. [x] **`std::expected`** (`core/umi_expected.hh`) - C++23標準のResult型、共通エラーコード

---

## 設計方針

### 条件コンパイルの禁止

UMI-OS では `#ifdef UMI_USE_MPU` のようなマクロによる機能切り替えを**禁止**する。

**理由**:
- マクロの組み合わせ爆発でテストが困難になる
- IDE/LSP がすべてのコードパスを解析できない
- ビルド設定が複雑化する

**代替方式**: ビルドツール側でインクルードヘッダーを切り替える

```lua
-- xmake.lua
target("firmware")
    if is_config("platform", "stm32f4_mpu") then
        add_includedirs("platform/stm32f4_mpu")
    else
        add_includedirs("platform/microkernel")
    end
```

各プラットフォームは同じインターフェースの `HwImpl` を提供:

```cpp
// platform/microkernel/umi_hw.hh (MPUなし)
struct HwImpl {
    static void configure_mpu_region(...) { /* 何もしない */ }
};

// platform/stm32f4_mpu/umi_hw.hh (MPUあり)
struct HwImpl {
    static void configure_mpu_region(std::size_t idx, const void* base, 
                                     std::size_t bytes, bool writable, bool executable) {
        MPU->RNR = idx;
        MPU->RBAR = reinterpret_cast<uint32_t>(base) | MPU_RBAR_VALID_Msk;
        // ... 実際のMPU設定
    }
};
```

### Heapの使用方針

組み込みでのHeap使用は一般に避けるべきだが、**起動時の初期化**には許容する。

**❌ 禁止: 実行時の動的確保**
```cpp
void audio_callback() {
    auto* buf = new float[256];  // 絶対禁止
    // ...
    delete[] buf;
}
```

**✅ 許容: 起動時に一度だけ確保**
```cpp
class Synthesizer {
    std::vector<Voice> voices_;
public:
    void init(int polyphony) {
        voices_.resize(polyphony);  // 起動時のみ
        // 以降は固定サイズで使用
    }
};
```

`HeapMonitor` は起動完了後に「これ以上allocしていない」ことを検証するために有用。

### Priority Ceiling Mutex について

`std::mutex` は Priority Ceiling をサポートしない（スケジューラ統合が必要）。

**現在のUMI-OS方針**: ロックフリー設計を維持
- `std::atomic` + `SpscQueue` で多くのケースをカバー
- 優先度逆転が問題になる場面では設計を見直す

将来必要になれば `set_priority()` APIを追加して対応可能。

---

## 結論

MOS-STM32 から取り入れるべき設計（再評価後）:

1. **`co_await sleep()`**: 非ブロッキング遅延（コルーチン awaiter）
2. **タスク名**: デバッグ・シェル表示用
3. **シェル**: 開発・テスト・デバッグに有用
4. **ラウンドロビン**: User 優先度でバックグラウンドタスク公平化

取り入れない設計:

1. **ブロッキング `delay()`**: コルーチンで代替
2. **独自 `Result<T,E>`**: `std::expected` (C++23) で代替可能
3. **Barrier / CondVar / MPSC**: 電子楽器には過剰

UMI-OS の強み (維持すべき点):

1. **ミニマル設計**: 電子楽器に必要十分な機能のみ
2. **オーディオ特化**: DMA管理、低レイテンシISR、LoadMonitor
3. **C++23 コルーチン**: 非ブロッキング非同期処理
4. **ヘッダオンリー**: 依存なし、組み込み容易
