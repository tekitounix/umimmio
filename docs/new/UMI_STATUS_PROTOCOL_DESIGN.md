# UMI Status Protocol 設計書

バージョン: 0.1.0 (Draft)
ステータス: 設計段階

## 1. 概要

本文書は、UMIデバイスのステータス情報取得・設定プロトコルの設計を行う。既存のMIDI-CI、OSC、その他の独自プロトコルとの比較を行い、UMIに最適な仕様を策定する。

## 2. 設計要件

### 2.1 最重要要件: リアルタイム処理への非干渉

**MIDIリアルタイムメッセージとSysExの優先度の違いを明確にする:**

| 種類 | 優先度 | 処理タイミング | 例 |
|-----|-------|--------------|-----|
| Note On/Off | 最高 | 即座（割り込み） | 演奏データ |
| CC (Control Change) | 最高 | 即座（割り込み） | パラメータ変更 |
| Timing Clock | 最高 | 即座（割り込み） | 同期 |
| SysEx (Status) | 最低 | アイドル時のみ | 本プロトコル |

**SysEx処理は以下の条件を満たすこと:**
1. オーディオ処理（DMA割り込み、DSP処理）に一切影響を与えない
2. MIDI演奏データの処理に影響を与えない
3. UI更新に影響を与えない
4. アイドルタスクまたは低優先度タスクでのみ処理

### 2.2 現在の実装状況

#### UMI-OSカーネルのタスク優先度

```c++
enum class Priority : std::uint8_t {
    Realtime = 0,  // オーディオ処理、DMAコールバック - 最高
    Server   = 1,  // ドライバ、I/Oハンドラ
    User     = 2,  // アプリケーションタスク
    Idle     = 3,  // バックグラウンド - 最低
};
```

#### 処理レイヤーの責務定義（重要）

**設計原則:** 割り込み処理内ではタスク通知や最低限のメモリ操作のみを行う。少しでも重い処理は通知を受けたServerタスクで行う。

```
┌─────────────────────────────────────────────────────────────────────┐
│                    処理レイヤー別責務一覧                             │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  【割り込みハンドラ】 - 最小限の処理のみ                              │
│    ✓ 生バイトをリングバッファ/FIFOにコピー                           │
│    ✓ タスク通知（kernel.notify()）                                   │
│    ✗ SysExの組み立て・蓄積 → Serverで行う                            │
│    ✗ MIDIメッセージの解析 → Serverで行う                             │
│    ✗ コールバック呼び出し → Serverで行う                             │
│                                                                      │
│  【Serverタスク】 - パケット処理（取りこぼし防止）                    │
│    ✓ USB-MIDIパケットのパース（CIN解析）                             │
│    ✓ SysExの組み立て（パケット→完結メッセージ）                      │
│    ✓ MIDIメッセージの分類（Note/CC vs SysEx）                        │
│    ✓ 適切なキューへの振り分け                                        │
│    ✗ SysExコマンドの解釈 → Idleで行う                                │
│    ✗ snprintf等の重い処理 → Idleで行う                               │
│                                                                      │
│  【Idleタスク】 - プロトコル解釈（時間制約なし）                      │
│    ✓ UMI SysExプロトコルの解釈                                       │
│    ✓ シェルコマンド処理                                              │
│    ✓ stdout/stderrのフラッシュ                                       │
│    ✓ 文字列フォーマット（snprintf）                                  │
│    ✓ ファームウェア更新処理                                          │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

#### 現在のSysEx処理フロー（問題点）

```
┌─────────────────────────────────────────────────────────────────────┐
│                     USB MIDI 割り込みハンドラ                         │
│                    (lib/umios/backend/cm/stm32f4/usb_midi.hh)        │
│                                                                      │
│  USB_MIDI::handle_rx()                                               │
│    │                                                                 │
│    ├── CIN 0x04-0x07 (SysEx) ──→ sysex_buf_[] に蓄積  【問題】       │
│    │         └── 完了時 ──→ on_sysex(sysex_buf_, len)  【問題】      │
│    │              コールバック呼び出しが割り込み内                    │
│    │                                                                 │
│    └── CIN 0x08-0x0E (Note/CC) ──→ on_midi(cable, msg, len)          │
│                                    コールバック         【問題】      │
│                                                                      │
│  【現状の問題点】                                                     │
│  - SysExバッファへの蓄積処理が割り込み内で実行される                  │
│  - コールバック（on_sysex, on_midi）が割り込み内で呼ばれる            │
│  - 長いSysExや複雑なコールバック処理が割り込み時間を延長              │
└─────────────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      コールバック登録先                               │
│                                                                      │
│  on_midi コールバック:                                                │
│    ├── Option A: SpscQueue<midi::Event> へpush (lock-free)          │
│    │     └── 後続タスクで処理                                        │
│    │                                                                 │
│    └── Option B: 直接DSP通知 (kernel.notify())                       │
│          └── 即座にRealtime taskへ通知                               │
│                                                                      │
│  on_sysex コールバック:                                               │
│    └── StandardIO::process_message() を呼び出し                      │
│          └── 【問題】割り込みコンテキストで実行される可能性            │
└─────────────────────────────────────────────────────────────────────┘
```

#### USB_MIDI クラスの実装詳細

```c++
// lib/umios/backend/cm/stm32f4/usb_midi.hh より

class USB_MIDI {
    // SysEx累積バッファ (256バイト)
    uint8_t sysex_buf_[256];
    uint16_t sysex_pos_ = 0;
    bool in_sysex_ = false;

    // コールバック
    void (*on_midi)(uint8_t cable, const uint8_t* data, uint8_t len) = nullptr;
    void (*on_sysex)(const uint8_t* data, uint16_t len) = nullptr;

