// SPDX-License-Identifier: MIT
/// @file
/// @brief Demonstration of rt::snprintf format specifiers.

#include <array>
#include <cstdio>
#include <umirtm/printf.hh>

int main() {
    std::array<char, 256> buf{};

    // Integer formats
    rt::snprintf(buf.data(), buf.size(), "decimal: %d, unsigned: %u\n", -42, 42u);
    std::printf("%s", buf.data());

    // Hex and octal
    rt::snprintf(buf.data(), buf.size(), "hex: 0x%x, UPPER: 0x%X, octal: %o\n", 255, 255, 255);
    std::printf("%s", buf.data());

    // String and char
    rt::snprintf(buf.data(), buf.size(), "string: '%s', char: '%c'\n", "hello", 'Z');
    std::printf("%s", buf.data());

    // Float
    rt::snprintf(buf.data(), buf.size(), "float: %f, scientific: %e, shortest: %g\n", 3.14159, 0.001, 12345.0);
    std::printf("%s", buf.data());

    // Width and padding
    rt::snprintf(buf.data(), buf.size(), "right: [%10d], left: [%-10d], zero: [%010d]\n", 42, 42, 42);
    std::printf("%s", buf.data());

    // Precision
    rt::snprintf(buf.data(), buf.size(), "precision: %.2f, string: %.3s\n", 3.14159, "abcdef");
    std::printf("%s", buf.data());

    // Pointer
    int x = 0;
    rt::snprintf(buf.data(), buf.size(), "pointer: %p\n", static_cast<void*>(&x));
    std::printf("%s", buf.data());

    // Truncation safety
    std::array<char, 8> small{};
    int n = rt::snprintf(small.data(), small.size(), "long string that won't fit");
    std::printf("truncated (%d chars): '%s'\n", n, small.data());

    return 0;
}
