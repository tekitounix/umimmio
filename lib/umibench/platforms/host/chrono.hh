// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief Host timer backend based on `std::chrono::steady_clock`.

#include <chrono>
#include <cstdint>

namespace umi::bench {

/// @brief `std::chrono`-based timer for host benchmarking in nanoseconds.
struct ChronoTimer {
    /// @brief Timer counter type in nanoseconds.
    using Counter = std::uint64_t;

    /// @brief Reset the timer epoch to the current time point.
    static void enable() { base() = Clock::now(); }

    /// @brief Read elapsed time since the last `enable()`.
    /// @return Elapsed time in nanoseconds.
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