    void handle_rx(uint8_t ep, uint8_t* data, uint16_t len) {
        // USB-MIDIパケット処理 (4バイトずつ)
        for (...) {
            switch (cin) {
                case 0x04:  // SysEx start/continue
                    // バッファに蓄積
                    break;
                case 0x05-0x07:  // SysEx end
                    if (on_sysex) {
                        on_sysex(sysex_buf_, sysex_pos_);  // ← 【問題】
                    }
                    break;
                case 0x08-0x0E:  // Note/CC
                    if (on_midi) {
                        on_midi(cable, msg, len);  // ← 即座に処理
                    }
                    break;
            }
        }
    }
};
```

#### 問題点の詳細

| 問題 | 説明 | 影響 |
|-----|------|------|
| **割り込みコンテキストでのSysEx処理** | `on_sysex`コールバックが`handle_rx`内で直接呼ばれる | 割り込み処理時間が長くなる |
| **StandardIO処理の優先度** | `process_message()`内でsnprintf等の重い処理 | オーディオDMAに影響の可能性 |
| **シェル出力のタイミング** | `write_stdout()`がいつでも呼べる | 予期しないタイミングでUSB送信 |
| **SysExバッファサイズ** | 256バイト固定 | 大きなSysExで溢れる可能性 |

### 2.3 推奨される改善（詳細版）

#### アーキテクチャ改善案

**原則:** 割り込み処理内ではタスク通知と最低限のメモリ操作のみを行う。

```
┌─────────────────────────────────────────────────────────────────────┐
│               USB MIDI 割り込みハンドラ (変更後)                      │
│                                                                      │
│  handle_rx()  【割り込みコンテキスト】                                │
│    │                                                                 │
│    │  【許可される操作】                                              │
│    │  - 生バイトをraw_rx_bufferにコピー                              │
│    │  - kernel.notify(server_task, EVENT_USB_RX)                     │
│    │                                                                 │
│    │  【禁止される操作】                                              │
│    │  - CINの解析                                                    │
│    │  - SysExの組み立て                                              │
│    │  - コールバック呼び出し                                          │
│    │                                                                 │
│    └── raw_rx_buffer.push(data, len) ──→ タスク通知                  │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────────┐
│               Server タスク (Priority::Server)                       │
│               【パケット処理 - 取りこぼし防止】                        │
│                                                                      │
│  packet_handler_task()                                               │
│    │                                                                 │
│    │  【許可される操作】                                              │
│    │  - USB-MIDIパケットのCIN解析                                    │
│    │  - SysExの組み立て（パケット→完結メッセージ）                    │
│    │  - MIDIメッセージの分類                                         │
│    │  - キューへの振り分け                                           │
│    │                                                                 │
│    │  【禁止される操作】                                              │
│    │  - SysExコマンドの解釈                                          │
│    │  - snprintf等の重い処理                                         │
│    │                                                                 │
│    ├── Note/CC ──→ midi_queue.try_push() ──→ [Realtime Task]        │
│    │                (SpscQueue, lock-free)    即座にDSP処理          │
│    │                                                                 │
│    └── SysEx ──→ sysex_queue.try_push() ──→ [Idle Task]             │
│                   (SpscQueue, lock-free)      バックグラウンド処理   │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
                            │
                ┌───────────┴───────────┐
                ▼                       ▼
┌───────────────────────┐   ┌───────────────────────────────────┐
│   Realtime Task       │   │         Idle Task                 │
│   (Priority::Realtime)│   │         (Priority::Idle)          │
│                       │   │                                   │
│ while (auto msg =     │   │ while (auto sysex =               │
│   midi_queue.try_pop())│  │   sysex_queue.try_pop()) {        │
│ {                     │   │                                   │
│   // Note On/Off      │   │   if (is_stdin(sysex)) {          │
│   // CC処理           │   │     shell.process(sysex);         │
│   // DSPへ即座に反映  │   │   }                               │
│ }                     │   │   if (is_fw_update(sysex)) {      │
│                       │   │     fw_handler.process(sysex);    │
│ // 処理時間: <1ms     │   │   }                               │
│                       │   │   if (is_status_req(sysex)) {     │
└───────────────────────┘   │     send_status_response();       │
                            │   }                               │
                            │ }                                 │
                            │                                   │
                            │ // 他タスクがないときのみ実行      │
                            │ // オーディオには一切影響しない    │
                            └───────────────────────────────────┘
```

#### 実装コード例

```c++
// ===== キュー定義 =====

// 割り込み→Serverタスク用: 生の受信データ
struct RawRxPacket {
    uint8_t data[64];  // USB-MIDIパケット (4バイト×16)
    uint8_t len;
};
SpscQueue<RawRxPacket, 16> raw_rx_queue;  // 割り込み→Server

// Server→各タスク用
SpscQueue<midi::Event, 64> midi_queue;     // Note/CC用 (Server→Realtime)

struct SysExMessage {
    uint8_t data[256];
    uint16_t len;
};
SpscQueue<SysExMessage, 8> sysex_queue;    // SysEx用 (Server→Idle)

// ===== 割り込みハンドラ (最小限の処理のみ) =====
void usb_midi_rx_interrupt(uint8_t ep, uint8_t* data, uint16_t len) {
    // 【許可】生バイトをキューにコピーするだけ
    RawRxPacket pkt;
    memcpy(pkt.data, data, std::min<size_t>(len, sizeof(pkt.data)));
    pkt.len = static_cast<uint8_t>(len);
    raw_rx_queue.try_push(pkt);

    // 【許可】タスク通知
    kernel.notify_from_isr(server_task_id, KernelEvent::UsbRxReady);

    // 【禁止】以下の処理は行わない:
    // - CINの解析
    // - SysExの組み立て
    // - コールバック呼び出し
    // - snprintf等
}

// ===== Server タスク (パケット処理) =====
// SysEx組み立て用の状態 (Serverタスク内で管理)
static uint8_t sysex_assemble_buf[256];
static uint16_t sysex_assemble_pos = 0;
static bool in_sysex = false;

void packet_handler_task(void*) {
    while (true) {
        // RXキューからパケットを取り出す
        while (auto pkt = raw_rx_queue.try_pop()) {
            // USB-MIDIパケットを4バイトずつ処理
            for (size_t i = 0; i + 4 <= pkt->len; i += 4) {
                uint8_t cin = pkt->data[i] & 0x0F;
                uint8_t* msg = &pkt->data[i + 1];

                switch (cin) {
                    case 0x04:  // SysEx start/continue
                        // 【許可】SysExの組み立て
                        if (!in_sysex) {
                            sysex_assemble_pos = 0;
                            in_sysex = true;
                        }
                        for (int j = 0; j < 3 && sysex_assemble_pos < sizeof(sysex_assemble_buf); ++j) {
                            sysex_assemble_buf[sysex_assemble_pos++] = msg[j];
                        }
                        break;

                    case 0x05: case 0x06: case 0x07:  // SysEx end
                        {
                            int count = (cin == 0x05) ? 1 : (cin == 0x06) ? 2 : 3;
                            for (int j = 0; j < count && sysex_assemble_pos < sizeof(sysex_assemble_buf); ++j) {
                                sysex_assemble_buf[sysex_assemble_pos++] = msg[j];
                            }
                            // 【許可】完結したSysExをIdleキューへ転送
                            SysExMessage sysex;
                            memcpy(sysex.data, sysex_assemble_buf, sysex_assemble_pos);
                            sysex.len = sysex_assemble_pos;
                            sysex_queue.try_push(sysex);
                            in_sysex = false;
                            // Idleタスクは通知不要（暇なときに処理される）
                        }
                        break;

                    case 0x08: case 0x09: case 0x0A: case 0x0B:
                    case 0x0C: case 0x0D: case 0x0E:  // Note/CC等
                        {
                            // 【許可】MIDIメッセージをRealtimeキューへ
                            midi::Message midi_msg{msg[0], msg[1], msg[2], 0};
                            midi_queue.try_push(midi::Event{midi_msg, 0});
                            // Realtimeタスクを起こす
                            kernel.notify(audio_task_id, KernelEvent::MidiReady);
                        }
                        break;
                }
            }
        }

        // 次のRXイベントを待つ
        kernel.wait_block(server_task_id, KernelEvent::UsbRxReady);
    }
}

// ===== Realtime タスク (DSP処理) =====
void audio_task(void*) {
    while (true) {
        // 高速MIDI処理 (Note/CC)
        while (auto event = midi_queue.try_pop()) {
            process_midi_event(*event);  // DSPへ反映
        }

        // オーディオバッファ処理
        kernel.wait_block(audio_task_id, KernelEvent::AudioReady);
        process_audio_buffer();
    }
}

// ===== Idle タスク (プロトコル解釈) =====
void stdio_task(void*) {
    while (true) {
        // SysEx処理 (最低優先度)
        // 【許可】ここで重い処理を行う
        while (auto sysex = sysex_queue.try_pop()) {
            // UMI SysExプロトコルの解釈
            // snprintf等の重い処理もここで行う
            standard_io.process_message(sysex->data, sysex->len);
        }

        // シェル出力処理 (バッファリング)
        flush_pending_stdout();

        // 暇なときだけ実行されるので yield して他タスクへ
        kernel.yield();
    }
}
```

#### シェル出力のバッファリング

```c++
// ===== シェル出力バッファ =====
class BufferedStdout {
    SpscQueue<char, 2048> buffer_;

public:
    // シェルコマンドからの出力 (どこからでも呼べる)
    void write(const char* str, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            buffer_.try_push(str[i]);  // 溢れたら捨てる (非クリティカル)
        }
    }

    // Idleタスクから呼ばれる
    template <typename SendFn>
    void flush(SendFn send_fn) {
        char chunk[64];
        size_t pos = 0;

        while (auto c = buffer_.try_pop()) {
            chunk[pos++] = *c;
            if (pos >= sizeof(chunk) - 1) {
                send_stdout(chunk, pos, send_fn);
                pos = 0;
            }
        }

        if (pos > 0) {
            send_stdout(chunk, pos, send_fn);
        }
    }
};
```

#### タイミングダイアグラム

```
時間 →

