/**
 * Diode Ladder Filter Benchmark for Cortex-M4
 *
 * This benchmark has been refactored to use the umi::bench framework.
 *
 * Compares three implementations:
 * 1. Original (Karrikuh-style, per-sample coefficient calculation)
 * 2. Method 2 (D factorization)
 * 3. Method 3 (Fully optimized with shared sub-terms)
 *
 * Build: xmake build bench_diode_ladder
 * Run:   xmake run bench_diode_ladder (via Renode)
 */

#include <cstdint>
#include <cmath>
#include <algorithm>

#include "bench/bench.hh"
#include "bench/platform/stm32f4.hh"

// ============================================================================
// Common
// ============================================================================
constexpr float PI = 3.14159265358979323846f;

inline float clip(float x) {
    return x / (1.0f + std::abs(x));
}

// ============================================================================
// s0 Calculation Benchmark - Isolated s0 derivation only
// ============================================================================

// Shared state for all methods
struct FilterState {
    float z[4] = {0.1f, 0.2f, 0.3f, 0.4f};  // Non-zero for realistic test
};

// Method 1: Original (Karrikuh expanded form)
// s0 = (z[0]*a²*a + z[1]*a²*b + z[2]*(b²-2a²)*a + z[3]*(b²-3a²)*b) * c
struct S0_Original {
    static float compute(float a, float a2, float b, float b2, float c, const float* z) {
        return (a2 * a * z[0]
              + a2 * b * z[1]
              + z[2] * (b2 - 2.0f * a2) * a
              + z[3] * (b2 - 3.0f * a2) * b) * c;
    }
};

// Method 2: D Factorization
// D = b² - 2a², s0 = c * (a² * (a*z0 + b*(z1-z3)) + D * (a*z2 + b*z3))
struct S0_DFactor {
    static float compute(float a, float a2, float b, float b2, float c, const float* z) {
        float D = b2 - 2.0f * a2;
        float term1 = a * z[0] + b * (z[1] - z[3]);
        float term2 = a * z[2] + b * z[3];
        return c * (a2 * term1 + D * term2);
    }
};

// ============================================================================
// 近似逆数を使った実装
// ============================================================================

// Fast inverse square root (Quake III style) adapted for reciprocal
// Initial approximation using integer bit manipulation
inline float fast_recip_initial(float x) {
    union { float f; uint32_t i; } u = {x};
    // Magic number for reciprocal: 0x7EF311C3
    u.i = 0x7EF311C3 - u.i;
    return u.f;
}

// Fast reciprocal with 1 Newton-Raphson iteration
// Error: ~0.1%
inline float fast_recip(float x) {
    float r = fast_recip_initial(x);
    // Newton-Raphson: r = r * (2 - x * r)
    r = r * (2.0f - x * r);
    return r;
}

// Fast reciprocal with 2 Newton-Raphson iterations
// Error: ~0.0001%
inline float fast_recip_2nr(float x) {
    float r = fast_recip_initial(x);
    r = r * (2.0f - x * r);
    r = r * (2.0f - x * r);
    return r;
}

// Fast reciprocal without Newton-Raphson (lowest precision)
// Error: ~12%
inline float fast_recip_approx(float x) {
    return fast_recip_initial(x);
}

// Method 3: D Factorization + fast_recip (1 NR iteration)
struct S0_DFactor_FastRecip {
    static float compute(float a, float a2, float b, float b2, float /*c_unused*/, const float* z) {
        float D = b2 - 2.0f * a2;
        float denom = D * D - 2.0f * a2 * a2;
        float c = fast_recip(denom);
        float term1 = a * z[0] + b * (z[1] - z[3]);
        float term2 = a * z[2] + b * z[3];
        return c * (a2 * term1 + D * term2);
    }
};

// Method 4: D Factorization + fast_recip_approx (no NR, lowest precision)
struct S0_DFactor_Approx {
    static float compute(float a, float a2, float b, float b2, float /*c_unused*/, const float* z) {
        float D = b2 - 2.0f * a2;
        float denom = D * D - 2.0f * a2 * a2;
        float c = fast_recip_approx(denom);
        float term1 = a * z[0] + b * (z[1] - z[3]);
        float term2 = a * z[2] + b * z[3];
        return c * (a2 * term1 + D * term2);
    }
};

// ============================================================================
// Benchmark Runner
// ============================================================================
constexpr float TEST_FC = 0.1f;
volatile float sink = 0;  // Prevent optimization

// Prevent inlining to see actual generated code
template<typename S0Method>
__attribute__((noinline))
float compute_s0_wrapper(float a, float a2, float b, float b2, float c, float* z) {
    return S0Method::compute(a, a2, b, b2, c, z);
}

// Compute reference value and error
void measure_error(umi::bench::UartOutput output) {
    volatile float fc = TEST_FC;
    float a = PI * fc;
    float a2 = a * a;
    float b = 2.0f * a + 1.0f;
    float b2 = b * b;
    float c = 1.0f / (2.0f * a2 * a2 - 4.0f * a2 * b2 + b2 * b2);

    float z[4] = {0.1f, 0.2f, 0.3f, 0.4f};

    // Reference (exact division)
    float ref = S0_Original::compute(a, a2, b, b2, c, z);
    float d_exact = S0_DFactor::compute(a, a2, b, b2, c, z);
    float d_fast = S0_DFactor_FastRecip::compute(a, a2, b, b2, c, z);
    float d_approx = S0_DFactor_Approx::compute(a, a2, b, b2, c, z);

    output.puts("--- Error Analysis ---\n");
    output.puts("Reference (Original): ");
    output.print_float(ref, 6);
    output.puts("\n");

    output.puts("D-Factor exact:       ");
    output.print_float(d_exact, 6);
    output.puts(" (err: ");
    output.print_float((d_exact - ref) / ref * 100.0f, 6);
    output.puts("%)\n");

    output.puts("D-Factor + FastRecip: ");
    output.print_float(d_fast, 6);
    output.puts(" (err: ");
    output.print_float((d_fast - ref) / ref * 100.0f, 6);
    output.puts("%)\n");

    output.puts("D-Factor + Approx:    ");
    output.print_float(d_approx, 6);
    output.puts(" (err: ");
    output.print_float((d_approx - ref) / ref * 100.0f, 6);
    output.puts("%)\n\n");
}

