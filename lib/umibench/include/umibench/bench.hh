// SPDX-License-Identifier: MIT
#pragma once

// Core
#include "umibench/core/measure.hh"
#include "umibench/core/runner.hh"
#include "umibench/core/stats.hh"

// Timer
#include "umibench/timer/concept.hh"

// Output
#include "umibench/output/concept.hh"

namespace umi::bench {

/// Report a benchmark result
template <typename Output>
void report(const char* name, const Stats& stats, std::uint32_t expected = 0) {
    Output::puts("  ");
    Output::puts(name);
    Output::puts(": ");
    Output::print_uint(static_cast<std::uint32_t>(stats.min));
    Output::puts(" cy");

    if (stats.iterations > 1) {
        Output::puts(" (net=");
        Output::print_uint(static_cast<std::uint32_t>(stats.min / stats.iterations));
        Output::puts("/iter");
        if (expected > 0) {
            Output::puts(" exp=");
            Output::print_uint(expected);
        }
        Output::puts(")");
    }
    Output::puts("\n");
}

} // namespace umi::bench