[Audio DMA割り込み] ═══════════════════════════════════════════════════
                    │         │         │         │
                    ▼         ▼         ▼         ▼
                 処理      処理      処理      処理
                 256サンプル

[USB MIDI割り込み]  ─────────────────────────────────────────────────────
                         │                    │
                         ▼                    ▼
                      Note On             SysEx完了
                      → midi_queue        → sysex_queue
                      → notify RT task    (通知なし)

[Realtime Task]     ════════════════════════════════════════════════════
                         │    │
                         ▼    ▼
                      Note処理
                      DSP更新

[Server Task]       ────────────────────────────────────────────────────
                    (使用しない)

[Idle Task]         ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
                                  │              │
                                  ▼              ▼
                               SysEx処理      stdout flush
                               (時間かかってもOK)

═══ : 高優先度 (プリエンプト可能)
─── : 中優先度
░░░ : 最低優先度 (他が暇なときのみ)
```

#### 実装時の注意点

| 項目 | 現在 | 改善後 | 備考 |
|-----|------|--------|------|
| Note/CC処理 | コールバック内 | Realtimeタスク | 最大レイテンシを保証 |
| SysEx処理 | コールバック内 | Idleタスク | 時間制約なし |
| シェル出力 | 即座に送信 | バッファリング | USB送信は非同期 |
| snprintf | 割り込み中 | Idleタスク | 重い処理は後回し |

#### メモリ構成

```c++
// 必要なキューとバッファ
SpscQueue<midi::Event, 64> midi_queue;     // 256 bytes
SpscQueue<SysExMessage, 8> sysex_queue;    // ~2KB (256 * 8)
SpscQueue<char, 2048> stdout_buffer;       // 2KB
SpscQueue<char, 512> stderr_buffer;        // 512 bytes

