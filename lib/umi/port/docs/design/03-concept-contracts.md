# 03: Concept 契約

## 概要

各レイヤーの同名ヘッダが提供すべきインターフェースを **C++23 Concept** で形式的に定義する。
これにより:

- 新しいバックエンド実装が契約を満たしているか**コンパイル時に検証**できる
- 各レイヤーが何を提供すべきかのドキュメントとして機能する
- プロジェクト既存のパターン（`Hal`, `AudioBridge`, `BlockDeviceLike`等）と一貫性を保つ

Concept定義は `lib/umiport/concepts/` に配置し、全ターゲットから参照可能とする。
各実装ヘッダでは `static_assert` でコンパイル時検証を行う。

```
lib/umiport/concepts/
  ├── arch_concepts.hh     arch/ レイヤーの契約
  ├── mcu_concepts.hh      mcu/ レイヤーの契約
  └── board_concepts.hh    board/ レイヤーの契約
```

## arch/ の契約

```cpp
// concepts/arch_concepts.hh
namespace umi::concepts {

/// キャッシュ操作
template<typename T>
concept CacheOps = requires {
    { T::enable_icache() } -> std::same_as<void>;
    { T::enable_dcache() } -> std::same_as<void>;
    { T::invalidate_dcache() } -> std::same_as<void>;
    { T::clean_dcache() } -> std::same_as<void>;
    { T::clean_invalidate_dcache_by_addr(std::declval<volatile void*>(),
                                          uint32_t{}) } -> std::same_as<void>;
};

/// FPU制御
template<typename T>
concept FpuOps = requires {
    { T::enable() } -> std::same_as<void>;
    /// FPUコンテキストのスタック必要サイズ（バイト）
    { T::context_size } -> std::convertible_to<uint32_t>;
};

/// コンテキストスイッチ
template<typename T>
concept ContextSwitch = requires(uint32_t* stack_ptr) {
    { T::init_stack(stack_ptr, std::declval<void(*)()>(),
                    uint32_t{}) } -> std::convertible_to<uint32_t*>;
    { T::trigger_switch() } -> std::same_as<void>;
};

/// アーキテクチャ特性定数
template<typename T>
concept ArchTraits = requires {
    { T::mpu_regions } -> std::convertible_to<uint32_t>;
    { T::has_dcache } -> std::convertible_to<bool>;
    { T::has_icache } -> std::convertible_to<bool>;
    { T::has_double_precision_fpu } -> std::convertible_to<bool>;
};

} // namespace umi::concepts
```

**実装例:**

```cpp
// arch/cm4/arch/cache.hh
namespace umi::arch {
struct Cache {
    static void enable_icache() {}                      // M4: no-op
    static void enable_dcache() {}
    static void invalidate_dcache() {}
    static void clean_dcache() {}
    static void clean_invalidate_dcache_by_addr(volatile void*, uint32_t) {}
};
} // namespace umi::arch
static_assert(umi::concepts::CacheOps<umi::arch::Cache>);

// arch/cm7/arch/cache.hh
namespace umi::arch {
struct Cache {
    static void enable_icache()    { /* SCB->ICIALLU ... */ }
    static void enable_dcache()    { /* SCB->DCISW ... */ }
    static void invalidate_dcache() { /* ... */ }
    static void clean_dcache()     { /* ... */ }
    static void clean_invalidate_dcache_by_addr(volatile void* addr, uint32_t size) { /* ... */ }
};
} // namespace umi::arch
static_assert(umi::concepts::CacheOps<umi::arch::Cache>);
```

呼び出し側は `#include <arch/cache.hh>` だけ書き、`umi::arch::Cache::enable_dcache()` を呼ぶ。
M4ビルドではno-op、M7ビルドでは実際のキャッシュ操作になる。

## mcu/ の契約

