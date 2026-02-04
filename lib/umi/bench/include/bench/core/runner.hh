// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "bench/core/measure.hh"
#include "bench/core/stats.hh"
#include "bench/timer/concept.hh"

namespace umi::bench {

/// Benchmark runner with automatic baseline calibration
template <TimerLike Timer, std::size_t DefaultSamples = 64>
class Runner {
  public:
    using Counter = typename Timer::Counter;

    Runner() = default;

    /// Calibrate baseline overhead
    template <std::size_t N = DefaultSamples>
    Runner& calibrate() {
        std::array<Counter, N> samples{};
        for (std::size_t i = 0; i < N; ++i) {
            samples[i] = measure<Timer>([] {});
        }
        baseline = *std::min_element(samples.begin(), samples.end());
        return *this;
    }

    /// Get current baseline value
    Counter get_baseline() const { return baseline; }

    /// Run benchmark: N samples, single iteration per sample
    template <std::size_t N = DefaultSamples, typename Func>
    Stats run(Func&& func) const {
        std::array<Counter, N> samples{};
        for (std::size_t i = 0; i < N; ++i) {
            samples[i] = measure_corrected<Timer>(std::forward<Func>(func), baseline);
        }
        return compute_stats(samples);
    }

    /// Run benchmark: N samples, multiple iterations per sample
    template <std::size_t N = DefaultSamples, typename Func>
    Stats run(std::uint32_t iterations, Func&& func) const {
        std::array<Counter, N> samples{};
        for (std::size_t i = 0; i < N; ++i) {
            samples[i] = measure_corrected_n<Timer>(std::forward<Func>(func), baseline, iterations);
        }
        return compute_stats(samples, iterations);
    }

  private:
    Counter baseline = 0;
};

} // namespace umi::bench