// 合計: 約5KB の追加メモリ
```

### 2.4 StandardIO アーキテクチャ（トランスポート抽象化）

#### 設計原則

1. **割り込み処理の最小化**
   - **割り込み内**: タスク通知と最低限のメモリ操作（バッファコピー）のみ
   - SysEx組み立て、MIDIパース等の処理は一切行わない
   - これにより割り込みレイテンシを最小化し、オーディオDMAに影響を与えない

2. **トランスポート層とプロトコル層の分離**
   - トランスポート (割り込み): 生バイトの受信/送信のみ
   - パケット処理 (Server): SysEx組み立て、MIDI分類、キュー振り分け
   - プロトコル (Idle): SysExコマンド解釈、シェル処理

3. **共通の内部接続フロー**
   - どのトランスポートでも同じキューとバッファを使用
   - 複数トランスポートの同時使用に対応

4. **優先度の適切な分離**
   - 割り込み: 最小限の処理のみ（バッファコピー、タスク通知）
   - Server: パケットハンドリング（取りこぼし防止）
   - Idle: プロトコル解釈、イベントハンドリング

#### 全体アーキテクチャ

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        割り込み層 (Interrupt Layer)                          │
│                        【最小限の処理のみ】                                   │
│                                                                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
│  │  USB-MIDI   │  │    UART     │  │  Bluetooth  │  │   Future    │        │
│  │  IRQ        │  │    IRQ      │  │    IRQ      │  │    IRQ      │        │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘        │
│         │                │                │                │               │
│         │   【許可】     │   【許可】     │   【許可】     │               │
│         │   ・バッファ   │   ・バッファ   │   ・バッファ   │               │
│         │     コピー     │     コピー     │     コピー     │               │
│         │   ・タスク通知 │   ・タスク通知 │   ・タスク通知 │               │
│         │                │                │                │               │
│         │   【禁止】     │   【禁止】     │   【禁止】     │               │
│         │   ・パケット   │   ・行バッファ │   ・プロト     │               │
│         │     解析       │     リング     │     コル解析   │               │
│         │   ・SysEx組立  │   ・コマンド   │   ・コール     │               │
│         │   ・コール     │     解釈       │     バック     │               │
│         │     バック     │                │                │               │
│         ▼                ▼                ▼                ▼               │
│  ┌──────────────────────────────────────────────────────────────────┐      │
│  │                    Raw RX Queue (生バイト)                        │      │
│  │                    SpscQueue<RawRxPacket, 32>                    │      │
│  │                    (lock-free, 割り込み→Server)                  │      │
│  │                    + kernel.notify(server_task)                  │      │
│  └──────────────────────────────────────────────────────────────────┘      │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                     パケットハンドラ (Packet Handler)                        │
│                     【Server優先度タスク】                                   │
│                                                                             │
│  void packet_handler_task() {       // Priority::Server                     │
│      // SysEx組み立て用状態                                                  │
│      static uint8_t sysex_buf[256];                                         │
│      static uint16_t sysex_pos = 0;                                         │
│      static bool in_sysex = false;                                          │
│                                                                             │
│      while (auto raw = raw_rx_queue.try_pop()) {                            │
│          // 【許可】USB-MIDIパケットの解析                                   │
│          for (size_t i = 0; i + 4 <= raw->len; i += 4) {                    │
│              uint8_t cin = raw->data[i] & 0x0F;                             │
│              uint8_t* msg = &raw->data[i + 1];                              │
│                                                                             │
│              switch (cin) {                                                 │
│                  case 0x04:  // SysEx start/continue                        │
│                      // 【許可】SysExの組み立て                               │
│                      assemble_sysex(msg, 3);                                │
│                      break;                                                 │
│                  case 0x05: case 0x06: case 0x07:  // SysEx end             │
│                      // 【許可】SysEx完結→Idleキューへ                       │
│                      complete_sysex_and_push();                             │
│                      break;                                                 │
│                  case 0x08 ... 0x0E:  // Note/CC等                          │
│                      // 【許可】MIDIメッセージ→Realtimeキューへ              │
│                      midi_queue.try_push(parse_midi(msg));                  │
│                      kernel.notify(audio_task, MidiReady);                  │
│                      break;                                                 │
│              }                                                              │
│          }                                                                  │
│      }                                                                      │
│      kernel.wait_block(my_id, EVENT_TRANSPORT_RX);                          │
│  }                                                                          │
│                                                                             │
│  【このタスクの責務】                                                        │
│  - 生バイトからパケットの組み立て (SysEx蓄積等)                              │
│  - パケットの分類と振り分け                                                  │
│  - 取りこぼし防止のためServerで動作                                          │
│                                                                             │
│  【禁止される操作】                                                          │
│  - SysExコマンドの解釈                                                       │
│  - snprintf等の重い処理                                                      │
│  - シェルコマンド実行                                                        │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                    ┌───────────────┼───────────────┐
                    ▼               ▼               ▼
            ┌───────────┐   ┌───────────┐   ┌───────────┐
            │midi_queue │   │sysex_queue│   │stdin_queue│
            │(Realtime) │   │  (Idle)   │   │  (Idle)   │
            └─────┬─────┘   └─────┬─────┘   └─────┬─────┘
                  │               │               │
                  ▼               ▼               ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      プロトコル層 (Protocol Layer)                           │
│                                                                             │
│  ┌─────────────────────┐                                                    │
│  │   Audio Task        │  Priority::Realtime                                │
│  │   ─────────────     │                                                    │
│  │   while (midi_queue.try_pop()) {                                         │
│  │       process_note_cc();  // DSP更新                                     │
│  │   }                                                                      │
│  │   process_audio_buffer();                                                │
│  └─────────────────────┘                                                    │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────┐        │
│  │   StandardIO Task   │  Priority::Idle                           │        │
│  │   ─────────────────                                             │        │
│  │                                                                 │        │
│  │   // SysExプロトコル解釈                                         │        │
│  │   while (auto sysex = sysex_queue.try_pop()) {                  │        │
│  │       auto msg = parse_umi_sysex(sysex);                        │        │
│  │       switch (msg.command) {                                    │        │
│  │           case STDIN_DATA:  stdin_buffer.write(msg.payload);    │        │
│  │           case STATUS_REQ:  send_status_response();             │        │
│  │           case FW_BEGIN:    fw_handler.begin(msg);              │        │
│  │       }                                                         │        │
│  │   }                                                             │        │
│  │                                                                 │        │
│  │   // UART stdin処理                                              │        │
│  │   while (auto byte = stdin_queue.try_pop()) {                   │        │
│  │       shell.process_char(*byte);                                │        │
│  │   }                                                             │        │
│  │                                                                 │        │
│  │   // stdout/stderr フラッシュ                                    │        │
│  │   flush_stdout_to_transports();                                 │        │
│  │                                                                 │        │
│  │   kernel.yield();  // 他に仕事があれば譲る                       │        │
│  └─────────────────────────────────────────────────────────────────┘        │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                        TX フロー (stdout/stderr)                             │
│                                                                             │
│  ┌──────────────────────────────────────────────────────────────────┐      │
│  │                    stdout_buffer / stderr_buffer                 │      │
│  │                    SpscQueue<char, 2048>                         │      │
│  │                    (どこからでも書き込み可能)                      │      │
│  └──────────────────────────────────────────────────────────────────┘      │
│                                    │                                       │
│                                    ▼                                       │
│  ┌──────────────────────────────────────────────────────────────────┐      │
│  │   flush_stdout_to_transports()   【Idleタスク内で実行】           │      │
│  │                                                                  │      │
│  │   while (auto c = stdout_buffer.try_pop()) {                     │      │
│  │       tx_chunk[pos++] = *c;                                      │      │
│  │       if (pos >= CHUNK_SIZE) {                                   │      │
│  │           // 各トランスポートへ送信                               │      │
│  │           for (auto& transport : active_transports) {            │      │
│  │               transport->send_stdout(tx_chunk, pos);             │      │
│  │           }                                                      │      │
│  │           pos = 0;                                               │      │
│  │       }                                                          │      │
│  │   }                                                              │      │
│  └──────────────────────────────────────────────────────────────────┘      │
│                                    │                                       │
│         ┌──────────────────────────┼──────────────────────────┐            │
│         ▼                          ▼                          ▼            │
│  ┌─────────────┐            ┌─────────────┐            ┌─────────────┐     │
│  │  USB-MIDI   │            │    UART     │            │  Bluetooth  │     │
│  │  TX Queue   │            │  TX Queue   │            │  TX Queue   │     │
│  └─────────────┘            └─────────────┘            └─────────────┘     │
│         │                          │                          │            │
│         ▼                          ▼                          ▼            │
│  [Server Task]              [Server Task]              [Server Task]       │
│  USB送信処理                 UART送信処理               BT送信処理          │
└─────────────────────────────────────────────────────────────────────────────┘
```

