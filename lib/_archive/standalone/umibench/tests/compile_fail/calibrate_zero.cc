// SPDX-License-Identifier: MIT
/// @file
/// @brief Negative compile test: calibration with zero samples must fail.

#include <cstdint>

#include <umibench/core/runner.hh>

namespace {

struct DummyTimer {
    /// @brief Counter type placeholder for TimerLike compatibility.
    using Counter = std::uint32_t;

    /// @brief No-op timer enable for compile test.
    static void enable() {}
    /// @brief Always returns zero for compile test.
    static Counter now() { return 0; }
};

} // namespace

/// @brief Compile-fail test entrypoint.
int main() {
    umi::bench::Runner<DummyTimer> runner;
    runner.calibrate<0>();
    return 0;
}
