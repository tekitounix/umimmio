// SPDX-License-Identifier: MIT
#pragma once

#include <concepts>
#include <cstdint>

namespace umi::bench {

/// Output concept for benchmark results
template <typename T>
concept OutputLike = requires(const char* s, char c, std::uint32_t n) {
    { T::init() } -> std::same_as<void>;
    { T::putc(c) } -> std::same_as<void>;
    { T::puts(s) } -> std::same_as<void>;
    { T::print_uint(n) } -> std::same_as<void>;
};

} // namespace umi::bench
