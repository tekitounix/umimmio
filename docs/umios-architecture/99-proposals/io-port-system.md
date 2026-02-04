# 統合 I/O ポートシステム提案

## 概要

本提案は、入出力を **型情報を持たない汎用ポート** として統一的に扱うシステムを定義する。

**設計原則:**
1. 全ての I/O は「状態バッファ」と「イベント」で表現できる
2. ポートは具象型 (LED, CV, etc.) を持たず、サイズ情報のみを持つ
3. 静的ポート（オンボード）と動的ポート（拡張デバイス）を同一の仕組みで管理
4. 各ポートは DMA 転送可能な連続メモリ領域

```
従来:  LED[8], RGB[8], CV[8], Display[1024]  ← 具象型の羅列
本提案: IoPort[N] ← サイズ情報のみ、用途は OS/App が解釈
```

## 用語定義

| 用語 | 説明 |
|------|------|
| `IoPort` | 入出力の単位。状態バッファへの参照を持つ |
| `IoShape` | ポートのサイズ情報 (stride × length) |
| `IoDirection` | 入力 (INPUT) または出力 (OUTPUT) |
| `io_port_id_t` | ポートの識別子 |

## IoShape — ポートサイズ記述子

```cpp
namespace umi {

/// ポートのサイズ情報（型情報なし）
struct IoShape {
    uint16_t stride;    // 1要素のバイト数
    uint16_t length;    // 要素数 (1 = スカラー, >1 = 配列)

    constexpr size_t size() const { return stride * length; }
};

} // namespace umi
```

**具体例:**

| 用途 | stride | length | size() | 説明 |
|------|--------|--------|--------|------|
| 単一 LED | 1 | 1 | 1B | 輝度 0-255 |
| CV 入力 | 4 | 1 | 4B | float 1ch |
| CV 入力 8ch | 4 | 8 | 32B | float 8ch |
| RGB LED | 3 | 1 | 3B | R,G,B 各1B |
| LED ストリップ 144 | 3 | 144 | 432B | RGB × 144 |
| ボタン 16個 | 1 | 16 | 16B | 状態 × 16 |
| ディスプレイ 128×64 1bpp | 1 | 1024 | 1KB | フレームバッファ |
| ノブ 8個 | 4 | 8 | 32B | float × 8 |

## IoPort — 統一ポート記述子

```cpp
namespace umi {

enum class IoDirection : uint8_t {
    INPUT,
    OUTPUT,
};

using io_port_id_t = uint16_t;
constexpr io_port_id_t IO_PORT_INVALID = 0xFFFF;

/// ポート記述子（ランタイム情報）
struct IoPort {
    io_port_id_t id;            // ポートID
    IoDirection direction;      // 入力 or 出力
    IoShape shape;              // サイズ情報
    uint32_t buffer_offset;     // バッファプール内オフセット
    uint32_t dirty_mask;        // 変更フラグ用ビットマスク

    bool is_static : 1;         // 静的 or 動的
    bool is_active : 1;         // 有効 or 無効
};

} // namespace umi
```

## メモリレイアウト

### バッファプール構造

```cpp
namespace umi {

/// I/O バッファプール（DMA可能領域に配置）
struct IoBufferPool {
    // 静的ポート用（コンパイル時確定）
    alignas(4) uint8_t static_buffer[STATIC_IO_SIZE];

    // 動的ポート用（拡張デバイス接続時に確保）
    alignas(4) uint8_t dynamic_pool[DYNAMIC_IO_POOL_SIZE];

    // メタ情報
    uint32_t dynamic_pool_used;     // 使用済みサイズ
    uint32_t input_dirty_flags;     // 入力変更フラグ
    uint32_t output_dirty_flags;    // 出力変更フラグ
};

} // namespace umi
```

### レイアウト計算（コンパイル時）

```cpp
namespace umi {

/// コンパイル時レイアウト計算
template<size_t N>
constexpr auto calc_io_layout(const IoShape (&shapes)[N], uint8_t align = 4) {
    struct Layout {
        uint32_t offsets[N];
        uint32_t total_size;
    };

    Layout layout{};
    uint32_t offset = 0;

    for (size_t i = 0; i < N; ++i) {
        // アライメント調整
        offset = (offset + align - 1) & ~(align - 1);
        layout.offsets[i] = offset;
        offset += shapes[i].size();
    }

    layout.total_size = (offset + align - 1) & ~(align - 1);
    return layout;
}

} // namespace umi
```

## ポートテーブル

### 静的ポートテーブル（コンパイル時定義）