#### データ構造定義

```c++
// ===== トランスポート共通パケット =====
enum class PacketType : uint8_t {
    MidiRealtime,   // Note, CC, PitchBend等
    SysEx,          // SysExメッセージ (完結したもの)
    RawBytes,       // UART生バイト列
};

struct TransportPacket {
    PacketType type;
    uint8_t transport_id;  // どのトランスポートから来たか
    union {
        midi::Event midi_event;
        struct {
            uint8_t data[256];
            uint16_t len;
        } sysex;
        struct {
            uint8_t data[64];
            uint8_t len;
        } raw;
    };
};

// ===== トランスポートインターフェース =====
class TransportBackend {
public:
    virtual ~TransportBackend() = default;

    // 初期化
    virtual void init() = 0;

    // 受信データをキューにpush (割り込みから呼ばれる)
    virtual void poll_rx(SpscQueue<TransportPacket, 32>& rx_queue) = 0;

    // 送信 (Idleタスクから呼ばれる)
    virtual void send_stdout(const uint8_t* data, size_t len) = 0;
    virtual void send_stderr(const uint8_t* data, size_t len) = 0;

    // 状態
    virtual bool is_connected() const = 0;
    virtual const char* name() const = 0;
};

// ===== USB-MIDI バックエンド =====
class UsbMidiBackend : public TransportBackend {
    USB_MIDI& usb_midi_;

public:
    void poll_rx(SpscQueue<TransportPacket, 32>& rx_queue) override {
        // USB_MIDIのコールバックから呼ばれる
        // SysEx完了時、Note/CC受信時にキューへpush
    }

    void send_stdout(const uint8_t* data, size_t len) override {
        // STDOUT_DATA SysExとしてエンコードして送信
        MessageBuilder<1280> builder;
        builder.begin(Command::STDOUT_DATA, tx_seq_++);
        builder.add_data(data, len);
        usb_midi_.send_sysex(builder.data(), builder.finalize());
    }
};

// ===== UART バックエンド =====
class UartBackend : public TransportBackend {
    UART& uart_;

public:
    void poll_rx(SpscQueue<TransportPacket, 32>& rx_queue) override {
        // UART RX FIFOからバイト列を取得
        uint8_t buf[64];
        size_t len = uart_.read(buf, sizeof(buf));
        if (len > 0) {
            TransportPacket pkt;
            pkt.type = PacketType::RawBytes;
            pkt.transport_id = id_;
            memcpy(pkt.raw.data, buf, len);
            pkt.raw.len = len;
            rx_queue.try_push(pkt);
        }
    }

    void send_stdout(const uint8_t* data, size_t len) override {
        // 生バイトとして送信
        uart_.write(data, len);
    }
};
```

#### タスク構成と優先度

```
┌────────────────────────────────────────────────────────────────────┐
│                         タスク優先度マップ                          │
├────────────────────────────────────────────────────────────────────┤
│                                                                    │
│  Priority::Realtime (0)                                            │
│  ├── Audio Task        : DSP処理、MIDI Note/CC反映                 │
│  │                       【最大レイテンシ保証が必要】               │
│  │                                                                 │
│  Priority::Server (1)                                              │
│  ├── Packet Handler    : トランスポートRX振り分け                   │
│  │                       【取りこぼし防止】                         │
│  ├── USB TX Task       : USB送信完了処理                           │
│  ├── UART TX Task      : UART送信完了処理                          │
│  │                                                                 │
│  Priority::User (2)                                                │
│  ├── (アプリケーション固有タスク)                                   │
│  │                                                                 │
│  Priority::Idle (3)                                                │
│  ├── StandardIO Task   : SysExパース、シェル処理、stdout flush     │
│  │                       【時間制約なし、他が暇なときのみ】         │
│  └── (将来: ログ出力、診断等)                                      │
│                                                                    │
└────────────────────────────────────────────────────────────────────┘
```

#### 処理フロー詳細

