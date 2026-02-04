# 統合 I/O ポートシステム（第3版・簡素化版）

## 設計哲学

**過度な抽象化を排除し、シンプルな「要求-提供」モデルを採用する。**

- ❌ Capabilityマッチング（複雑なマッチングアルゴリズムは不要）
- ❌ ボードケイパビリティの厳密な定義
- ❌ 動的メモリ確保（組み込み環境では不要）
- ✅ 全メモリは静的確保、有効要素数のみ変化
- ✅ アプリがポートを要求 → あれば使う、なければ無視
- ✅ ログで不足を通知

```
従来の複雑モデル: AppRequirements ↔ CapabilityMatching ↔ BoardCapability
                         ↓
第3版シンプルモデル:  App「ポートXを使いたい」→ OS「あるよ/ないよ」
                                              ↓
                                         あれば使う、なければ無視
```

---

## Core Types

### IoShape — サイズ記述子

```cpp
namespace umi {

struct IoShape {
    uint16_t stride;    // 要素サイズ（バイト）
    uint16_t count;     // 要素数
    
    [[nodiscard]] constexpr size_t size() const noexcept {
        return static_cast<size_t>(stride) * count;
    }
    
    // 互換性チェック（単純な包含関係）
    [[nodiscard]] constexpr bool contains(const IoShape& req) const noexcept {
        return stride <= req.stride && size() >= req.size();
    }
};

// よく使う形の定義
namespace shapes {
    constexpr IoShape f32_1ch{4, 1};     // float 1ch
    constexpr IoShape f32_8ch{4, 8};     // float 8ch
    constexpr IoShape u8_1ch{1, 1};      // uint8 1ch
    constexpr IoShape u8_16ch{1, 16};    // uint8 16ch
    constexpr IoShape rgb_1ch{3, 1};     // RGB 1個
}

} // namespace umi
```

### IoPort — ポート記述子

```cpp
namespace umi {

enum class IoDirection : uint8_t { INPUT, OUTPUT };
using io_port_id_t = uint16_t;
inline constexpr io_port_id_t IO_PORT_INVALID = 0xFFFF;

struct IoPort {
    io_port_id_t id;
    IoDirection direction;
    IoShape shape;              // 最大キャパシティ（静的確保サイズ）
    uint32_t buffer_offset;     // プール内オフセット
    uint16_t active_count = 0;  // 現在有効な要素数（0 = 未接続）
    
    /// 有効な要素のみを含むspanを返す
    template<typename T>
    [[nodiscard]] std::span<T> as_span(std::span<std::byte> pool) const noexcept {
        if (active_count == 0) return {};
        if (shape.stride != sizeof(T)) return {};  // 型チェック
        return std::span<T>(reinterpret_cast<T*>(&pool[buffer_offset]), active_count);
    }
    
    /// ポートが有効か
    [[nodiscard]] constexpr bool is_active() const noexcept {
        return active_count > 0;
    }
};

} // namespace umi
```

---

## Memory Layout

### Unified Buffer Pool

**方針: 1つの大きなプールを論理的に分割。ボード固有サイズ。**

