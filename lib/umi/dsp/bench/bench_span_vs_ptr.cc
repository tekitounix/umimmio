/**
 * span vs raw pointer vs iterator benchmark for Cortex-M4
 *
 * Compares:
 * 1. Raw pointer access (current API: ctx.output(ch) -> float*)
 * 2. std::span access (proposed API: ctx.output_span(ch) -> std::span<float>)
 * 3. Iterator-based patterns (range-for, std::transform, ranges)
 * 4. C++23 views::zip, views::enumerate patterns
 *
 * Build: xmake build bench_span_vs_ptr
 * Run:   xmake run bench_span_vs_ptr (via Renode)
 */

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <span>

// ============================================================================
// Baremetal stubs
// ============================================================================
#ifdef __arm__
extern "C" {
void __cxa_pure_virtual() {
    while (1)
        ;
}
}
void operator delete(void*, unsigned int) noexcept {}
void operator delete(void*) noexcept {}
#endif

// ============================================================================
// Startup Code and UART for Cortex-M4
// ============================================================================
#ifdef __arm__

extern "C" {
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _estack;

void Reset_Handler();
void Default_Handler();
int main();
}

__attribute__((section(".isr_vector"), used)) const void* const g_vector_table[] = {
    reinterpret_cast<const void*>(&_estack),
    reinterpret_cast<const void*>(Reset_Handler),
    reinterpret_cast<const void*>(Default_Handler), // NMI
    reinterpret_cast<const void*>(Default_Handler), // HardFault
    reinterpret_cast<const void*>(Default_Handler), // MemManage
    reinterpret_cast<const void*>(Default_Handler), // BusFault
    reinterpret_cast<const void*>(Default_Handler), // UsageFault
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    reinterpret_cast<const void*>(Default_Handler), // SVC
    nullptr,
    nullptr,
    reinterpret_cast<const void*>(Default_Handler), // PendSV
    reinterpret_cast<const void*>(Default_Handler), // SysTick
};

extern "C" __attribute__((noreturn)) void Reset_Handler() {
    // Copy .data
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    // Zero .bss
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    // Enable FPU (Cortex-M4F)
    volatile uint32_t& CPACR = *reinterpret_cast<volatile uint32_t*>(0xE000ED88);
    CPACR |= (0xFU << 20);
    asm volatile("dsb\n isb" ::: "memory");

    // Call global constructors
    extern void (*__init_array_start[])();
    extern void (*__init_array_end[])();
    for (auto fn = __init_array_start; fn < __init_array_end; ++fn) {
        (*fn)();
    }

    main();
    while (true) {
        asm volatile("wfi");
    }
}

extern "C" void Default_Handler() {
    while (true) {
        asm volatile("bkpt #0");
    }
}

extern "C" __attribute__((noreturn, alias("Reset_Handler"))) void _start();

// UART2 output
namespace uart {
constexpr uint32_t USART2_BASE = 0x40004400UL;
constexpr uint32_t RCC_APB1ENR = 0x40023840UL;

inline void init() {
    auto* rcc = reinterpret_cast<volatile uint32_t*>(RCC_APB1ENR);
    *rcc |= (1 << 17);

    auto* cr1 = reinterpret_cast<volatile uint32_t*>(USART2_BASE + 0x0C);
    auto* brr = reinterpret_cast<volatile uint32_t*>(USART2_BASE + 0x08);
    *cr1 = 0;
    *brr = 0x0683;               // 115200 baud @ 16MHz
    *cr1 = (1 << 13) | (1 << 3); // UE | TE
}

inline void putc(char c) {
    auto* sr = reinterpret_cast<volatile uint32_t*>(USART2_BASE + 0x00);
    auto* dr = reinterpret_cast<volatile uint32_t*>(USART2_BASE + 0x04);
    while (!(*sr & (1 << 7))) {
    }
    *dr = static_cast<uint32_t>(c);
}

inline void puts(const char* s) {
    while (*s) {
        if (*s == '\n') {
            putc('\r');
        }
        putc(*s++);
    }
}

inline void print_uint(uint32_t v) {
    char buf[12];
    int i = 0;
    if (v == 0) {
        buf[i++] = '0';
    } else {
        while (v > 0) {
            buf[i++] = '0' + (v % 10);
            v /= 10;
        }
    }
    while (i > 0) {
        putc(buf[--i]);
    }
}

inline void print_float(float v, int decimals = 2) {
    if (v < 0) {
        putc('-');
        v = -v;
    }
    auto int_part = static_cast<uint32_t>(v);
    print_uint(int_part);
    putc('.');
    v -= static_cast<float>(int_part);
    for (int d = 0; d < decimals; ++d) {
        v *= 10.0f;
        auto digit = static_cast<int>(v);
        putc('0' + digit);
        v -= static_cast<float>(digit);
    }
}
} // namespace uart

