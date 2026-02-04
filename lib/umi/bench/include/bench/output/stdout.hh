// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <cstdio>

namespace umi::bench {

/// Standard output for host benchmarking
struct StdoutOutput {
    static void init() {}

    static void putc(char c) { std::fputc(c, stdout); }

    static void puts(const char* s) { std::fputs(s, stdout); }

    static void print_uint(std::uint32_t value) { std::printf("%u", value); }
};

} // namespace umi::bench