```cpp
namespace umi {

// ボード固有サイズ（テンプレート特殊化で定義）
template<typename Board>
struct BoardConfig;

// STM32F407VG (192KB SRAM)
template<>
struct BoardConfig<class STM32F407VG> {
    static constexpr size_t SHARED_SIZE = 16 * 1024;
    static constexpr size_t AUDIO_FRAMES = 128;
    static constexpr uint8_t MAX_INPUTS = 8;
    static constexpr uint8_t MAX_OUTPUTS = 8;
};

// STM32H750 (1MB SRAM)  
template<>
struct BoardConfig<class STM32H750> {
    static constexpr size_t SHARED_SIZE = 32 * 1024;
    static constexpr size_t AUDIO_FRAMES = 256;
    static constexpr uint8_t MAX_INPUTS = 16;
    static constexpr uint8_t MAX_OUTPUTS = 16;
};

// 統一バッファプール
template<typename Board>
class UnifiedBufferPool {
    using C = BoardConfig<Board>;
    
public:
    static constexpr size_t AUDIO_INPUT_SIZE = C::AUDIO_FRAMES * 2 * sizeof(float);
    static constexpr size_t AUDIO_OUTPUT_SIZE = C::AUDIO_FRAMES * 2 * sizeof(float);
    static constexpr size_t IO_BUFFER_SIZE = 4096;  // 全I/Oポート用（静的確保）
    static constexpr size_t EVENT_QUEUE_SIZE = 2048;
    static constexpr size_t STATE_SIZE = 1024;
    
    static_assert(
        AUDIO_INPUT_SIZE + AUDIO_OUTPUT_SIZE + IO_BUFFER_SIZE + 
        EVENT_QUEUE_SIZE + STATE_SIZE <= C::SHARED_SIZE,
        "Total size exceeds shared memory"
    );
    
private:
    alignas(4) std::array<std::byte, C::SHARED_SIZE> pool_;
    
    // レイアウト（コンストラクタで初期化）
    struct Layout {
        size_t audio_input_off;
        size_t audio_output_off;
        size_t io_buffer_off;
        size_t events_off;
        size_t state_off;
    } layout_;
    
    // ポート管理（全て静的確保）
    std::array<IoPort, C::MAX_INPUTS> inputs_;
    std::array<IoPort, C::MAX_OUTPUTS> outputs_;
    
public:
    // ---------------------------------------------------------
    // シンプルなアクセスAPI（システムの核）
    // ---------------------------------------------------------
    
    /// 入力ポート取得（未接続なら空のspan）
    template<typename T>
    [[nodiscard]] std::span<T> input_port(uint8_t index) noexcept {
        if (index >= C::MAX_INPUTS) {
            umi::log("Input port ", index, " out of range");
            return {};
        }
        return inputs_[index].template as_span<T>(io_span());
    }
    
    /// 出力ポート取得（未接続なら空のspan）
    template<typename T>
    [[nodiscard]] std::span<T> output_port(uint8_t index) noexcept {
        if (index >= C::MAX_OUTPUTS) {
            umi::log("Output port ", index, " out of range");
            return {};
        }
        return outputs_[index].template as_span<T>(io_span());
    }
    
    // ---------------------------------------------------------
    // 拡張デバイス接続/切断時のactive_count更新（OS側が呼ぶ）
    // ---------------------------------------------------------
    
    /// 入力ポートの有効要素数を設定
    void set_input_active_count(uint8_t index, uint16_t count) noexcept {
        if (index >= C::MAX_INPUTS) return;
        auto& port = inputs_[index];
        port.active_count = std::min(count, port.shape.count);
        umi::log("Input port ", index, " active_count=", port.active_count);
    }
    
    /// 出力ポートの有効要素数を設定
    void set_output_active_count(uint8_t index, uint16_t count) noexcept {
        if (index >= C::MAX_OUTPUTS) return;
        auto& port = outputs_[index];
        port.active_count = std::min(count, port.shape.count);
        umi::log("Output port ", index, " active_count=", port.active_count);
    }
    
private:
    [[nodiscard]] std::span<std::byte> io_span() noexcept {
        return std::span<std::byte>(&pool_[layout_.io_buffer_off], IO_BUFFER_SIZE);
    }
};

} // namespace umi
```

---

## Application API

### AudioContext v3 — シンプル版

```cpp
namespace umi {

struct AudioContextV3 {
    // オーディオ
    std::span<const float> audio_in_left;
    std::span<const float> audio_in_right;
    std::span<float> audio_out_left;
    std::span<float> audio_out_right;
    
    // I/Oプール参照
    UnifiedBufferPool<CurrentBoard>* io_pool = nullptr;
    
    // イベント
    std::span<const Event> input_events;
    EventQueue<>* output_events = nullptr;
    
    // タイミング
    uint32_t sample_rate = 48000;
    uint32_t buffer_size = 128;
    uint64_t sample_position = 0;
    
    // ---------------------------------------------------------
    // シンプルなI/Oアクセス
    // ---------------------------------------------------------
    
    template<typename T>
    [[nodiscard]] std::span<T> input(uint8_t port_index) noexcept {
        if (!io_pool) return {};
        return io_pool->template input_port<T>(port_index);
    }
    
    template<typename T>
    [[nodiscard]] std::span<T> output(uint8_t port_index) noexcept {
        if (!io_pool) return {};
        return io_pool->template output_port<T>(port_index);
    }
    
    // 便利関数
    [[nodiscard]] std::span<float> knobs(uint8_t index = 0) noexcept {
        return input<float>(index);
    }
    
    [[nodiscard]] std::span<uint8_t> buttons(uint8_t index = 1) noexcept {
        return input<uint8_t>(index);
    }
    
    [[nodiscard]] std::span<uint8_t> leds(uint8_t index = 0) noexcept {
        return output<uint8_t>(index);
    }
};

} // namespace umi
```