#endif // __arm__

// ============================================================================
// Cortex-M4 Cycle Counter (DWT)
// ============================================================================
namespace dwt {

#ifdef __arm__
inline volatile uint32_t* const DWT_CTRL = reinterpret_cast<volatile uint32_t*>(0xE0001000);
inline volatile uint32_t* const DWT_CYCCNT = reinterpret_cast<volatile uint32_t*>(0xE0001004);
inline volatile uint32_t* const SCB_DEMCR = reinterpret_cast<volatile uint32_t*>(0xE000EDFC);

inline void enable() {
    *SCB_DEMCR |= 0x01000000;
    *DWT_CYCCNT = 0;
    *DWT_CTRL |= 1;
}

inline uint32_t cycles() {
    return *DWT_CYCCNT;
}
inline void reset() {
    *DWT_CYCCNT = 0;
}
#else
inline void enable() {}
inline uint32_t cycles() {
    return 0;
}
inline void reset() {}
#endif

} // namespace dwt

// ============================================================================
// Test Subject: Simulated AudioContext access patterns
// ============================================================================
namespace {

constexpr uint32_t BUFFER_SIZE = 64;
constexpr uint32_t NUM_CHANNELS = 2;

// Audio buffers (simulating what kernel provides)
alignas(16) float audio_buf_l[BUFFER_SIZE];
alignas(16) float audio_buf_r[BUFFER_SIZE];
float* channel_ptrs[NUM_CHANNELS] = {audio_buf_l, audio_buf_r};

// ============================================================================
// Current API: returns raw pointer, nullptr if invalid
// ============================================================================
__attribute__((noinline)) float* output_ptr_noinline(size_t ch) {
    return ch < NUM_CHANNELS ? channel_ptrs[ch] : nullptr;
}

inline float* output_ptr_inline(size_t ch) {
    return ch < NUM_CHANNELS ? channel_ptrs[ch] : nullptr;
}

// ============================================================================
// Proposed API: returns span, empty if invalid
// ============================================================================
__attribute__((noinline)) std::span<float> output_span_noinline(size_t ch) {
    return ch < NUM_CHANNELS ? std::span<float>{channel_ptrs[ch], BUFFER_SIZE} : std::span<float>{};
}

inline std::span<float> output_span_inline(size_t ch) {
    return ch < NUM_CHANNELS ? std::span<float>{channel_ptrs[ch], BUFFER_SIZE} : std::span<float>{};
}

// ============================================================================
// Realistic DSP: Simple oscillator + filter state
// ============================================================================
struct OscState {
    float phase = 0.0f;
    float freq = 440.0f;
    float dt = 1.0f / 48000.0f;
    float delta = 440.0f * 6.28318530717959f / 48000.0f; // phase increment
};

struct FilterState {
    float y1 = 0.0f;
    float y2 = 0.0f;
    float a1 = -1.8f;
    float a2 = 0.81f;
    float b0 = 0.0025f;
    float b1 = 0.005f;
    float b2 = 0.0025f;
};

// Fast sine approximation (Bhaskara)
inline float fast_sin(float x) {
    constexpr float PI = 3.14159265358979f;
    constexpr float TWO_PI = 6.28318530717959f;
    // Normalize to [-PI, PI]
    while (x > PI)
        x -= TWO_PI;
    while (x < -PI)
        x += TWO_PI;
    // Bhaskara approximation
    return (16.0f * x * (PI - x)) / (5.0f * PI * PI - 4.0f * x * (PI - x));
}

// Biquad filter tick
inline float biquad_tick(FilterState& f, float x) {
    float y = f.b0 * x + f.b1 * f.y1 + f.b2 * f.y2 - f.a1 * f.y1 - f.a2 * f.y2;
    f.y2 = f.y1;
    f.y1 = y;
    return y;
}

// ============================================================================
// Benchmark: API call overhead (10000 iterations)
// ============================================================================
constexpr uint32_t CALL_ITERATIONS = 10000;

void bench_ptr_call_noinline() {
    volatile float* p;
    dwt::reset();
    for (uint32_t i = 0; i < CALL_ITERATIONS; ++i) {
        p = output_ptr_noinline(0);
        asm volatile("" ::: "memory"); // Prevent optimization
    }
    (void)p;
    uint32_t cycles = dwt::cycles();
    uart::puts("ptr_noinline  (10k): ");
    uart::print_uint(cycles);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(cycles) / CALL_ITERATIONS, 1);
    uart::puts(" per call)\n");
}

