// SPDX-License-Identifier: MIT
// Comprehensive Instruction Benchmark (host version, new API)
#include <bench/bench.hh>
#include <bench/platform/host.hh>
#include <cstdint>

namespace {

using Platform = umi::bench::Host;
using Output = Platform::Output;

constexpr std::uint32_t iterations = 100;

template <typename Func>
void bench(const char* name, umi::bench::Runner<Platform::Timer>& runner, Func&& func) {
    auto stats = runner.run<64>(iterations, func);
    umi::bench::report<Output>(name, stats);
}

} // namespace

int main() {
    Platform::init();
    Output::puts("\n=== Comprehensive Instruction Benchmark (Host) ===\n");

    umi::bench::Runner<Platform::Timer> runner;
    runner.calibrate<256>();

    static volatile int add_x = 1;
    static volatile int mul_x = 2;
    static volatile int div_x = 100000;
    static volatile int div_y = 2;
    static volatile float fadd_x = 1.0f;
    static volatile float fmul_x = 1.0f;
    static volatile float fdiv_x = 100.0f;
    static volatile float fdiv_y = 2.0f;

    Output::puts("Baseline: ");
    Output::print_uint(static_cast<std::uint32_t>(runner.get_baseline()));
    Output::puts(" ns\n\n");

    Output::puts("Arithmetic:\n");
    bench("ADD", runner, [&] { add_x += 1; });
    bench("MUL", runner, [&] { mul_x *= 2; });
    bench("DIV", runner, [&] { div_x = div_x / div_y; });

    Output::puts("\nFloat:\n");
    bench("FADD", runner, [&] { fadd_x += 1.0f; });
    bench("FMUL", runner, [&] { fmul_x *= 1.1f; });
    bench("FDIV", runner, [&] { fdiv_x = fdiv_x / fdiv_y; });

    Output::puts("\n=== Done ===\n");
    return 0;
}
