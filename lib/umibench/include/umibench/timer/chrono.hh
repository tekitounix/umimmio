// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>
#include <cstdint>

namespace umi::bench {

/// std::chrono-based timer for host benchmarking (nanoseconds)
struct ChronoTimer {
    using Counter = std::uint64_t;

    static void enable() { base() = Clock::now(); }

    static Counter now() {
        const auto elapsed = Clock::now() - base();
        return static_cast<Counter>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    }

  private:
    using Clock = std::chrono::steady_clock;

    static Clock::time_point& base() {
        static Clock::time_point value = Clock::now();
        return value;
    }
};

} // namespace umi::bench