void bench_ptr_call_inline() {
    volatile float* p;
    dwt::reset();
    for (uint32_t i = 0; i < CALL_ITERATIONS; ++i) {
        p = output_ptr_inline(0);
        asm volatile("" ::: "memory");
    }
    (void)p;
    uint32_t cycles = dwt::cycles();
    uart::puts("ptr_inline    (10k): ");
    uart::print_uint(cycles);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(cycles) / CALL_ITERATIONS, 1);
    uart::puts(" per call)\n");
}

void bench_span_call_noinline() {
    std::span<float> s;
    dwt::reset();
    for (uint32_t i = 0; i < CALL_ITERATIONS; ++i) {
        s = output_span_noinline(0);
        asm volatile("" ::"r"(s.data()), "r"(s.size()) : "memory");
    }
    (void)s;
    uint32_t cycles = dwt::cycles();
    uart::puts("span_noinline (10k): ");
    uart::print_uint(cycles);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(cycles) / CALL_ITERATIONS, 1);
    uart::puts(" per call)\n");
}

void bench_span_call_inline() {
    std::span<float> s;
    dwt::reset();
    for (uint32_t i = 0; i < CALL_ITERATIONS; ++i) {
        s = output_span_inline(0);
        asm volatile("" ::"r"(s.data()), "r"(s.size()) : "memory");
    }
    (void)s;
    uint32_t cycles = dwt::cycles();
    uart::puts("span_inline   (10k): ");
    uart::print_uint(cycles);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(cycles) / CALL_ITERATIONS, 1);
    uart::puts(" per call)\n");
}

// ============================================================================
// Benchmark: Realistic synth process() - oscillator + filter
// ============================================================================
constexpr uint32_t DSP_ITERATIONS = 1000;

