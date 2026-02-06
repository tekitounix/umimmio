// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief Null output backend that discards all benchmark output.

#include <cstdint>

namespace umi::bench {

/// @brief Output backend that drops all bytes.
struct NullOutput {
    static void init() {}
    static void putc(char) {}
    static void puts(const char*) {}
    static void print_uint(std::uint64_t) {}
    static void print_double(double) {}
};

} // namespace umi::bench
