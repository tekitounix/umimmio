// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief Host output backend that writes benchmark text to stdout.

#include <cstdint>
#include <cstdio>

namespace umi::bench {

/// @brief Standard output backend for host benchmarks.
struct StdoutOutput {
    /// @brief Initialize output backend (no-op on host).
    static void init() {}

    /// @brief Write one character.
    /// @param c Character to write.
    static void putc(char c) { std::fputc(c, stdout); }

    /// @brief Write a C-string.
    /// @param s Null-terminated string to write.
    static void puts(const char* s) { std::fputs(s, stdout); }

    /// @brief Write an unsigned integer.
    /// @param value Integer value.
    static void print_uint(std::uint64_t value) { std::printf("%llu", static_cast<unsigned long long>(value)); }

    /// @brief Write a floating-point number with two decimals.
    /// @param value Floating-point value.
    static void print_double(double value) { std::printf("%.2f", value); }
};

} // namespace umi::bench