```cpp
// === OS 側: ハードウェア Capability として定義 ===
namespace board {

constexpr IoShape HW_INPUT_SHAPES[] = {
    {4, 8},     // port 0: ノブ 8ch (float × 8 = 32B)
    {1, 16},    // port 1: ボタン 16個 (16B)
    {4, 4},     // port 2: CV IN 4ch (float × 4 = 16B)
};

constexpr IoShape HW_OUTPUT_SHAPES[] = {
    {1, 8},     // port 0: LED 8個 (8B)
    {3, 144},   // port 1: LED ストリップ 144 RGB (432B)
    {4, 4},     // port 2: CV OUT 4ch (float × 4 = 16B)
    {1, 1024},  // port 3: ディスプレイ 128×64 1bpp (1KB)
};

constexpr auto INPUT_LAYOUT = calc_io_layout(HW_INPUT_SHAPES);
constexpr auto OUTPUT_LAYOUT = calc_io_layout(HW_OUTPUT_SHAPES);

} // namespace board
```

### 動的ポートテーブル（ランタイム管理）

```cpp
namespace umi {

/// 動的ポートマネージャ
class DynamicPortManager {
public:
    static constexpr size_t MAX_DYNAMIC_PORTS = 32;

    /// 動的ポートを確保
    io_port_id_t allocate(IoDirection dir, IoShape shape) {
        // プールから連続領域を確保（4バイトアライン）
        uint32_t aligned_size = (shape.size() + 3) & ~3;
        if (pool_used + aligned_size > DYNAMIC_IO_POOL_SIZE) {
            return IO_PORT_INVALID;  // プール不足
        }

        auto id = find_free_slot();
        if (id == IO_PORT_INVALID) return id;

        ports[id] = {
            .id = static_cast<io_port_id_t>(DYNAMIC_PORT_BASE + id),
            .direction = dir,
            .shape = shape,
            .buffer_offset = pool_used,
            .dirty_mask = 1u << id,
            .is_static = false,
            .is_active = true,
        };

        pool_used += aligned_size;
        return ports[id].id;
    }

    /// 動的ポートを解放
    void deallocate(io_port_id_t id);

    /// ポート情報取得
    const IoPort* get(io_port_id_t id) const;

private:
    IoPort ports[MAX_DYNAMIC_PORTS];
    uint32_t pool_used = 0;
};

} // namespace umi
```

## 統一アクセス API

### ポートバッファ取得

```cpp
namespace umi {

/// ポートバッファを取得（DMA転送可能な連続領域）
std::span<uint8_t> get_port_buffer(io_port_id_t id) {
    if (is_static_port(id)) {
        auto& port = static_ports[id];
        return {&static_buffer[port.buffer_offset], port.shape.size()};
    } else {
        auto* port = dynamic_ports.get(id);
        if (!port || !port->is_active) return {};
        return {&dynamic_pool[port->buffer_offset], port->shape.size()};
    }
}

/// 型付きアクセス（ユーティリティ）
template<typename T>
std::span<T> get_port_as(io_port_id_t id) {
    auto buf = get_port_buffer(id);
    return {reinterpret_cast<T*>(buf.data()), buf.size() / sizeof(T)};
}

} // namespace umi
```

### process() からのアクセス

```cpp
void process(umi::AudioContext& ctx) {
    // ポートIDでアクセス（型はアプリが解釈）
    auto knobs = ctx.input_port<float>(0);      // ノブ 8ch
    auto buttons = ctx.input_port<uint8_t>(1);  // ボタン 16個

    auto leds = ctx.output_port<uint8_t>(0);    // LED 8個
    auto strip = ctx.output_port<uint8_t>(1);   // LED ストリップ

    // 処理
    for (int i = 0; i < 8; ++i) {
        leds[i] = static_cast<uint8_t>(knobs[i] * 255);
    }

    // 変更をマーク
    ctx.mark_output_dirty(0);
}
```

## IoEvent — 統一イベント

状態変化イベントも汎用化する。

```cpp
namespace umi {

/// 汎用 I/O イベント
struct IoEvent {
    uint32_t timestamp;         // hw_timestamp または sample_pos
    io_port_id_t port_id;       // 対象ポート
    uint16_t index;             // ポート内インデックス
    uint32_t value;             // 値（解釈はポート依存）
};

/// イベントキュー
using IoEventQueue = RingBuffer<IoEvent, 256>;

} // namespace umi
```

**使用例:**

```cpp
// ボタン押下イベント
IoEvent{.timestamp = now, .port_id = 1, .index = 3, .value = 1}

// MIDI Note On (port_id = MIDI_PORT, value = packed MIDI data)
IoEvent{.timestamp = now, .port_id = MIDI_IN_PORT, .index = 0, .value = 0x90407F}
```