int main() {
    using Platform = umi::bench::Stm32f4;
    using Timer = Platform::Timer;
    using Output = Platform::Output;

    Platform::init();
    Output::puts("\n===========================================\n");
    Output::puts("s0 Calculation Benchmark (Cortex-M4)\n");
    Output::puts("Refactored to use umi::bench framework\n");
    Output::puts("===========================================\n");

    // Create and calibrate the runner
    umi::bench::Runner<Timer> runner;
    runner.calibrate();
    
    Output::puts("Cutoff fc: 0.1 (normalized)\n");
    Output::puts("Runner calibrated. Baseline: ");
    Output::print_uint(runner.get_baseline());
    Output::puts(" cy\n\n");

    // Error analysis first
    measure_error(Output{});

    // --- Benchmark setup ---
    constexpr uint32_t ITERATIONS = 10000;
    
    // Pre-compute coefficients (same for all methods)
    volatile float fc = TEST_FC;  // volatile to prevent constant folding
    float a = PI * fc;
    float a2 = a * a;
    float b = 2.0f * a + 1.0f;
    float b2 = b * b;
    float c = 1.0f / (2.0f * a2 * a2 - 4.0f * a2 * b2 + b2 * b2);
    
    // --- Run benchmarks ---
    Output::puts("--- Speed Benchmark ---\n");
    Output::puts("Running with ");
    Output::print_uint(ITERATIONS);
    Output::puts(" iterations per sample...\n");

    auto stats1 = runner.run(ITERATIONS, [&] {
        float z[4] = {0.1f, 0.2f, 0.3f, 0.4f};
        float result = compute_s0_wrapper<S0_Original>(a, a2, b, b2, c, z);
        sink = result;
        z[0] = result * 0.001f + 0.1f;
        z[1] = result * 0.002f + 0.2f;
        z[2] = result * 0.003f + 0.3f;
        z[3] = result * 0.004f + 0.4f;
    });
    umi::bench::report<Output>("Original ", stats1, 0);

    auto stats2 = runner.run(ITERATIONS, [&] {
        float z[4] = {0.1f, 0.2f, 0.3f, 0.4f};
        float result = compute_s0_wrapper<S0_DFactor>(a, a2, b, b2, c, z);
        sink = result;
        z[0] = result * 0.001f + 0.1f;
        z[1] = result * 0.002f + 0.2f;
        z[2] = result * 0.003f + 0.3f;
        z[3] = result * 0.004f + 0.4f;
    });
    umi::bench::report<Output>("D-Factor ", stats2, 0);

    auto stats3 = runner.run(ITERATIONS, [&] {
        float z[4] = {0.1f, 0.2f, 0.3f, 0.4f};
        float result = compute_s0_wrapper<S0_DFactor_FastRecip>(a, a2, b, b2, c, z);
        sink = result;
        z[0] = result * 0.001f + 0.1f;
        z[1] = result * 0.002f + 0.2f;
        z[2] = result * 0.003f + 0.3f;
        z[3] = result * 0.004f + 0.4f;
    });
    umi::bench::report<Output>("FastRecip", stats3, 0);
    
    auto stats4 = runner.run(ITERATIONS, [&] {
        float z[4] = {0.1f, 0.2f, 0.3f, 0.4f};
        float result = compute_s0_wrapper<S0_DFactor_Approx>(a, a2, b, b2, c, z);
        sink = result;
        z[0] = result * 0.001f + 0.1f;
        z[1] = result * 0.002f + 0.2f;
        z[2] = result * 0.003f + 0.3f;
        z[3] = result * 0.004f + 0.4f;
    });
    umi::bench::report<Output>("Approx   ", stats4, 0);


    Output::puts("\n--- Speed Results ---\n");
    float c1 = stats1.min / ITERATIONS;
    float c2 = stats2.min / ITERATIONS;
    float c3 = stats3.min / ITERATIONS;
    float c4 = stats4.min / ITERATIONS;

    Output::puts("D-Factor vs Original:  ");
    Output::print_float(100.0f * c2 / c1, 1);
    Output::puts("% (");
    Output::print_float(c1 / c2, 2);
    Output::puts("x)\n");

    Output::puts("FastRecip vs Original: ");
    Output::print_float(100.0f * c3 / c1, 1);
    Output::puts("% (");
    Output::print_float(c1 / c3, 2);
    Output::puts("x)\n");

    Output::puts("Approx vs Original:    ");
    Output::print_float(100.0f * c4 / c1, 1);
    Output::puts("% (");
    Output::print_float(c1 / c4, 2);
    Output::puts("x)\n");

    Output::puts("\n=== BENCHMARK COMPLETE ===\n");

    Platform::halt();
    return 0; // Unreachable
}
