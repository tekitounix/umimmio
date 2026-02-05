// SPDX-License-Identifier: MIT
// umibench host test with UART output simulation
#include <iostream>
#include <umibench/bench.hh>
#include <umibench/platform/host.hh>

using namespace umi::bench;

int main() {
    std::cout << "umibench Host Test\n";

    // Initialize platform
    Host::init();

    // Create runner
    Runner<Host::Timer> runner;
    runner.calibrate<64>();

    // Simple benchmark: increment counter
    auto stats = runner.run<64>(1000, [] {
        volatile uint32_t counter = 0;
        for (uint32_t i = 0; i < 1000; ++i) {
            counter++;
        }
    });

    // Report results
    std::cout << "Benchmark Results:\n";
    std::cout << "  min: " << stats.min << " cycles\n";
    std::cout << "  mean: " << stats.mean << " cycles\n";
    std::cout << "  median: " << stats.median << " cycles\n";
    std::cout << "  max: " << stats.max << " cycles\n";

    return 0;
}