## App 側の要求定義

### 旧方式（具象型）

```cpp
// ❌ 旧: 具象型の羅列
struct AppConfig {
    uint8_t num_leds;
    uint8_t num_cv_outputs;
    // ... 種別ごとに増える
};
```

### 新方式（汎用ポート）

```cpp
// ✅ 新: サイズ要求のみ
constexpr IoShape APP_INPUT_REQUIREMENTS[] = {
    {4, 8},     // "8個の 4B 入力が欲しい" (ノブとして使う)
    {1, 4},     // "4個の 1B 入力が欲しい" (ボタンとして使う)
};

constexpr IoShape APP_OUTPUT_REQUIREMENTS[] = {
    {1, 8},     // "8個の 1B 出力が欲しい" (LED として使う)
    {4, 4},     // "4個の 4B 出力が欲しい" (CV として使う)
};
```

## Capability マッチング

```cpp
namespace umi {

/// App 要求と HW Capability のマッチング結果
struct IoPortBinding {
    io_port_id_t app_port;      // App が要求したポート
    io_port_id_t hw_port;       // バインドされた HW ポート
    bool compatible;            // 互換性あり
};

/// マッチング実行
auto match_io_ports(
    std::span<const IoShape> app_requirements,
    std::span<const IoShape> hw_capabilities
) -> std::vector<IoPortBinding>;

} // namespace umi
```

**マッチングルール:**
- App 要求 `stride × length` ≤ HW 提供 `stride × length` で互換
- 順序でマッチング、または名前付きバインディング

## 動的ポート: 拡張デバイス対応

### 拡張デバイス接続時

```cpp
// USB MIDI コントローラ接続
void on_usb_device_connected(const UsbDeviceInfo& info) {
    if (info.is_midi_controller()) {
        // 動的入力ポートを確保
        auto port_id = dynamic_ports.allocate(
            IoDirection::INPUT,
            IoShape{.stride = 1, .length = 128}  // 128 CC values
        );

        // ドライバにバインド
        usb_midi_driver.bind_input_port(port_id);

        // App に通知
        notify_port_added(port_id);
    }
}
```

### 拡張デバイス切断時

```cpp
void on_usb_device_disconnected(uint8_t device_id) {
    auto port_id = find_port_by_device(device_id);
    if (port_id != IO_PORT_INVALID) {
        // App に通知
        notify_port_removed(port_id);

        // ポート解放
        dynamic_ports.deallocate(port_id);
    }
}
```

## IoRouter — 統合ルーター

EventRouter と OutputRouter を統合。

```cpp
namespace umi {

class IoRouter {
public:
    /// 入力処理（HW → App）
    void process_inputs(IoBufferPool& pool, IoEventQueue& events) {
        // 1. 静的入力ポートを BSP から更新
        for (auto& port : static_input_ports) {
            if (bsp::poll_input(port.id, get_buffer(port))) {
                pool.input_dirty_flags |= port.dirty_mask;
            }
        }

        // 2. 動的入力ポートを更新
        for (auto& port : dynamic_input_ports) {
            // ドライバが直接書き込み済み
        }

        // 3. イベント生成（状態変化検出）
        generate_change_events(pool, events);
    }

    /// 出力処理（App → HW）
    void process_outputs(IoBufferPool& pool) {
        if (pool.output_dirty_flags == 0) return;

        // 各ポートを BSP に反映
        for (auto& port : static_output_ports) {
            if (pool.output_dirty_flags & port.dirty_mask) {
                bsp::write_output(port.id, get_buffer(port));
            }
        }

        // 動的出力ポートも同様
        for (auto& port : dynamic_output_ports) {
            // ...
        }

        pool.output_dirty_flags = 0;
    }
};

} // namespace umi
```

## SharedMemory への統合

```cpp
struct SharedMemory {
    // === オーディオ (8KB) ===
    float audio_input[BUFFER_SIZE * MAX_CHANNELS];
    float audio_output[BUFFER_SIZE * MAX_CHANNELS];

    // === I/O バッファプール (4KB) ===
    IoBufferPool io;

    // === イベントキュー (2KB) ===
    IoEventQueue input_events;
    IoEventQueue output_events;

    // === パラメータ (2KB) ===
    SharedParams params;
};
```

メモリ領域配置:

| セクション | サイズ | 内容 |
|-----------|--------|------|
| Shared Audio | 8KB | オーディオバッファ |
| Shared IO | 4KB | IoBufferPool (静的 + 動的) |
| Shared Events | 2KB | IoEventQueue × 2 |
| Shared Params | 2KB | パラメータ |

