// SPDX-License-Identifier: MIT
/// @file
/// @brief Cortex-M specific instruction benchmark example for STM32F4.

#include <cstdint>
#include <umibench/bench.hh>
#include <umibench/platform.hh>

namespace {

using Platform = umi::bench::Platform;
using Timer = Platform::Timer;
using Output = Platform::Output;

constexpr std::uint32_t iterations = 100;

/// @brief Run one named benchmark case and print compact output.
/// @tparam Func Callable type.
/// @param name Case name.
/// @param runner Benchmark runner.
/// @param func Callable to benchmark.
/// @param expected Optional expected cycles per iteration for reference.
template <typename Func>
void bench(const char* name, umi::bench::Runner<Timer>& runner, Func&& func, std::uint32_t expected = 0) {
    auto stats = runner.run<64>(iterations, func);
    umi::bench::report_compact<Platform>(name, stats);
    if (expected > 0) {
        Output::puts("    expected=");
        Output::print_uint(expected);
        Output::puts(" cy/iter\n");
    }
}

} // namespace

/// @brief Program entry point for Cortex-M instruction benchmark.
int main() {
    Output::puts("\n=== Cortex-M Instruction Benchmark (STM32F4) ===\n");
    Output::puts("unit=cy\n\n");

    umi::bench::Runner<Timer> runner;
    runner.calibrate<256>();

    Output::puts("Baseline: ");
    Output::print_uint(static_cast<std::uint64_t>(runner.get_baseline()));
    Output::puts(" cy\n\n");

    // Branch prediction
    static volatile int pred_x = 0;
    static volatile int mispred_x = 0;

    Output::puts("[Branch]\n");
    bench(
        "Predicted",
        runner,
        [&] {
            if (pred_x >= 0)
                pred_x = pred_x + 1;
        },
        2);
    bench(
        "Mispredict",
        runner,
        [&] {
            if (mispred_x & 1)
                mispred_x = mispred_x + 1;
            else
                mispred_x = mispred_x - 1;
        },
        3);

    // Pipeline / dependency chains
    static volatile int indep_a = 1;
    static volatile int indep_b = 2;
    static volatile int indep_c = 3;
    static volatile int indep_d = 4;
    static volatile int dep_x = 1;

    Output::puts("\n[Pipeline]\n");
    bench(
        "Independent",
        runner,
        [&] {
            indep_a += 1;
            indep_b += 2;
            indep_c += 3;
            indep_d += 4;
        },
        0);
    bench("Dependent", runner, [&] { dep_x = dep_x * 2 + 1; }, 0);

    // Arithmetic with expected cycle counts (Cortex-M4 specific)
    static volatile int add_x = 1;
    static volatile int mul_x = 2;
    static volatile int div_x = 100000;
    static volatile int div_y = 2;

    Output::puts("\n[Arithmetic]\n");
    bench("ADD", runner, [&] { add_x += 1; }, 1);
    bench("MUL", runner, [&] { mul_x *= 2; }, 1);
    bench("DIV", runner, [&] { div_x = div_x / div_y; }, 12);

    // Float with expected cycle counts
    static volatile float fadd_x = 1.0f;
    static volatile float fmul_x = 1.0f;
    static volatile float fdiv_x = 100.0f;
    static volatile float fdiv_y = 2.0f;

    Output::puts("\n[Float]\n");
    bench("FADD", runner, [&] { fadd_x += 1.0f; }, 1);
    bench("FMUL", runner, [&] { fmul_x *= 1.1f; }, 1);
    bench("FDIV", runner, [&] { fdiv_x = fdiv_x / fdiv_y; }, 14);

    Output::puts("\n=== Done ===\n");
    Platform::halt();
    return 0;
}