```cpp
// concepts/mcu_concepts.hh
namespace umi::concepts {

/// GPIO制御
template<typename T>
concept GpioPort = requires(T& gpio, uint8_t pin, uint8_t af) {
    { gpio.set(pin) } -> std::same_as<void>;
    { gpio.clear(pin) } -> std::same_as<void>;
    { gpio.read(pin) } -> std::convertible_to<bool>;
    { gpio.config_output(pin) } -> std::same_as<void>;
    { gpio.config_input(pin) } -> std::same_as<void>;
    { gpio.config_af(pin, af) } -> std::same_as<void>;
};

/// クロック制御
template<typename T>
concept ClockControl = requires {
    { T::init() } -> std::same_as<void>;
    { T::sysclk_hz } -> std::convertible_to<uint32_t>;
    { T::ahb_hz } -> std::convertible_to<uint32_t>;
    { T::apb1_hz } -> std::convertible_to<uint32_t>;
    { T::apb2_hz } -> std::convertible_to<uint32_t>;
};

/// DMAストリーム
template<typename T>
concept DmaStream = requires(T& dma, volatile void* periph_addr,
                             void* mem0, void* mem1, uint32_t count) {
    { dma.configure_m2p(periph_addr, mem0, mem1, count) } -> std::same_as<void>;
    { dma.configure_p2m(periph_addr, mem0, mem1, count) } -> std::same_as<void>;
    { dma.enable() } -> std::same_as<void>;
    { dma.disable() } -> std::same_as<void>;
    { dma.set_half_complete_callback(std::declval<void(*)()>()) } -> std::same_as<void>;
    { dma.set_complete_callback(std::declval<void(*)()>()) } -> std::same_as<void>;
};

/// オーディオバスインターフェース（I2S / SAI 共通）
template<typename T>
concept AudioBus = requires(T& bus) {
    { bus.init_tx() } -> std::same_as<void>;
    { bus.init_rx() } -> std::same_as<void>;
    { bus.enable() } -> std::same_as<void>;
    { bus.disable() } -> std::same_as<void>;
    { bus.sample_rate() } -> std::convertible_to<uint32_t>;
};

/// I2Cバス
template<typename T>
concept I2cBus = requires(T& i2c, uint8_t addr, uint8_t reg, uint8_t val,
                          uint8_t* buf, uint32_t len) {
    { i2c.init() } -> std::same_as<void>;
    { i2c.write_reg(addr, reg, val) } -> std::convertible_to<bool>;
    { i2c.read_reg(addr, reg, buf, len) } -> std::convertible_to<bool>;
};

} // namespace umi::concepts
```

F4の `I2s` と H7の `Sai` は内部実装が全く異なるが、
両方とも `AudioBus` Conceptを満たせばカーネル側からは同一のAPIで扱える。

## board/ の契約

```cpp
// concepts/board_concepts.hh
namespace umi::concepts {

/// ボード仕様定数
template<typename T>
concept BoardSpec = requires {
    { T::cpu_freq } -> std::convertible_to<uint32_t>;
    { T::audio_sample_rate } -> std::convertible_to<uint32_t>;
    { T::audio_buffer_frames } -> std::convertible_to<uint32_t>;
    { T::audio_channels_in } -> std::convertible_to<uint32_t>;
    { T::audio_channels_out } -> std::convertible_to<uint32_t>;
};

/// コーデック初期化
template<typename T>
concept Codec = requires(T& codec) {
    { codec.init() } -> std::convertible_to<bool>;
    { codec.reset() } -> std::same_as<void>;
};

/// MCU初期化（ボード層が提供）
template<typename T>
concept McuInit = requires(T& m) {
    { m.init_clocks() } -> std::same_as<void>;
    { m.init_gpio() } -> std::same_as<void>;
    { m.init_audio() } -> std::same_as<void>;
};

} // namespace umi::concepts
```

**実装例:**

```cpp
// board/daisy_seed/board/bsp.hh
namespace umi::board {
struct Spec {
    static constexpr uint32_t cpu_freq = 480'000'000;
    static constexpr uint32_t audio_sample_rate = 48'000;
    static constexpr uint32_t audio_buffer_frames = 64;
    static constexpr uint32_t audio_channels_in = 2;
    static constexpr uint32_t audio_channels_out = 2;
};
} // namespace umi::board
static_assert(umi::concepts::BoardSpec<umi::board::Spec>);

// board/stm32f4_disco/board/bsp.hh
namespace umi::board {
struct Spec {
    static constexpr uint32_t cpu_freq = 168'000'000;
    static constexpr uint32_t audio_sample_rate = 48'000;
    static constexpr uint32_t audio_buffer_frames = 64;
    static constexpr uint32_t audio_channels_in = 2;
    static constexpr uint32_t audio_channels_out = 2;
};
} // namespace umi::board
static_assert(umi::concepts::BoardSpec<umi::board::Spec>);
```

## Concept検証の仕組み

各実装ヘッダの末尾に `static_assert` を配置する:

```cpp
// mcu/stm32h7/mcu/gpio.hh の末尾
static_assert(umi::concepts::GpioPort<umi::mcu::Gpio>);

// arch/cm7/arch/cache.hh の末尾
static_assert(umi::concepts::CacheOps<umi::arch::Cache>);

// board/daisy_seed/board/codec.hh の末尾
static_assert(umi::concepts::Codec<umi::board::Ak4556>);
static_assert(umi::concepts::Codec<umi::board::Wm8731>);
```

これにより、新しいバックエンドを実装した時点で契約違反が即座にコンパイルエラーになる。
既存の `Hal`, `AudioBridge`, `BlockDeviceLike` 等と同じパターン。