void bench_ptr_synth() {
    OscState osc;
    FilterState flt;
    uint32_t total = 0;

    for (uint32_t iter = 0; iter < DSP_ITERATIONS; ++iter) {
        float* out_l = output_ptr_inline(0);
        float* out_r = output_ptr_inline(1);
        if (!out_l)
            continue;

        dwt::reset();
        for (uint32_t i = 0; i < BUFFER_SIZE; ++i) {
            // Oscillator
            float sample = fast_sin(osc.phase);
            osc.phase += osc.freq * osc.dt * 6.28318530717959f;
            if (osc.phase > 6.28318530717959f)
                osc.phase -= 6.28318530717959f;

            // Filter
            sample = biquad_tick(flt, sample);

            // Output
            out_l[i] = sample * 0.5f;
            if (out_r)
                out_r[i] = sample * 0.5f;
        }
        total += dwt::cycles();
    }

    uart::puts("ptr_synth   (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / DSP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}

void bench_span_synth() {
    OscState osc;
    FilterState flt;
    uint32_t total = 0;

    for (uint32_t iter = 0; iter < DSP_ITERATIONS; ++iter) {
        auto out_l = output_span_inline(0);
        auto out_r = output_span_inline(1);
        if (out_l.empty())
            continue;

        dwt::reset();
        for (uint32_t i = 0; i < out_l.size(); ++i) {
            // Oscillator
            float sample = fast_sin(osc.phase);
            osc.phase += osc.freq * osc.dt * 6.28318530717959f;
            if (osc.phase > 6.28318530717959f)
                osc.phase -= 6.28318530717959f;

            // Filter
            sample = biquad_tick(flt, sample);

            // Output
            out_l[i] = sample * 0.5f;
            if (!out_r.empty())
                out_r[i] = sample * 0.5f;
        }
        total += dwt::cycles();
    }

    uart::puts("span_synth  (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / DSP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}

// Span with data() extraction pattern
void bench_span_data_synth() {
    OscState osc;
    FilterState flt;
    uint32_t total = 0;

    for (uint32_t iter = 0; iter < DSP_ITERATIONS; ++iter) {
        auto out_l_span = output_span_inline(0);
        auto out_r_span = output_span_inline(1);
        if (out_l_span.empty())
            continue;

        float* out_l = out_l_span.data();
        float* out_r = out_r_span.empty() ? nullptr : out_r_span.data();
        const uint32_t len = out_l_span.size();

        dwt::reset();
        for (uint32_t i = 0; i < len; ++i) {
            // Oscillator
            float sample = fast_sin(osc.phase);
            osc.phase += osc.freq * osc.dt * 6.28318530717959f;
            if (osc.phase > 6.28318530717959f)
                osc.phase -= 6.28318530717959f;

            // Filter
            sample = biquad_tick(flt, sample);

            // Output
            out_l[i] = sample * 0.5f;
            if (out_r)
                out_r[i] = sample * 0.5f;
        }
        total += dwt::cycles();
    }

    uart::puts("span->ptr   (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / DSP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}

// ============================================================================
// Benchmark: Effect processing (stereo in -> stereo out)
// ============================================================================
alignas(16) float input_buf_l[BUFFER_SIZE];
alignas(16) float input_buf_r[BUFFER_SIZE];
alignas(16) float output_buf_r[BUFFER_SIZE]; // Secondary output buffer for stereo tests
float* input_ptrs[NUM_CHANNELS] = {input_buf_l, input_buf_r};

inline float* input_ptr_inline(size_t ch) {
    return ch < NUM_CHANNELS ? input_ptrs[ch] : nullptr;
}

inline std::span<const float> input_span_inline(size_t ch) {
    return ch < NUM_CHANNELS ? std::span<const float>{input_ptrs[ch], BUFFER_SIZE} : std::span<const float>{};
}

void bench_ptr_effect() {
    FilterState flt_l, flt_r;
    uint32_t total = 0;

    // Initialize input with some data
    for (uint32_t i = 0; i < BUFFER_SIZE; ++i) {
        input_buf_l[i] = static_cast<float>(i) * 0.01f;
        input_buf_r[i] = static_cast<float>(i) * 0.01f;
    }

    for (uint32_t iter = 0; iter < DSP_ITERATIONS; ++iter) {
        const float* in_l = input_ptr_inline(0);
        const float* in_r = input_ptr_inline(1);
        float* out_l = output_ptr_inline(0);
        float* out_r = output_ptr_inline(1);
        if (!in_l || !out_l)
            continue;

        dwt::reset();
        for (uint32_t i = 0; i < BUFFER_SIZE; ++i) {
            out_l[i] = biquad_tick(flt_l, in_l[i]);
            if (in_r && out_r)
                out_r[i] = biquad_tick(flt_r, in_r[i]);
        }
        total += dwt::cycles();
    }

    uart::puts("ptr_effect  (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / DSP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}

void bench_span_effect() {
    FilterState flt_l, flt_r;
    uint32_t total = 0;

    for (uint32_t iter = 0; iter < DSP_ITERATIONS; ++iter) {
        auto in_l = input_span_inline(0);
        auto in_r = input_span_inline(1);
        auto out_l = output_span_inline(0);
        auto out_r = output_span_inline(1);
        if (in_l.empty() || out_l.empty())
            continue;

        dwt::reset();
        for (uint32_t i = 0; i < out_l.size(); ++i) {
            out_l[i] = biquad_tick(flt_l, in_l[i]);
            if (!in_r.empty() && !out_r.empty())
                out_r[i] = biquad_tick(flt_r, in_r[i]);
        }
        total += dwt::cycles();
    }

    uart::puts("span_effect (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / DSP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}

// ============================================================================
// Benchmark: Buffer write loop (simple)
// ============================================================================
constexpr uint32_t LOOP_ITERATIONS = 1000;

void bench_ptr_loop() {
    uint32_t total = 0;
    for (uint32_t iter = 0; iter < LOOP_ITERATIONS; ++iter) {
        float* out = output_ptr_inline(0);
        if (!out)
            continue;

        dwt::reset();
        for (uint32_t i = 0; i < BUFFER_SIZE; ++i) {
            out[i] = static_cast<float>(i) * 0.015625f;
        }
        total += dwt::cycles();
    }
    uart::puts("ptr_loop    (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / LOOP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}

void bench_span_loop() {
    uint32_t total = 0;
    for (uint32_t iter = 0; iter < LOOP_ITERATIONS; ++iter) {
        auto out = output_span_inline(0);
        if (out.empty())
            continue;

        dwt::reset();
        for (uint32_t i = 0; i < out.size(); ++i) {
            out[i] = static_cast<float>(i) * 0.015625f;
        }
        total += dwt::cycles();
    }
    uart::puts("span_loop   (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / LOOP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}

// ============================================================================
// Iterator-based patterns
// ============================================================================

// Pattern 1: Range-based for with pointer arithmetic
void bench_rangef_ptr() {
    uint32_t total = 0;
    for (uint32_t iter = 0; iter < LOOP_ITERATIONS; ++iter) {
        float* out = output_ptr_inline(0);
        if (!out)
            continue;

        dwt::reset();
        float val = 0.0f;
        for (float& sample : std::span<float>{out, BUFFER_SIZE}) {
            sample = val;
            val += 0.015625f;
        }
        total += dwt::cycles();
    }
    uart::puts("rangef_ptr  (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / LOOP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}

// Pattern 2: Range-based for with span
void bench_rangef_span() {
    uint32_t total = 0;
    for (uint32_t iter = 0; iter < LOOP_ITERATIONS; ++iter) {
        auto out = output_span_inline(0);
        if (out.empty())
            continue;

        dwt::reset();
        float val = 0.0f;
        for (float& sample : out) {
            sample = val;
            val += 0.015625f;
        }
        total += dwt::cycles();
    }
    uart::puts("rangef_span (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / LOOP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}

// Pattern 3: std::transform (copy with modification)
void bench_transform() {
    uint32_t total = 0;

    // Initialize input
    for (uint32_t i = 0; i < BUFFER_SIZE; ++i) {
        input_buf_l[i] = static_cast<float>(i) * 0.01f;
    }

    for (uint32_t iter = 0; iter < LOOP_ITERATIONS; ++iter) {
        auto in = input_span_inline(0);
        auto out = output_span_inline(0);
        if (in.empty() || out.empty())
            continue;

        dwt::reset();
        std::transform(in.begin(), in.end(), out.begin(), [](float x) { return x * 2.0f; });
        total += dwt::cycles();
    }
    uart::puts("transform   (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / LOOP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}

// Pattern 4: std::transform vs index loop (same operation)
void bench_idx_mul() {
    uint32_t total = 0;
    for (uint32_t iter = 0; iter < LOOP_ITERATIONS; ++iter) {
        auto in = input_span_inline(0);
        auto out = output_span_inline(0);
        if (in.empty() || out.empty())
            continue;

        dwt::reset();
        for (uint32_t i = 0; i < out.size(); ++i) {
            out[i] = in[i] * 2.0f;
        }
        total += dwt::cycles();
    }
    uart::puts("idx_mul     (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / LOOP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}

// Pattern 5: Pointer iterator manual
void bench_ptr_iter() {
    uint32_t total = 0;
    for (uint32_t iter = 0; iter < LOOP_ITERATIONS; ++iter) {
        auto in = input_span_inline(0);
        auto out = output_span_inline(0);
        if (in.empty() || out.empty())
            continue;

        dwt::reset();
        const float* ip = in.data();
        float* op = out.data();
        const float* const end = op + out.size();
        while (op != end) {
            *op++ = *ip++ * 2.0f;
        }
        total += dwt::cycles();
    }
    uart::puts("ptr_iter    (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / LOOP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}

// Pattern 6: Effect with range-for (using index for dual buffer access)
void bench_effect_idx() {
    FilterState flt_l, flt_r;
    uint32_t total = 0;

    for (uint32_t iter = 0; iter < DSP_ITERATIONS; ++iter) {
        auto in_l = input_span_inline(0);
        auto in_r = input_span_inline(1);
        auto out_l = output_span_inline(0);
        auto out_r = output_span_inline(1);
        if (in_l.empty() || out_l.empty())
            continue;

        dwt::reset();
        for (uint32_t i = 0; i < out_l.size(); ++i) {
            out_l[i] = biquad_tick(flt_l, in_l[i]);
            if (!in_r.empty() && !out_r.empty())
                out_r[i] = biquad_tick(flt_r, in_r[i]);
        }
        total += dwt::cycles();
    }
    uart::puts("effect_idx  (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / DSP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}

// Pattern 7: Mono effect with range-for (no index needed)
void bench_effect_rangef_mono() {
    FilterState flt;
    uint32_t total = 0;

    // Initialize input
    for (uint32_t i = 0; i < BUFFER_SIZE; ++i) {
        input_buf_l[i] = static_cast<float>(i) * 0.01f;
    }

    for (uint32_t iter = 0; iter < DSP_ITERATIONS; ++iter) {
        auto in = input_span_inline(0);
        auto out = output_span_inline(0);
        if (in.empty() || out.empty())
            continue;

        dwt::reset();
        auto it_in = in.begin();
        for (float& sample : out) {
            sample = biquad_tick(flt, *it_in++);
        }
        total += dwt::cycles();
    }
    uart::puts("effect_rf_m (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / DSP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}

// Pattern 8: std::transform for effect
void bench_effect_transform() {
    FilterState flt;
    uint32_t total = 0;

    for (uint32_t iter = 0; iter < DSP_ITERATIONS; ++iter) {
        auto in = input_span_inline(0);
        auto out = output_span_inline(0);
        if (in.empty() || out.empty())
            continue;

        dwt::reset();
        std::transform(in.begin(), in.end(), out.begin(), [&flt](float x) { return biquad_tick(flt, x); });
        total += dwt::cycles();
    }
    uart::puts("effect_xfrm (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / DSP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}

// ============================================================================
// Benchmark: Stereo patterns (zip vs index)
// ============================================================================

// Pattern: Index loop for stereo (baseline)
void bench_stereo_idx() {
    OscState osc;
    uint32_t total = 0;

    for (uint32_t iter = 0; iter < DSP_ITERATIONS; ++iter) {
        auto out_l = output_span_inline(0);
        auto out_r = std::span<float>{output_buf_r, BUFFER_SIZE};
        if (out_l.empty())
            continue;

        dwt::reset();
        for (uint32_t i = 0; i < out_l.size(); ++i) {
            float sample = fast_sin(osc.phase);
            osc.phase += osc.delta;
            // Simple panning: mono to stereo
            out_l[i] = sample * 0.7f;
            out_r[i] = sample * 0.7f;
        }
        total += dwt::cycles();
    }
    uart::puts("stereo_idx  (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / DSP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}

// Pattern: views::zip for stereo
void bench_stereo_zip() {
    OscState osc;
    uint32_t total = 0;

    for (uint32_t iter = 0; iter < DSP_ITERATIONS; ++iter) {
        auto out_l = output_span_inline(0);
        auto out_r = std::span<float>{output_buf_r, BUFFER_SIZE};
        if (out_l.empty())
            continue;

        dwt::reset();
        for (auto&& [l, r] : std::views::zip(out_l, out_r)) {
            float sample = fast_sin(osc.phase);
            osc.phase += osc.delta;
            l = sample * 0.7f;
            r = sample * 0.7f;
        }
        total += dwt::cycles();
    }
    uart::puts("stereo_zip  (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / DSP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}

// Pattern: Separate range-for for each channel (when processing is independent)
void bench_stereo_separate() {
    OscState osc;
    uint32_t total = 0;

    for (uint32_t iter = 0; iter < DSP_ITERATIONS; ++iter) {
        auto out_l = output_span_inline(0);
        auto out_r = std::span<float>{output_buf_r, BUFFER_SIZE};
        if (out_l.empty())
            continue;

        dwt::reset();
        // Generate to L first
        for (auto& sample : out_l) {
            sample = fast_sin(osc.phase) * 0.7f;
            osc.phase += osc.delta;
        }
        // Copy L to R (stereo mono)
        std::ranges::copy(out_l, out_r.begin());
        total += dwt::cycles();
    }
    uart::puts("stereo_sep  (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / DSP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}

// ============================================================================
// Benchmark: Enumerate pattern (index + value)
// ============================================================================

// Pattern: range-for with separate counter (manual enumerate)
// Pattern: range-for with separate counter (manual enumerate, for libc++)
void bench_enumerate() {
    uint32_t total = 0;

    for (uint32_t iter = 0; iter < LOOP_ITERATIONS; ++iter) {
        auto out = output_span_inline(0);
        if (out.empty())
            continue;

        dwt::reset();
        uint32_t i = 0;
        for (auto& sample : out) {
            sample = static_cast<float>(i) * 0.015625f;
            ++i;
        }
        total += dwt::cycles();
    }
    uart::puts("enum_manual (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / LOOP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}

#if __cpp_lib_ranges_enumerate >= 202302L
// Pattern: std::views::enumerate (C++23, libstdc++ only)
void bench_enumerate_real() {
    uint32_t total = 0;

    for (uint32_t iter = 0; iter < LOOP_ITERATIONS; ++iter) {
        auto out = output_span_inline(0);
        if (out.empty())
            continue;

        dwt::reset();
        for (auto&& [i, sample] : std::views::enumerate(out)) {
            sample = static_cast<float>(i) * 0.015625f;
        }
        total += dwt::cycles();
    }
    uart::puts("enum_real   (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / LOOP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}
#endif

// Pattern: Index loop equivalent
void bench_enumerate_idx() {
    uint32_t total = 0;

    for (uint32_t iter = 0; iter < LOOP_ITERATIONS; ++iter) {
        auto out = output_span_inline(0);
        if (out.empty())
            continue;

        dwt::reset();
        for (uint32_t i = 0; i < out.size(); ++i) {
            out[i] = static_cast<float>(i) * 0.015625f;
        }
        total += dwt::cycles();
    }
    uart::puts("enum_idx    (1k): ");
    uart::print_uint(total);
    uart::puts(" cycles (");
    uart::print_float(static_cast<float>(total) / LOOP_ITERATIONS, 1);
    uart::puts(" per buffer)\n");
}

} // namespace

// ============================================================================
// Main
// ============================================================================
int main() {
    uart::init();
    dwt::enable();

    uart::puts("\n");
    uart::puts("==========================================\n");
    uart::puts("  span vs raw pointer benchmark (CM4)\n");
    uart::puts("  Buffer size: 64 samples\n");
#if defined(__OPTIMIZE_SIZE__)
    uart::puts("  Optimization: -Os (size)\n");
#elif defined(__OPTIMIZE__)
    uart::puts("  Optimization: -O2/-O3/-Ofast\n");
#else
    uart::puts("  Optimization: -O0 (none)\n");
#endif
    uart::puts("==========================================\n\n");

    uart::puts("--- API Call Overhead (noinline) ---\n");
    bench_ptr_call_noinline();
    bench_span_call_noinline();

    uart::puts("\n--- API Call Overhead (inline) ---\n");
    bench_ptr_call_inline();
    bench_span_call_inline();

    uart::puts("\n--- Simple Loop (64 samples) ---\n");
    bench_ptr_loop();
    bench_span_loop();

    uart::puts("\n--- Realistic Synth (osc+filter) ---\n");
    bench_ptr_synth();
    bench_span_synth();
    bench_span_data_synth();

    uart::puts("\n--- Stereo Effect (biquad) ---\n");
    bench_ptr_effect();
    bench_span_effect();

    uart::puts("\n--- Iterator Patterns: Simple Loop ---\n");
    bench_rangef_ptr();
    bench_rangef_span();

    uart::puts("\n--- Iterator Patterns: Copy+Mul ---\n");
    bench_idx_mul();
    bench_transform();
    bench_ptr_iter();

    uart::puts("\n--- Iterator Patterns: Effect ---\n");
    bench_effect_idx();
    bench_effect_rangef_mono();
    bench_effect_transform();

    uart::puts("\n--- Stereo Patterns: zip vs index ---\n");
    bench_stereo_idx();
    bench_stereo_zip();
    bench_stereo_separate();

    uart::puts("\n--- Enumerate Pattern ---\n");
    bench_enumerate();
#if __cpp_lib_ranges_enumerate >= 202302L
    bench_enumerate_real();
#endif
    bench_enumerate_idx();

    uart::puts("\n==========================================\n");
    uart::puts("  Benchmark complete\n");
    uart::puts("==========================================\n");

    while (true) {
        asm volatile("wfi");
    }
}
