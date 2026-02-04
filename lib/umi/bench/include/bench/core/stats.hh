// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace umi::bench {

/// Benchmark statistics
struct Stats {
    std::uint64_t min = 0;
    std::uint64_t max = 0;
    std::uint64_t mean = 0;
    std::uint64_t median = 0;
    std::uint32_t samples = 0;
    std::uint32_t iterations = 1;
};

namespace detail {

/// In-place insertion sort (O(n²) but no heap allocation, suitable for small N)
template <typename T, std::size_t N>
void insertion_sort(std::array<T, N>& arr) {
    for (std::size_t i = 1; i < N; ++i) {
        T key = arr[i];
        std::size_t j = i;
        while (j > 0 && arr[j - 1] > key) {
            arr[j] = arr[j - 1];
            --j;
        }
        arr[j] = key;
    }
}

} // namespace detail

/// Compute statistics from samples
template <typename Counter, std::size_t N>
Stats compute_stats(const std::array<Counter, N>& samples, std::uint32_t iterations = 1) {
    static_assert(N > 0, "Need at least one sample");

    Stats stats;
    stats.samples = static_cast<std::uint32_t>(N);
    stats.iterations = iterations;
    stats.min = samples[0];
    stats.max = samples[0];

    std::uint64_t sum = 0;
    for (const auto value : samples) {
        if (value < stats.min)
            stats.min = value;
        if (value > stats.max)
            stats.max = value;
        sum += value;
    }
    stats.mean = sum / N;

    // Sort for median
    std::array<Counter, N> sorted = samples;
    detail::insertion_sort(sorted);

    // Proper median: average of middle two for even N
    if constexpr (N % 2 == 0) {
        stats.median = (sorted[N / 2 - 1] + sorted[N / 2]) / 2;
    } else {
        stats.median = sorted[N / 2];
    }

    return stats;
}

} // namespace umi::bench
