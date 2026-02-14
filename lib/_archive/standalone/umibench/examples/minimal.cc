// SPDX-License-Identifier: MIT
/// @file
/// @brief Minimal end-to-end benchmark example.

#include <umibench/bench.hh>
#include <umibench/platform.hh>

/// @brief Program entry point for the minimal benchmark example.
int main() {
    using Platform = umi::bench::Platform;
    using Timer = Platform::Timer;

    umi::bench::Runner<Timer> runner;
    runner.calibrate<64>();

    auto stats = runner.run<64>(100, [] {
        volatile int x = 0;
        x += 1;
        (void)x;
    });

    umi::bench::report<Platform>("minimal", stats);
    Platform::halt();
    return 0;
}
