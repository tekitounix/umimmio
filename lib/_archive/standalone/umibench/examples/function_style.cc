// SPDX-License-Identifier: MIT
/// @file
/// @brief Example showing function-symbol and wrapped-call benchmarking.

#include <umibench/bench.hh>
#include <umibench/platform.hh>

namespace {

volatile int accumulator = 0;

/// @brief Target function used as benchmark subject.
void target_function() {
    accumulator += 1;
}

} // namespace

/// @brief Program entry point for function-style benchmark example.
int main() {
    using Platform = umi::bench::Platform;
    using Timer = Platform::Timer;

    umi::bench::Runner<Timer> runner;
    runner.calibrate<64>();

    // Function pointer style
    auto stats = runner.run<64>(100, target_function);
    umi::bench::report<Platform>("target_function", stats);

    // Lambda wrapping function call — equivalent (§5.2)
    auto stats2 = runner.run<64>(100, [] { target_function(); });
    umi::bench::report<Platform>("target_function_lambda", stats2);

    Platform::halt();
    return 0;
}