## 全体アーキテクチャ

```
┌─────────────────────────────────────────────────────────────┐
│ ハードウェア                                                  │
│   オンボード: ノブ, ボタン, LED, CV, Display                  │
│   拡張: USB MIDI, 拡張ボード, etc.                           │
└─────────────────────────────────────────────────────────────┘
        │                                       ▲
        ▼                                       │
┌─────────────────────────────────────────────────────────────┐
│ BSP / ドライバ層                                             │
│   bsp::poll_input(port_id, buffer)                          │
│   bsp::write_output(port_id, buffer)                        │
└─────────────────────────────────────────────────────────────┘
        │                                       ▲
        ▼                                       │
┌─────────────────────────────────────────────────────────────┐
│ IoRouter                                                     │
│   ┌─────────────────┐    ┌─────────────────┐                │
│   │ 入力処理         │    │ 出力処理         │                │
│   │ HW → IoBuffer   │    │ IoBuffer → HW   │                │
│   │ → IoEventQueue  │    │ dirty_flags参照  │                │
│   └─────────────────┘    └─────────────────┘                │
└─────────────────────────────────────────────────────────────┘
        │                                       ▲
        ▼                                       │
┌─────────────────────────────────────────────────────────────┐
│ SharedMemory                                                 │
│   ┌─────────────────────────────────────────────────────┐   │
│   │ IoBufferPool                                         │   │
│   │   static_buffer[]: 静的ポートデータ                   │   │
│   │   dynamic_pool[]:  動的ポートデータ                   │   │
│   │   dirty_flags:     変更追跡                          │   │
│   └─────────────────────────────────────────────────────┘   │
│   ┌─────────────────────────────────────────────────────┐   │
│   │ IoEventQueue (input / output)                        │   │
│   └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
        │                                       ▲
        ▼                                       │
┌─────────────────────────────────────────────────────────────┐
│ Application                                                  │
│                                                              │
│   Processor::process(ctx)                                    │
│     auto knobs = ctx.input_port<float>(0);                   │
│     auto leds = ctx.output_port<uint8_t>(0);                 │
│     leds[i] = f(knobs[i]);                                   │
│     ctx.mark_output_dirty(0);                                │
│                                                              │
│   Controller (main)                                          │
│     wait_event() → IoEvent                                   │
│     get_port_buffer(port_id)                                 │
└─────────────────────────────────────────────────────────────┘
```

## WASM / Plugin バックエンド

### WASM

```javascript
class UmiProcessor extends AudioWorkletProcessor {
    process(inputs, outputs, parameters) {
        // IoBufferPool を WASM メモリ経由でアクセス
        const dirtyFlags = this.wasmMemory.getUint32(IO_DIRTY_OFFSET);

        // 変更があったポートのみ UI に通知
        for (let i = 0; i < this.outputPorts.length; i++) {
            if (dirtyFlags & (1 << i)) {
                const port = this.outputPorts[i];
                this.postMessage({
                    type: 'port_update',
                    portId: port.id,
                    data: this.wasmMemory.slice(port.offset, port.offset + port.size)
                });
            }
        }

        return true;
    }
}
```

### Plugin (VST3/AU/CLAP)

```cpp
tresult UmiVst3Processor::process(ProcessData& data) {
    // IoBufferPool の dirty_flags をチェック
    if (io_pool.output_dirty_flags != 0) {
        // GUI に通知（ポートIDと dirty_flags を送信）
        sendMessageToController(io_pool.output_dirty_flags);
    }
}
```

## 実装計画

| フェーズ | 内容 | 優先度 |
|----------|------|--------|
| Phase 1 | IoShape, IoPort, IoBufferPool 定義 | 高 |
| Phase 2 | 静的ポートのコンパイル時レイアウト計算 | 高 |
| Phase 3 | get_port_buffer() 統一アクセス API | 高 |
| Phase 4 | IoRouter 実装（入出力統合） | 高 |
| Phase 5 | IoEvent による状態変化イベント | 中 |
| Phase 6 | DynamicPortManager（動的ポート） | 中 |
| Phase 7 | Capability マッチング | 中 |
| Phase 8 | WASM / Plugin バックエンド対応 | 低 |

## 関連ドキュメント

- [01-audio-context.md](../00-fundamentals/01-audio-context.md) — AudioContext 仕様
- [03-event-system.md](../01-application/03-event-system.md) — EventRouter（本提案で IoRouter に統合）
- [10-shared-memory.md](../01-application/10-shared-memory.md) — SharedMemory 構造
- [21-config-mismatch.md](../01-application/21-config-mismatch.md) — Capability マッチング