### 使用例

```cpp
// アプリ側の実装例
class MySynth {
public:
    void process(umi::AudioContextV3& ctx) {
        // ---------------------------------------------------------
        // 1. 入力取得（存在しなければ空のspan、ログで通知）
        // ---------------------------------------------------------
        
        // ノブ（ポート0、float 8ch想定）
        auto knobs = ctx.knobs(0);
        if (!knobs.empty()) {
            cutoff_ = knobs[0];
            resonance_ = knobs[1];
        } else {
            // ログで通知済み（「Input port 0 not active」）
            // デフォルト値を使用
            cutoff_ = 0.5f;
            resonance_ = 0.0f;
        }
        
        // ボタン（ポート1、uint8 16ch想定）
        auto buttons = ctx.buttons(1);
        if (!buttons.empty()) {
            bool play_btn = buttons[0] & 0x01;
            bool stop_btn = buttons[0] & 0x02;
            // ...
        }
        
        // ---------------------------------------------------------
        // 2. 存在しないポートへのアクセス → ログのみ、例外なし
        // ---------------------------------------------------------
        
        auto nonexistent = ctx.input<float>(99);  // 範囲外
        // → 空のspanが返る
        // → ログ: "Input port 99 out of range"
        
        // ---------------------------------------------------------
        // 3. 出力
        // ---------------------------------------------------------
        
        auto leds = ctx.leds(0);
        if (!leds.empty()) {
            leds[0] = static_cast<uint8_t>(cutoff_ * 255.0f);  // 単色LED
        }
        
        // CV出力（float 4ch想定）
        auto cv_out = ctx.output<float>(2);
        if (!cv_out.empty()) {
            cv_out[0] = filter_.process(audio_in);
        }
        
        // ---------------------------------------------------------
        // 4. 拡張デバイス対応（OS側がactive_countを変更）
        // ---------------------------------------------------------
        
        // 例: USB-MIDIコントローラが接続されると
        // OS側で set_input_active_count(3, 8) が呼ばれ、
        // 次回から ctx.input<float>(3) が 8要素のspanを返す
        //
        // アプリは接続状態を意識せず、単に empty() をチェックするだけ
        auto ext_knobs = ctx.input<float>(3);
        if (!ext_knobs.empty()) {
            // 拡張ノブがある場合の処理
            for (size_t i = 0; i < ext_knobs.size(); ++i) {
                // 可変長で対応
            }
        }
    }
    
private:
    float cutoff_ = 0.5f;
    float resonance_ = 0.0f;
};
```

---

## エラー処理とログ

### シンプルな方針

| 状況 | 動作 | ログレベル |
|------|------|-----------|
| ポート存在、正常アクセス | 正常動作 | なし |
| ポート不在（範囲外） | 空のspan返却 | `log::warning` |
| ポート未接続（active_count=0） | 空のspan返却 | なし（正常動作） |
| 型不一致 | 空のspan返却 | `log::warning` |
| active_count変更 | ログ出力 | `log::info` |

### ログ実装例

```cpp
namespace umi {

// シンプルなログ（SysEx経由でホストへ送信）
template<typename... Args>
void log(Args&&... args) noexcept {
    char buffer[128];
    int len = snprintf(buffer, sizeof(buffer), std::forward<Args>(args)...);
    if (len > 0) {
        syscall::log(buffer, static_cast<size_t>(len));
    }
}

} // namespace umi
```

---

## 静的ポート設定（BSP層）