```
【受信フロー (RX)】

1. 割り込み発生 (USB/UART/etc)
   └── ハードウェア割り込みハンドラ
       │
       │  【許可される操作のみ】
       ├── 生バイトをraw_rx_queueへコピー (memcpy + try_push)
       └── kernel.notify_from_isr(server_task, EVENT_RX)

       │  【禁止】以下は行わない
       ├── パケット解析 (CIN判定等)
       ├── SysEx組み立て
       └── コールバック呼び出し

2. Packet Handler Task (Server優先度) が起床
   └── raw_rx_queueから生バイトをpop
   │
   │  【許可される操作】
   ├── USB-MIDIパケット解析 (CIN判定)
   ├── SysExパケットの組み立て (複数パケット→1メッセージ)
   ├── MIDIメッセージのパース
   └── 分類・振り分け:
       ├── Note/CC → midi_queueへ、Realtimeタスク通知
       ├── 完結SysEx → sysex_queueへ (通知なし)
       └── UART生バイト → stdin_queueへ (通知なし)

   │  【禁止】以下は行わない
   ├── SysExコマンドの解釈
   ├── snprintf等の重い処理
   └── シェルコマンド実行

3a. Audio Task (Realtime優先度)
    └── midi_queueからpop
    └── DSP処理に即座に反映

3b. StandardIO Task (Idle優先度)
    │  【すべての重い処理をここで実行】
    ├── sysex_queueからpop → UMIプロトコル解釈
    ├── stdin_queueからpop → シェル入力処理
    ├── snprintf等の文字列処理
    └── stdout/stderrフラッシュ


【送信フロー (TX)】

1. シェルコマンド実行等
   └── stdout_buffer.write() (どこからでも、lock-free)

2. StandardIO Task (Idle優先度)
   └── stdout_bufferからまとめて読み出し
   └── 各トランスポートのsend_stdout()を呼び出し

3. 各トランスポートのTXキューへ積む

4. TX Server Task
   └── 実際のハードウェア送信処理
```

#### フロー制御

```c++
// ===== 受信側フロー制御 =====
// SysExキューが満杯に近づいたらXOFFを送信

void packet_handler_task() {
    while (auto pkt = transport_rx_queue.try_pop()) {
        if (pkt->type == PacketType::SysEx) {
            if (sysex_queue.size_approx() > SYSEX_QUEUE_HIGH_WATER) {
                // フロー制御: XOFF送信
                send_flow_control(FlowControl::XOFF);
                flow_paused_ = true;
            }
            sysex_queue.try_push(pkt->sysex);
        }
    }
}

void stdio_task() {
    while (auto sysex = sysex_queue.try_pop()) {
        process_sysex(sysex);
    }

    // キューが空いたらXON
    if (flow_paused_ && sysex_queue.size_approx() < SYSEX_QUEUE_LOW_WATER) {
        send_flow_control(FlowControl::XON);
        flow_paused_ = false;
    }
}

// ===== 送信側フロー制御 =====
// ホストからXOFFを受けたら送信を一時停止

void send_stdout_chunk() {
    if (tx_paused_) return;  // XOFF状態

    // ... 送信処理
}

void on_flow_control(FlowControl fc) {
    tx_paused_ = (fc == FlowControl::XOFF);
}
```

#### 複数トランスポートの同時使用

```c++
// ===== トランスポートマネージャ =====
class TransportManager {
    std::array<TransportBackend*, 4> backends_{};
    size_t backend_count_ = 0;

public:
    void register_backend(TransportBackend* backend) {
        if (backend_count_ < backends_.size()) {
            backends_[backend_count_++] = backend;
        }
    }

    // すべてのアクティブなトランスポートへstdout送信
    void broadcast_stdout(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < backend_count_; ++i) {
            if (backends_[i]->is_connected()) {
                backends_[i]->send_stdout(data, len);
            }
        }
    }

    // いずれかのトランスポートへstdout送信 (最初に接続されたもの)
    void send_stdout(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < backend_count_; ++i) {
            if (backends_[i]->is_connected()) {
                backends_[i]->send_stdout(data, len);
                return;
            }
        }
    }
};

// 使用例:
// TransportManager transport;
// transport.register_backend(&usb_midi_backend);
// transport.register_backend(&uart_backend);
// transport.broadcast_stdout("Hello\r\n", 7);  // 両方に送信
```

#### まとめ: レイヤー責務

| レイヤー | 責務 | 優先度 | 許可される処理 | 禁止される処理 |
|---------|------|--------|---------------|---------------|
| **割り込み** | 最小限のI/O | - | バッファコピー、タスク通知 | パケット解析、SysEx組立、コールバック |
| **Server** | パケット処理 | Server | SysEx組み立て、MIDIパース、振り分け | コマンド解釈、snprintf、シェル処理 |
| **Idle** | プロトコル解釈 | Idle | UMIコマンド解釈、シェル処理、文字列処理 | (時間制約なし、すべて許可) |
| **Application** | ビジネスロジック | User/Idle | ファームウェア更新、設定変更等 | - |

**設計原則の要約:**
```
割り込み処理内ではタスク通知や最低限のメモリ操作のみを行うべきで、
少しでも重い処理は通知を受けたServerタスクで行うべき。
Serverタスクではパケットの組み立てと振り分けのみを行い、
実際のプロトコル解釈やイベントハンドリングはIdleタスクで行う。
```

## 3. 既存プロトコルとの比較

### 3.1 MIDI-CI (MIDI Capability Inquiry)

**概要:** MIDI 2.0仕様の一部。デバイス間の機能交渉とプロパティ交換。

**構造:**
```
Discovery → Profile Configuration → Property Exchange
```

| 機能 | 説明 | UMIでの必要性 |
|-----|------|-------------|
| Discovery | MUID割り当て、デバイス検出 | △ 単一デバイス想定 |
| Protocol Negotiation | MIDI 1.0/2.0選択 | × 固定で良い |
| Profile Configuration | 標準プロファイル | × 不要 |
| Property Exchange | JSON/CBORでのプロパティ | ◎ 参考になる |

**Property Exchange詳細:**
```json
{
  "resource": "DeviceInfo",
  "resId": "X-12345",
  "command": "get",
  "data": {}
}
```

**利点:**
- 標準化された柔軟なデータ形式
- サブスクリプション機能
- 大規模なパラメータセットに対応

**欠点:**
- オーバーヘッドが大きい
- 複雑なステートマシン
- 組み込みにはJSON/CBORパーサーが必要

### 3.2 OSC (Open Sound Control)

**概要:** UDP/TCPベースの楽器制御プロトコル。

**メッセージ形式:**
```
/synth/filter/cutoff ,f 0.75
/synth/adsr/attack ,i 100
```

| 機能 | 説明 | UMIでの必要性 |
|-----|------|-------------|
| アドレスパターン | 階層的名前空間 | ◎ 参考になる |
| 型タグ | 明示的な型情報 | ○ 有用 |
| バンドル | 複数メッセージの原子操作 | △ 複雑 |
| パターンマッチ | ワイルドカード | × 過剰 |

