// SPDX-License-Identifier: MIT
/// @file
/// @brief Example showing lambda-style benchmark registration.

#include <umibench/bench.hh>
#include <umibench/platform.hh>

namespace {

using Platform = umi::bench::Platform;
using Timer = Platform::Timer;
using Output = Platform::Output;

/// @brief Execute one benchmark case and emit full report.
/// @tparam Func Callable type.
/// @param name Benchmark name.
/// @param runner Benchmark runner.
/// @param func Callable to benchmark.
template <typename Func>
void bench(const char* name, umi::bench::Runner<Timer>& runner, Func&& func) {
    auto stats = runner.run<64>(100, func);
    umi::bench::report<Platform>(name, stats);
}

} // namespace

/// @brief Program entry point for lambda-style benchmark example.
int main() {
    umi::bench::Runner<Timer> runner;
    runner.calibrate<64>();

    // Arithmetic operations
    static volatile int add_x = 1;
    static volatile int mul_x = 2;
    static volatile int div_x = 100000;
    static volatile int div_y = 2;

    bench("ADD", runner, [&] { add_x += 1; });
    bench("MUL", runner, [&] { mul_x *= 2; });
    bench("DIV", runner, [&] { div_x = div_x / div_y; });

    // Float operations
    static volatile float fadd_x = 1.0f;
    static volatile float fmul_x = 1.0f;
    static volatile float fdiv_x = 100.0f;
    static volatile float fdiv_y = 2.0f;

    bench("FADD", runner, [&] { fadd_x += 1.0f; });
    bench("FMUL", runner, [&] { fmul_x *= 1.1f; });
    bench("FDIV", runner, [&] { fdiv_x = fdiv_x / fdiv_y; });

    Platform::halt();
    return 0;
}