```cpp
// lib/umi/port/board/daisy_pod/board/io_config.hh
namespace umi::board::daisy_pod {

using Board = daisy_pod;  // タグ型

// ボード設定（テンプレート特殊化）
template<>
struct BoardConfig<Board> {
    static constexpr size_t SHARED_SIZE = 16 * 1024;
    static constexpr size_t AUDIO_FRAMES = 128;
    static constexpr uint8_t MAX_INPUTS = 4;   // オンボード + 拡張用予約
    static constexpr uint8_t MAX_OUTPUTS = 2;  // オンボード + 拡張用予約
};

// 静的ポート初期化（起動時にOSが実行）
// 最大キャパシティを登録、active_count は接続状態に応じて変化
inline void init_static_ports(UnifiedBufferPool<Board>& pool) {
    // === オンボードI/O（起動時に active_count 設定） ===
    
    // 入力ポート0: ノブ（最大8ch分確保、オンボードは2ch）
    pool.register_input(0, IoShape{4, 8});   // float × 8
    pool.set_input_active_count(0, 2);        // オンボード: 2ch
    
    // 入力ポート1: ボタン（最大16個分確保、オンボードは2個）
    pool.register_input(1, IoShape{1, 16});  // uint8 × 16
    pool.set_input_active_count(1, 2);        // オンボード: 2個
    
    // 入力ポート2: エンコーダ（最大4個分確保、オンボードは1個）
    pool.register_input(2, IoShape{1, 4});
    pool.set_input_active_count(2, 1);
    
    // 入力ポート3: 拡張用（USB-MIDIコントローラ等）
    pool.register_input(3, IoShape{4, 16});  // float × 16 分確保
    pool.set_input_active_count(3, 0);        // 初期状態: 未接続
    
    // === 出力 ===
    
    // 出力ポート0: LED（最大16個分確保、オンボードはRGB 1個）
    pool.register_output(0, IoShape{3, 16}); // RGB × 16
    pool.set_output_active_count(0, 1);       // オンボード: 1個
    
    // 出力ポート1: 拡張用（LEDストリップ等）
    pool.register_output(1, IoShape{3, 144}); // RGB × 144 分確保
    pool.set_output_active_count(1, 0);       // 初期状態: 未接続
}

// 拡張デバイス接続時（OSドライバが呼ぶ）
inline void on_usb_midi_controller_connected(UnifiedBufferPool<Board>& pool, uint8_t num_knobs) {
    // 入力ポート3 を有効化
    pool.set_input_active_count(3, num_knobs);
    // → アプリ側は次の process() から knobs が増える
}

// 拡張デバイス切断時
inline void on_usb_midi_controller_disconnected(UnifiedBufferPool<Board>& pool) {
    pool.set_input_active_count(3, 0);
    // → アプリ側は次の process() から span.empty() になる
}

} // namespace umi::board::daisy_pod
```

---

## まとめ：設計の変遷

| 項目 | 第1版（複雑） | 第3版（簡素） |
|------|-------------|--------------|
| **マッチング** | CapabilityMatchResult, match_io_ports() | ❌ 削除 |
| **エラー処理** | 厳密な型/サイズチェック | 空span返却 + ログ |
| **メモリ管理** | 複雑なレイアウト計算 | UnifiedBufferPool（静的確保） |
| **ボード対応** | BoardCapability構造体 | テンプレート特殊化 |
| **拡張デバイス** | 動的確保/解放 | active_count変更のみ |
| **API** | input_port<T>(id, strict=true) | input<T>(index), 空なら無視 |
| **行数見積** | ~2000行 | ~400行（80%削減） |

### 設計原則（第3版）

1. **YAGNI**: 必要になるまで複雑な機能は実装しない
2. **Fail Soft**: エラーは例外ではなく空の値で表現
3. **Visibility**: 問題はログで可視化、デバッグしやすく
4. **Simplicity**: 1つの大きなプール、単純なアクセスパターン
5. **Static First**: メモリは静的確保、変化するのは有効要素数のみ

### 実装規模

- **ヘッダファイル**: 2ファイル（io_port.hh, io_pool.hh）
- **総行数**: ~400行（第1版の20%）
- **テスト**: 各ポートタイプで接続/未接続ケースをカバー
- **統合**: 既存AudioContextと並行運用可能
