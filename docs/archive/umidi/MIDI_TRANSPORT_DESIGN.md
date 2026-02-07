# MIDI トランスポート統合設計

## 目標

USB MIDI と UART MIDI（および将来の BLE MIDI 等）を、アプリケーション側から
トランスポートの違いを意識せずに扱えるようにする。

## 現状の問題

現在 3 つの異なる MIDI 表現が混在している：

| 表現 | 場所 | サイズ | 用途 |
|------|------|--------|------|
| `umidi::UMP32` + `umidi::Event` | lib/umidi | 4B / 8B | 統一 MIDI パケット (UMP) |
| `umi::midi::Message` + `umi::midi::Event` | lib/umi/kernel/umi_midi.hh | 4B / 8B | カーネル内 IPC |
| `MidiMsg` (raw bytes) | kernel.cc | 5B | レガシー USB→カーネルブリッジ |

USB MIDI アダプタ (`UmidiUsbMidiAdapter`) は umidi::Parser → umidi::EventQueue への
変換を行うが、AudioInterface のコールバックが context なし関数ポインタのため
グローバルシングルトン (`active_`) に依存している。

UART MIDI の実装はまだない。

## 設計方針

### 原則

1. **umidi が正規表現** — 全トランスポートの出力先は `umidi::EventQueue`
2. **パーサは共有** — `umidi::Parser` は MIDI 1.0 バイトストリーム汎用。USB も UART も同じ
3. **アダプタは薄く** — 各トランスポート固有のグルーコードのみ。ロジックは umidi に集約
4. **カーネル変換は一箇所** — `umidi::Event` → `umi::midi::Event` のブリッジは 1 つだけ
5. **リアルタイム安全** — アダプタ・パーサ・キュー全てヒープ不使用、ロックフリー

### 全体アーキテクチャ

```
┌──────────────────────────────────────────────────┐
│  Transport Layer (入力ソースごと)                  │
│                                                    │
│  USB MIDI          UART MIDI         (BLE MIDI)   │
│  AudioInterface    DMA Circular      (将来)        │
│  callback          1kHz poll                       │
│      │                 │                 │         │
│      ▼                 ▼                 ▼         │
│  ┌─────────────────────────────────────────────┐  │
│  │  umidi::Parser (バイトストリーム → UMP32)    │  │
│  │  ※ 各トランスポートがインスタンスを持つ      │  │
│  └─────────────────────────────────────────────┘  │
│      │                 │                 │         │
│      ▼                 ▼                 ▼         │
│  ┌─────────────────────────────────────────────┐  │
│  │  umidi::EventQueue (SPSC ロックフリー)       │  │
│  │  ※ トランスポートごとに独立キュー            │  │
│  │     または共有キュー (MPSC の場合)            │  │
│  └─────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────────────────┐
│  Merge Layer                                       │
│                                                    │
│  MidiInputMerger                                   │
│  複数の EventQueue を sample_pos 順にマージ        │
│  出力: 単一の umidi::EventQueue                    │
└──────────────────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────────────────┐
│  Application Layer                                 │
│                                                    │
│  カーネル: umidi::Event → AudioContext events      │
│  アプリ: queue.pop() で統一的に受信                │
└──────────────────────────────────────────────────┘
```

### 送信 (TX) の流れ

```
Application
    │ umidi::UMP32
    ▼
MidiOutputRouter
    ├── USB: AudioInterface::send_midi()
    ├── UART: DMA 送信キュー → UART TX
    └── (BLE: 将来)
```

## コンポーネント詳細

### 1. MidiInput concept

各トランスポートアダプタが満たすべきインターフェース。

```cpp
namespace umidi {

template<typename T>
concept MidiInput = requires(T& t) {
    { t.poll() } -> std::same_as<void>;             // ポーリング (必要なら)
    { t.is_connected() } -> std::convertible_to<bool>;
};

} // namespace umidi
```

`poll()` は UART のように定期呼び出しが必要なトランスポート用。
USB はコールバック駆動なので `poll()` は空実装。

### 2. USB MIDI アダプタ (既存の改善)

