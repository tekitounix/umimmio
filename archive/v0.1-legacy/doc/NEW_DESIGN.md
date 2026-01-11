# UMI-OS 仕様書

**Universal Musical Instruments Operating System**

**バージョン:** 0.1.0 (Draft)  
**日付:** 2025年1月

---

## 目次

1. [概要](#1-概要)
2. [設計原則](#2-設計原則)
3. [システムアーキテクチャ](#3-システムアーキテクチャ)
4. [保護モデル](#4-保護モデル)
5. [実行モデル](#5-実行モデル)
6. [I/Oモデル](#6-ioモデル)
7. [Processor API](#7-processor-api)
8. [カーネルアーキテクチャ](#8-カーネルアーキテクチャ)
9. [ドライバモデル](#9-ドライバモデル)
10. [プラットフォーム抽象化層 (PAL)](#10-プラットフォーム抽象化層-pal)
11. [アダプタ層](#11-アダプタ層)
12. [状態管理](#12-状態管理)
13. [ケーパビリティとセキュリティ](#13-ケーパビリティとセキュリティ)
14. [エラー処理](#14-エラー処理)
15. [Instrument Service Protocol (ISP)](#15-instrument-service-protocol-isp)
16. [ビルドシステム](#16-ビルドシステム)
17. [数値表現](#17-数値表現)
18. [バージョニングと互換性](#18-バージョニングと互換性)
19. [Processorライフサイクル](#19-processorライフサイクル)
20. [テストインフラ](#20-テストインフラ)
21. [付録](#付録)

---

## 1. 概要

### 1.1 目的

UMI-OS (Universal Musical Instruments Operating System) は、電子楽器向けの統一実行環境を定義する。同一のアプリケーションコードが以下の環境で動作することを目標とする。

- 組み込みハードウェア (Cortex-A, Cortex-M, ESP32, RP2040)
- デスクトッププラグイン (VST3, AU, CLAP)
- WebAudio (WASM/AudioWorklet)
- スタンドアロンアプリケーション

### 1.2 本仕様の性質

UMI-OS は「OS実装」ではなく「実行契約と権限モデル」を定義する。具体的な実装はプラットフォームごとに異なるが、アプリケーションから見える振る舞いは統一される。

### 1.3 用語定義

| 用語 | 定義 |
|------|------|
| UMI-OS | Universal Musical Instruments Operating System、本仕様の名称 |
| Kernel | ハードウェア抽象化とタスク管理を行うシステム層 |
| Processor | オーディオ処理を行うアプリケーション単位 |
| Driver | ハードウェアデバイスへのアクセスを提供するモジュール |
| PAL | Platform Abstraction Layer、RTOS/ベアメタルを抽象化 |
| Adapter | 外部ホスト形式とUMI-OS形式を変換する層 |
| ISP | Instrument Service Protocol、診断・更新用プロトコル |

---

## 2. 設計原則

### 2.1 基本方針

1. **オーディオ処理を最優先**: すべての設計判断においてリアルタイムオーディオ処理の確実性を優先する
2. **保護能力の段階化**: ハードウェア能力に応じた保護レベルを提供し、下位互換を維持する
3. **正規化されたデータ**: アプリケーションはハードウェア非依存の正規化データのみを扱う
4. **最小権限の原則**: すべてのリソースアクセスはケーパビリティハンドルで管理する

### 2.2 C++言語方針

#### 2.2.1 言語バージョン

C++23以降を使用し、モダンな言語機能を積極的に活用する。

```cpp
// 使用するC++23機能の例
#include <expected>      // std::expected (エラーハンドリング)
#include <mdspan>        // 多次元配列ビュー
#include <print>         // std::print (デバッグ用)

// constexpr拡張
constexpr auto make_sin_table() {
    std::array<int16_t, 256> table{};
    for (int i = 0; i < 256; ++i) {
        table[i] = static_cast<int16_t>(std::sin(i * M_PI / 128) * 32767);
    }
    return table;
}
constexpr auto SIN_TABLE = make_sin_table();  // コンパイル時生成

// std::expected によるエラーハンドリング
std::expected<Capability, Error> acquire_capability(CapabilityType type);

// if constexpr による静的分岐
template<typename T>
sample_t convert(T value) {
    if constexpr (std::same_as<T, float>) {
        return value;
    } else if constexpr (std::same_as<T, int16_t>) {
        return Q15{value};
    }
}
```

#### 2.2.2 コンテナ使用方針

| コンテナ | 使用 | 条件 |
|----------|------|------|
| `std::array` | ✓ 推奨 | サイズ固定、スタック配置 |
| `std::span` | ✓ 推奨 | 非所有、参照渡し |
| `std::string_view` | ✓ 推奨 | 非所有、文字列参照 |
| `std::optional` | ✓ 許可 | オーバーヘッド最小 |
| `std::expected` | ✓ 許可 | エラーハンドリング |
| `std::vector` | △ 条件付 | 初期化時のみ使用可 |
| `std::string` | △ 条件付 | 初期化時のみ使用可 |
| `std::map/set` | ✗ 非推奨 | ヒープ使用、オーバーヘッド大 |

```cpp
// OK: std::arrayとstd::span
class Processor {
    std::array<sample_t, MAX_BUFFER> scratch_;
    
    void process(std::span<sample_t> output) {
        // spanは軽量、コピーなし
    }
};

// OK: 初期化時のみvector使用
class MySynth : public Processor {
    std::vector<float> wavetable_;  // 初期化時に確保
    
    void initialize() override {
        wavetable_.resize(2048);  // ここでのみヒープ使用
        // ... テーブル生成
    }
    
    void process(AudioContext& ctx) override {
        // wavetable_ を読むだけ、リサイズしない
    }
};
```

### 2.3 メモリ管理方針

#### 2.3.1 ヒープ使用ルール

| フェーズ | ヒープ使用 | 備考 |
|----------|------------|------|
| 初期化 (initialize) | ✓ 許可 | ウェーブテーブル、バッファ確保等 |
| 準備 (prepare) | △ 最小限 | サンプルレート依存のバッファ再確保 |
| 処理 (process) | ✗ 禁止 | リアルタイム要件 |
| 制御 (control) | △ 最小限 | 可能なら避ける |

```cpp
class Processor {
    // ヒープ使用タイミングの明示
    
    void initialize() override {
        // ✓ OK: 起動時の一度きり
        large_buffer_ = std::make_unique<float[]>(LARGE_SIZE);
    }
    
    void prepare(const StreamConfig& config) override {
        // △ 許容: サンプルレート変更時のみ
        if (config.sample_rate != current_rate_) {
            delay_buffer_.resize(config.sample_rate * MAX_DELAY_SEC);
        }
    }
    
    void process(AudioContext& ctx) override {
        // ✗ 禁止: 以下はNG
        // auto* p = new float[100];
        // std::vector<float> temp(100);
        // std::string s = "hello";
    }
};
```

#### 2.3.2 静的確保パターン

```cpp
// パターン1: 固定サイズ配列
class DelayLine {
    static constexpr size_t MAX_SAMPLES = 48000;  // 1秒 @ 48kHz
    std::array<sample_t, MAX_SAMPLES> buffer_;
    size_t write_pos_ = 0;
    size_t delay_samples_ = 0;
};

// パターン2: テンプレートパラメータ
template<size_t MaxVoices>
class PolySynth {
    std::array<Voice, MaxVoices> voices_;
};

// パターン3: コンパイル時定数テーブル
constexpr auto MIDI_TO_FREQ = [] {
    std::array<float, 128> table{};
    for (int i = 0; i < 128; ++i) {
        table[i] = 440.0f * std::pow(2.0f, (i - 69) / 12.0f);
    }
    return table;
}();
```

### 2.4 禁止事項

オーディオスレッド内での以下の操作を禁止する。

- 動的メモリ確保 (malloc, new, std::vector::push_back等)
- ブロッキングI/O
- ミューテックスロック
- システムコール（許可されたものを除く）
- 例外送出 (throw)

### 2.5 ログとアサート

#### 2.5.1 層ごとの使用方針

| 層 | assert | log | 依存 |
|----|--------|-----|------|
| DSP部品 | ✗ なし | ✗ なし | C++標準のみ |
| Processor | ✓ バッファ単位 | ✓ 適宜 | UMI-OS |
| Kernel | ✓ 必要に応じて | ✓ 必要に応じて | UMI-OS + PAL |

DSP部品は完全ポータブルとし、他のプロジェクトでも再利用可能な形を維持する。

```cpp
// === DSP部品: 依存なし、assertなし ===
// dsp/oscillator.hpp

class Oscillator {
    float phase_ = 0.0f;
    
public:
    // 事前条件は呼び出し側が保証
    // チェックは入れない（毎サンプル実行のため）
    float tick(float freq_normalized) {
        float out = std::sin(phase_ * 6.283185307f);
        phase_ += freq_normalized;
        phase_ -= static_cast<int>(phase_);
        return out;
    }
};
```

```cpp
// === Processor: UMI-OS依存OK ===
// app/my_synth.cpp

#include <umi/assert.hpp>
#include <umi/log.hpp>
#include "dsp/oscillator.hpp"

class MySynth : public umi::Processor {
    Oscillator osc_;
    
public:
    void initialize() override {
        umi::log::info("MySynth initialized");
    }
    
    void process(AudioContext& ctx) override {
        // バッファ単位のチェックはOK
        umi::assert(ctx.buffer_size <= MAX_BUFFER);
        
        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            // ホットパスではチェックしない
            ctx.outputs[0][i] = osc_.tick(freq_norm_);
        }
    }
};
```

#### 2.5.2 umi::assert

```cpp
// umi/assert.hpp

namespace umi {

namespace detail {
    // バックエンドにリンク時解決
    [[noreturn]] void assert_fail(
        const char* expr,
        std::source_location loc
    );
}

constexpr void assert(
    bool cond,
    std::source_location loc = std::source_location::current()
) {
    if (!cond) [[unlikely]] {
        detail::assert_fail("", loc);
    }
}

// メッセージ付き
template<typename... Args>
constexpr void assert(
    bool cond,
    std::format_string<Args...> fmt,
    Args&&... args,
    std::source_location loc = std::source_location::current()
) {
    if (!cond) [[unlikely]] {
        // フォーマットしてから fail
        detail::assert_fail(
            std::format(fmt, std::forward<Args>(args)...).c_str(),
            loc
        );
    }
}

// Releaseでも残るチェック（重要な不変条件用）
template<typename... Args>
constexpr void verify(
    bool cond,
    std::format_string<Args...> fmt,
    Args&&... args,
    std::source_location loc = std::source_location::current()
) {
    if (!cond) [[unlikely]] {
        log::error(fmt, std::forward<Args>(args)...);
        detail::assert_fail("verify failed", loc);
    }
}

} // namespace umi
```

#### 2.5.3 umi::log

```cpp
// umi/log.hpp

namespace umi::log {

enum class Level : uint8_t {
    Trace = 0,  // 超詳細（開発時のみ）
    Debug = 1,  // デバッグ情報
    Info  = 2,  // 通常情報
    Warn  = 3,  // 警告
    Error = 4,  // エラー
};

// コンパイル時レベル（NDEBUG時はInfo以上のみ）
inline constexpr Level compile_level = 
#if defined(NDEBUG)
    Level::Info;
#else
    Level::Trace;
#endif

namespace detail {
    // バックエンドにリンク時解決
    void write(Level level, std::string_view msg, std::source_location loc);
}

template<typename... Args>
void trace(std::format_string<Args...> fmt, Args&&... args,
           std::source_location loc = std::source_location::current()) {
    if constexpr (Level::Trace >= compile_level) {
        detail::write(Level::Trace, 
            std::format(fmt, std::forward<Args>(args)...), loc);
    }
}

template<typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args,
           std::source_location loc = std::source_location::current()) {
    if constexpr (Level::Debug >= compile_level) {
        detail::write(Level::Debug,
            std::format(fmt, std::forward<Args>(args)...), loc);
    }
}

template<typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args,
          std::source_location loc = std::source_location::current()) {
    if constexpr (Level::Info >= compile_level) {
        detail::write(Level::Info,
            std::format(fmt, std::forward<Args>(args)...), loc);
    }
}

template<typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args,
          std::source_location loc = std::source_location::current()) {
    detail::write(Level::Warn,
        std::format(fmt, std::forward<Args>(args)...), loc);
}

template<typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args,
           std::source_location loc = std::source_location::current()) {
    detail::write(Level::Error,
        std::format(fmt, std::forward<Args>(args)...), loc);
}

} // namespace umi::log
```

#### 2.5.4 バックエンド実装

バックエンドはPAL層で提供し、ビルド/リンク時に切り替える。

```cpp
// === 組み込み版 (RTT) ===
// kernel/pal/rtt/log_backend.cpp

namespace umi::detail {

void log::write(Level level, std::string_view msg, std::source_location loc) {
    // RTT Control Blockに書き込み
    rtt_write(std::format("[{}] {}:{} {}", 
        level_char(level), loc.file_name(), loc.line(), msg));
    
    // Info以上はSysExバッファにも追加
    if (level >= Level::Info) {
        sysex_buffer_push(level, msg);
    }
}

[[noreturn]] void assert_fail(const char* expr, std::source_location loc) {
    rtt_write(std::format("ASSERT FAILED: {} at {}:{}", 
        expr, loc.file_name(), loc.line()));
    
    __BKPT(0);  // デバッガブレーク
    while (1);
}

} // namespace umi::detail
```

```cpp
// === プラグイン/スタンドアロン版 ===
// adapter/plugin/log_backend.cpp

namespace umi::detail {

void log::write(Level level, std::string_view msg, std::source_location loc) {
    std::cerr << std::format("[{}] {}:{} {}\n",
        level_char(level), loc.file_name(), loc.line(), msg);
}

[[noreturn]] void assert_fail(const char* expr, std::source_location loc) {
    std::cerr << std::format("ASSERT FAILED: {} at {}:{}\n",
        expr, loc.file_name(), loc.line());
    std::abort();
}

} // namespace umi::detail
```

#### 2.5.5 RTT互換プロトコル

組み込み環境でのRTTバックエンド実装。

```cpp
// kernel/pal/rtt/rtt.hpp

namespace umi::rtt {

// RTT Control Block（デバッガが検索するマジック文字列を含む）
struct ControlBlock {
    char id[16] = "SEGGER RTT";
    int32_t max_up_buffers = 1;
    int32_t max_down_buffers = 0;
    Buffer up[1];
};

struct Buffer {
    const char* name = "Terminal";
    char* data;
    uint32_t size;
    volatile uint32_t write_pos;
    volatile uint32_t read_pos;  // デバッガが更新
    uint32_t flags = 0;
};

// リンカスクリプトで配置
// .rtt セクション、RAMの検索しやすい位置に
[[gnu::section(".rtt"), gnu::used]]
inline ControlBlock cb;

// ロックフリー書き込み（Audio thread安全）
inline void write(std::string_view msg) {
    auto& buf = cb.up[0];
    // SPSCリングバッファ書き込み
    // ...
}

} // namespace umi::rtt
```

#### 2.5.6 ビルド構成

| 構成 | assert | trace/debug | info以上 | 最適化 |
|------|--------|-------------|----------|--------|
| Debug | 有効→RTT+ブレーク | 有効→RTT | 有効→RTT+SysEx | -O0/-Og |
| Release | 消去 | 消去 | 有効→SysEx | -O2/-O3 |

```lua
-- xmake.lua

target("mysynth")
    set_kind("binary")
    add_deps("umi-core")
    add_files("app/*.cpp")
    
    -- Debug
    if is_mode("debug") then
        add_defines("UMI_BUILD_DEBUG")
        add_files("kernel/pal/rtt/*.cpp")  -- RTTバックエンド
        set_optimize("none")
    
    -- Release
    elseif is_mode("release") then
        add_defines("NDEBUG", "UMI_BUILD_RELEASE")
        add_files("kernel/pal/sysex/*.cpp")  -- SysExのみ
        set_optimize("fastest")
    end
```

### 2.6 ランタイムモニタリング

開発・デバッグ支援のため、以下のモニタリング機能を提供する（ISP経由で取得可能）。

| 項目 | 説明 | 更新頻度 |
|------|------|----------|
| Stack使用量 | 各タスクのスタック使用量/最大値 | 100ms |
| Heap使用量 | 総ヒープ使用量/確保量 | 100ms |
| CPU使用率 | process()の実行時間/バッファ周期 | バッファ毎 |
| ドロップ数 | 累積オーディオドロップ数 | バッファ毎 |
| 同期状態 | 外部同期のドリフト量 | 100ms |

### 2.7 時間管理

#### 2.7.1 2種類の時間

UMI-OSでは2種類の時間を明確に区別する。

| 種類 | 型 | 基準点 | 精度 | 用途 |
|------|-----|--------|------|------|
| サンプル時間 | `uint64_t` | ストリーム開始 | サンプル精度 | DSP、イベント、同期 |
| システム時間 | `std::chrono` | 起動時点 | μs | タイムアウト、ログ、統計 |

#### 2.7.2 使い分け判定フロー

```
「このタイミングは音に影響するか？」
    │
    ├─ Yes → サンプル時間を使用
    │         ・ノートオン/オフのタイミング
    │         ・シーケンサーのステップ
    │         ・LFO同期
    │         ・ディレイタイム
    │         ・エンベロープ時間
    │
    └─ No  → std::chrono を使用
              ・タイムアウト待機
              ・ログのタイムスタンプ
              ・定期的な状態保存
              ・UIの更新間隔
```

#### 2.7.3 サンプル時間

`AudioContext::sample_position` がストリーム開始からの累積サンプル数を示す。

```cpp
void MyProcessor::process(AudioContext& ctx) {
    // 現在のバッファの開始位置
    uint64_t buf_start = ctx.sample_position;
    uint64_t buf_end = buf_start + ctx.buffer_size;
    
    // シーケンサー: 次のステップがこのバッファ内にあるか
    while (next_step_sample_ < buf_end) {
        uint32_t offset = next_step_sample_ - buf_start;
        trigger_note_at(offset);  // バッファ内オフセットで処理
        next_step_sample_ += step_interval_samples_;
    }
}
```

**時間→サンプル数変換**（prepare時に一度だけ計算）:

```cpp
void MyProcessor::prepare(const StreamConfig& config) {
    sample_rate_ = config.sample_rate;
    
    // ディレイタイム: 500ms → サンプル数
    delay_samples_ = umi::time::ms_to_samples(500, sample_rate_);
    
    // BPM 120の16分音符間隔
    float step_sec = 60.0f / 120.0f / 4.0f;
    step_interval_samples_ = static_cast<uint32_t>(step_sec * sample_rate_);
    
    // アタックタイム: 10ms
    attack_samples_ = umi::time::ms_to_samples(10, sample_rate_);
}
```

#### 2.7.4 システム時間

`std::chrono::steady_clock` を使用する。ポータブルで単調増加が保証される。

```cpp
#include <chrono>

void MyProcessor::control(ControlContext& ctx) {
    using namespace std::chrono;
    
    auto now = steady_clock::now();
    
    // 30秒ごとに状態保存
    if (now - last_save_time_ > seconds(30)) {
        save_state();
        last_save_time_ = now;
    }
    
    // タイムアウト付き待機（Control thread内のみ）
    auto deadline = now + milliseconds(100);
    while (!condition_met()) {
        if (steady_clock::now() > deadline) {
            umi::log::warn("Operation timed out");
            break;
        }
        // yield or sleep
    }
}
```

#### 2.7.5 時間ユーティリティ

```cpp
// umi/time.hpp

namespace umi::time {

// サンプル数 ↔ 時間変換
constexpr uint64_t ms_to_samples(uint32_t ms, uint32_t sample_rate) {
    return static_cast<uint64_t>(ms) * sample_rate / 1000;
}

constexpr uint64_t us_to_samples(uint64_t us, uint32_t sample_rate) {
    return us * sample_rate / 1'000'000;
}

constexpr uint64_t samples_to_us(uint64_t samples, uint32_t sample_rate) {
    return samples * 1'000'000 / sample_rate;
}

constexpr uint32_t samples_to_ms(uint64_t samples, uint32_t sample_rate) {
    return static_cast<uint32_t>(samples * 1000 / sample_rate);
}

// BPM関連
constexpr uint32_t bpm_to_samples_per_beat(float bpm, uint32_t sample_rate) {
    return static_cast<uint32_t>(60.0f / bpm * sample_rate);
}

constexpr uint32_t bpm_to_samples_per_16th(float bpm, uint32_t sample_rate) {
    return bpm_to_samples_per_beat(bpm, sample_rate) / 4;
}

} // namespace umi::time
```

#### 2.7.6 PAL層での実装

組み込み環境では標準ライブラリが内部で呼び出す低レベル関数を実装する。

```cpp
// kernel/pal/cortex_m/time.cpp

#include <sys/time.h>
#include <time.h>

namespace {
    volatile uint64_t g_system_ticks_us = 0;  // マイクロ秒カウント
}

// SysTick割り込み（1ms周期を推奨）
extern "C" void SysTick_Handler() {
    g_system_ticks_us += 1000;  // 1ms = 1000us
}

// newlib: clock_gettime を実装
// std::chrono::steady_clock が内部でこれを呼ぶ
extern "C" int clock_gettime(clockid_t clock_id, struct timespec* tp) {
    uint64_t us = g_system_ticks_us;
    
    // 高精度が必要な場合: DWTサイクルカウンタで補完
    // uint32_t cycles = DWT->CYCCNT;
    // us += cycles / (SystemCoreClock / 1'000'000);
    
    tp->tv_sec = us / 1'000'000;
    tp->tv_nsec = (us % 1'000'000) * 1000;
    
    // CLOCK_MONOTONIC, CLOCK_REALTIME 両方に対応
    (void)clock_id;
    return 0;
}

// 古いnewlibの場合: _gettimeofday を実装
extern "C" int _gettimeofday(struct timeval* tv, void* tz) {
    (void)tz;
    uint64_t us = g_system_ticks_us;
    tv->tv_sec = us / 1'000'000;
    tv->tv_usec = us % 1'000'000;
    return 0;
}
```

アプリケーションは通常通り `std::chrono::steady_clock::now()` を使用できる。標準ライブラリが内部で上記の関数を呼び出す。

```cpp
// アプリケーションコード（変更不要）
#include <chrono>

void example() {
    auto start = std::chrono::steady_clock::now();
    // ... 処理 ...
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
}
```

#### 2.7.7 RTC（リアルタイムクロック）

RTCはオプション機能。ログへの実時刻付与やファイルタイムスタンプに使用。

```cpp
// umi/time.hpp

namespace umi::time {

struct DateTime {
    int16_t year;       // 2000-2099
    uint8_t month;      // 1-12
    uint8_t day;        // 1-31
    uint8_t hour;       // 0-23
    uint8_t minute;     // 0-59
    uint8_t second;     // 0-59
};

// RTCがあれば現在時刻を返す、なければnullopt
std::optional<DateTime> get_realtime();

// 起動時刻 + 経過時間から計算（RTC読み取りは起動時のみ）
std::optional<DateTime> get_datetime_now();

} // namespace umi::time
```

RTCがない場合でも、ISP経由でホストから時刻を設定可能:

```cpp
// ISP Time Setコマンド受信時
void on_isp_time_set(const DateTime& dt) {
    g_boot_datetime = dt;
    g_boot_steady_time = std::chrono::steady_clock::now();
    g_rtc_valid = true;
}

std::optional<DateTime> get_datetime_now() {
    if (!g_rtc_valid) return std::nullopt;
    
    auto elapsed = std::chrono::steady_clock::now() - g_boot_steady_time;
    auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(elapsed);
    
    return g_boot_datetime + elapsed_sec.count();
}
```

#### 2.7.8 禁止事項

| 場所 | 禁止事項 | 理由 |
|------|----------|------|
| process() | `std::chrono` で時間計測 | システム時間はサンプル時間と非同期 |
| process() | `std::this_thread::sleep_for` | ブロッキング禁止 |
| DSP部品 | いかなる時間API | C++標準依存のみ |

```cpp
// NG: process()内でchrono使用
void process(AudioContext& ctx) {
    auto start = std::chrono::steady_clock::now();  // NG
    // ...処理...
    auto elapsed = std::chrono::steady_clock::now() - start;  // NG
}

// OK: サンプル数ベースで時間管理
void process(AudioContext& ctx) {
    // エンベロープの経過: サンプル数でカウント
    if (env_phase_ < attack_samples_) {
        env_value_ = float(env_phase_) / attack_samples_;
        env_phase_++;
    }
}
```

#### 2.7.9 実装例: BPM同期シーケンサー

```cpp
class Sequencer : public umi::Processor {
    // 設定
    float bpm_ = 120.0f;
    uint32_t steps_ = 16;
    
    // サンプル時間ベースの状態
    uint64_t next_step_sample_ = 0;
    uint32_t step_interval_samples_ = 0;
    uint32_t current_step_ = 0;
    
    // パターンデータ
    std::array<uint8_t, 16> pattern_ = {60, 0, 62, 0, 64, 0, 65, 0,
                                         67, 0, 69, 0, 71, 0, 72, 0};
    
public:
    void prepare(const umi::StreamConfig& config) override {
        step_interval_samples_ = umi::time::bpm_to_samples_per_16th(bpm_, config.sample_rate);
        next_step_sample_ = 0;
        current_step_ = 0;
    }
    
    void process(umi::AudioContext& ctx) override {
        uint64_t buf_end = ctx.sample_position + ctx.buffer_size;
        
        while (next_step_sample_ < buf_end) {
            // バッファ内オフセット計算
            uint32_t offset = 0;
            if (next_step_sample_ >= ctx.sample_position) {
                offset = next_step_sample_ - ctx.sample_position;
            }
            
            // ノート発音
            uint8_t note = pattern_[current_step_];
            if (note > 0) {
                ctx.events.push_midi(0, offset, 0x90, note, 100);
                
                // ノートオフを次のステップ直前にスケジュール
                uint32_t off_offset = offset + step_interval_samples_ - 10;
                if (off_offset < ctx.buffer_size) {
                    ctx.events.push_midi(0, off_offset, 0x80, note, 0);
                }
            }
            
            // 次のステップへ
            current_step_ = (current_step_ + 1) % steps_;
            next_step_sample_ += step_interval_samples_;
        }
    }
    
    void control(umi::ControlContext& ctx) override {
        // BPM変更時に間隔を再計算
        if (ctx.params.has_changed(PARAM_BPM)) {
            bpm_ = ctx.params.get(PARAM_BPM);
            step_interval_samples_ = umi::time::bpm_to_samples_per_16th(
                bpm_, sample_rate_);
        }
    }
};
```

---

## 3. システムアーキテクチャ

### 3.1 全体構造

```
┌─────────────────────────────────────────────────────────────┐
│                     Application                             │
│              (Processor実装、プラットフォーム非依存)           │
├─────────────────────────────────────────────────────────────┤
│                    Processor API                            │
│         (process(), params(), ports() 等)                   │
├──────────────┬──────────────┬──────────────┬───────────────┤
│   Adapter    │   Adapter    │   Adapter    │    Adapter    │
│   Embedded   │   VST3/AU    │    CLAP      │  WASM/WebAudio│
├──────────────┼──────────────┼──────────────┼───────────────┤
│  UMI Kernel  │   DAW Host   │   DAW Host   │    Browser    │
│   + Driver   │              │              │  AudioWorklet │
├──────────────┴──────────────┴──────────────┴───────────────┤
│                Platform Abstraction Layer                   │
├─────────────────────────────────────────────────────────────┤
│  UMI-RTOS │ FreeRTOS │ Zephyr │ POSIX │ Browser Runtime    │
│ (Cortex-M)│ (ESP-IDF)│        │(Linux)│     (WASM)         │
└─────────────────────────────────────────────────────────────┘
```

### 3.2 バイナリ構成

| 構成 | 用途 | 説明 |
|------|------|------|
| 分離型 | 製品向け | Kernelとアプリを別バイナリとして分離 |
| 統合型 | 開発/小型機 | 単一イメージにKernelとアプリを統合 |

---

## 4. 保護モデル

### 4.1 保護レベル

| レベル | ハードウェア | 保護方式 | 対象 |
|--------|--------------|----------|------|
| L3 | MMUあり | 完全メモリ分離 | Cortex-A |
| L2 | MPUあり | 軽量メモリ分離 | Cortex-M3/M4/M7 |
| L1 | 保護機構なし | 論理分離のみ | ESP32, RP2040, Cortex-M0 |

### 4.2 L3: 完全分離モード

- プロセスごとに独立したアドレス空間
- カーネル/ユーザモード分離
- システムコールによるカーネルアクセス

### 4.3 L2: 軽量分離モード

- MPUによるメモリ領域保護
- スタックオーバーフロー検出
- 周辺機器アクセス制限

### 4.4 L1: 論理分離モード

- ソフトウェアによる境界チェック
- 協調的な分離（悪意あるコードには無力）
- 主にバグからの回復を目的とする

### 4.5 デバイスアクセス方式

#### カーネルドライバ方式

製品向け。ハードウェア制御はカーネル内に隠蔽される。

```
Application → syscall → Kernel Driver → Hardware
```

#### ユーザドライバ方式

開発ボード向け。ユーザがドライバを実装可能。

```
Application → IPC → User Driver Server → (MPU制限付き) → Hardware
```

両方式は同一の `device_handle` とAPIで統一される。

### 4.6 権限制御

- すべてのリソースアクセスはケーパビリティハンドルで管理
- 権限はマニフェストで宣言
- 製品モード: 署名必須
- 開発モード: 制限付きで開放

### 4.7 危険操作の扱い

以下の操作は常にカーネル専用とする。

- フラッシュメモリ書き込み
- DMAコントローラ設定
- クロック/電源管理
- 割り込みコントローラ直接操作

---

## 5. 実行モデル

### 5.1 ドメイン分離

```
┌─────────────────────────────────────────────────────────────┐
│                    Audio Domain                             │
│         サンプル精度が要求される / process()内で処理          │
│                                                             │
│  - Audio Stream                                             │
│  - MIDI Events (サンプル位置補正済み)                        │
│  - High-rate CV                                             │
│  - Sample-accurate Parameter Changes                        │
├─────────────────────────────────────────────────────────────┤
│                   Control Domain                            │
│         バッファ単位以下の精度で十分 / 非同期で処理可          │
│                                                             │
│  - UI Events                                                │
│  - Low-rate Sensor                                          │
│  - File I/O 完了通知                                        │
│  - Network Messages                                         │
└─────────────────────────────────────────────────────────────┘
```

### 5.2 カーネルタスク構成

カーネルは以下の固定タスクを生成する。

| タスク | 優先度 | 役割 |
|--------|--------|------|
| Audio Runner | 最高 | アプリのprocess()を周期実行 |
| Control Runner | 中 | アプリのcontrol()を実行 |
| Driver Server | 中 | ドライバ要求を処理 |
| System Monitor | 最低 | watchdog、異常検知 |

### 5.3 アプリケーションタスクモデル

アプリケーションから見えるタスクは2つに限定する。

```cpp
class Processor {
    // Audio Thread: サンプル精度処理
    virtual void process(AudioContext& ctx) = 0;
    
    // Control Thread: 非同期処理 (オプション)
    virtual void control(ControlContext& ctx) {}
};
```

### 5.4 Audio Runnerの駆動

Audio Runnerは I2S等のオーディオDMA完了割り込みから通知を受けて駆動される。

```
DMA Complete IRQ → Kernel → Audio Runner → Processor::process()
```

---

## 6. I/Oモデル

### 6.1 本質的分類

すべてのI/Oは以下の2種類に分類される。

| 種類 | 特性 | 例 |
|------|------|-----|
| Continuous | 固定サンプルレート/バッファサイズで周期実行 | Audio, CV |
| Event | 発生タイミング不定、データサイズ可変 | MIDI, OSC, パラメータ変更 |

### 6.2 正規化規約

アプリケーションが受け取るデータは以下の形式に正規化される。

| データ種別 | 形式 | 範囲 |
|------------|------|------|
| Audio | float, non-interleaved | -1.0 〜 +1.0 |
| CV (unipolar) | float | 0.0 〜 1.0 |
| CV (bipolar) | float | -1.0 〜 +1.0 |
| Gate | float | 0.0 (off) 〜 1.0 (on) |
| MIDI | MidiEvent構造体 | - |
| Parameter | float | ParamDescriptorで定義 |

### 6.3 ポート宣言

```cpp
struct PortDescriptor {
    uint32_t id;
    std::string_view name;
    PortKind kind;           // Continuous または Event
    PortDirection dir;       // In または Out
    
    // Continuousの場合
    uint32_t channels;       // 1=mono, 2=stereo, ...
    
    // Eventの場合
    TypeHint expected_type;  // MidiBytes, Osc, ParamChange, etc.
};

enum class PortKind { Continuous, Event };
enum class PortDirection { In, Out };

// イベントタイプヒント（パース方法の参考、強制ではない）
enum class TypeHint : uint16_t {
    Unknown     = 0x0000,   // 不明/汎用
    
    // MIDI関連
    MidiBytes   = 0x0100,   // 生MIDI (1-3 bytes)
    MidiUmp     = 0x0101,   // MIDI 2.0 UMP
    MidiSysex   = 0x0102,   // SysEx
    
    // パラメータ関連
    ParamChange = 0x0200,   // {id: u32, value: f32}
    ParamGesture= 0x0201,   // ジェスチャー開始/終了
    
    // ネットワーク/シリアル
    Osc         = 0x0300,   // OSC message
    Serial      = 0x0301,   // 生シリアルデータ
    
    // システム
    Clock       = 0x0400,   // テンポ/クロック同期
    Transport   = 0x0401,   // 再生/停止/位置
    
    // ユーザ定義 (0x8000以降)
    UserDefined = 0x8000,
};
```

---

## 7. Processor API

### 7.1 基本インターフェース

```cpp
class Processor {
public:
    // === セットアップ ===
    virtual void prepare(const StreamConfig& config) {}
    
    // === オーディオ処理（必須） ===
    virtual void process(AudioContext& ctx) = 0;
    
    // === コントロール処理（オプション） ===
    virtual void control(ControlContext& ctx) {}
    
    // === 宣言 ===
    virtual std::span<const ParamDescriptor> params() { return {}; }
    virtual std::span<const PortDescriptor> ports() { return {}; }
    
    // === 状態管理 ===
    virtual std::vector<uint8_t> save_app_state() { return {}; }
    virtual void load_app_state(std::span<const uint8_t>) {}
    
    // === リソース要件 ===
    static constexpr Requirements requirements();
};
```

### 7.2 StreamConfig

```cpp
struct StreamConfig {
    uint32_t sample_rate;
    uint32_t buffer_size;
};
```

### 7.3 AudioContext

```cpp
struct AudioContext {
    // オーディオバッファ
    std::span<const sample_t* const> inputs;   // 入力チャンネル配列
    std::span<sample_t* const> outputs;        // 出力チャンネル配列
    
    // Sample-Accurate Events
    EventQueue& events;
    
    // Timing
    uint32_t sample_rate;
    uint32_t buffer_size;
    uint64_t sample_position;  // ストリーム開始からの累積サンプル数
};

// 使用例:
// ctx.inputs[0]  - 入力チャンネル0のバッファ
// ctx.inputs[1]  - 入力チャンネル1のバッファ
// ctx.outputs[0] - 出力チャンネル0のバッファ
// ctx.outputs[0][i] - 出力チャンネル0のi番目のサンプル
```

### 7.4 ControlContext

```cpp
struct ControlContext {
    EventQueue& events;
    ParamState& params;
};
```

### 7.5 ParamState

```cpp
class ParamState {
public:
    // 現在値の取得
    float get(uint32_t id) const;
    
    // 値の設定（Audio threadへ通知される）
    void set(uint32_t id, float value);
    
    // 範囲制限付き設定
    void set_normalized(uint32_t id, float normalized_0_1);
    
    // 変更検出（前回のclear_changes()以降）
    bool has_changed(uint32_t id) const;
    bool any_changed() const;
    
    // 変更フラグをクリア
    void clear_changes();
    
    // パラメータ情報
    const ParamDescriptor* descriptor(uint32_t id) const;
    uint32_t count() const;
    
    // イテレーション
    struct Iterator;
    Iterator begin() const;
    Iterator end() const;
};
```

### 7.5 EventQueue

```cpp
class EventQueue {
public:
    // 読み取り
    bool pop(Event& out);
    bool pop_until(uint32_t sample_pos, Event& out);
    bool pop_from(uint32_t port_id, Event& out);
    bool pop_from_until(uint32_t port_id, uint32_t sample_pos, Event& out);
    
    // 書き込み
    bool push(const Event& e);
    bool push_midi(uint32_t port, uint32_t sample_pos, 
                   uint8_t status, uint8_t d1, uint8_t d2 = 0);
    bool push_param(uint32_t param_id, uint32_t sample_pos, float value);
};
```

### 7.6 Event

```cpp
struct Event {
    uint32_t port_id;
    uint32_t sample_pos;
    EventType type;
    
    union {
        MidiData midi;
        ParamData param;
        RawData raw;
    };
};

enum class EventType : uint8_t {
    Midi,
    Param,
    Raw,
};

struct MidiData {
    uint8_t bytes[3];
    uint8_t size;
};

struct ParamData {
    uint32_t id;
    float value;
};

struct RawData {
    uint16_t size;
    uint8_t data[14];
};
```

### 7.7 ParamDescriptor

```cpp
struct ParamDescriptor {
    uint32_t id;
    std::string_view name;
    float min;
    float max;
    float default_value;
    Unit unit;                // Hz, dB, ms, Percent, etc.
    Scale scale;              // Linear, Log, Exp
    
    // UIヒント（オプション）
    enum class Widget { Knob, Slider, Toggle, Combo, XYPad };
    Widget widget_hint = Widget::Knob;
    std::string_view group;
    
    // 離散値の場合
    std::span<const std::string_view> value_labels;
};

enum class Unit { None, Hz, dB, Ms, Percent, Semitones, Octaves };
enum class Scale { Linear, Log, Exp };
```

### 7.8 Requirements

```cpp
struct Requirements {
    uint32_t min_sample_rate = 44100;
    uint32_t max_sample_rate = 96000;
    uint32_t min_buffer_size = 16;
    uint32_t max_buffer_size = 1024;
    
    uint32_t stack_bytes = 4096;
    uint32_t heap_bytes = 0;
    uint32_t scratch_bytes = 0;
    
    uint32_t latency_samples = 0;    // 処理レイテンシ（ルックアヘッド等）
    
    bool needs_double = false;
    bool needs_filesystem = false;
    bool needs_network = false;
};
```

### 7.9 I/Oモデル

#### 7.9.1 仮想I/Oと物理I/O

アプリケーションは「仮想I/O」を通じてオーディオを処理する。物理ハードウェアとのマッピングはKernel/Adapterが担当する。

```
物理I/O (Driver層)              仮想I/O (App層)
┌─────────────────────┐        ┌─────────────────────┐
│ hw:0 DAC L          │◄───┬───│ virt:0 Out L        │
│ hw:1 DAC R          │◄───┘   │ virt:1 Out R        │
│ hw:2 Headphone      │◄──mix──│ virt:0 + virt:1     │ (モノミックス)
└─────────────────────┘        └─────────────────────┘

┌─────────────────────┐        ┌─────────────────────┐
│ hw:0 ADC L          │────┬──▶│ virt:0 In L         │
│ hw:1 ADC R          │────┘   │ virt:1 In R         │
│ hw:2 Line In (mono) │──dup──▶│ virt:0, virt:1      │ (モノ→ステレオ)
└─────────────────────┘        └─────────────────────┘
```

#### 7.9.2 標準I/O構成

すべてのアプリケーションは以下の標準構成を前提として実装する。

| ID | 名前 | 方向 | 説明 |
|----|------|------|------|
| 0 | In L / Out L | In/Out | 左チャンネル |
| 1 | In R / Out R | In/Out | 右チャンネル |

```cpp
// 標準的なProcessorの実装
void MyProcessor::process(AudioContext& ctx) {
    // 常にステレオで実装
    // ctx.inputs[0] = L, ctx.inputs[1] = R
    // ctx.outputs[0] = L, ctx.outputs[1] = R
    
    for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
        sample_t in_l = ctx.inputs[0][i];
        sample_t in_r = ctx.inputs[1][i];
        
        // 処理...
        
        ctx.outputs[0][i] = out_l;
        ctx.outputs[1][i] = out_r;
    }
}
```

**ハードウェアがモノラルの場合**: Kernelが自動的にステレオ→モノ変換を行う。アプリケーションの変更は不要。

#### 7.9.3 拡張I/O構成

追加のI/Oが必要な場合、`ports()` で宣言する。

```cpp
std::span<const PortDescriptor> MyProcessor::ports() {
    static constexpr PortDescriptor p[] = {
        // 標準ステレオ出力
        {0, "Out L", PortKind::Audio, PortDirection::Out},
        {1, "Out R", PortKind::Audio, PortDirection::Out},
        
        // 追加: サイドチェイン入力
        {2, "Sidechain L", PortKind::Audio, PortDirection::In},
        {3, "Sidechain R", PortKind::Audio, PortDirection::In},
        
        // 追加: 個別出力
        {4, "Kick", PortKind::Audio, PortDirection::Out},
        {5, "Snare", PortKind::Audio, PortDirection::Out},
        
        // MIDI
        {100, "MIDI In", PortKind::Event, PortDirection::In},
    };
    return p;
}
```

#### 7.9.4 I/Oマッピング設定

物理I/Oと仮想I/Oのマッピングはビルド時設定またはISP経由で設定可能。

```cpp
// kernel/config/io_mapping.hpp

struct IoMapping {
    uint32_t virtual_id;       // アプリ側ID
    uint32_t physical_id;      // ハード側ID (UINT32_MAX = 未接続)
    MixMode mode;              // マッピングモード
    float gain;                // ゲイン補正 (1.0 = unity)
};

enum class MixMode : uint8_t {
    Direct,      // 1:1 直接マッピング
    Sum,         // N:1 加算ミックス
    Average,     // N:1 平均ミックス
    Duplicate,   // 1:N 複製
    LeftOnly,    // Lチャンネルのみ使用
    RightOnly,   // Rチャンネルのみ使用
};
```

#### 7.9.5 I/O構成プリセット

よく使われる構成をプリセットとして提供する。

```cpp
// kernel/config/io_presets.hpp

namespace umi::io_preset {

// ステレオハード + ステレオアプリ（デフォルト）
constexpr IoConfig STEREO_STEREO = {
    .input_map = {
        {0, 0, MixMode::Direct, 1.0f},   // hw:0 → virt:0 (L)
        {1, 1, MixMode::Direct, 1.0f},   // hw:1 → virt:1 (R)
    },
    .output_map = {
        {0, 0, MixMode::Direct, 1.0f},   // virt:0 (L) → hw:0
        {1, 1, MixMode::Direct, 1.0f},   // virt:1 (R) → hw:1
    },
};

// モノラルハード + ステレオアプリ（平均ミックス）
constexpr IoConfig MONO_OUT_AVERAGE = {
    .input_map = {
        {0, 0, MixMode::Duplicate, 1.0f},  // hw:0 → virt:0, virt:1
        {1, 0, MixMode::Duplicate, 1.0f},
    },
    .output_map = {
        {0, 0, MixMode::Average, 0.5f},    // virt:0 + virt:1 → hw:0 (平均)
        {1, 0, MixMode::Average, 0.5f},
    },
};

// モノラルハード + ステレオアプリ（Lのみ使用）
constexpr IoConfig MONO_OUT_LEFT_ONLY = {
    .input_map = {
        {0, 0, MixMode::Duplicate, 1.0f},
        {1, 0, MixMode::Duplicate, 1.0f},
    },
    .output_map = {
        {0, 0, MixMode::Direct, 1.0f},     // virt:0 (L) → hw:0
        {1, UINT32_MAX, MixMode::Direct, 0.0f},  // virt:1 (R) → 破棄
    },
};

// モノラルハード + ステレオアプリ（加算ミックス、クリップ注意）
constexpr IoConfig MONO_OUT_SUM = {
    .input_map = {
        {0, 0, MixMode::Duplicate, 1.0f},
        {1, 0, MixMode::Duplicate, 1.0f},
    },
    .output_map = {
        {0, 0, MixMode::Sum, 1.0f},        // virt:0 + virt:1 → hw:0 (加算)
        {1, 0, MixMode::Sum, 1.0f},
    },
};

// ヘッドフォン分岐（メイン出力をヘッドフォンにも複製）
constexpr IoConfig STEREO_WITH_HEADPHONE = {
    .output_map = {
        {0, 0, MixMode::Direct, 1.0f},     // virt:0 → hw:0 (Main L)
        {1, 1, MixMode::Direct, 1.0f},     // virt:1 → hw:1 (Main R)
        {0, 2, MixMode::Direct, 1.0f},     // virt:0 → hw:2 (HP L)
        {1, 3, MixMode::Direct, 1.0f},     // virt:1 → hw:3 (HP R)
    },
};

} // namespace umi::io_preset
```

#### 7.9.6 ビルド時設定

```cpp
// kernel/config/board_config.hpp

// ボード固有のI/O設定
namespace board {

// 物理I/O数
constexpr uint32_t NUM_PHYSICAL_INPUTS = 2;
constexpr uint32_t NUM_PHYSICAL_OUTPUTS = 1;  // モノラルハード

// 使用するI/Oプリセット
constexpr auto IO_CONFIG = umi::io_preset::MONO_OUT_AVERAGE;

// または完全カスタム
constexpr IoConfig CUSTOM_IO_CONFIG = {
    .input_map = { /* ... */ },
    .output_map = { /* ... */ },
};

} // namespace board
```

#### 7.9.7 ISP経由での動的設定

```cpp
// ISP IoConfig コマンド
struct IspIoConfigPayload {
    uint8_t config_id;         // 0=custom, 1-N=preset
    uint8_t input_count;
    uint8_t output_count;
    IoMapping mappings[];      // input_count + output_count 個
};

// ISP コマンド
// IoConfigSet (0x30): I/O設定を変更
// IoConfigGet (0x31): 現在のI/O設定を取得
```

#### 7.9.8 変換動作の詳細

| MixMode | 入力時動作 | 出力時動作 |
|---------|------------|------------|
| Direct | hw[n] → virt[n] | virt[n] → hw[n] |
| Sum | - | virt[n] を hw[m] に加算 |
| Average | - | virt[n] × gain を hw[m] に加算 |
| Duplicate | hw[n] → virt[m], virt[k] | - |
| LeftOnly | hw[0] → virt[0], virt[1] | virt[0] → hw[0] |
| RightOnly | hw[1] → virt[0], virt[1] | virt[1] → hw[0] |

**注意事項**:
- `Sum` はクリッピングの可能性があるため、事前にゲイン調整を推奨
- `Average` は自動的に 0.5 ゲインを適用（2ch → 1ch の場合）
- 未接続入力（physical_id = UINT32_MAX）はゼロバッファを提供
- 未接続出力は書き込みを破棄

```cpp
// Kernel内部の変換処理
void AudioRunner::convert_outputs() {
    // 物理出力バッファをゼロクリア
    for (auto& buf : physical_out_) {
        std::fill(buf.begin(), buf.end(), 0);
    }
    
    // マッピングに従って変換
    for (auto& map : output_mappings_) {
        if (map.physical_id == UINT32_MAX) continue;  // 未接続
        
        switch (map.mode) {
        case MixMode::Direct:
        case MixMode::LeftOnly:
        case MixMode::RightOnly:
            copy(virtual_out_[map.virtual_id], physical_out_[map.physical_id]);
            break;
        case MixMode::Sum:
            add(virtual_out_[map.virtual_id], physical_out_[map.physical_id]);
            break;
        case MixMode::Average:
            add_scaled(virtual_out_[map.virtual_id], 
                       physical_out_[map.physical_id], map.gain);
            break;
        }
    }
}

---

## 8. カーネルアーキテクチャ

### 8.1 構造

```
┌─────────────────────────────────────────────────────────────┐
│                       Kernel                                │
├─────────────────────────────────────────────────────────────┤
│  ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌──────────┐ │
│  │   Audio    │ │  Control   │ │   Driver   │ │  System  │ │
│  │   Runner   │ │   Runner   │ │   Server   │ │  Monitor │ │
│  └─────┬──────┘ └─────┬──────┘ └─────┬──────┘ └────┬─────┘ │
│        │              │              │              │       │
│  ┌─────┴──────────────┴──────────────┴──────────────┴─────┐ │
│  │                    Kernel Services                     │ │
│  │  - Event Routing                                       │ │
│  │  - Buffer Management                                   │ │
│  │  - Capability Management                               │ │
│  │  - ISP Handler                                         │ │
│  └────────────────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────────────────┤
│                          Drivers                            │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐       │
│  │  Audio   │ │   MIDI   │ │    CV    │ │  System  │       │
│  │  Driver  │ │  Driver  │ │  Driver  │ │  Driver  │       │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘       │
├─────────────────────────────────────────────────────────────┤
│              Platform Abstraction Layer (PAL)               │
└─────────────────────────────────────────────────────────────┘
```

### 8.2 Audio Runner

#### 8.2.1 バッファリング構成

DMAトリプルバッファリングにより、process()の遅延がDMAを止めないようにする。

```
┌──────────────────────────────────────────────────────────────┐
│ DMA Triple Buffering                                         │
│                                                              │
│   Buffer A ←── DMA書込中（ハードウェア）                      │
│   Buffer B ←── process()処理中                               │
│   Buffer C ←── DMA読出中（出力、前回の結果）                  │
│                                                              │
│ ※ process()が遅れてもDMAは止まらない                         │
│ ※ 遅れた場合はBuffer Cを再利用（前回値保持 or 無音）          │
└──────────────────────────────────────────────────────────────┘
```

#### 8.2.2 基本実装

```cpp
class AudioRunner {
    AudioDriver* audio_drv_;
    Processor* app_;
    
    sample_t* audio_in_[MAX_CHANNELS];
    sample_t* audio_out_[MAX_CHANNELS];
    EventQueue event_queue_;
    
    // タイミング管理
    uint64_t current_sample_pos_ = 0;
    uint32_t buffer_size_;
    uint32_t sample_rate_;
    
    // 統計
    uint32_t drop_count_ = 0;
    uint32_t total_cycles_ = 0;
    
public:
    void run() {
        while (running_) {
            // DMA完了を待機
            audio_drv_->wait_buffer_complete();
            
            // Watchdogキック（1命令）
            kick_watchdog();
            
            // DMAが示す現在位置を取得
            uint64_t dma_pos = audio_drv_->get_sample_position();
            
            // ドロップ検出と同期維持
            if (dma_pos > current_sample_pos_ + buffer_size_) {
                // 1バッファ以上遅れた → ドロップ
                handle_drop(dma_pos);
            } else {
                // 通常処理
                run_cycle();
                current_sample_pos_ += buffer_size_;
            }
            
            total_cycles_++;
        }
    }
    
private:
    void run_cycle() {
        // 1. ドライバから正規化データを取得
        audio_drv_->read_input({audio_in_, channel_count_});
        
        // 2. イベントを収集
        collect_events();
        
        // 3. AudioContextを構築
        AudioContext ctx = build_context();
        ctx.sample_position = current_sample_pos_;
        
        // 4. アプリケーション呼び出し
        app_->process(ctx);
        
        // 5. 出力をドライバへ
        audio_drv_->write_output({audio_out_, channel_count_});
        
        // 6. 出力イベントを送信
        dispatch_output_events();
        
        // 7. キューをクリア
        event_queue_.clear();
    }
    
    void handle_drop(uint64_t dma_pos) {
        // 遅れたバッファ数を計算
        uint32_t dropped = (dma_pos - current_sample_pos_) / buffer_size_;
        drop_count_ += dropped;
        
        // sample_positionを同期（遅れを受け入れる）
        current_sample_pos_ = dma_pos;
        
        // 出力: 前バッファ保持（DMAが自動的に再利用）
        // または明示的に無音を書き込む
        // audio_drv_->write_silence();
        
        // イベントキューは時間が飛ぶのでクリア
        event_queue_.clear();
        
        // Control threadへ通知（ロックフリー）
        drop_notify_.store(true, std::memory_order_release);
    }
};
```

#### 8.2.3 同期維持の原則

```
【最重要】タイミング同期 > 音質

ドロップ発生時:
  ✗ 遅れを取り戻そうとしない（CPU負荷増大、連鎖的ドロップ）
  ✓ 遅れを受け入れ、現在位置に同期

      時間軸 ──────────────────────────────────────────▶
      
      正常:  [Buf0][Buf1][Buf2][Buf3][Buf4]...
                                 ↑ 現在
      
      ドロップ: [Buf0][Buf1][  遅延  ][Buf4]...
                              ↑ Buf2,3はスキップ
                              sample_posはBuf4の位置に飛ぶ
```

#### 8.2.4 Watchdog構成

```cpp
// Hardware Watchdog（最終防衛線）
// Audio Runner内で毎サイクルキック
// 典型的なタイムアウト: バッファサイズの10倍程度

inline void kick_watchdog() {
    // 単一のレジスタ書き込み（最小オーバーヘッド）
    IWDG->KR = 0xAAAA;  // STM32の例
}

// タイムアウト時: ハードウェアリセット
// → 起動時にリセット原因を確認し、ISP経由で報告
```

### 8.3 外部同期

#### 8.3.1 同期ソース

```cpp
enum class SyncSource : uint8_t {
    Internal,       // 内部クロック（デフォルト）
    WordClock,      // Word Clock入力
    MidiClock,      // MIDI Clock
    Spdif,          // S/PDIF embedded clock
    Adat,           // ADAT embedded clock
    Usb,            // USB SOF (Start of Frame)
};
```

#### 8.3.2 同期アーキテクチャ

```
┌─────────────────────────────────────────────────────────────┐
│ 外部クロック入力                                             │
│ (Word Clock / MIDI Clock / etc.)                            │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│ Clock ISR                                                   │
│  - external_sample_pos を更新                               │
│  - 外部クロックが「正」の時間基準                             │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│ Audio Runner                                                │
│  - DMA完了時に external_sample_pos と比較                    │
│  - 差分に応じて同期調整                                      │
└─────────────────────────────────────────────────────────────┘
```

#### 8.3.3 同期調整ロジック

```cpp
class SyncManager {
    SyncSource source_ = SyncSource::Internal;
    std::atomic<uint64_t> external_pos_{0};  // ISRから更新
    
    // 許容ドリフト（サンプル数）
    static constexpr int32_t DRIFT_TOLERANCE = 4;
    
public:
    // Clock ISRから呼ばれる
    void on_external_clock() {
        external_pos_.fetch_add(samples_per_clock_, 
                                std::memory_order_release);
    }
    
    // Audio Runnerから呼ばれる
    SyncAction check_sync(uint64_t internal_pos) {
        if (source_ == SyncSource::Internal) {
            return SyncAction::None;
        }
        
        uint64_t ext_pos = external_pos_.load(std::memory_order_acquire);
        int64_t drift = int64_t(internal_pos) - int64_t(ext_pos);
        
        if (drift > DRIFT_TOLERANCE) {
            // 内部が進みすぎ → 同じバッファを再出力（レア）
            return SyncAction::Repeat;
        } else if (drift < -DRIFT_TOLERANCE) {
            // 内部が遅れ → process()スキップで追従
            return SyncAction::Skip;
        }
        
        return SyncAction::None;
    }
};

enum class SyncAction {
    None,    // 通常処理
    Skip,    // process()スキップ、位置を進める
    Repeat,  // 同じバッファを再出力、位置を進めない
};
```

#### 8.3.4 Audio Runnerへの統合

```cpp
void AudioRunner::run() {
    while (running_) {
        audio_drv_->wait_buffer_complete();
        kick_watchdog();
        
        // 外部同期チェック
        SyncAction action = sync_manager_.check_sync(current_sample_pos_);
        
        switch (action) {
        case SyncAction::None:
            // 通常処理
            run_cycle();
            current_sample_pos_ += buffer_size_;
            break;
            
        case SyncAction::Skip:
            // 遅れ → スキップして同期
            drop_count_++;
            current_sample_pos_ += buffer_size_;
            // process()は呼ばない、前バッファ保持
            break;
            
        case SyncAction::Repeat:
            // 進みすぎ → 同じ位置で再処理
            run_cycle();
            // current_sample_pos_ は進めない
            break;
        }
        
        total_cycles_++;
    }
}
```

### 8.4 System Monitor

リアルタイム監視ではなく、統計収集・報告に特化する。

```cpp
class SystemMonitor {
    // Audio Runnerからの統計（アトミック）
    std::atomic<uint32_t> drop_count_{0};
    std::atomic<uint32_t> total_cycles_{0};
    std::atomic<uint32_t> max_process_us_{0};
    
    // Control Runnerからの統計
    uint32_t control_overruns_ = 0;
    
public:
    void run() {
        while (running_) {
            // 低頻度でスリープ（最低優先度）
            pal_task_sleep_us(100000);  // 100ms
            
            // 統計を収集
            Stats stats = collect_stats();
            
            // ISP経由で報告（要求があれば）
            if (isp_stats_requested_) {
                isp_send_stats(stats);
            }
            
            // 異常検知（しきい値ベース）
            if (stats.drop_rate > DROP_RATE_THRESHOLD) {
                isp_send_warning(Warning::HighDropRate);
            }
        }
    }
    
    struct Stats {
        uint32_t drop_count;
        uint32_t total_cycles;
        float drop_rate;          // drops per second
        float cpu_usage;          // 推定CPU使用率
        uint32_t max_process_us;  // 最大処理時間
    };
};
```

### 8.5 スレッド間通信

SPSCロックフリーキューを使用する。

```cpp
template<typename T, size_t N>
class SpscQueue {
    static_assert((N & (N - 1)) == 0, "N must be power of 2");
    
    alignas(64) std::array<T, N> buffer_;
    alignas(64) std::atomic<size_t> read_{0};
    alignas(64) std::atomic<size_t> write_{0};
    
public:
    // 要素を追加（満杯時はfalse）
    bool try_push(const T& item) {
        size_t w = write_.load(std::memory_order_relaxed);
        size_t next_w = (w + 1) & (N - 1);
        if (next_w == read_.load(std::memory_order_acquire)) {
            return false;  // 満杯
        }
        buffer_[w] = item;
        write_.store(next_w, std::memory_order_release);
        return true;
    }
    
    // 要素を取り出し（空時はfalse）
    bool try_pop(T& out) {
        size_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) {
            return false;  // 空
        }
        out = buffer_[r];
        read_.store((r + 1) & (N - 1), std::memory_order_release);
        return true;
    }
    
    // 溢れ時に古いデータを捨てて追加
    void push_overwrite(const T& item) {
        if (!try_push(item)) {
            // 最古のデータを捨てる
            T discard;
            try_pop(discard);
            try_push(item);  // 必ず成功
        }
    }
    
    bool is_empty() const {
        return read_.load(std::memory_order_acquire) == 
               write_.load(std::memory_order_acquire);
    }
    
    bool is_full() const {
        size_t w = write_.load(std::memory_order_acquire);
        size_t next_w = (w + 1) & (N - 1);
        return next_w == read_.load(std::memory_order_acquire);
    }
    
    size_t size() const {
        size_t w = write_.load(std::memory_order_acquire);
        size_t r = read_.load(std::memory_order_acquire);
        return (w - r) & (N - 1);
    }
};
```

用途別キュー構成:

| キュー | 方向 | サイズ | 内容 | 溢れ時 |
|--------|------|--------|------|--------|
| control_to_audio | Control → Audio | 64 | パラメータ変更 | 古いを捨てる |
| audio_to_control | Audio → Control | 16 | メーター、状態 | 古いを捨てる |
| event_in | External → Audio | 128 | MIDI等イベント | 古いを捨てる |
| event_out | Audio → External | 128 | 出力イベント | 古いを捨てる |

溢れ時の方針: 最新のデータを優先し、古いデータを捨てる（`push_overwrite`使用）。

---

## 9. ドライバモデル

### 9.1 ドライバインターフェース

```cpp
class AudioDriver {
public:
    virtual void start(const StreamConfig& config) = 0;
    virtual void stop() = 0;
    virtual void read_input(std::span<float*> channels) = 0;
    virtual void write_output(std::span<const float*> channels) = 0;
};

class MidiDriver {
public:
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool read_event(MidiEvent& out) = 0;
    virtual bool write_event(const MidiEvent& e) = 0;
};

class CvDriver {
public:
    virtual float read_normalized(uint32_t channel) = 0;
    virtual void write_normalized(uint32_t channel, float value) = 0;
};
```

### 9.2 ドライバの責務

| 責務 | 内容 |
|------|------|
| データ変換 | 生データ ↔ 正規化float |
| DMA管理 | バッファ管理、割り込み処理 |
| プロトコル処理 | MIDI パース、SysEx組み立て等 |
| エラー処理 | ハードウェアエラーの検出と報告 |

### 9.3 実装例: I2Sオーディオドライバ

```cpp
class I2sAudioDriver : public AudioDriver {
    int32_t* dma_buffer_in_;
    int32_t* dma_buffer_out_;
    uint32_t buffer_size_;
    
public:
    void read_input(std::span<float*> channels) override {
        const float scale = 1.0f / 8388608.0f;  // 2^23
        
        for (uint32_t i = 0; i < buffer_size_; ++i) {
            channels[0][i] = dma_buffer_in_[i * 2 + 0] * scale;
            channels[1][i] = dma_buffer_in_[i * 2 + 1] * scale;
        }
    }
    
    void write_output(std::span<const float*> channels) override {
        const float scale = 8388607.0f;
        
        for (uint32_t i = 0; i < buffer_size_; ++i) {
            dma_buffer_out_[i * 2 + 0] = int32_t(channels[0][i] * scale);
            dma_buffer_out_[i * 2 + 1] = int32_t(channels[1][i] * scale);
        }
    }
};
```

---

## 10. プラットフォーム抽象化層 (PAL)

### 10.1 PALインターフェース

```cpp
struct PalOps {
    // タスク管理
    void (*task_create)(void (*entry)(void*), void* arg, int priority);
    void (*task_yield)(void);
    void (*task_sleep_us)(uint32_t us);
    
    // 同期
    void (*mutex_create)(pal_mutex_t* m);
    void (*mutex_lock)(pal_mutex_t* m);
    void (*mutex_unlock)(pal_mutex_t* m);
    
    // 時間
    uint64_t (*time_us)(void);
    
    // クリティカルセクション
    void (*critical_enter)(void);
    void (*critical_exit)(void);
};
```

### 10.2 対応プラットフォーム

| プラットフォーム | PAL実装 | 備考 |
|------------------|---------|------|
| Cortex-M (STM32等) | UMI-RTOS | UMI-OS専用軽量RTOS |
| Cortex-M (STM32等) | FreeRTOS | 互換用（オプション） |
| ESP-IDF | FreeRTOS | ESP-IDFと不可分 |
| Zephyr | Zephyr native | 複数ハードウェア対応 |
| Linux | POSIX threads | シミュレータ用 |
| WASM | 単一スレッド | AudioWorklet |

### 10.3 UMI-RTOS

UMI-RTOSはUMI-OS専用に設計された最小限のリアルタイムカーネルである。

#### 10.3.1 設計原則

- **固定タスク数**: 動的タスク生成なし、コンパイル時に確定
- **固定優先度**: 4レベル（Audio, Driver, Control, Background）
- **最小コンテキスト**: オーディオパスでのオーバーヘッド最小化
- **静的メモリ**: 動的確保なし、すべてコンパイル時に確保

#### 10.3.2 タスク構造

```cpp
// kernel/rtos/umi_rtos.hpp

namespace umi::rtos {

// 固定優先度レベル
enum class Priority : uint8_t {
    Audio = 0,      // 最高: オーディオ処理
    Driver = 1,     // 高: ドライバサービス
    Control = 2,    // 中: コントロール処理
    Background = 3, // 低: システム監視
};

// タスク状態
enum class TaskState : uint8_t {
    Ready,
    Running,
    Blocked,
    Suspended,
};

// タスク制御ブロック（最小構成）
struct TaskControl {
    uint32_t* stack_ptr;          // 現在のスタックポインタ
    uint32_t* stack_base;         // スタック底
    uint16_t stack_size;          // スタックサイズ（ワード単位）
    Priority priority;
    TaskState state;
    void (*entry)(void*);         // エントリポイント
    void* arg;                    // 引数
    uint32_t wake_tick;           // スリープ復帰時刻
};

// システム構成（コンパイル時定数）
constexpr uint32_t MAX_TASKS = 4;
constexpr uint32_t TICK_HZ = 1000;

} // namespace umi::rtos
```

#### 10.3.3 スケジューラ実装

```cpp
// kernel/rtos/umi_scheduler.cpp

namespace umi::rtos {

class Scheduler {
    TaskControl tasks_[MAX_TASKS];
    uint8_t task_count_ = 0;
    uint8_t current_task_ = 0;
    volatile uint32_t tick_count_ = 0;
    
    // 優先度別レディキュー（ビットマップ）
    uint8_t ready_mask_[4] = {0, 0, 0, 0};
    
public:
    // タスク登録（初期化時のみ）
    void register_task(Priority prio, void (*entry)(void*), void* arg,
                       uint32_t* stack, uint16_t stack_size) {
        TaskControl& t = tasks_[task_count_];
        t.priority = prio;
        t.entry = entry;
        t.arg = arg;
        t.stack_base = stack;
        t.stack_size = stack_size;
        t.state = TaskState::Ready;
        
        // 初期スタックフレーム構築
        t.stack_ptr = init_stack_frame(stack + stack_size, entry, arg);
        
        ready_mask_[static_cast<uint8_t>(prio)] |= (1 << task_count_);
        task_count_++;
    }
    
    // 最高優先度のレディタスクを選択
    uint8_t select_next() {
        for (uint8_t p = 0; p < 4; ++p) {
            if (ready_mask_[p]) {
                return __builtin_ctz(ready_mask_[p]);  // 最下位ビット
            }
        }
        return current_task_;  // フォールバック
    }
    
    // コンテキストスイッチ（PendSVハンドラから呼ばれる）
    uint32_t* switch_context(uint32_t* current_sp) {
        tasks_[current_task_].stack_ptr = current_sp;
        current_task_ = select_next();
        return tasks_[current_task_].stack_ptr;
    }
    
    // SysTickハンドラ
    void tick() {
        tick_count_++;
        
        // スリープ中タスクの復帰チェック
        for (uint8_t i = 0; i < task_count_; ++i) {
            if (tasks_[i].state == TaskState::Blocked &&
                tick_count_ >= tasks_[i].wake_tick) {
                tasks_[i].state = TaskState::Ready;
                ready_mask_[static_cast<uint8_t>(tasks_[i].priority)] |= (1 << i);
            }
        }
        
        // プリエンプション判定
        if (select_next() != current_task_) {
            trigger_pendsv();
        }
    }
    
    // 現在タスクをスリープ
    void sleep_ticks(uint32_t ticks) {
        tasks_[current_task_].wake_tick = tick_count_ + ticks;
        tasks_[current_task_].state = TaskState::Blocked;
        ready_mask_[static_cast<uint8_t>(tasks_[current_task_].priority)] 
            &= ~(1 << current_task_);
        trigger_pendsv();
    }
    
    // 明示的yield
    void yield() {
        trigger_pendsv();
    }
    
private:
    uint32_t* init_stack_frame(uint32_t* stack_top, void (*entry)(void*), void* arg);
    void trigger_pendsv();
};

// グローバルインスタンス
Scheduler g_scheduler;

} // namespace umi::rtos
```

#### 10.3.4 Cortex-M アセンブリ部（ARMv7-M）

```cpp
// kernel/rtos/umi_rtos_cm4.S

.syntax unified
.cpu cortex-m4
.thumb

.global PendSV_Handler
.global SVC_Handler
.global umi_start_scheduler

// PendSVハンドラ: コンテキストスイッチ
.thumb_func
PendSV_Handler:
    // 現在のコンテキストを保存
    mrs     r0, psp
    stmdb   r0!, {r4-r11}       // R4-R11をプッシュ
    
    // switch_context呼び出し
    bl      umi_rtos_switch_context
    
    // 新しいコンテキストを復元
    ldmia   r0!, {r4-r11}       // R4-R11をポップ
    msr     psp, r0
    
    // スレッドモードへ復帰
    ldr     r0, =0xFFFFFFFD
    bx      r0

// SysTickハンドラ
.thumb_func
SysTick_Handler:
    push    {lr}
    bl      umi_rtos_tick
    pop     {pc}

// スケジューラ開始
.thumb_func
umi_start_scheduler:
    // PSPを最初のタスクのスタックに設定
    msr     psp, r0
    
    // スレッドモードでPSP使用に切り替え
    mrs     r0, control
    orr     r0, #2
    msr     control, r0
    isb
    
    // 最初のタスクへジャンプ
    pop     {r4-r11}
    pop     {r0-r3, r12, lr}
    pop     {pc}
```

#### 10.3.5 同期プリミティブ

```cpp
// kernel/rtos/umi_sync.hpp

namespace umi::rtos {

// バイナリセマフォ（割り込み→タスク通知用）
class BinarySemaphore {
    volatile uint8_t count_ = 0;
    uint8_t waiting_task_ = 0xFF;
    
public:
    // ISRから呼び出し可能
    void give_from_isr() {
        count_ = 1;
        if (waiting_task_ != 0xFF) {
            g_scheduler.wake_task(waiting_task_);
        }
    }
    
    // タスクから呼び出し
    void take() {
        while (count_ == 0) {
            waiting_task_ = g_scheduler.current_task();
            g_scheduler.block_current();
        }
        count_ = 0;
        waiting_task_ = 0xFF;
    }
    
    bool try_take() {
        if (count_) {
            count_ = 0;
            return true;
        }
        return false;
    }
};

// クリティカルセクション（割り込み禁止）
class CriticalSection {
    uint32_t primask_;
public:
    CriticalSection() {
        primask_ = __get_PRIMASK();
        __disable_irq();
    }
    ~CriticalSection() {
        __set_PRIMASK(primask_);
    }
};

// 優先度継承なしの軽量ミューテックス
// Audioタスクでは使用禁止
class Mutex {
    volatile uint8_t owner_ = 0xFF;
    
public:
    void lock() {
        while (!try_lock()) {
            g_scheduler.yield();
        }
    }
    
    bool try_lock() {
        CriticalSection cs;
        if (owner_ == 0xFF) {
            owner_ = g_scheduler.current_task();
            return true;
        }
        return false;
    }
    
    void unlock() {
        owner_ = 0xFF;
    }
};

} // namespace umi::rtos
```

#### 10.3.6 PAL実装（UMI-RTOS用）

```cpp
// kernel/pal/umi-rtos/pal_umi_rtos.cpp

#include "umi_rtos.hpp"

using namespace umi::rtos;

static void pal_task_create(void (*entry)(void*), void* arg, int prio) {
    static uint32_t stacks[MAX_TASKS][256];  // 静的スタック
    static uint8_t task_idx = 0;
    
    Priority p = static_cast<Priority>(prio);
    g_scheduler.register_task(p, entry, arg, stacks[task_idx], 256);
    task_idx++;
}

static void pal_task_yield(void) {
    g_scheduler.yield();
}

static void pal_task_sleep_us(uint32_t us) {
    uint32_t ticks = (us * TICK_HZ) / 1000000;
    if (ticks == 0) ticks = 1;
    g_scheduler.sleep_ticks(ticks);
}

static uint64_t pal_time_us(void) {
    return (uint64_t)g_scheduler.tick_count() * (1000000 / TICK_HZ);
}

static void pal_critical_enter(void) {
    __disable_irq();
}

static void pal_critical_exit(void) {
    __enable_irq();
}

const PalOps pal_ops_umi_rtos = {
    .task_create = pal_task_create,
    .task_yield = pal_task_yield,
    .task_sleep_us = pal_task_sleep_us,
    .mutex_create = nullptr,    // 静的初期化のみ
    .mutex_lock = nullptr,      // Mutex::lock()を直接使用
    .mutex_unlock = nullptr,
    .time_us = pal_time_us,
    .critical_enter = pal_critical_enter,
    .critical_exit = pal_critical_exit,
};
```

#### 10.3.7 メモリフットプリント

| コンポーネント | ROM | RAM |
|----------------|-----|-----|
| スケジューラ | ~800 bytes | ~64 bytes |
| タスク制御ブロック×4 | - | ~96 bytes |
| スタック×4 | - | ~4KB（設定依存） |
| 同期プリミティブ | ~200 bytes | 使用数依存 |
| **合計** | **~1KB** | **~4.2KB** |

#### 10.3.8 FreeRTOS比較

| 項目 | UMI-RTOS | FreeRTOS |
|------|----------|----------|
| ROM | ~1KB | ~6-10KB |
| RAM（最小構成） | ~4KB | ~8KB |
| タスク数 | 固定4 | 動的 |
| 優先度 | 固定4 | 設定可能 |
| 動的メモリ | なし | あり |
| コンテキストスイッチ | ~50 cycles | ~80-100 cycles |
| 機能 | 最小限 | 豊富 |

### 10.4 FreeRTOS互換PAL（オプション）

既存プロジェクトとの互換性が必要な場合のオプション。

```cpp
// kernel/pal/freertos/pal_freertos.cpp

#include "FreeRTOS.h"
#include "task.h"

static void pal_task_create(void (*entry)(void*), void* arg, int prio) {
    UBaseType_t freertos_prio = configMAX_PRIORITIES - 1 - prio;
    xTaskCreate(entry, "umi", configMINIMAL_STACK_SIZE * 4, 
                arg, freertos_prio, NULL);
}

// ... 以下同様
```

### 10.5 ESP-IDF統合

ESP32ではESP-IDFとFreeRTOSが不可分のため、FreeRTOSベースのPALを使用する。

```cpp
// kernel/pal/esp-idf/pal_esp.cpp
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static void pal_task_create(void (*entry)(void*), void* arg, int prio) {
    int freertos_prio = configMAX_PRIORITIES - 1 - prio;
    // Core 1にピン留め（Core 0はWiFi/BLE用）
    xTaskCreatePinnedToCore(entry, "umi_task", 4096, arg, freertos_prio, NULL, 1);
}

static void pal_task_yield(void) {
    taskYIELD();
}

static void pal_task_sleep_us(uint32_t us) {
    vTaskDelay(pdMS_TO_TICKS((us + 999) / 1000));
}

static uint64_t pal_time_us(void) {
    return esp_timer_get_time();
}

static void pal_critical_enter(void) {
    portENTER_CRITICAL(&g_spinlock);
}

static void pal_critical_exit(void) {
    portEXIT_CRITICAL(&g_spinlock);
}

const PalOps pal_ops_esp = {
    .task_create = pal_task_create,
    .task_yield = pal_task_yield,
    .task_sleep_us = pal_task_sleep_us,
    .mutex_create = pal_mutex_create_freertos,
    .mutex_lock = pal_mutex_lock_freertos,
    .mutex_unlock = pal_mutex_unlock_freertos,
    .time_us = pal_time_us,
    .critical_enter = pal_critical_enter,
    .critical_exit = pal_critical_exit,
};
```

### 10.6 POSIX PAL（シミュレータ用）

```cpp
// kernel/pal/posix/pal_posix.cpp
#include <pthread.h>
#include <time.h>
#include <sched.h>

static void pal_task_create(void (*entry)(void*), void* arg, int prio) {
    pthread_t thread;
    pthread_attr_t attr;
    struct sched_param param;
    
    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    param.sched_priority = sched_get_priority_max(SCHED_FIFO) - prio;
    pthread_attr_setschedparam(&attr, &param);
    
    pthread_create(&thread, &attr, (void*(*)(void*))entry, arg);
    pthread_detach(thread);
}

static uint64_t pal_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}

const PalOps pal_ops_posix = {
    .task_create = pal_task_create,
    .task_yield = sched_yield,
    .task_sleep_us = pal_sleep_us_posix,
    .mutex_create = pal_mutex_create_posix,
    .mutex_lock = pal_mutex_lock_posix,
    .mutex_unlock = pal_mutex_unlock_posix,
    .time_us = pal_time_us,
    .critical_enter = pal_critical_enter_posix,
    .critical_exit = pal_critical_exit_posix,
};
```

---

## 11. アダプタ層

### 11.1 概要

アダプタ層は外部ホスト形式と本システム形式を変換する。

| アダプタ | 入力ソース | 出力先 | 備考 |
|----------|------------|--------|------|
| Embedded | I2S DMA | I2S DMA | 本カーネル上 |
| VST3 | IAudioProcessor | 同上 | Steinberg SDK |
| AU | AUAudioUnit | 同上 | Apple AudioUnit |
| CLAP | clap_plugin_t | 同上 | CLAP SDK |
| WASM | AudioWorkletProcessor | 同上 | Emscripten |
| Standalone | PortAudio/RTAudio | 同上 | デバッグ用 |

### 11.2 アダプタの責務

1. ホスト形式のオーディオバッファ → AudioContext変換
2. ホスト形式のイベント → EventQueue変換
3. パラメータ同期
4. 状態保存/復元の橋渡し

### 11.3 CLAPアダプタ例

```cpp
class ClapAdapter {
    std::unique_ptr<Processor> processor_;
    EventQueue event_queue_;
    
    // バッファポインタ配列（スタック上に確保）
    std::array<const sample_t*, 2> input_ptrs_;
    std::array<sample_t*, 2> output_ptrs_;
    
public:
    clap_process_status process(const clap_process_t* process) {
        // イベント変換
        for (uint32_t i = 0; i < process->in_events->size(process->in_events); ++i) {
            auto* hdr = process->in_events->get(process->in_events, i);
            if (hdr->type == CLAP_EVENT_MIDI) {
                auto* midi = reinterpret_cast<const clap_event_midi_t*>(hdr);
                event_queue_.push_midi(0, hdr->time, 
                    midi->data[0], midi->data[1], midi->data[2]);
            }
        }
        
        // ポインタ配列を設定
        input_ptrs_[0] = process->audio_inputs[0].data32[0];
        input_ptrs_[1] = process->audio_inputs[0].data32[1];
        output_ptrs_[0] = process->audio_outputs[0].data32[0];
        output_ptrs_[1] = process->audio_outputs[0].data32[1];
        
        // AudioContext構築
        AudioContext ctx{
            .inputs = input_ptrs_,
            .outputs = output_ptrs_,
            .events = event_queue_,
            .sample_rate = sample_rate_,
            .buffer_size = process->frames_count,
            .sample_position = sample_position_,
        };
        
        processor_->process(ctx);
        event_queue_.clear();
        sample_position_ += process->frames_count;
        
        return CLAP_PROCESS_CONTINUE;
    }
};
```

### 11.4 WASMアダプタ例

```cpp
// adapter/wasm/worklet_adapter.cpp
#include <emscripten/bind.h>

class WorkletAdapter {
    std::unique_ptr<Processor> processor_;
    
public:
    void process(uintptr_t in_ptr, uintptr_t out_ptr, uint32_t frames) {
        auto* in = reinterpret_cast<float*>(in_ptr);
        auto* out = reinterpret_cast<float*>(out_ptr);
        
        AudioContext ctx{/* ... */};
        processor_->process(ctx);
    }
};

EMSCRIPTEN_BINDINGS(worklet) {
    emscripten::class_<WorkletAdapter>("Processor")
        .constructor<>()
        .function("process", &WorkletAdapter::process);
}
```

---

## 12. 状態管理

### 12.1 二層構造

状態はフレームワーク管理とアプリケーション管理に分離する。

```cpp
// フレームワークが管理（共通）
struct FrameworkState {
    uint16_t current_page;
    uint16_t current_bank;
    uint8_t ui_mode;
    uint32_t current_preset_id;
    uint8_t midi_channel;
    std::vector<uint8_t> framework_extended;
};

// アプリケーションが管理（不透明）
struct AppState {
    uint32_t format_id;       // アプリ定義の形式識別子
    uint32_t version;         // 形式バージョン
    std::vector<uint8_t> data;
};

// 全体
struct State {
    FrameworkState framework;
    AppState app;
};
```

### 12.2 Processorインターフェース

```cpp
class Processor {
public:
    // アプリ固有状態のみ扱う
    // フレームワークは中身を解釈しない
    virtual std::vector<uint8_t> save_app_state() { return {}; }
    virtual void load_app_state(std::span<const uint8_t>) {}
};
```

### 12.3 バイナリ形式

```cpp
// 状態ファイルヘッダ
struct StateFileHeader {
    uint32_t magic;           // 'INST' (0x54534E49)
    uint16_t format_version;  // 形式バージョン
    uint16_t flags;
    uint32_t framework_size;
    uint32_t app_size;
};
```

---

## 13. ケーパビリティとセキュリティ

### 20.1 ケーパビリティモデル

すべてのリソースアクセスはケーパビリティハンドルを通じて行う。

```cpp
// ケーパビリティハンドル
struct Capability {
    uint32_t id;              // 一意識別子
    CapabilityType type;      // リソース種別
    uint32_t instance;        // インスタンス番号（ポート番号等）
    CapabilityFlags flags;    // 権限フラグ
};

enum class CapabilityType : uint16_t {
    // オーディオ
    AudioIn         = 0x0100,
    AudioOut        = 0x0101,
    
    // イベント
    MidiIn          = 0x0200,
    MidiOut         = 0x0201,
    CvIn            = 0x0202,
    CvOut           = 0x0203,
    
    // ストレージ
    FlashRead       = 0x0300,
    FlashWrite      = 0x0301,
    
    // システム
    ParamAccess     = 0x0400,
    StateAccess     = 0x0401,
    IspAccess       = 0x0402,
    
    // デバイス（ユーザドライバ用）
    GpioAccess      = 0x0500,
    SpiAccess       = 0x0501,
    I2cAccess       = 0x0502,
};

struct CapabilityFlags {
    uint16_t read       : 1;  // 読み取り可能
    uint16_t write      : 1;  // 書き込み可能
    uint16_t exclusive  : 1;  // 排他アクセス
    uint16_t realtime   : 1;  // リアルタイムコンテキストで使用可
    uint16_t reserved   : 12;
};
```

### 20.2 ケーパビリティ取得

```cpp
class CapabilityManager {
public:
    // ケーパビリティ要求（マニフェストに基づく）
    Result<Capability> acquire(CapabilityType type, uint32_t instance);
    
    // ケーパビリティ解放
    void release(Capability cap);
    
    // 権限チェック
    bool check(Capability cap, CapabilityFlags required) const;
    
    // 現在保持しているケーパビリティ一覧
    std::span<const Capability> held() const;
};
```

### 20.3 マニフェスト

アプリケーションの権限要求はマニフェストで宣言する。

```cpp
// バイナリマニフェスト形式
struct Manifest {
    // ヘッダ
    uint32_t magic;               // 'UMIM' (0x4D494D55)
    uint16_t manifest_version;    // マニフェスト形式バージョン
    uint16_t flags;
    
    // アプリ識別
    char app_id[32];              // 一意識別子（null終端）
    Version app_version;
    Version min_api_version;
    
    // 署名（製品モード）
    uint8_t signature_type;       // 0: none, 1: Ed25519
    uint8_t signature[64];        // Ed25519署名
    
    // リソース要求
    Requirements requirements;
    
    // ケーパビリティ要求（可変長）
    uint16_t capability_count;
    CapabilityRequest capabilities[];
};

struct CapabilityRequest {
    CapabilityType type;
    uint32_t instance;            // 0xFFFFFFFF = any
    CapabilityFlags required;     // 必須フラグ
    CapabilityFlags optional;     // オプションフラグ
};
```

### 20.4 署名検証

製品モードでは署名検証が必須。

```cpp
// 署名検証
enum class SignatureResult {
    Valid,              // 有効な署名
    Invalid,            // 無効な署名
    Expired,            // 期限切れ
    Revoked,            // 失効済み
    NoSignature,        // 署名なし（開発モードのみ許可）
    UnknownKey,         // 未知の公開鍵
};

class SignatureVerifier {
public:
    // マニフェスト署名検証
    SignatureResult verify(const Manifest& manifest) const;
    
    // 公開鍵登録（製造時）
    void register_public_key(const uint8_t key[32]);
    
    // 開発モード設定
    void set_development_mode(bool enabled);
};
```

### 22.5 セキュリティレベル

| モード | 署名 | 未署名アプリ | デバッグ機能 |
|--------|------|--------------|--------------|
| Production | 必須 | 拒否 | 無効 |
| Development | オプション | 制限付き許可 | 有効 |
| Factory | 不要 | 許可 | 全機能 |

### 22.6 保護対象リソース

```cpp
// 常にカーネル専用（アプリからアクセス不可）
namespace ProtectedResources {
    // フラッシュ書き込み（ISP経由のみ）
    // DMAコントローラ直接操作
    // クロック/電源設定
    // 割り込みコントローラ
    // MPU/MMU設定
    // 署名鍵
}
```

---

## 14. エラー処理

### 20.1 エラーコード体系

```cpp
// 統一エラーコード
enum class Error : int32_t {
    // 成功
    Ok                  = 0,
    
    // 一般エラー (0x0001-0x00FF)
    Unknown             = 0x0001,
    NotImplemented      = 0x0002,
    InvalidArgument     = 0x0003,
    InvalidState        = 0x0004,
    OutOfMemory         = 0x0005,
    OutOfResources      = 0x0006,
    Timeout             = 0x0007,
    Busy                = 0x0008,
    Cancelled           = 0x0009,
    
    // ケーパビリティエラー (0x0100-0x01FF)
    CapabilityNotHeld   = 0x0100,
    CapabilityDenied    = 0x0101,
    CapabilityExhausted = 0x0102,
    CapabilityInvalid   = 0x0103,
    
    // I/Oエラー (0x0200-0x02FF)
    IoError             = 0x0200,
    BufferOverflow      = 0x0201,
    BufferUnderflow     = 0x0202,
    DeviceNotFound      = 0x0203,
    DeviceError         = 0x0204,
    
    // オーディオエラー (0x0300-0x03FF)
    AudioUnderrun       = 0x0300,
    AudioOverrun        = 0x0301,
    SampleRateUnsupported = 0x0302,
    BufferSizeUnsupported = 0x0303,
    
    // 状態エラー (0x0400-0x04FF)
    StateCorrupted      = 0x0400,
    StateVersionMismatch= 0x0401,
    StateTooLarge       = 0x0402,
    
    // ISPエラー (0x0500-0x05FF)
    IspChecksumError    = 0x0500,
    IspSequenceError    = 0x0501,
    IspVerifyFailed     = 0x0502,
    IspFlashError       = 0x0503,
    
    // セキュリティエラー (0x0600-0x06FF)
    SignatureInvalid    = 0x0600,
    SignatureExpired    = 0x0601,
    SignatureRevoked    = 0x0602,
    PermissionDenied    = 0x0603,
};

// エラー情報付き結果型
template<typename T>
class Result {
    union {
        T value_;
        Error error_;
    };
    bool has_value_;
    
public:
    bool ok() const { return has_value_; }
    T& value() { return value_; }
    Error error() const { return error_; }
    
    // モナディック操作
    template<typename F>
    auto map(F&& f) -> Result<decltype(f(value_))>;
    
    template<typename F>
    auto and_then(F&& f) -> decltype(f(value_));
};
```

### 20.2 エラーハンドリング方針

```cpp
// Audio thread: エラー時の動作
enum class AudioErrorPolicy {
    OutputSilence,      // 無音を出力
    OutputLastBuffer,   // 前回バッファを保持
    OutputPassthrough,  // 入力をそのまま出力
};

// process()内でのエラー報告
struct ProcessResult {
    Error error;            // エラーコード（Error::Ok = 正常）
    uint32_t frames_processed;  // 実際に処理したフレーム数
};

// Control thread: エラー時の動作
// - ログ出力
// - ISP経由で通知
// - 可能なら回復を試みる
```

### 20.3 ログシステム

```cpp
enum class LogLevel : uint8_t {
    Trace   = 0,    // 開発時詳細
    Debug   = 1,    // デバッグ情報
    Info    = 2,    // 通常情報
    Warn    = 3,    // 警告
    Error   = 4,    // エラー
    Fatal   = 5,    // 致命的エラー
};

// ログ出力（ISP SysEx経由）
void log(LogLevel level, const char* fmt, ...);

// コンパイル時ログレベル設定
#ifndef UMI_LOG_LEVEL
    #ifdef NDEBUG
        #define UMI_LOG_LEVEL LogLevel::Warn
    #else
        #define UMI_LOG_LEVEL LogLevel::Debug
    #endif
#endif

#define UMI_LOG(level, fmt, ...) \
    do { \
        if constexpr (static_cast<uint8_t>(level) >= static_cast<uint8_t>(UMI_LOG_LEVEL)) { \
            ::umi::log(level, fmt, ##__VA_ARGS__); \
        } \
    } while(0)

#define UMI_TRACE(fmt, ...) UMI_LOG(LogLevel::Trace, fmt, ##__VA_ARGS__)
#define UMI_DEBUG(fmt, ...) UMI_LOG(LogLevel::Debug, fmt, ##__VA_ARGS__)
#define UMI_INFO(fmt, ...)  UMI_LOG(LogLevel::Info, fmt, ##__VA_ARGS__)
#define UMI_WARN(fmt, ...)  UMI_LOG(LogLevel::Warn, fmt, ##__VA_ARGS__)
#define UMI_ERROR(fmt, ...) UMI_LOG(LogLevel::Error, fmt, ##__VA_ARGS__)
#define UMI_FATAL(fmt, ...) UMI_LOG(LogLevel::Fatal, fmt, ##__VA_ARGS__)
```

### 20.4 アサーションとパニック

```cpp
// デバッグアサーション（リリースビルドで消える）
#define UMI_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            umi::panic(__FILE__, __LINE__, #cond); \
        } \
    } while(0)

// 常に有効なアサーション
#define UMI_VERIFY(cond) \
    do { \
        if (!(cond)) { \
            umi::panic(__FILE__, __LINE__, #cond); \
        } \
    } while(0)

// パニックハンドラ
[[noreturn]] void panic(const char* file, int line, const char* msg);

// パニック時の動作（設定可能）
enum class PanicAction {
    Halt,           // 停止（デフォルト）
    Reset,          // リセット
    SafeMode,       // セーフモードで再起動
};
```

---

## 15. Instrument Service Protocol (ISP)

### 20.1 概要

ISPはMIDI SysExを用いた診断・更新プロトコルである。

設計原則:
1. 既存MIDIインフラで動作
2. MIDI 2.0 UMPへの移行パスあり
3. 最小実装が容易
4. 拡張可能

### 20.2 SysExフォーマット

```
F0 <manufacturer_id> <header> <payload> F7
```

### 20.3 ヘッダ構造

```cpp
struct IspHeader {
    uint8_t version;          // プロトコルバージョン (0x01)
    uint8_t device_id;        // 0x00-0x7E: 特定, 0x7F: ブロードキャスト
    uint8_t transaction_id;   // 要求/応答のペアリング
    uint8_t message_type;     // カテゴリ
    uint8_t command;          // 具体的コマンド
};
```

### 20.4 メッセージタイプ

| Type | 値 | 説明 |
|------|-----|------|
| Discovery | 0x01 | デバイス発見 |
| Diagnostic | 0x02 | 診断・ログ |
| Parameter | 0x03 | パラメータ操作 |
| State | 0x04 | 状態・プリセット |
| Firmware | 0x05 | ファームウェア更新 |
| Stream | 0x06 | ストリーミングデータ |
| Vendor | 0x7F | ベンダー拡張 |

### 22.5 応答ステータス

| Status | 値 | 説明 |
|--------|-----|------|
| Ok | 0x00 | 成功 |
| Accepted | 0x01 | 非同期処理開始 |
| UnknownCommand | 0x40 | 未知のコマンド |
| InvalidParam | 0x41 | 無効なパラメータ |
| Busy | 0x42 | 処理中 |
| NotSupported | 0x43 | 未サポート |
| ChecksumError | 0x44 | チェックサムエラー |
| Timeout | 0x45 | タイムアウト |
| DeviceError | 0x7F | デバイスエラー |

### 22.6 Discovery コマンド

| Command | 値 | 説明 |
|---------|-----|------|
| Ping | 0x01 | 応答確認 |
| DeviceInfo | 0x02 | デバイス情報取得 |
| Capabilities | 0x03 | 機能一覧 |

### 22.7 Diagnostic コマンド

| Command | 値 | 説明 |
|---------|-----|------|
| LogSubscribe | 0x01 | ログ購読開始 |
| LogUnsubscribe | 0x02 | ログ購読停止 |
| LogMessage | 0x03 | ログメッセージ (デバイス→ホスト) |
| LogSetLevel | 0x04 | ログレベル設定 |
| StatsSubscribe | 0x10 | 統計情報購読開始 |
| StatsUnsubscribe | 0x11 | 統計情報購読停止 |
| StatsReport | 0x12 | 統計情報レポート (デバイス→ホスト) |
| StatsRequest | 0x13 | 統計情報の即時要求 |

ログレベル:

| Level | 値 | 説明 |
|-------|-----|------|
| Trace | 0x00 | 開発時詳細 |
| Debug | 0x01 | デバッグ情報 |
| Info | 0x02 | 通常情報 |
| Warn | 0x03 | 警告 |
| Error | 0x04 | エラー |
| Fatal | 0x05 | 致命的エラー |

### 22.8 統計情報フォーマット

StatsReportペイロード構造:

```cpp
struct StatsPayload {
    // ヘッダ
    uint8_t stats_version;    // 統計フォーマットバージョン (0x01)
    uint8_t flags;            // ビットフラグ
    uint16_t interval_ms;     // レポート間隔 (ms)
    
    // CPU / オーディオ処理
    uint8_t cpu_usage;        // CPU使用率 (0-100, 255=unknown)
    uint8_t audio_load;       // process()負荷 (0-100)
    uint16_t max_process_us;  // 最大process()時間 (μs)
    uint32_t drop_count;      // 累積ドロップ数
    uint32_t total_buffers;   // 累積処理バッファ数
    
    // メモリ
    uint16_t heap_used_kb;    // ヒープ使用量 (KB)
    uint16_t heap_total_kb;   // ヒープ総量 (KB)
    
    // タスク別スタック (4タスク分)
    struct TaskStack {
        uint16_t used;        // 使用量 (bytes)
        uint16_t total;       // 総量 (bytes)
    } task_stack[4];          // Audio, Driver, Control, Background
    
    // 同期状態
    int16_t sync_drift;       // 同期ドリフト (samples, 正=進み)
    uint8_t sync_source;      // SyncSource enum値
    uint8_t sync_status;      // 0=OK, 1=Lost, 2=Seeking
    
    // 拡張用
    uint8_t extended[];       // 可変長拡張データ
};
```

7bitエンコーディング（SysEx制約）:

```cpp
// 16bit値: 3バイトに分割
// value = 0xABCD
// → byte0 = (value >> 14) & 0x7F  // 上位2bit
// → byte1 = (value >> 7) & 0x7F   // 中位7bit
// → byte2 = value & 0x7F          // 下位7bit

// 32bit値: 5バイトに分割
```

### 22.9 統計購読フロー

```
Host                          Device
  │                              │
  │──── StatsSubscribe ────────▶│
  │     (interval=100ms)         │
  │◀─── Ok ─────────────────────│
  │                              │
  │◀─── StatsReport ────────────│  ← 100ms毎に自動送信
  │◀─── StatsReport ────────────│
  │◀─── StatsReport ────────────│
  │        ...                   │
  │──── StatsUnsubscribe ──────▶│
  │◀─── Ok ─────────────────────│
  │                              │
```

### 20.10 スタック監視実装

```cpp
// UMI-RTOSでのスタック監視
class StackMonitor {
public:
    // タスク登録時にスタック領域を記録
    void register_task(uint8_t id, uint32_t* stack_bottom, size_t size) {
        tasks_[id] = {stack_bottom, size};
        // スタックをパターンで埋める（ウォーターマーク方式）
        fill_pattern(stack_bottom, size);
    }
    
    // 使用量を計算（未使用パターンを下から探索）
    size_t get_used(uint8_t id) const {
        auto& t = tasks_[id];
        size_t unused = 0;
        for (size_t i = 0; i < t.size / 4; ++i) {
            if (t.bottom[i] != STACK_PATTERN) break;
            unused += 4;
        }
        return t.size - unused;
    }
    
private:
    static constexpr uint32_t STACK_PATTERN = 0xDEADBEEF;
    
    struct TaskInfo {
        uint32_t* bottom;
        size_t size;
    };
    std::array<TaskInfo, 4> tasks_;
    
    void fill_pattern(uint32_t* p, size_t size) {
        for (size_t i = 0; i < size / 4; ++i) {
            p[i] = STACK_PATTERN;
        }
    }
};
```

### 20.11 CPU使用率計測

```cpp
class CpuMonitor {
    uint32_t process_start_tick_;
    uint32_t process_total_ticks_ = 0;
    uint32_t measure_start_tick_;
    uint32_t buffer_count_ = 0;
    
public:
    // process()開始時に呼ぶ
    void begin_process() {
        process_start_tick_ = get_cycle_count();
    }
    
    // process()終了時に呼ぶ
    void end_process() {
        process_total_ticks_ += get_cycle_count() - process_start_tick_;
        buffer_count_++;
    }
    
    // 定期的に呼んで使用率を取得（0-100）
    uint8_t get_usage_and_reset() {
        uint32_t elapsed = get_cycle_count() - measure_start_tick_;
        uint8_t usage = (process_total_ticks_ * 100) / elapsed;
        
        // リセット
        process_total_ticks_ = 0;
        measure_start_tick_ = get_cycle_count();
        buffer_count_ = 0;
        
        return std::min(usage, uint8_t(100));
    }
};
```

### 20.12 Firmware コマンド

| Command | 値 | 説明 |
|---------|-----|------|
| Capabilities | 0x01 | 対応機能確認 |
| Prepare | 0x02 | 更新準備 |
| Data | 0x03 | データブロック転送 |
| Verify | 0x04 | チェックサム検証 |
| Apply | 0x05 | 適用実行 |
| Abort | 0x06 | 中止 |
| Progress | 0x07 | 進捗通知 |

### 20.13 ファームウェア更新フロー

```
Host                          Device
  │                              │
  │──── Capabilities ──────────▶│
  │◀─── Capabilities Response ──│
  │                              │
  │──── Prepare ───────────────▶│
  │◀─── Accepted ───────────────│
  │                              │
  │──── Data (block 0) ────────▶│
  │◀─── Ok ─────────────────────│
  │──── Data (block 1) ────────▶│
  │◀─── Ok ─────────────────────│
  │        ...                   │
  │──── Data (block N) ────────▶│
  │◀─── Ok ─────────────────────│
  │                              │
  │──── Verify ────────────────▶│
  │◀─── Ok ─────────────────────│
  │                              │
  │──── Apply ─────────────────▶│
  │◀─── Accepted ───────────────│
  │        (device reboots)      │
  │                              │
```

### 20.14 Firmware Capabilities

```cpp
struct FirmwareCapabilities {
    uint32_t max_firmware_size;
    uint16_t block_size;
    uint8_t flags;
    // flags bits:
    //   bit 0: supports_resume
    //   bit 1: supports_verify
    //   bit 2: supports_rollback
    //   bit 3: requires_reset
};
```

### 20.11 Firmware Prepare

```cpp
struct FirmwarePrepare {
    uint32_t firmware_size;
    uint32_t checksum_crc32;
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t version_patch;
    uint16_t target_device_type;
};
```

### 20.12 Firmware Data

```cpp
struct FirmwareData {
    uint32_t offset;          // バイトオフセット
    uint16_t length;          // ブロックサイズ
    uint16_t checksum_crc16;  // ブロックCRC
    // followed by 7-bit encoded data
};
```

### 20.13 データエンコーディング

8bitデータは7bitにエンコードする（MIDI互換）。

```cpp
// エンコード: 7バイトの入力 → 8バイトの出力
void encode_7bit(std::span<const uint8_t> in, std::vector<uint8_t>& out);

// デコード: 8バイトの入力 → 7バイトの出力
void decode_7bit(std::span<const uint8_t> in, std::vector<uint8_t>& out);
```

### 20.14 エラー回復

```cpp
struct RetryPolicy {
    uint8_t max_retries = 3;
    uint16_t timeout_ms = 1000;
    uint16_t backoff_ms = 100;
};
```

---

## 16. ビルドシステム

### 20.1 概要

xmakeを使用し、単一のコードベースから複数ターゲットをビルドする。

### 20.2 プロジェクト構造

```
umi-os/
├── core/
│   ├── include/
│   │   ├── umi/
│   │   │   ├── processor.hpp
│   │   │   ├── param.hpp
│   │   │   ├── audio_context.hpp
│   │   │   ├── event.hpp
│   │   │   └── types.hpp
│   │   └── umi.hpp          # 統合ヘッダ
│   └── src/
│
├── kernel/
│   ├── include/
│   ├── src/
│   ├── rtos/                # UMI-RTOS実装
│   │   ├── umi_rtos.hpp
│   │   ├── umi_scheduler.cpp
│   │   ├── umi_sync.hpp
│   │   ├── umi_rtos_cm4.S   # Cortex-M4用
│   │   ├── umi_rtos_cm0.S   # Cortex-M0用
│   │   └── umi_rtos_cm7.S   # Cortex-M7用
│   └── pal/
│       ├── umi-rtos/        # UMI-RTOS用PAL
│       ├── freertos/        # FreeRTOS互換PAL
│       ├── esp-idf/         # ESP-IDF用PAL
│       ├── zephyr/
│       └── posix/
│
├── drivers/
│   ├── audio/
│   │   ├── i2s_esp32/
│   │   ├── i2s_stm32/
│   │   ├── sai_stm32/
│   │   └── portaudio/
│   ├── midi/
│   │   ├── uart/
│   │   └── usb/
│   └── cv/
│
├── adapters/
│   ├── embedded/
│   ├── vst3/
│   ├── au/
│   ├── clap/
│   ├── wasm/
│   └── standalone/
│
├── isp/
│   ├── include/
│   ├── src/
│   └── tools/
│
├── platforms/
│   ├── esp32/
│   ├── stm32f4/
│   ├── stm32h7/
│   ├── rp2040/
│   └── linux-sim/
│
├── examples/
│   └── simple_synth/
│
├── tests/
│
└── xmake.lua
```

### 20.3 xmake.lua 例

```lua
set_project("umi-os")
set_version("0.1.0")

-- ターゲットプラットフォーム
option("target_platform")
    set_values("esp32", "stm32f4", "stm32h7", "rp2040", "linux-sim", "wasm")
    set_default("linux-sim")

-- RTOS選択（Cortex-M用）
option("rtos")
    set_values("umi-rtos", "freertos", "zephyr")
    set_default("umi-rtos")

-- コアライブラリ
target("umi-core")
    set_kind("static")
    add_files("core/src/*.cpp")
    add_includedirs("core/include", {public = true})

-- UMI-RTOS
target("umi-rtos")
    set_kind("static")
    add_files("kernel/rtos/*.cpp")
    add_files("kernel/rtos/*.S")
    add_includedirs("kernel/rtos", {public = true})

-- カーネル
target("umi-kernel")
    set_kind("static")
    add_deps("umi-core")
    add_files("kernel/src/*.cpp")
    add_includedirs("kernel/include", {public = true})
    
    if is_config("target_platform", "esp32") then
        -- ESP32はFreeRTOS必須
        add_files("kernel/pal/esp-idf/*.cpp")
        add_defines("PAL_ESP_IDF")
    elseif is_config("target_platform", "stm32f4") or is_config("target_platform", "stm32h7") then
        if is_config("rtos", "umi-rtos") then
            add_deps("umi-rtos")
            add_files("kernel/pal/umi-rtos/*.cpp")
            add_defines("PAL_UMI_RTOS")
        elseif is_config("rtos", "freertos") then
            add_files("kernel/pal/freertos/*.cpp")
            add_defines("PAL_FREERTOS")
        end
    elseif is_config("target_platform", "linux-sim") then
        add_files("kernel/pal/posix/*.cpp")
        add_defines("PAL_POSIX")
        add_links("pthread")
    end

-- ドライバ
target("drivers-stm32f4")
    set_kind("static")
    add_deps("umi-core")
    add_files("drivers/audio/i2s_stm32/*.cpp")
    add_files("drivers/midi/uart/*.cpp")
    add_defines("STM32F4")

target("drivers-stm32h7")
    set_kind("static")
    add_deps("umi-core")
    add_files("drivers/audio/sai_stm32/*.cpp")
    add_files("drivers/midi/usb/*.cpp")
    add_defines("STM32H7")

target("drivers-esp32")
    set_kind("static")
    add_deps("umi-core")
    add_files("drivers/audio/i2s_esp32/*.cpp")
    add_files("drivers/midi/uart/*.cpp")

-- アダプタ
target("adapter-vst3")
    set_kind("static")
    add_deps("umi-core")
    add_files("adapters/vst3/*.cpp")

target("adapter-clap")
    set_kind("static")
    add_deps("umi-core")
    add_files("adapters/clap/*.cpp")

target("adapter-wasm")
    set_kind("static")
    add_deps("umi-core")
    add_files("adapters/wasm/*.cpp")

-- サンプルアプリケーション: VST3
target("simple_synth_vst3")
    set_kind("shared")
    add_deps("umi-core", "adapter-vst3")
    add_files("examples/simple_synth/*.cpp")

-- サンプルアプリケーション: CLAP
target("simple_synth_clap")
    set_kind("shared")
    add_deps("umi-core", "adapter-clap")
    add_files("examples/simple_synth/*.cpp")

-- サンプルアプリケーション: WASM
target("simple_synth_wasm")
    set_kind("binary")
    set_toolchains("emscripten")
    add_deps("umi-core", "adapter-wasm")
    add_files("examples/simple_synth/*.cpp")
    add_ldflags("-s WASM=1", "-s EXPORTED_FUNCTIONS=['_malloc','_free']")

-- サンプルアプリケーション: STM32F4 + UMI-RTOS
target("simple_synth_stm32f4")
    set_kind("binary")
    set_toolchains("arm-none-eabi")
    add_deps("umi-core", "umi-kernel", "umi-rtos", "drivers-stm32f4")
    add_files("examples/simple_synth/*.cpp")
    add_files("platforms/stm32f4/startup.c")
    add_files("platforms/stm32f4/system.c")
    add_ldflags("-T platforms/stm32f4/link.ld")
    add_defines("STM32F4", "USE_HAL_DRIVER")

-- サンプルアプリケーション: ESP32
target("simple_synth_esp32")
    set_kind("binary")
    add_deps("umi-core", "umi-kernel", "drivers-esp32")
    add_files("examples/simple_synth/*.cpp")
```

---

## 17. 数値表現

### 17.1 設計思想

UMI-OSは、float版とfixed版の両方をサポートする。ただし、同一コードでの両対応ではなく、**移植容易性**を重視した設計とする。

**実運用の想定:**
- 開発時はターゲットに合わせて片方のみ実装
- 製品展開や派生開発時に、別プロセッサへ移植
- 移植時の書き換え量を最小化することが目標

```
製品A (Cortex-M4, FPUあり)
  └→ float版で開発・リリース

製品B (Cortex-M0, FPUなし)    ← 製品Aの廉価版
  └→ DSP部分をfixed版に書き換えて移植

製品C (ESP32, FPUあり)        ← 製品AのWiFi対応版
  └→ float版をほぼそのまま移植
```

### 17.2 共通化の範囲

| 層 | 共通/分離 | 移植時の扱い |
|----|----------|--------------|
| Processorクラス構造 | 共通 | そのまま使用 |
| ポート/パラメータ宣言 | 共通 | そのまま使用 |
| イベント処理ロジック | 共通 | そのまま使用 |
| 状態管理 | 共通 | そのまま使用 |
| ライフサイクル | 共通 | そのまま使用 |
| sample_t型定義 | 分離 | 切り替え |
| DSP演算実装 | 分離 | 書き換え |
| 数学関数 | 分離 | 書き換え |

### 17.3 sample_t型

ビルド構成によって切り替わる基本型。

```cpp
// core/include/umi/types.hpp

namespace umi {

#if defined(UMI_SAMPLE_FLOAT)
    using sample_t = float;
    constexpr sample_t SAMPLE_MAX = 1.0f;
    constexpr sample_t SAMPLE_MIN = -1.0f;
    
#elif defined(UMI_SAMPLE_Q15)
    using sample_t = Q15;
    constexpr sample_t SAMPLE_MAX = Q15{32767};
    constexpr sample_t SAMPLE_MIN = Q15{-32768};
    
#elif defined(UMI_SAMPLE_Q31)
    using sample_t = Q31;
    constexpr sample_t SAMPLE_MAX = Q31{2147483647};
    constexpr sample_t SAMPLE_MIN = Q31{-2147483648};
    
#else
    #error "Define UMI_SAMPLE_FLOAT, UMI_SAMPLE_Q15, or UMI_SAMPLE_Q31"
#endif

} // namespace umi
```

### 17.4 固定小数点型

```cpp
// core/include/umi/fixed.hpp

namespace umi {

struct Q15 {
    int16_t raw;
    
    constexpr Q15() : raw(0) {}
    constexpr explicit Q15(int16_t r) : raw(r) {}
    
    static constexpr Q15 from_float(float f) {
        return Q15{static_cast<int16_t>(f * 32767.0f)};
    }
    
    constexpr float to_float() const {
        return raw / 32768.0f;
    }
    
    constexpr Q15 operator+(Q15 b) const { return Q15(raw + b.raw); }
    constexpr Q15 operator-(Q15 b) const { return Q15(raw - b.raw); }
    
    constexpr Q15 operator*(Q15 b) const {
        return Q15(static_cast<int16_t>((int32_t(raw) * b.raw) >> 15));
    }
    
    // 飽和演算
    static constexpr Q15 add_sat(Q15 a, Q15 b) {
        int32_t sum = int32_t(a.raw) + b.raw;
        if (sum > 32767) return Q15{32767};
        if (sum < -32768) return Q15{-32768};
        return Q15{static_cast<int16_t>(sum)};
    }
    
    static constexpr Q15 mul_sat(Q15 a, Q15 b) {
        int32_t prod = (int32_t(a.raw) * b.raw) >> 15;
        if (prod > 32767) return Q15{32767};
        if (prod < -32768) return Q15{-32768};
        return Q15{static_cast<int16_t>(prod)};
    }
};

struct Q31 {
    int32_t raw;
    
    constexpr Q31() : raw(0) {}
    constexpr explicit Q31(int32_t r) : raw(r) {}
    
    static constexpr Q31 from_float(float f) {
        return Q31{static_cast<int32_t>(f * 2147483647.0f)};
    }
    
    constexpr float to_float() const {
        return raw / 2147483648.0f;
    }
    
    constexpr Q31 operator+(Q31 b) const { return Q31(raw + b.raw); }
    constexpr Q31 operator-(Q31 b) const { return Q31(raw - b.raw); }
    
    constexpr Q31 operator*(Q31 b) const {
        return Q31(static_cast<int32_t>((int64_t(raw) * b.raw) >> 31));
    }
};

} // namespace umi
```

### 17.5 APIは共通

AudioContext等のAPIはsample_tを使用し、float/fixed両方で同一シグネチャ。

```cpp
// Processor API（共通）
struct AudioContext {
    std::span<const sample_t* const> inputs;
    std::span<sample_t* const> outputs;
    EventQueue& events;
    uint32_t sample_rate;
    uint32_t buffer_size;
    uint64_t sample_position;
};

class Processor {
public:
    virtual void prepare(const StreamConfig& config) {}
    virtual void process(AudioContext& ctx) = 0;
    virtual void control(ControlContext& ctx) {}
    // ...
};
```

### 17.6 DSP実装の分離パターン

DSP処理は分離し、移植時に差し替える設計とする。

```cpp
// === アプリケーション構造（共通） ===
// mysynth/processor.hpp

class MySynth : public umi::Processor {
    Oscillator osc_;        // DSP部品（実装は分離）
    Filter filter_;
    Envelope env_;
    
public:
    void process(AudioContext& ctx) override {
        // この構造は共通
        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            sample_t osc_out = osc_.tick();
            sample_t flt_out = filter_.tick(osc_out);
            sample_t env_val = env_.tick();
            ctx.outputs[0][i] = mul(flt_out, env_val);  // mul()で抽象化
        }
    }
};
```

```cpp
// === DSP実装（float版） ===
// mysynth/dsp_float.cpp

class Oscillator {
    float phase_ = 0.0f;
    float freq_ = 440.0f;
    float sample_rate_ = 48000.0f;
    
public:
    sample_t tick() {
        sample_t out = std::sin(phase_ * 6.283185307f);
        phase_ += freq_ / sample_rate_;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        return out;
    }
};

inline sample_t mul(sample_t a, sample_t b) {
    return a * b;
}
```

```cpp
// === DSP実装（fixed版） ===
// mysynth/dsp_fixed.cpp

class Oscillator {
    uint16_t phase_ = 0;       // 0-65535 = 0-2π
    uint16_t freq_inc_ = 600;  // 周波数に対応
    
public:
    sample_t tick() {
        sample_t out = sin_table_q15(phase_);
        phase_ += freq_inc_;
        return out;
    }
};

inline sample_t mul(sample_t a, sample_t b) {
    return Q15::mul_sat(a, b);
}
```

### 17.7 DSP部品の設計指針

#### 17.7.1 仮想関数の使用基準

仮想関数呼び出しには間接ジャンプのコストがある（数サイクル + インライン化不可）。
呼び出し頻度に応じて使用可否を判断する。

| 呼び出し頻度 | 仮想関数 | 理由 |
|-------------|----------|------|
| バッファ毎（数百回/秒） | ✓ OK | オーバーヘッド無視可能 |
| サンプル毎（数万回/秒） | ✗ 避ける | 累積コストが大きい |

```cpp
// ✓ OK: Processor::process() はバッファ毎
class Processor {
public:
    virtual void process(AudioContext& ctx) = 0;  // OK
};

// ✗ NG: DSP部品を仮想関数で抽象化
class OscillatorBase {
public:
    virtual sample_t tick() = 0;  // NG: サンプル毎に呼ばれる
};

// ✓ OK: DSP部品はテンプレートまたは具象クラス
class SineOscillator {
public:
    sample_t tick();  // 非仮想、インライン化可能
};

// ✓ OK: テンプレートで静的ポリモーフィズム
template<typename Osc>
class Synth : public Processor {
    Osc osc_;
public:
    void process(AudioContext& ctx) override {
        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            ctx.outputs[0][i] = osc_.tick();  // インライン化される
        }
    }
};
```

#### 17.7.2 DSP部品の推奨パターン

```cpp
// パターン1: 具象クラス（最もシンプル）
class LowpassFilter {
    // 状態
    sample_t y1_ = 0;
    sample_t coef_;
    
public:
    void set_cutoff(float normalized);
    
    // インライン化されるべきホットパス
    sample_t tick(sample_t in) {
        y1_ = in * coef_ + y1_ * (sample_t(1) - coef_);
        return y1_;
    }
};

// パターン2: テンプレートパラメータで変種を生成
template<int Order>
class BiquadCascade {
    Biquad stages_[Order];
public:
    sample_t tick(sample_t in) {
        for (int i = 0; i < Order; ++i) {
            in = stages_[i].tick(in);
        }
        return in;  // ループ展開される
    }
};

// パターン3: バッファ単位処理（SIMD最適化可能）
class Oscillator {
public:
    void process(sample_t* out, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) {
            out[i] = tick_internal();
        }
    }
private:
    sample_t tick_internal();
};
```

#### 17.7.3 ランタイム切り替えが必要な場合

オシレータ波形切り替えなど、ランタイムで種類を変える必要がある場合:

```cpp
// 方法1: switch文（分岐予測が効く）
class MultiOscillator {
    enum class Waveform { Sine, Saw, Square };
    Waveform waveform_ = Waveform::Sine;
    
public:
    sample_t tick() {
        switch (waveform_) {
        case Waveform::Sine:   return tick_sine();
        case Waveform::Saw:    return tick_saw();
        case Waveform::Square: return tick_square();
        }
    }
    
private:
    sample_t tick_sine();
    sample_t tick_saw();
    sample_t tick_square();
};

// 方法2: 関数ポインタ（波形変更時のみ更新）
class MultiOscillator {
    using TickFn = sample_t (MultiOscillator::*)();
    TickFn tick_fn_ = &MultiOscillator::tick_sine;
    
public:
    void set_waveform(Waveform w) {
        switch (w) {
        case Waveform::Sine:   tick_fn_ = &tick_sine; break;
        case Waveform::Saw:    tick_fn_ = &tick_saw; break;
        case Waveform::Square: tick_fn_ = &tick_square; break;
        }
    }
    
    sample_t tick() {
        return (this->*tick_fn_)();
    }
};
```

### 17.8 移植時の作業

**Float → Float（別プラットフォーム）:**
- DSPコードはそのまま
- ドライバ/PALのみ差し替え
- 作業量: 小

**Float → Fixed:**
- Processor構造はそのまま
- DSP実装を書き換え
- 作業量: 中〜大（DSP複雑度による）

**移植チェックリスト:**

| 項目 | Float→Float | Float→Fixed |
|------|-------------|-------------|
| Processor構造 | ✓ そのまま | ✓ そのまま |
| ポート/パラメータ宣言 | ✓ そのまま | ✓ そのまま |
| イベント処理 | ✓ そのまま | ✓ そのまま |
| 状態保存/復元 | ✓ そのまま | △ 型注意 |
| オシレータ | ✓ そのまま | ✗ 書き換え |
| フィルタ | ✓ そのまま | ✗ 書き換え |
| エンベロープ | ✓ そのまま | ✗ 書き換え |
| エフェクト | ✓ そのまま | ✗ 書き換え |
| 数学関数 | ✓ そのまま | ✗ テーブル等 |

### 17.9 DSPライブラリ

UMI-OSは基本的なDSP部品のリファレンス実装を提供する（両版）。

```
umi-dsp/
├── float/
│   ├── oscillator.hpp    # sin, saw, square, etc.
│   ├── filter.hpp        # biquad, svf, onepole
│   ├── envelope.hpp      # adsr, ar
│   └── delay.hpp         # delay, comb, allpass
│
├── fixed/
│   ├── oscillator.hpp    # テーブルベース実装
│   ├── filter.hpp        # 整数演算最適化
│   ├── envelope.hpp      # 整数演算最適化
│   └── delay.hpp
│
└── common/
    ├── tables.hpp        # sinテーブル等
    └── math.hpp          # 共通数学ユーティリティ
```

使用は任意。プロジェクト固有の最適化が必要な場合は独自実装を推奨。

### 17.10 環境別推奨構成

| ターゲット | FPU | 推奨 | 備考 |
|------------|-----|------|------|
| Cortex-M4/M7 | 単精度 | float | 標準選択 |
| Cortex-M33 | 単精度 | float | TrustZone対応 |
| Cortex-A | 単精度/倍精度 | float | 問題なし |
| ESP32 | 単精度 | float | WiFi/BLE共存注意 |
| Cortex-M0/M0+ | なし | Q15 | soft floatは非実用的 |
| RP2040 | なし | Q15/Q31 | Cortex-M0+デュアルコア |
| WASM | あり | float | ブラウザ実行 |

### 17.11 係数計算と精度

フィルタ係数など精度が必要な計算について:

```cpp
// Float版: 係数計算は倍精度、処理は単精度
class BiquadFloat {
    double b0_, b1_, b2_, a1_, a2_;  // 係数は倍精度で保持
    float x1_, x2_, y1_, y2_;
    
public:
    void set_lowpass(float freq, float q, float sr) {
        double omega = 2.0 * M_PI * freq / sr;
        // ... 倍精度で計算
    }
    
    sample_t tick(sample_t in) {
        // 処理は単精度
        float y = float(b0_)*in + float(b1_)*x1_ + ...;
        return y;
    }
};

// Fixed版: 係数は事前計算またはテーブル
class BiquadFixed {
    Q15 b0_, b1_, b2_, a1_, a2_;  // 事前計算済み
    Q15 x1_, x2_, y1_, y2_;
    
public:
    void set_lowpass_table(uint8_t freq_idx, uint8_t q_idx) {
        // テーブルから係数取得
        const auto& c = BIQUAD_COEF_TABLE[freq_idx][q_idx];
        b0_ = c.b0; b1_ = c.b1; // ...
    }
    
    sample_t tick(sample_t in) {
        // Q15演算（オーバーフロー注意）
        int32_t acc = int32_t(b0_.raw) * in.raw;
        acc += int32_t(b1_.raw) * x1_.raw;
        // ... 中間結果は32bitで保持、最後にシフト
        return Q15{static_cast<int16_t>(acc >> 15)};
    }
};
```

---

## 18. UI/コントローラー分離

### 20.1 設計原則

Processorはヘッドレス（UI非依存）で実装し、UIは別レイヤーで提供する。

```
┌─────────────────────────────────────────────────────────────┐
│                    Processor (ヘッドレス)                   │
│  - process(), control()                                     │
│  - params(), ports()                                        │
│  - save_state(), load_state()                               │
├─────────────────────────────────────────────────────────────┤
│                    Controller (UI抽象化)                    │
│  - パラメータ変更通知                                        │
│  - 表示用データ取得                                          │
│  - ユーザー入力イベント                                      │
├─────────────────┬─────────────────┬─────────────────────────┤
│ Hardware UI     │   Plugin GUI    │    Web UI              │
│ - 物理ノブ/LED  │   - VST3 View   │    - WebSocket         │
│ - ディスプレイ  │   - CLAP GUI    │    - React等           │
│ - ADC/GPIO      │                 │                        │
└─────────────────┴─────────────────┴─────────────────────────┘
```

### 20.2 Controller インターフェース

```cpp
// umi/controller.hpp

namespace umi {

class Controller {
public:
    virtual ~Controller() = default;
    
    // === Processor → UI ===
    
    // パラメータ値が変更された
    virtual void on_param_changed(uint32_t id, float value) {}
    
    // メーターレベル更新（VUメーター等）
    virtual void on_meter_update(std::span<const float> levels) {}
    
    // 波形/スペクトラム表示用データ
    virtual void on_waveform_data(std::span<const float> data) {}
    
    // カスタム表示データ（ディスプレイ等）
    virtual void on_display_data(uint32_t display_id, 
                                  std::span<const uint8_t> data) {}
    
    // 状態変更通知
    virtual void on_state_changed(ProcessorState state) {}
    
    // === UI → Processor ===
    
    // パラメータ設定（内部でControl Runnerへ通知）
    void set_param(uint32_t id, float value);
    
    // アクション送信（ボタン押下等）
    void send_action(uint32_t action_id);
    void send_action(uint32_t action_id, std::span<const uint8_t> data);
    
    // プリセット操作
    void load_preset(uint32_t preset_id);
    void save_preset(uint32_t preset_id);
    
protected:
    Processor* processor_ = nullptr;
    friend class Kernel;
};

} // namespace umi
```

### 20.3 ハードウェアUI実装例

```cpp
// 組み込み向け: 物理ノブ + LED + OLED

class HardwareController : public umi::Controller {
    // ハードウェアリソース
    Adc& adc_;
    GpioLed& leds_;
    OledDisplay& oled_;
    
    // パラメータマッピング
    static constexpr uint32_t KNOB_TO_PARAM[] = {
        PARAM_CUTOFF,    // Knob 0
        PARAM_RESONANCE, // Knob 1
        PARAM_ATTACK,    // Knob 2
        PARAM_RELEASE,   // Knob 3
    };
    
    std::array<uint16_t, 4> last_adc_{};
    
public:
    // ADC読み取り → パラメータ更新
    void poll_knobs() {
        for (size_t i = 0; i < 4; ++i) {
            uint16_t value = adc_.read(i);
            
            // ヒステリシス付き変化検出
            if (std::abs(int(value) - int(last_adc_[i])) > 16) {
                last_adc_[i] = value;
                float normalized = value / 4095.0f;
                set_param(KNOB_TO_PARAM[i], normalized);
            }
        }
    }
    
    // Processor → UI: パラメータ変更をLEDに反映
    void on_param_changed(uint32_t id, float value) override {
        if (id == PARAM_CUTOFF) {
            leds_.set_brightness(0, value);  // LED 0 の明るさ
        }
    }
    
    // Processor → UI: メーターをOLEDに表示
    void on_meter_update(std::span<const float> levels) override {
        oled_.draw_meter(0, 0, levels[0]);  // L
        oled_.draw_meter(64, 0, levels[1]); // R
    }
    
    // Processor → UI: 波形表示
    void on_waveform_data(std::span<const float> data) override {
        oled_.draw_waveform(0, 32, data);
    }
};
```

### 20.4 プラグインGUI実装例

```cpp
// VST3/CLAP向け: デスクトップGUI

class PluginGuiController : public umi::Controller {
    // GUIフレームワーク（JUCE, VSTGUI等）への参照
    GuiFramework& gui_;
    
public:
    void on_param_changed(uint32_t id, float value) override {
        // スレッドセーフにGUI更新をキュー
        gui_.post_async([=] {
            if (auto* knob = gui_.find_knob(id)) {
                knob->set_value(value);
            }
        });
    }
    
    void on_meter_update(std::span<const float> levels) override {
        gui_.post_async([levels = std::vector(levels.begin(), levels.end())] {
            gui_.update_meters(levels);
        });
    }
    
    // GUIからのコールバック
    void on_gui_knob_changed(uint32_t id, float value) {
        set_param(id, value);  // Processorへ通知
    }
};
```

### 22.5 表示データの生成

Processorがメーター/波形等の表示データを生成する方法:

```cpp
class MySynth : public umi::Processor {
    // 表示用データ（Control threadで読み取り）
    std::atomic<float> output_level_l_{0.0f};
    std::atomic<float> output_level_r_{0.0f};
    
    // 波形バッファ（ダブルバッファリング）
    std::array<float, 128> waveform_buffer_[2];
    std::atomic<int> waveform_write_index_{0};
    
public:
    void process(AudioContext& ctx) override {
        float peak_l = 0, peak_r = 0;
        
        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            // ... DSP処理 ...
            
            peak_l = std::max(peak_l, std::abs(ctx.outputs[0][i]));
            peak_r = std::max(peak_r, std::abs(ctx.outputs[1][i]));
        }
        
        // ピークをアトミックに更新
        output_level_l_.store(peak_l, std::memory_order_relaxed);
        output_level_r_.store(peak_r, std::memory_order_relaxed);
        
        // 波形データを間引いてコピー
        int idx = waveform_write_index_.load();
        auto& buf = waveform_buffer_[idx];
        for (size_t i = 0; i < 128; ++i) {
            buf[i] = ctx.outputs[0][i * ctx.buffer_size / 128];
        }
        waveform_write_index_.store(1 - idx);  // バッファ切り替え
    }
    
    void control(ControlContext& ctx) override {
        // Controllerへ表示データを送信
        if (controller_) {
            float levels[2] = {
                output_level_l_.load(std::memory_order_relaxed),
                output_level_r_.load(std::memory_order_relaxed)
            };
            controller_->on_meter_update(levels);
            
            int idx = 1 - waveform_write_index_.load();
            controller_->on_waveform_data(waveform_buffer_[idx]);
        }
    }
};
```

---

## 19. プリセット/バンク/パターン

### 21.1 階層構造

```
Library (ライブラリ全体)
  └── Bank[] (バンク: 音色のグループ)
        └── Preset[] (プリセット: パラメータスナップショット)
              └── Pattern[] (パターン: シーケンスデータ、オプション)
```

### 21.2 プリセット

プリセットは `save_state()` / `load_state()` で保存・復元される状態のスナップショット。

```cpp
// umi/preset.hpp

namespace umi {

struct PresetInfo {
    uint32_t id;
    std::string_view name;
    std::string_view author;
    std::string_view category;      // "Bass", "Lead", "Pad", etc.
    std::string_view tags;          // "analog, warm, fat"
    uint32_t bank_id;
};

class PresetManager {
public:
    // プリセット一覧
    std::span<const PresetInfo> list() const;
    std::span<const PresetInfo> list_by_bank(uint32_t bank_id) const;
    std::span<const PresetInfo> search(std::string_view query) const;
    
    // 読み込み/保存
    bool load(uint32_t preset_id);
    bool save(uint32_t preset_id);
    bool save_as(const PresetInfo& info);
    
    // 現在のプリセット
    std::optional<uint32_t> current() const;
    
    // バンク操作
    std::span<const BankInfo> banks() const;
    bool create_bank(std::string_view name);
    bool delete_bank(uint32_t bank_id);
    
private:
    Processor& processor_;
};

} // namespace umi
```

### 21.3 プリセットストレージ

```cpp
// 組み込み: Flash/EEPROMに保存
// プラグイン: ホストのプリセット機構を使用
// スタンドアロン: ファイルシステム

struct PresetStorage {
    virtual std::vector<uint8_t> read(uint32_t preset_id) = 0;
    virtual bool write(uint32_t preset_id, std::span<const uint8_t> data) = 0;
    virtual bool exists(uint32_t preset_id) = 0;
    virtual bool remove(uint32_t preset_id) = 0;
};

// Flash実装例
class FlashPresetStorage : public PresetStorage {
    static constexpr uint32_t PRESET_BASE = 0x08040000;  // Flash領域
    static constexpr uint32_t PRESET_SIZE = 4096;        // 1プリセット最大4KB
    // ...
};
```

### 21.4 パターン (UMI Pattern Format)

グリッドベースのシーケンスデータの標準フォーマット。

```cpp
// umi/pattern.hpp

namespace umi::pattern {

// ステップデータ
struct Step {
    uint8_t note;          // MIDIノート (0-127, 255=rest/tie)
    uint8_t velocity;      // ベロシティ (0-127)
    uint8_t length;        // ゲート長 (1-255 ステップ単位、0=legato/tie)
    uint8_t probability;   // 発音確率 (0-100)
    int8_t pitch_offset;   // ピッチオフセット (セミトーン単位、-48〜+48)
    uint8_t flags;         // ビットフラグ
    uint8_t reserved[2];
};

// Step::flags ビット定義
constexpr uint8_t STEP_ACCENT   = 0x01;  // アクセント
constexpr uint8_t STEP_SLIDE    = 0x02;  // スライド/ポルタメント
constexpr uint8_t STEP_LEGATO   = 0x04;  // レガート（ノートオフなし）
constexpr uint8_t STEP_RETRIG   = 0x08;  // リトリガー
constexpr uint8_t STEP_REVERSE  = 0x10;  // リバース再生
constexpr uint8_t STEP_MUTE     = 0x20;  // ミュート

// 特殊ノート値
constexpr uint8_t NOTE_REST = 255;  // 休符
constexpr uint8_t NOTE_TIE  = 254;  // タイ（前のノートを継続）

// オートメーションポイント
struct AutomationPoint {
    uint16_t position;     // ステップ位置 (固定小数点 8.8)
    uint16_t value;        // 値 (0-65535 → 0.0-1.0)
    uint8_t curve;         // カーブタイプ
    uint8_t reserved[3];
};

// AutomationPoint::curve 値
constexpr uint8_t CURVE_STEP   = 0;  // ステップ（即座に変化）
constexpr uint8_t CURVE_LINEAR = 1;  // リニア補間
constexpr uint8_t CURVE_EXP_IN = 2;  // Exponential in
constexpr uint8_t CURVE_EXP_OUT = 3; // Exponential out
constexpr uint8_t CURVE_SMOOTH = 4;  // S-curve

// オートメーショントラック
struct AutomationTrack {
    uint32_t param_id;
    std::vector<AutomationPoint> points;
};

// トラック
struct Track {
    uint32_t id;
    TrackType type;
    uint8_t channel;           // MIDIチャンネル (0-15)
    uint8_t root_note;         // ルートノート（トランスポーズ基準）
    std::vector<Step> steps;
    std::vector<AutomationTrack> automation;
};

enum class TrackType : uint8_t {
    Melodic,      // メロディ/ベースライン（単音）
    Polyphonic,   // 和音対応
    Drum,         // ドラム（ノート=楽器）
    Automation,   // オートメーション専用
};

// パターン
struct Pattern {
    uint32_t id;
    std::string name;
    
    uint16_t length_steps;     // パターン長（ステップ数）
    uint8_t steps_per_beat;    // 1拍あたりのステップ数 (4=16分音符)
    uint8_t time_sig_num;      // 拍子の分子
    uint8_t time_sig_denom;    // 拍子の分母
    
    float swing;               // スウィング量 (0.0-1.0, 0.5=なし)
    
    std::vector<Track> tracks;
};

// シリアライズ
std::vector<uint8_t> serialize(const Pattern& p);
std::expected<Pattern, Error> deserialize(std::span<const uint8_t> data);

// ファイルI/O
bool save_to_file(const Pattern& p, std::string_view path);
std::expected<Pattern, Error> load_from_file(std::string_view path);

} // namespace umi::pattern
```

### 21.5 パターン再生

```cpp
class PatternPlayer {
public:
    void set_pattern(const pattern::Pattern& p);
    void set_bpm(float bpm);
    
    // 再生制御
    void play();
    void stop();
    void pause();
    bool is_playing() const;
    
    // 位置
    uint32_t current_step() const;
    void set_position(uint32_t step);
    
    // process()内で呼ぶ: MIDIイベントを生成
    void generate_events(uint64_t sample_start, 
                         uint32_t sample_count,
                         uint32_t sample_rate,
                         EventQueue& out_events);
    
private:
    const pattern::Pattern* pattern_ = nullptr;
    float bpm_ = 120.0f;
    uint64_t position_samples_ = 0;
    bool playing_ = false;
};
```

### 21.6 パターンエディタ統合

```cpp
// パターン編集用のController拡張
class PatternEditorController : public Controller {
public:
    // パターンデータの送受信
    virtual void on_pattern_changed(const pattern::Pattern& p) {}
    
    void set_step(uint32_t track, uint32_t step, const pattern::Step& s);
    void clear_step(uint32_t track, uint32_t step);
    void set_track_length(uint32_t track, uint16_t length);
    
    // パターン全体の操作
    void copy_pattern(uint32_t src, uint32_t dst);
    void clear_pattern(uint32_t pattern_id);
    void randomize_pattern(uint32_t pattern_id, RandomizeParams params);
};
```

### 21.7 パターンファイルフォーマット

```
UMI Pattern File (.upf)

Header (16 bytes):
  magic: "UPF1"           4 bytes
  version: uint16         2 bytes
  flags: uint16           2 bytes
  pattern_count: uint32   4 bytes
  reserved: uint32        4 bytes

Pattern Entry:
  id: uint32
  name_length: uint16
  name: utf8[name_length]
  length_steps: uint16
  steps_per_beat: uint8
  time_sig_num: uint8
  time_sig_denom: uint8
  swing: float32
  track_count: uint16
  
  Track Entry:
    id: uint32
    type: uint8
    channel: uint8
    root_note: uint8
    step_count: uint16
    automation_count: uint16
    steps: Step[step_count]
    automation: AutomationTrack[automation_count]
```

---

## 20. バージョニングと互換性

### 20.1 バージョン形式

セマンティックバージョニングを採用する。

```cpp
struct Version {
    uint8_t major;  // 互換性なしの変更
    uint8_t minor;  // 後方互換の追加
    uint8_t patch;  // バグ修正
};
```

### 20.2 APIバージョン

```cpp
constexpr Version API_VERSION = {1, 0, 0};
```

### 20.3 互換性チェック

```cpp
bool is_compatible(Version app_requires, Version kernel_provides) {
    return app_requires.major == kernel_provides.major 
        && app_requires.minor <= kernel_provides.minor;
}
```

### 20.4 マニフェスト

アプリケーションは要件をマニフェストで宣言する。

```cpp
struct AppManifest {
    const char* app_id;
    Version app_version;
    Version min_api_version;
    Requirements requirements;
    const PortDescriptor* ports;
    uint32_t port_count;
    const ParamDescriptor* params;
    uint32_t param_count;
};
```

---

## 21. Processorライフサイクル

### 21.1 状態遷移

```
┌─────────────────────────────────────────────────────────────┐
│                                                             │
│    ┌──────────┐      ┌──────────┐      ┌──────────┐       │
│    │ Created  │─────▶│ Prepared │─────▶│ Running  │       │
│    └──────────┘      └──────────┘      └──────────┘       │
│         │                 │                 │              │
│         │                 │                 │              │
│         ▼                 ▼                 ▼              │
│    ┌─────────────────────────────────────────────┐        │
│    │              Destroyed                       │        │
│    └─────────────────────────────────────────────┘        │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 21.2 ライフサイクルコールバック

```cpp
class Processor {
public:
    // === 生成時（コンストラクタ後に呼ばれる） ===
    // 重い初期化はここで行う
    virtual void initialize() {}
    
    // === オーディオ開始前 ===
    // サンプルレート/バッファサイズが確定
    // バッファ確保等を行う
    virtual void prepare(const StreamConfig& config) {}
    
    // === オーディオ処理中 ===
    virtual void process(AudioContext& ctx) = 0;
    virtual void control(ControlContext& ctx) {}
    
    // === オーディオ停止後 ===
    // リソース解放（再度prepareが呼ばれる可能性あり）
    virtual void release() {}
    
    // === 破棄前（デストラクタ前に呼ばれる） ===
    virtual void terminate() {}
    
    // === デストラクタ ===
    virtual ~Processor() = default;
};
```

### 21.3 呼び出し順序

```cpp
// 組み込み環境での典型的な呼び出し順序
{
    // 1. 生成
    auto processor = create_processor();  // コンストラクタ
    processor->initialize();
    
    // 2. 準備
    StreamConfig config{48000, 256};
    processor->prepare(config);
    
    // 3. 実行ループ
    while (running) {
        // Audio thread
        processor->process(audio_ctx);
        
        // Control thread (並行)
        processor->control(control_ctx);
    }
    
    // 4. 停止
    processor->release();
    
    // 5. 破棄
    processor->terminate();
    // デストラクタ
}
```

### 21.4 再構成

サンプルレート/バッファサイズ変更時は以下の順序:

```cpp
processor->release();
processor->prepare(new_config);
// 処理再開
```

### 21.5 バッファ所有権

```cpp
// AudioContext内のバッファはカーネルが所有
// process()呼び出し中のみ有効
// process()終了後にポインタを保持してはならない

void MyProcessor::process(AudioContext& ctx) {
    // NG: ポインタを保存
    cached_buffer_ = ctx.outputs[0];  // 危険！
    
    // OK: 値をコピー
    for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
        ctx.outputs[0][i] = generate_sample();
    }
}
```

### 21.6 スレッド安全性

```cpp
// process(): Audio threadから呼ばれる
//   - 排他的（同時に1つのみ）
//   - ブロック禁止
//   - 動的確保禁止

// control(): Control threadから呼ばれる
//   - process()と並行実行される可能性あり
//   - ブロック可能
//   - 動的確保可能（推奨しない）

// 両方からアクセスするデータはアトミック操作またはロックフリー構造で保護
class MyProcessor : public Processor {
    std::atomic<float> target_gain_{1.0f};  // Control→Audio
    std::atomic<float> current_level_{0.0f}; // Audio→Control
    
    void process(AudioContext& ctx) override {
        float gain = target_gain_.load(std::memory_order_relaxed);
        // ...
        current_level_.store(level, std::memory_order_relaxed);
    }
    
    void control(ControlContext& ctx) override {
        float level = current_level_.load(std::memory_order_relaxed);
        // UIに表示など
    }
};
```

### 21.7 複数Processor対応

将来的な複数Processor対応のための設計指針:

```cpp
// ProcessorGraphはカーネル/アダプタが管理
// 個々のProcessorはグラフを意識しない
class ProcessorGraph {
public:
    // Processor追加
    ProcessorId add(std::unique_ptr<Processor> p);
    
    // 接続
    void connect(ProcessorId src, uint32_t src_port,
                 ProcessorId dst, uint32_t dst_port);
    
    // 処理順序はトポロジカルソートで自動決定
    void process(AudioContext& ctx);
};

// 現バージョン(0.1)では単一Processorのみサポート
// 複数Processor対応はバージョン0.2以降で検討
```

---

## 22. テストインフラ

### 22.1 TestRunner

オフラインテスト用ハーネスを提供する。

```cpp
class TestRunner {
public:
    TestRunner(std::unique_ptr<Processor> p, 
               uint32_t sample_rate = 48000,
               uint32_t buffer_size = 256);
    
    // 入力設定
    void set_input(uint32_t port, std::span<const float> mono);
    void set_input_stereo(uint32_t port, 
                          std::span<const float> l, 
                          std::span<const float> r);
    void push_event(const Event& e);
    void set_param(uint32_t id, float value);
    
    // 実行
    void process(uint32_t frames);
    
    // 出力取得
    std::span<const float> output(uint32_t port, uint32_t channel);
    std::vector<Event> output_events();
    
    // 検証ヘルパー
    float output_rms(uint32_t port);
    float output_peak(uint32_t port);
    bool is_silent(uint32_t port, float threshold = 1e-6f);
};
```

### 22.2 使用例

```cpp
TEST(MySynth, NoteOnProducesSound) {
    TestRunner t(std::make_unique<MySynth>());
    
    t.push_event(MidiEvent::note_on(0, 60, 0.8f).at_sample(0));
    t.process(4800);  // 100ms at 48kHz
    
    EXPECT_FALSE(t.is_silent(0));
    EXPECT_GT(t.output_rms(0), 0.01f);
}

TEST(MySynth, NoteOffStopsSound) {
    TestRunner t(std::make_unique<MySynth>());
    
    t.push_event(MidiEvent::note_on(0, 60, 0.8f).at_sample(0));
    t.process(4800);
    
    t.push_event(MidiEvent::note_off(0, 60).at_sample(0));
    t.process(48000);  // 1 second release
    
    EXPECT_TRUE(t.is_silent(0));
}
```

---

## 付録

### A. 公開APIリスト

#### A.1 ヘッダ一覧

```
umi/
├── processor.hpp      # Processor基底クラス、Context
├── types.hpp          # sample_t、基本型定義
├── fixed.hpp          # Q15、Q31 固定小数点型
├── event.hpp          # Event、EventQueue
├── param.hpp          # ParamDescriptor、ParamState
├── port.hpp           # PortDescriptor、PortKind、PortDirection、TypeHint
├── stream.hpp         # StreamConfig
├── assert.hpp         # umi::assert、umi::verify
├── log.hpp            # umi::log::trace/debug/info/warn/error
├── error.hpp          # Error enum、Result<T>
└── version.hpp        # Version、API_VERSION
```

#### A.2 umi/processor.hpp

```cpp
namespace umi {

struct StreamConfig {
    uint32_t sample_rate;
    uint32_t buffer_size;
};

struct AudioContext {
    std::span<const sample_t* const> inputs;
    std::span<sample_t* const> outputs;
    EventQueue& events;
    uint32_t sample_rate;
    uint32_t buffer_size;
    uint64_t sample_position;
};

struct ControlContext {
    EventQueue& events;
    ParamState& params;
};

class Processor {
public:
    virtual ~Processor() = default;
    
    // ライフサイクル
    virtual void initialize() {}
    virtual void prepare(const StreamConfig& config) {}
    virtual void process(AudioContext& ctx) = 0;
    virtual void control(ControlContext& ctx) {}
    virtual void release() {}
    virtual void terminate() {}
    
    // メタデータ
    virtual std::span<const PortDescriptor> ports() { return {}; }
    virtual std::span<const ParamDescriptor> params() { return {}; }
    virtual Requirements requirements() { return {}; }
    
    // 状態保存/復元
    virtual std::vector<uint8_t> save_state() { return {}; }
    virtual void load_state(std::span<const uint8_t>) {}
};

} // namespace umi
```

#### A.3 umi/types.hpp

```cpp
namespace umi {

#if defined(UMI_SAMPLE_FLOAT)
    using sample_t = float;
#elif defined(UMI_SAMPLE_Q15)
    using sample_t = Q15;
#elif defined(UMI_SAMPLE_Q31)
    using sample_t = Q31;
#endif

constexpr sample_t SAMPLE_MAX = /* ... */;
constexpr sample_t SAMPLE_MIN = /* ... */;

} // namespace umi
```

#### A.4 umi/event.hpp

```cpp
namespace umi {

enum class EventType : uint8_t { Midi, Param, Raw };

struct MidiData {
    uint8_t bytes[3];
    uint8_t size;
};

struct ParamData {
    uint32_t id;
    float value;
};

struct Event {
    uint32_t port_id;
    uint32_t sample_pos;
    EventType type;
    union {
        MidiData midi;
        ParamData param;
    };
};

class EventQueue {
public:
    bool pop(Event& out);
    bool pop_until(uint32_t sample_pos, Event& out);
    bool push(const Event& e);
    bool push_midi(uint32_t port, uint32_t sample_pos,
                   uint8_t status, uint8_t d1, uint8_t d2 = 0);
    void clear();
};

// ヘルパー
struct MidiEvent {
    static MidiEvent note_on(uint8_t ch, uint8_t note, float velocity);
    static MidiEvent note_off(uint8_t ch, uint8_t note);
    static MidiEvent cc(uint8_t ch, uint8_t cc, uint8_t value);
    MidiEvent at_sample(uint32_t pos) const;
};

} // namespace umi
```

#### A.5 umi/param.hpp

```cpp
namespace umi {

enum class Unit { None, Hz, dB, Ms, Percent, Semitones, Octaves };
enum class Scale { Linear, Log, Exp };

struct ParamDescriptor {
    uint32_t id;
    std::string_view name;
    float min;
    float max;
    float default_value;
    Unit unit;
    Scale scale;
};

class ParamState {
public:
    float get(uint32_t id) const;
    void set(uint32_t id, float value);
    bool has_changed(uint32_t id) const;
    void clear_changes();
};

} // namespace umi
```

#### A.6 umi/port.hpp

```cpp
namespace umi {

enum class PortKind { Continuous, Event };
enum class PortDirection { In, Out };

enum class TypeHint : uint16_t {
    Unknown     = 0x0000,
    MidiBytes   = 0x0100,
    MidiSysex   = 0x0102,
    ParamChange = 0x0200,
    Osc         = 0x0300,
    Clock       = 0x0400,
    Transport   = 0x0401,
};

struct PortDescriptor {
    uint32_t id;
    std::string_view name;
    PortKind kind;
    PortDirection dir;
    uint32_t channels;           // Continuous用
    TypeHint expected_type;      // Event用
};

} // namespace umi
```

#### A.7 umi/assert.hpp

```cpp
namespace umi {

constexpr void assert(
    bool cond,
    std::source_location loc = std::source_location::current()
);

template<typename... Args>
constexpr void verify(
    bool cond,
    std::format_string<Args...> fmt,
    Args&&... args,
    std::source_location loc = std::source_location::current()
);

} // namespace umi
```

#### A.8 umi/log.hpp

```cpp
namespace umi::log {

enum class Level : uint8_t { Trace, Debug, Info, Warn, Error };

template<typename... Args> void trace(std::format_string<Args...> fmt, Args&&...);
template<typename... Args> void debug(std::format_string<Args...> fmt, Args&&...);
template<typename... Args> void info(std::format_string<Args...> fmt, Args&&...);
template<typename... Args> void warn(std::format_string<Args...> fmt, Args&&...);
template<typename... Args> void error(std::format_string<Args...> fmt, Args&&...);

} // namespace umi::log
```

#### A.9 umi/error.hpp

```cpp
namespace umi {

enum class Error : int32_t {
    Ok = 0,
    Unknown, InvalidArgument, InvalidState,
    OutOfMemory, Timeout, Busy,
    CapabilityDenied, IoError, BufferOverflow,
    // ... 完全なリストは本文参照
};

template<typename T>
class Result {
public:
    bool ok() const;
    T& value();
    Error error() const;
    
    template<typename F> auto map(F&& f);
    template<typename F> auto and_then(F&& f);
};

} // namespace umi
```

### B. サンプルアプリケーション

#### B.1 シンプルシンセサイザー

モノフォニックサイン波シンセ。MIDIノート入力に応答。

```cpp
// examples/simple_synth/simple_synth.hpp
#pragma once
#include <umi/processor.hpp>
#include <umi/assert.hpp>
#include <umi/log.hpp>
#include <cmath>

namespace examples {

class SimpleSynth : public umi::Processor {
    // DSP状態
    float phase_ = 0.0f;
    float freq_ = 440.0f;
    float velocity_ = 0.0f;
    float sample_rate_ = 48000.0f;
    
    // エンベロープ（簡易）
    float env_ = 0.0f;
    float attack_coef_ = 0.0f;
    float release_coef_ = 0.0f;
    bool note_on_ = false;
    
public:
    void initialize() override {
        umi::log::info("SimpleSynth initialized");
    }
    
    void prepare(const umi::StreamConfig& config) override {
        sample_rate_ = static_cast<float>(config.sample_rate);
        // 10ms attack, 200ms release
        attack_coef_ = 1.0f - std::exp(-1.0f / (0.010f * sample_rate_));
        release_coef_ = 1.0f - std::exp(-1.0f / (0.200f * sample_rate_));
    }
    
    void process(umi::AudioContext& ctx) override {
        umi::assert(ctx.buffer_size <= 1024);
        
        umi::sample_t* out_l = ctx.outputs[0];
        umi::sample_t* out_r = ctx.outputs[1];
        
        // イベント処理
        umi::Event e;
        while (ctx.events.pop(e)) {
            if (e.type == umi::EventType::Midi) {
                handle_midi(e.midi);
            }
        }
        
        // オーディオ生成
        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            // エンベロープ
            float target = note_on_ ? velocity_ : 0.0f;
            float coef = note_on_ ? attack_coef_ : release_coef_;
            env_ += (target - env_) * coef;
            
            // オシレータ
            float sample = std::sin(phase_ * 6.283185307f) * env_;
            out_l[i] = out_r[i] = sample;
            
            phase_ += freq_ / sample_rate_;
            if (phase_ >= 1.0f) phase_ -= 1.0f;
        }
    }
    
    std::span<const umi::PortDescriptor> ports() override {
        static constexpr umi::PortDescriptor p[] = {
            {0, "Audio Out", umi::PortKind::Continuous, umi::PortDirection::Out, 2},
            {1, "MIDI In", umi::PortKind::Event, umi::PortDirection::In, 0, 
             umi::TypeHint::MidiBytes},
        };
        return p;
    }
    
    std::span<const umi::ParamDescriptor> params() override {
        return {};  // パラメータなし
    }
    
private:
    void handle_midi(const umi::MidiData& midi) {
        uint8_t status = midi.bytes[0] & 0xF0;
        uint8_t note = midi.bytes[1];
        uint8_t vel = midi.bytes[2];
        
        if (status == 0x90 && vel > 0) {
            // Note On
            freq_ = 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
            velocity_ = vel / 127.0f;
            note_on_ = true;
            umi::log::debug("Note On: {} vel={}", note, vel);
        } else if (status == 0x80 || (status == 0x90 && vel == 0)) {
            // Note Off
            note_on_ = false;
            umi::log::debug("Note Off: {}", note);
        }
    }
};

} // namespace examples
```

#### B.2 ステレオディレイ

ピンポンディレイエフェクト。

```cpp
// examples/stereo_delay/stereo_delay.hpp
#pragma once
#include <umi/processor.hpp>
#include <umi/assert.hpp>
#include <array>

namespace examples {

class StereoDelay : public umi::Processor {
    static constexpr size_t MAX_DELAY = 96000;  // 2秒 @ 48kHz
    
    std::array<float, MAX_DELAY> buffer_l_{};
    std::array<float, MAX_DELAY> buffer_r_{};
    size_t write_pos_ = 0;
    size_t delay_samples_ = 24000;
    
    float feedback_ = 0.5f;
    float mix_ = 0.5f;
    float sample_rate_ = 48000.0f;
    
    // パラメータID
    static constexpr uint32_t PARAM_TIME = 0;
    static constexpr uint32_t PARAM_FEEDBACK = 1;
    static constexpr uint32_t PARAM_MIX = 2;
    
public:
    void prepare(const umi::StreamConfig& config) override {
        sample_rate_ = static_cast<float>(config.sample_rate);
        buffer_l_.fill(0.0f);
        buffer_r_.fill(0.0f);
        write_pos_ = 0;
    }
    
    void process(umi::AudioContext& ctx) override {
        const umi::sample_t* in_l = ctx.inputs[0];
        const umi::sample_t* in_r = ctx.inputs[1];
        umi::sample_t* out_l = ctx.outputs[0];
        umi::sample_t* out_r = ctx.outputs[1];
        
        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            // 読み出し位置
            size_t read_pos = (write_pos_ + MAX_DELAY - delay_samples_) % MAX_DELAY;
            
            // ディレイ読み出し（クロスフィードバックでピンポン）
            float delay_l = buffer_r_[read_pos];  // R→L
            float delay_r = buffer_l_[read_pos];  // L→R
            
            // 書き込み（入力 + フィードバック）
            buffer_l_[write_pos_] = in_l[i] + delay_l * feedback_;
            buffer_r_[write_pos_] = in_r[i] + delay_r * feedback_;
            
            // 出力（ドライ/ウェット ミックス）
            out_l[i] = in_l[i] * (1.0f - mix_) + delay_l * mix_;
            out_r[i] = in_r[i] * (1.0f - mix_) + delay_r * mix_;
            
            write_pos_ = (write_pos_ + 1) % MAX_DELAY;
        }
    }
    
    void control(umi::ControlContext& ctx) override {
        if (ctx.params.has_changed(PARAM_TIME)) {
            float time_ms = ctx.params.get(PARAM_TIME);
            delay_samples_ = static_cast<size_t>(time_ms * 0.001f * sample_rate_);
            if (delay_samples_ >= MAX_DELAY) delay_samples_ = MAX_DELAY - 1;
        }
        if (ctx.params.has_changed(PARAM_FEEDBACK)) {
            feedback_ = ctx.params.get(PARAM_FEEDBACK);
        }
        if (ctx.params.has_changed(PARAM_MIX)) {
            mix_ = ctx.params.get(PARAM_MIX);
        }
        ctx.params.clear_changes();
    }
    
    std::span<const umi::PortDescriptor> ports() override {
        static constexpr umi::PortDescriptor p[] = {
            {0, "Audio In", umi::PortKind::Continuous, umi::PortDirection::In, 2},
            {1, "Audio Out", umi::PortKind::Continuous, umi::PortDirection::Out, 2},
        };
        return p;
    }
    
    std::span<const umi::ParamDescriptor> params() override {
        static constexpr umi::ParamDescriptor p[] = {
            {PARAM_TIME, "Time", 10.0f, 2000.0f, 500.0f, umi::Unit::Ms, umi::Scale::Log},
            {PARAM_FEEDBACK, "Feedback", 0.0f, 0.95f, 0.5f, umi::Unit::Percent, umi::Scale::Linear},
            {PARAM_MIX, "Mix", 0.0f, 1.0f, 0.5f, umi::Unit::Percent, umi::Scale::Linear},
        };
        return p;
    }
};

} // namespace examples
```

#### B.3 MIDIコントローラー

フィジカルコントローラーの例。ノブ/ボタン入力をMIDI CC/Noteに変換。

```cpp
// examples/midi_controller/midi_controller.hpp
#pragma once
#include <umi/processor.hpp>
#include <umi/log.hpp>
#include <array>

namespace examples {

class MidiController : public umi::Processor {
    static constexpr size_t NUM_KNOBS = 8;
    static constexpr size_t NUM_BUTTONS = 8;
    
    // 前回値（変化検出用）
    std::array<uint8_t, NUM_KNOBS> knob_values_{};
    std::array<bool, NUM_BUTTONS> button_states_{};
    
    // 設定
    uint8_t midi_channel_ = 0;
    std::array<uint8_t, NUM_KNOBS> knob_cc_ = {1, 2, 3, 4, 5, 6, 7, 8};
    std::array<uint8_t, NUM_BUTTONS> button_notes_ = {36, 37, 38, 39, 40, 41, 42, 43};
    
public:
    void initialize() override {
        umi::log::info("MidiController: {} knobs, {} buttons", 
                       NUM_KNOBS, NUM_BUTTONS);
    }
    
    void process(umi::AudioContext& ctx) override {
        // このProcessorはオーディオ処理なし
        // Control threadで入力処理
    }
    
    void control(umi::ControlContext& ctx) override {
        // ノブ入力処理（ADC値が params 経由で来ると仮定）
        for (size_t i = 0; i < NUM_KNOBS; ++i) {
            if (ctx.params.has_changed(i)) {
                float value = ctx.params.get(i);
                uint8_t cc_value = static_cast<uint8_t>(value * 127.0f);
                
                if (cc_value != knob_values_[i]) {
                    knob_values_[i] = cc_value;
                    send_cc(ctx.events, knob_cc_[i], cc_value);
                }
            }
        }
        
        // ボタン入力処理
        for (size_t i = 0; i < NUM_BUTTONS; ++i) {
            uint32_t param_id = NUM_KNOBS + i;
            if (ctx.params.has_changed(param_id)) {
                bool pressed = ctx.params.get(param_id) > 0.5f;
                
                if (pressed != button_states_[i]) {
                    button_states_[i] = pressed;
                    if (pressed) {
                        send_note_on(ctx.events, button_notes_[i], 100);
                    } else {
                        send_note_off(ctx.events, button_notes_[i]);
                    }
                }
            }
        }
        
        ctx.params.clear_changes();
    }
    
    std::span<const umi::PortDescriptor> ports() override {
        static constexpr umi::PortDescriptor p[] = {
            {0, "MIDI Out", umi::PortKind::Event, umi::PortDirection::Out, 0,
             umi::TypeHint::MidiBytes},
        };
        return p;
    }
    
    std::span<const umi::ParamDescriptor> params() override {
        // 動的に生成するか、静的に定義
        static std::array<umi::ParamDescriptor, NUM_KNOBS + NUM_BUTTONS> p;
        static bool initialized = false;
        
        if (!initialized) {
            for (size_t i = 0; i < NUM_KNOBS; ++i) {
                p[i] = {static_cast<uint32_t>(i), "Knob", 0.0f, 1.0f, 0.0f,
                        umi::Unit::None, umi::Scale::Linear};
            }
            for (size_t i = 0; i < NUM_BUTTONS; ++i) {
                p[NUM_KNOBS + i] = {static_cast<uint32_t>(NUM_KNOBS + i), 
                                    "Button", 0.0f, 1.0f, 0.0f,
                                    umi::Unit::None, umi::Scale::Linear};
            }
            initialized = true;
        }
        return p;
    }
    
private:
    void send_cc(umi::EventQueue& q, uint8_t cc, uint8_t value) {
        q.push_midi(0, 0, 0xB0 | midi_channel_, cc, value);
        umi::log::trace("CC {}: {}", cc, value);
    }
    
    void send_note_on(umi::EventQueue& q, uint8_t note, uint8_t vel) {
        q.push_midi(0, 0, 0x90 | midi_channel_, note, vel);
        umi::log::debug("Note On: {}", note);
    }
    
    void send_note_off(umi::EventQueue& q, uint8_t note) {
        q.push_midi(0, 0, 0x80 | midi_channel_, note, 0);
        umi::log::debug("Note Off: {}", note);
    }
};

} // namespace examples
```

#### B.4 マルチエフェクト（Processor連結の例）

複数のエフェクトを直列に接続する例。

```cpp
// examples/multi_fx/multi_fx.hpp
#pragma once
#include <umi/processor.hpp>
#include <memory>
#include <array>

namespace examples {

// 単体エフェクトのインターフェース
// バッファ単位の処理なので仮想関数OK（サンプル単位ではない）
class FxUnit {
public:
    virtual ~FxUnit() = default;
    virtual void prepare(float sample_rate) = 0;
    virtual void process(float* l, float* r, size_t count) = 0;  // バッファ単位
    virtual void set_param(uint32_t id, float value) = 0;
};

// フィルタ
class Filter : public FxUnit {
    float cutoff_ = 1000.0f;
    float resonance_ = 0.5f;
    float sample_rate_ = 48000.0f;
    float lp_l_ = 0, bp_l_ = 0;
    float lp_r_ = 0, bp_r_ = 0;
    
public:
    void prepare(float sr) override { sample_rate_ = sr; }
    
    void process(float* l, float* r, size_t count) override {
        float f = 2.0f * std::sin(3.14159f * cutoff_ / sample_rate_);
        float q = 1.0f - resonance_ * 0.9f;
        
        for (size_t i = 0; i < count; ++i) {
            // State Variable Filter
            lp_l_ += f * bp_l_;
            float hp_l = l[i] - lp_l_ - q * bp_l_;
            bp_l_ += f * hp_l;
            l[i] = lp_l_;
            
            lp_r_ += f * bp_r_;
            float hp_r = r[i] - lp_r_ - q * bp_r_;
            bp_r_ += f * hp_r;
            r[i] = lp_r_;
        }
    }
    
    void set_param(uint32_t id, float value) override {
        if (id == 0) cutoff_ = value;
        else if (id == 1) resonance_ = value;
    }
};

// ドライブ
class Drive : public FxUnit {
    float amount_ = 0.5f;
    
public:
    void prepare(float) override {}
    
    void process(float* l, float* r, size_t count) override {
        float gain = 1.0f + amount_ * 10.0f;
        for (size_t i = 0; i < count; ++i) {
            l[i] = std::tanh(l[i] * gain);
            r[i] = std::tanh(r[i] * gain);
        }
    }
    
    void set_param(uint32_t id, float value) override {
        if (id == 0) amount_ = value;
    }
};

// マルチエフェクトProcessor
class MultiFx : public umi::Processor {
    std::array<std::unique_ptr<FxUnit>, 2> units_;
    std::array<float, 1024> temp_l_{};
    std::array<float, 1024> temp_r_{};
    
public:
    void initialize() override {
        units_[0] = std::make_unique<Filter>();
        units_[1] = std::make_unique<Drive>();
    }
    
    void prepare(const umi::StreamConfig& config) override {
        float sr = static_cast<float>(config.sample_rate);
        for (auto& u : units_) {
            if (u) u->prepare(sr);
        }
    }
    
    void process(umi::AudioContext& ctx) override {
        umi::assert(ctx.buffer_size <= temp_l_.size());
        
        // 入力をコピー
        std::copy_n(ctx.inputs[0], ctx.buffer_size, temp_l_.begin());
        std::copy_n(ctx.inputs[1], ctx.buffer_size, temp_r_.begin());
        
        // 直列処理
        for (auto& u : units_) {
            if (u) {
                u->process(temp_l_.data(), temp_r_.data(), ctx.buffer_size);
            }
        }
        
        // 出力にコピー
        std::copy_n(temp_l_.begin(), ctx.buffer_size, ctx.outputs[0]);
        std::copy_n(temp_r_.begin(), ctx.buffer_size, ctx.outputs[1]);
    }
    
    void control(umi::ControlContext& ctx) override {
        // パラメータをルーティング
        // 0-1: Filter, 2: Drive
        if (ctx.params.has_changed(0)) 
            units_[0]->set_param(0, ctx.params.get(0));
        if (ctx.params.has_changed(1)) 
            units_[0]->set_param(1, ctx.params.get(1));
        if (ctx.params.has_changed(2)) 
            units_[1]->set_param(0, ctx.params.get(2));
        ctx.params.clear_changes();
    }
    
    std::span<const umi::PortDescriptor> ports() override {
        static constexpr umi::PortDescriptor p[] = {
            {0, "In", umi::PortKind::Continuous, umi::PortDirection::In, 2},
            {1, "Out", umi::PortKind::Continuous, umi::PortDirection::Out, 2},
        };
        return p;
    }
    
    std::span<const umi::ParamDescriptor> params() override {
        static constexpr umi::ParamDescriptor p[] = {
            {0, "Filter Freq", 20.0f, 20000.0f, 1000.0f, umi::Unit::Hz, umi::Scale::Log},
            {1, "Filter Reso", 0.0f, 1.0f, 0.5f, umi::Unit::Percent, umi::Scale::Linear},
            {2, "Drive", 0.0f, 1.0f, 0.5f, umi::Unit::Percent, umi::Scale::Linear},
        };
        return p;
    }
};

} // namespace examples
```

### C. MIDIイベントヘルパー

```cpp
namespace umi {

struct MidiEvent {
    MidiData data;
    uint32_t sample_pos = 0;
    
    static MidiEvent note_on(uint8_t ch, uint8_t note, float velocity) {
        MidiEvent e;
        e.data.bytes[0] = 0x90 | (ch & 0x0F);
        e.data.bytes[1] = note & 0x7F;
        e.data.bytes[2] = uint8_t(velocity * 127.f) & 0x7F;
        e.data.size = 3;
        return e;
    }
    
    static MidiEvent note_off(uint8_t ch, uint8_t note) {
        MidiEvent e;
        e.data.bytes[0] = 0x80 | (ch & 0x0F);
        e.data.bytes[1] = note & 0x7F;
        e.data.bytes[2] = 0;
        e.data.size = 3;
        return e;
    }
    
    static MidiEvent cc(uint8_t ch, uint8_t cc_num, uint8_t value) {
        MidiEvent e;
        e.data.bytes[0] = 0xB0 | (ch & 0x0F);
        e.data.bytes[1] = cc_num & 0x7F;
        e.data.bytes[2] = value & 0x7F;
        e.data.size = 3;
        return e;
    }
    
    MidiEvent at_sample(uint32_t pos) const {
        MidiEvent e = *this;
        e.sample_pos = pos;
        return e;
    }
};

} // namespace umi
```

### D. 単位変換ユーティリティ

```cpp
namespace umi::units {

// dB ↔ リニア
inline float db_to_linear(float db) {
    return std::pow(10.f, db / 20.f);
}

inline float linear_to_db(float linear) {
    return 20.f * std::log10(std::max(linear, 1e-10f));
}

// Hz ↔ MIDIノート
inline float hz_to_note(float hz) {
    return 69.f + 12.f * std::log2(hz / 440.f);
}

inline float note_to_hz(float note) {
    return 440.f * std::pow(2.f, (note - 69.f) / 12.f);
}

// 秒 ↔ サンプル
inline uint32_t sec_to_samples(float sec, uint32_t sample_rate) {
    return uint32_t(sec * sample_rate);
}

inline float samples_to_sec(uint32_t samples, uint32_t sample_rate) {
    return float(samples) / float(sample_rate);
}

} // namespace umi::units
```

### E. ISP SysExフォーマット詳細

```
UMI-OS ISP (Instrument Service Protocol) SysExメッセージ構造:

┌────┬───────────────┬────────┬─────────┬─────┐
│ F0 │ Manufacturer  │ Header │ Payload │ F7  │
│    │ ID (1-3 byte) │(5 byte)│(N bytes)│     │
└────┴───────────────┴────────┴─────────┴─────┘

Manufacturer ID:
  - 0x00 0x21 0x?? : 3バイト形式（未取得の場合）
  - 0x7D         : 教育/研究用（非商用）

Header (5 bytes):
  byte 0: version (0x01)
  byte 1: device_id (0x00-0x7E: specific, 0x7F: broadcast)
  byte 2: transaction_id (0x00-0x7F)
  byte 3: message_type
  byte 4: command

Payload:
  コマンドにより可変
  8bitデータは7bitエンコード済み
```

---

## 改訂履歴

| バージョン | 日付 | 変更内容 |
|------------|------|----------|
| 0.1.11 | 2025-01 | I/Oモデル追加（仮想/物理I/O分離、標準ステレオ構成、自動変換）、UI/Controller分離、プリセット/バンク/パターン仕様（UPF形式） |
| 0.1.10 | 2025-01 | PAL chrono実装修正（clock_gettime使用）、AudioContext API統一（inputs/outputs形式）、属性のポータビリティ改善 |
| 0.1.9 | 2025-01 | 時間管理仕様追加（サンプル時間/システム時間の使い分け、変換ユーティリティ、chrono実装、BPM同期シーケンサー例） |
| 0.1.8 | 2025-01 | 公開APIリスト追加、サンプルアプリケーション追加（シンセ、ディレイ、MIDIコントローラー、マルチエフェクト） |
| 0.1.7 | 2025-01 | ログ・アサート仕様追加（層ごとの使用方針、umi::assert/log API、バックエンド切り替え、RTT互換プロトコル） |
| 0.1.6 | 2025-01 | C++23言語方針追加、メモリ管理方針追加、ISP統計情報仕様追加（Stats購読、スタック/ヒープ/CPU監視） |
| 0.1.5 | 2025-01 | DSP部品設計指針追加（仮想関数の使用基準、推奨パターン、ランタイム切り替え） |
| 0.1.4 | 2025-01 | Audio Runner拡充（トリプルバッファリング、ドロップ検出・同期維持、外部同期対応、Watchdog構成）、System Monitorを統計収集に特化 |
| 0.1.3 | 2025-01 | 数値表現セクション再構成（移植容易性の観点で整理、共通化範囲の明確化、DSP分離パターン） |
| 0.1.2 | 2025-01 | 数値表現セクション拡充（float/fixed版の詳細、DSPユーティリティ） |
| 0.1.1 | 2025-01 | ケーパビリティモデル追加、エラー処理体系追加、Processorライフサイクル追加、SpscQueue詳細実装、ParamState定義、TypeHint完全列挙 |
| 0.1.0 | 2025-01 | 初版ドラフト、UMI-OS命名、UMI-RTOS仕様追加 |

---

*UMI-OS (Universal Musical Instruments Operating System)*  
*本仕様書は継続的に更新される。最新版はリポジトリを参照のこと。*