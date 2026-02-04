// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>
#include <cstdint>

namespace umi::bench {

/// std::chrono-based timer for host benchmarking (nanoseconds)
struct ChronoTimer {
    using Counter = std::uint64_t;

    static void enable() { base() = clock::now(); }

    static Counter now() {
        const auto elapsed = clock::now() - base();
        return static_cast<Counter>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    }

  private:
    using clock = std::chrono::steady_clock;

    static clock::time_point& base() {
        static clock::time_point value = clock::now();
        return value;
    }
};

} // namespace umi::bench
