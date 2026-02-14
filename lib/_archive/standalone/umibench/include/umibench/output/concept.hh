// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief Output concept used by report helpers and platforms.

#include <concepts>
#include <cstdint>

namespace umi::bench {

/// @brief Concept for benchmark output backends.
/// @tparam T Output backend type.
template <typename T>
concept OutputLike = requires(const char* s, char c, std::uint64_t n, double d) {
    { T::init() } -> std::same_as<void>;
    { T::putc(c) } -> std::same_as<void>;
    { T::puts(s) } -> std::same_as<void>;
    { T::print_uint(n) } -> std::same_as<void>;
    { T::print_double(d) } -> std::same_as<void>;
};

} // namespace umi::bench