**利点:**
- 人間が読める形式
- 柔軟な名前空間

**欠点:**
- バイナリ効率が悪い
- 文字列解析が重い
- MIDIトランスポートに不向き

### 3.3 Elektron SysEx (Digitakt等)

**概要:** Elektron社の楽器で使用される独自SysExプロトコル。

**特徴:**
- バイナリ形式
- チャンク構造
- パターン/サウンド転送

**メッセージ例:**
```
F0 00 20 3C [DevID] [MsgType] [Data...] [Checksum] F7
```

| 機能 | 説明 |
|-----|------|
| Sound Dump | サウンドデータの一括転送 |
| Pattern Request | パターンデータ取得 |
| Global Settings | グローバル設定 |

**利点:**
- コンパクト
- MIDIネイティブ

**欠点:**
- 機種依存
- ドキュメントなし

### 3.4 Native Instruments Kontrol

**概要:** NIのコントローラー/ソフト間プロトコル。

**特徴:**
- USB HIDベース（MIDIではない）
- 双方向通信
- ディスプレイ情報転送

**参考点:**
- パラメータのメタデータ（名前、範囲、単位）
- グループ化（セクション）
- リアルタイムフィードバック

### 3.5 比較表

| 機能 | MIDI-CI | OSC | Elektron | UMI現状 | UMI提案 |
|-----|---------|-----|----------|--------|---------|
| トランスポート | SysEx | UDP/TCP | SysEx | SysEx | SysEx |
| データ形式 | JSON/CBOR | バイナリ+文字列 | バイナリ | バイナリ | バイナリ |
| オーバーヘッド | 大 | 中 | 小 | 小 | 小 |
| パラメータ照会 | ○ | ○ | ○ | × | ○ |
| サブスクリプション | ○ | × | × | × | △ |
| ファームウェア更新 | × | × | × | ○ | ○ |
| シェルI/O | × | × | × | ○ | ○ |

## 4. UMI Status Protocol 仕様案

### 4.1 設計原則

1. **シンプルさ**: 最小限のコマンドセット
2. **効率性**: バイナリ形式、低オーバーヘッド
3. **非同期**: リクエスト/レスポンス分離
4. **拡張性**: 将来のコマンド追加に対応

### 4.2 コマンド体系

```c++
enum class Command : uint8_t {
    // Standard IO (0x01-0x0F) - 既存
    STDOUT_DATA     = 0x01,
    STDERR_DATA     = 0x02,
    STDIN_DATA      = 0x03,
    STDIN_EOF       = 0x04,
    FLOW_CTRL       = 0x05,

    // Firmware Update (0x10-0x1F) - 既存
    FW_QUERY        = 0x10,
    // ... 省略 ...

    // System (0x20-0x2F)
    PING            = 0x20,
    PONG            = 0x21,
    RESET           = 0x22,
    VERSION         = 0x23,
    STATUS_REQUEST  = 0x24,  // ← 追加
    STATUS_RESPONSE = 0x25,  // ← 追加
    IDENTITY_REQ    = 0x26,  // ← 新規
    IDENTITY_RES    = 0x27,  // ← 新規

    // Audio Status (0x30-0x3F) - 新規
    AUDIO_STATUS_REQ = 0x30,
    AUDIO_STATUS_RES = 0x31,
    METER_REQ        = 0x32,
    METER_RES        = 0x33,

    // Parameter (0x40-0x4F) - 新規
    PARAM_LIST_REQ  = 0x40,
    PARAM_LIST_RES  = 0x41,
    PARAM_GET       = 0x42,
    PARAM_VALUE     = 0x43,
    PARAM_SET       = 0x44,
    PARAM_ACK       = 0x45,
    PARAM_SUBSCRIBE = 0x46,
    PARAM_NOTIFY    = 0x47,

    // Preset (0x50-0x5F) - 将来用
    PRESET_LIST_REQ = 0x50,
    PRESET_LIST_RES = 0x51,
    PRESET_LOAD     = 0x52,
    PRESET_SAVE     = 0x53,
};
```

### 4.3 メッセージ詳細

#### 4.3.1 IDENTITY_RESPONSE (0x27)

デバイス識別情報の応答。

**ペイロード構造:**
```
Byte 0-2:   Manufacturer ID (3 bytes, MIDI規格)
Byte 3-4:   Device Family (uint16_t, BE)
Byte 5-6:   Device Model (uint16_t, BE)
Byte 7:     Firmware Version Major
Byte 8:     Firmware Version Minor
Byte 9-10:  Firmware Version Patch (uint16_t, BE)
Byte 11:    Protocol Version Major
Byte 12:    Protocol Version Minor
Byte 13+:   Device Name (null-terminated UTF-8)
```

**例:**
```
Manufacturer: 7E 7F 00 (UMI)
Family: 00 01 (Synthesizer)
Model: 00 01 (UMI Synth)
FW Version: 1.2.3
Protocol: 1.0
Name: "UMI Synth v1"
```

#### 4.3.2 AUDIO_STATUS_RESPONSE (0x31)

オーディオ状態の詳細情報。

**ペイロード構造:**
```
Byte 0-1:   DSP Load (uint16_t, ×100, 例: 5600 = 56.00%)
Byte 2-4:   Sample Rate (uint24_t, 例: 48000)
Byte 5-6:   Buffer Size (uint16_t, samples)
Byte 7-8:   Latency Input (uint16_t, samples)
Byte 9-10:  Latency Output (uint16_t, samples)
Byte 11:    Polyphony Current
Byte 12:    Polyphony Max
Byte 13:    Flags
            bit 0: Audio Running
            bit 1: Clipping Detected
            bit 2: Buffer Underrun Detected
            bit 3: Buffer Overrun Detected
Byte 14-17: Underrun Count (uint32_t)
Byte 18-21: Overrun Count (uint32_t)
Byte 22-25: Uptime (uint32_t, seconds)
```

#### 4.3.3 METER_RESPONSE (0x33)

レベルメーター情報（高頻度更新用）。