```cpp
namespace umidi {

class UsbMidiInput {
public:
    bool is_connected() const { return connected_; }
    void poll() {} // USB はコールバック駆動、ポーリング不要

    // AudioInterface から呼ばれるコールバック
    void on_midi_rx(uint8_t cable, const uint8_t* data, uint8_t len) {
        uint32_t ts = hw_timestamp_fn_ ? hw_timestamp_fn_() : 0;
        for (uint8_t i = 0; i < len; ++i) {
            UMP32 ump;
            if (parser_.parse_running(data[i], ump)) {
                raw_input_queue_->push({ts, source_id_, ump});
            }
        }
    }

    void set_hw_timestamp_provider(uint32_t (*fn)()) { hw_timestamp_fn_ = fn; }
    void set_raw_input_queue(RawInputQueue* q) { raw_input_queue_ = q; }
    void set_source_id(uint8_t id) { source_id_ = id; }
    void set_connected(bool c) { connected_ = c; }

private:
    Parser parser_{};
    RawInputQueue* raw_input_queue_ = nullptr;
    uint32_t (*hw_timestamp_fn_)() = nullptr;
    uint8_t source_id_ = 0;
    bool connected_ = false;
};

} // namespace umidi
```

**変更点**: `active_` グローバルシングルトンを廃止。AudioInterface 側の
コールバック登録をラムダまたは context 付き関数ポインタに変更するのが理想だが、
当面は kernel 側で薄いブリッジ関数を書いてインスタンスを直接呼ぶ。

### 3. UART MIDI アダプタ (新規)

```cpp
namespace umidi {

class UartMidiInput {
public:
    bool is_connected() const { return true; } // UART は常時接続

    /// 1kHz タスクから呼ぶ。DMA circular バッファを掃く
    void poll() {
        if (!dma_buf_) return;
        uint32_t ts = hw_timestamp_fn_ ? hw_timestamp_fn_() : 0;
        uint16_t write_pos = buf_size_ - *dma_ndtr_;
        while (read_pos_ != write_pos) {
            UMP32 ump;
            if (parser_.parse_running(dma_buf_[read_pos_], ump)) {
                raw_input_queue_->push({ts, source_id_, ump});
            }
            read_pos_ = (read_pos_ + 1) % buf_size_;
        }
    }

    /// DMA circular バッファをバインド
    void bind(const uint8_t* dma_buf, uint16_t buf_size,
              volatile uint16_t* dma_ndtr) {
        dma_buf_ = dma_buf;
        buf_size_ = buf_size;
        dma_ndtr_ = dma_ndtr;
        read_pos_ = 0;
    }

    void set_hw_timestamp_provider(uint32_t (*fn)()) { hw_timestamp_fn_ = fn; }
    void set_raw_input_queue(RawInputQueue* q) { raw_input_queue_ = q; }
    void set_source_id(uint8_t id) { source_id_ = id; }

private:
    Parser parser_{};
    RawInputQueue* raw_input_queue_ = nullptr;
    const uint8_t* dma_buf_ = nullptr;
    volatile uint16_t* dma_ndtr_ = nullptr;
    uint16_t buf_size_ = 0;
    uint16_t read_pos_ = 0;
    uint32_t (*hw_timestamp_fn_)() = nullptr;
    uint8_t source_id_ = 0;
};

} // namespace umidi
```

### 4. RawInputQueue（マージ層）

従来の MidiInputMerger の機能は RawInputQueue への直接 push で実現する。
各トランスポートアダプタが共有の RawInputQueue に push し、
System Service が読み出して分類・ルーティングする。

マージ専用クラスは不要。RawInputQueue の定義は [EVENT_SYSTEM_DESIGN.md](EVENT_SYSTEM_DESIGN.md) を参照。

### 5. MidiOutput (送信側)

```cpp
namespace umidi {

template<typename T>
concept MidiOutput = requires(T& t, const UMP32& ump) {
    { t.send(ump) } -> std::convertible_to<bool>;
    { t.is_connected() } -> std::convertible_to<bool>;
};

} // namespace umidi
```

USB 送信は既存の `AudioInterface::send_midi()` をラップ。
UART 送信は `umidi::Serializer` で UMP32 → バイト列に変換し DMA 送信キューへ。

