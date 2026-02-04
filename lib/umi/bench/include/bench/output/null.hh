// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace umi::bench {

/// Null output (discard all output)
struct NullOutput {
    static void init() {}
    static void putc(char) {}
    static void puts(const char*) {}
    static void print_uint(std::uint32_t) {}
};

} // namespace umi::bench