**ペイロード構造:**
```
Byte 0:     Channel Count (N)
For each channel (N × 4 bytes):
  Byte 0-1: Peak Level (int16_t, -32768=silence, 0=0dBFS)
  Byte 2-3: RMS Level (int16_t)
```

**注意:** このメッセージは負荷が高いため、明示的なサブスクリプション後のみ送信。

#### 4.3.4 PARAM_LIST_RESPONSE (0x41)

パラメータ一覧。大きなリストは複数メッセージに分割。

**ペイロード構造:**
```
Byte 0:     Total Param Count
Byte 1:     Offset (このメッセージの開始位置)
Byte 2:     Count in this message
For each param:
  Byte 0:     Param ID
  Byte 1:     CC Number (0xFF = no CC mapping)
  Byte 2:     Type (0=int, 1=float, 2=bool, 3=enum)
  Byte 3-4:   Min Value (int16_t)
  Byte 5-6:   Max Value (int16_t)
  Byte 7-8:   Default Value (int16_t)
  Byte 9:     Flags
              bit 0: Read-only
              bit 1: Hidden
              bit 2: Automatable
  Byte 10:    Group ID
  Byte 11:    Name Length
  Byte 12+:   Name (UTF-8, not null-terminated)
```

#### 4.3.5 PARAM_VALUE (0x43)

パラメータ値の応答/通知。

**ペイロード構造:**
```
Byte 0:     Param ID
Byte 1-2:   Value (int16_t, normalized to param range)
```

#### 4.3.6 PARAM_SUBSCRIBE (0x46)

パラメータ変更通知の購読。

**ペイロード構造:**
```
Byte 0:     Action (0=unsubscribe, 1=subscribe)
Byte 1:     Param ID (0xFF = all params)
Byte 2:     Rate Limit (ms, 0=every change)
```

### 4.4 処理優先度ガイドライン

| コマンドグループ | 処理優先度 | 処理タスク |
|----------------|----------|----------|
| Note/CC | Realtime | 割り込み/RTタスク |
| STDIN_DATA | Idle | Idleタスク |
| STDOUT/STDERR | Idle | Idleタスク |
| FW_* | Idle | Idleタスク |
| STATUS/IDENTITY | Idle | Idleタスク |
| AUDIO_STATUS | Idle | Idleタスク |
| METER_* | Server | 定期タイマー |
| PARAM_* | Idle | Idleタスク |

### 4.5 フロー制御

SysExの大量送信によるバッファオーバーフローを防ぐため:

1. **レート制限**: PARAM_SUBSCRIBEでRate Limitを指定
2. **バックプレッシャー**: FLOW_CTRLでXOFF/XON
3. **優先度付きキュー**: 重要なメッセージを先に処理

## 5. 実装計画

### Phase 1: 基本ステータス (STATUS_REQUEST/RESPONSE)
- C++側にコマンド追加
- 基本的なシステム情報取得

### Phase 2: デバイス識別 (IDENTITY)
- 標準的なデバイス情報
- Web UI連携

### Phase 3: パラメータ照会 (PARAM_LIST, PARAM_GET/SET)
- パラメータメタデータ
- CC以外のパラメータ操作

### Phase 4: サブスクリプション (PARAM_SUBSCRIBE, PARAM_NOTIFY)
- リアルタイムパラメータ通知
- レート制限付き

### Phase 5: メーター情報 (METER)
- レベルメーター
- 高効率転送

## 6. 現在の実装の評価

### 6.1 リアルタイム安全性

**現在の状態:**
- SpscQueueはlock-free → ◎ 安全
- Server優先度でSysEx処理 → △ 改善余地あり
- シェル出力がServer優先度 → × 問題

**改善提案:**
```c++
// MIDIタスク（Server優先度）
void midi_task() {
    while (auto msg = midi_queue.try_pop()) {
        if (is_realtime_message(msg)) {
            // Note/CC → 即座に処理
            process_realtime(msg);
        } else {
            // SysEx → Idleキューへ転送
            sysex_queue.try_push(msg);
        }
    }
}

// SysExタスク（Idle優先度）
void sysex_task() {
    while (auto msg = sysex_queue.try_pop()) {
        process_sysex(msg);  // StandardIO等
    }
}
```

### 6.2 StandardIO実装

**現在の実装 (standard_io.hh):**
```c++
template <typename SendFn>
size_t write_stdout(const uint8_t* data, size_t len, SendFn send_fn) {
    if (tx_paused_) return 0;  // フロー制御あり
    constexpr size_t MAX_CHUNK = 1024;
    // ... チャンク分割送信
}
```

**評価:**
- フロー制御 (XON/XOFF) 実装済み → ◎
- チャンク分割 → ◎
- 優先度分離なし → × 要改善

### 6.3 カーネルとの連携

**現在:**
- シェルコマンド処理は同期的
- snprintf等の重い処理がServer優先度で実行される可能性

**推奨:**
- シェル出力のバッファリング
- Idle優先度での出力処理

## 7. 結論

### 7.1 採用するアプローチ

1. **バイナリ形式を維持** (MIDI-CIのJSON/CBORは採用しない)
2. **OSCの階層的思想を参考** (ただしバイナリで実装)
3. **サブスクリプションを限定的に採用** (レート制限付き)
4. **Idle優先度での処理を徹底**

### 7.2 次のステップ

1. commands.hhにSTATUS_REQUEST/RESPONSEを追加
2. SysEx処理のIdle優先度分離
3. Web側protocol.jsとの整合性確保
4. テスト実装とパフォーマンス測定

## 付録A: MIDI-CI Property Exchange 参考実装

```json
// Get device info
{
  "header": {"resource": "DeviceInfo"},
  "data": {}
}

// Response
{
  "header": {"resource": "DeviceInfo", "status": 200},
  "data": {
    "manufacturerId": [0x7E, 0x7F, 0x00],
    "familyId": [0x00, 0x01],
    "modelId": [0x00, 0x01],
    "versionId": [0x01, 0x02, 0x00, 0x03],
    "manufacturer": "UMI",
    "family": "Synthesizer",
    "model": "UMI Synth",
    "version": "1.2.3"
  }
}
```

## 付録B: 参考文献

1. MIDI 2.0 Specification (MMA/AMEI)
2. MIDI-CI Specification 1.2
3. Open Sound Control 1.0 Specification
4. USB MIDI 2.0 Specification
