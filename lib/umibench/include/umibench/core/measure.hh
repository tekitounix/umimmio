// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief Primitive measurement helpers built on TimerLike timers.

#include <atomic>
#include <cstdint>
#include <utility>

#include "umibench/timer/concept.hh"

namespace umi::bench {

/// @brief Measure one callable invocation.
/// @tparam Timer Timer implementation that satisfies TimerLike.
/// @tparam Func Callable type.
/// @param func Callable to execute.
/// @return Elapsed counter ticks reported by the timer.
/// @note Uses signal fences to reduce compiler reordering across timing boundaries.
template <TimerLike Timer, typename Func>
typename Timer::Counter measure(Func&& func) {
    // Compiler barrier to prevent reordering of timer reads
    std::atomic_signal_fence(std::memory_order_acquire);
    const auto start = Timer::now();
    std::atomic_signal_fence(std::memory_order_release);
    std::forward<Func>(func)();
    std::atomic_signal_fence(std::memory_order_acquire);
    const auto end = Timer::now();
    std::atomic_signal_fence(std::memory_order_release);
    return end - start;
}

/// @brief Measure repeated callable invocations.
/// @tparam Timer Timer implementation that satisfies TimerLike.
/// @tparam Func Callable type.
/// @param func Callable to execute.
/// @param iterations Number of invocations to execute.
/// @return Elapsed counter ticks for all invocations.
template <TimerLike Timer, typename Func>
typename Timer::Counter measure_n(Func&& func, std::uint32_t iterations) {
    return measure<Timer>([&func, iterations] {
        for (std::uint32_t i = 0; i < iterations; ++i) {
            func();
        }
    });
}

/// @brief Measure once and subtract baseline overhead.
/// @tparam Timer Timer implementation that satisfies TimerLike.
/// @tparam Func Callable type.
/// @param func Callable to execute.
/// @param baseline Baseline overhead value to subtract.
/// @return Saturating result (`0` when measured value is not greater than baseline).
template <TimerLike Timer, typename Func>
typename Timer::Counter measure_corrected(Func&& func, typename Timer::Counter baseline) {
    const auto measured = measure<Timer>(std::forward<Func>(func));
    return (measured > baseline) ? (measured - baseline) : 0;
}

/// @brief Measure repeated invocations and subtract baseline overhead.
/// @tparam Timer Timer implementation that satisfies TimerLike.
/// @tparam Func Callable type.
/// @param func Callable to execute.
/// @param baseline Baseline overhead value to subtract.
/// @param iterations Number of invocations to execute.
/// @return Saturating result (`0` when measured value is not greater than baseline).
template <TimerLike Timer, typename Func>
typename Timer::Counter measure_corrected_n(Func&& func, typename Timer::Counter baseline, std::uint32_t iterations) {
    const auto measured = measure_n<Timer>(std::forward<Func>(func), iterations);
    return (measured > baseline) ? (measured - baseline) : 0;
}

} // namespace umi::bench