## カーネル統合例

```cpp
// mcu.hh or kernel.hh

// 共有 RawInputQueue（System Service が読み出す）
RawInputQueue raw_input_queue;

// 入力アダプタ（RawInputQueue に直接 push）
umidi::UsbMidiInput  usb_midi_in;
umidi::UartMidiInput uart_midi_in;

// 初期化
void init_midi() {
    // hw_timestamp provider（DWT サイクルカウンタ）
    auto ts_fn = []() -> uint32_t { return DWT->CYCCNT; };

    // USB
    usb_midi_in.set_raw_input_queue(&raw_input_queue);
    usb_midi_in.set_hw_timestamp_provider(ts_fn);
    usb_midi_in.set_source_id(0);  // USB = 0
    usb_audio().set_midi_rx_callback([](uint8_t cable, const uint8_t* data, uint8_t len) {
        usb_midi_in.on_midi_rx(cable, data, len);
    });

    // UART
    uart_midi_in.set_raw_input_queue(&raw_input_queue);
    uart_midi_in.set_hw_timestamp_provider(ts_fn);
    uart_midi_in.set_source_id(1);  // UART = 1
    uart_midi_in.bind(uart_dma_buf, sizeof(uart_dma_buf), &USART2->NDTR);
}

// 1kHz タスク (SOF または SysTick)
void midi_poll() {
    uart_midi_in.poll();    // UART DMA バッファを掃く
    // USB はコールバック駆動なのでポーリング不要
}

// System Service が RawInputQueue を読み出して分類・ルーティング
// → 詳細は EVENT_SYSTEM_DESIGN.md を参照
```

## ファイル配置案

```
lib/umidi/include/
├── core/
│   ├── ump.hh              # (既存) UMP32
│   ├── parser.hh           # (既存) Parser, Serializer
│   └── ...
├── event.hh                # (既存) Event, EventQueue
├── transport/              # 新規ディレクトリ
│   ├── midi_input.hh       #   MidiInput concept
│   ├── midi_output.hh      #   MidiOutput concept
│   ├── usb_input.hh        #   UsbMidiInput (UmidiUsbMidiAdapter の後継)
│   └── uart_input.hh       #   UartMidiInput
└── umidi.hh                # (既存) 統合ヘッダー、transport/ を追加
```

`lib/umiusb/include/midi/umidi_adapter.hh` は `lib/umidi/include/transport/usb_input.hh`
に移動し、umiusb からの依存を削除する。USB 固有の AudioInterface コールバック登録は
カーネル側のグルーコードが担当する。

## 移行手順

1. `lib/umidi/include/transport/` に concept + UartMidiInput を追加
2. `UmidiUsbMidiAdapter` の中身を `UsbMidiInput` として transport/ に移動
3. `lib/umiusb/include/midi/umidi_adapter.hh` は後方互換エイリアスに縮退、または削除
4. カーネル側の MIDI 初期化を統合コードに置き換え（RawInputQueue 共有方式）
5. `umi::midi::Message` (4B レガシー) → `umidi::UMP32` への段階的移行

## sample_pos の精度について

トランスポート層は hw_timestamp（DWT サイクルカウンタ）のみ記録する。
sample_pos の算出は System Service が hw_timestamp から行う。
詳細は [JITTER_COMPENSATION.md](JITTER_COMPENSATION.md) を参照。

| トランスポート | hw_timestamp 精度 | sample_pos 精度 (@48kHz) |
|---------------|------------------|--------------------------|
| USB MIDI | SOF 単位 (~1ms) | ~48 サンプル |
| UART MIDI (ISR) | サイクル精度 (~6ns) | サンプル精度 |
| UART MIDI (poll) | ポーリング周期 (~1ms) | ~48 サンプル |
| (BLE MIDI) | ~7.5ms conn interval | ~360 サンプル |

## SysEx の扱い

SysEx の再組み立てとルーティングは System Service が一括処理する。
トランスポート層は UMP SysEx7 パケットをそのまま RawInputQueue に渡すだけ。

詳細は [SYSEX_ROUTING.md](SYSEX_ROUTING.md) を参照。
