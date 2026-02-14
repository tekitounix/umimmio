// SPDX-License-Identifier: MIT
/// @file
/// @brief Target-agnostic instruction benchmark example.

#include <cstdint>
#include <umibench/bench.hh>
#include <umibench/platform.hh>

namespace {

using Platform = umi::bench::Platform;
using Timer = Platform::Timer;
using Output = Platform::Output;

constexpr std::uint32_t iterations = 100;

/// @brief Run one named benchmark case and emit full report.
/// @tparam Func Callable type.
/// @param name Case name.
/// @param runner Benchmark runner.
/// @param func Callable to benchmark.
template <typename Func>
void bench(const char* name, umi::bench::Runner<Timer>& runner, Func&& func) {
    auto stats = runner.run<64>(iterations, func);
    umi::bench::report<Platform>(name, stats);
}

} // namespace

/// @brief Program entry point for instruction benchmark example.
int main() {
    Output::puts("\n=== Instruction Benchmark ===\n");
    Output::puts("target=");
    Output::puts(Platform::target_name());
    Output::puts(" unit=");
    Output::puts(Platform::timer_unit());
    Output::puts("\n\n");

    umi::bench::Runner<Timer> runner;
    runner.calibrate<256>();

    Output::puts("Baseline: ");
    Output::print_uint(static_cast<std::uint64_t>(runner.get_baseline()));
    Output::puts(" ");
    Output::puts(Platform::timer_unit());
    Output::puts("\n\n");

    // Integer arithmetic
    static volatile int add_x = 1;
    static volatile int mul_x = 2;
    static volatile int div_x = 100000;
    static volatile int div_y = 2;

    Output::puts("[Arithmetic]\n");
    bench("ADD", runner, [&] { add_x += 1; });
    bench("MUL", runner, [&] { mul_x *= 2; });
    bench("DIV", runner, [&] { div_x = div_x / div_y; });

    // Logic / bitwise
    static volatile int and_x = 0xFF;
    static volatile int or_x = 0;
    static volatile int xor_x = 0xFF;
    static volatile int lsl_x = 1;
    static volatile int lsr_x = 0x8000;

    Output::puts("\n[Logic]\n");
    bench("AND", runner, [&] { and_x &= 0xAA; });
    bench("OR", runner, [&] { or_x |= 0x55; });
    bench("XOR", runner, [&] { xor_x ^= 0x55; });
    bench("LSL", runner, [&] { lsl_x <<= 1; });
    bench("LSR", runner, [&] { lsr_x >>= 1; });

    // Memory
    static volatile int ldr_arr[4] = {1, 2, 3, 4};
    static volatile int str_arr[4];
    static volatile std::uint32_t ldr_index = 0;
    static volatile std::uint32_t str_index = 0;

    Output::puts("\n[Memory]\n");
    bench("LDR", runner, [&] {
        ldr_index = (ldr_index + 1) & 3u;
        volatile int tmp = ldr_arr[ldr_index];
        (void)tmp;
    });
    bench("STR", runner, [&] {
        str_index = (str_index + 1) & 3u;
        str_arr[str_index] = static_cast<int>(str_index);
    });

    // Floating point
    static volatile float fadd_x = 1.0f;
    static volatile float fmul_x = 1.0f;
    static volatile float fdiv_x = 100.0f;
    static volatile float fdiv_y = 2.0f;

    Output::puts("\n[Float]\n");
    bench("FADD", runner, [&] { fadd_x += 1.0f; });
    bench("FMUL", runner, [&] { fmul_x *= 1.1f; });
    bench("FDIV", runner, [&] { fdiv_x = fdiv_x / fdiv_y; });

    Output::puts("\n=== Done ===\n");
    Platform::halt();
    return 0;
}
