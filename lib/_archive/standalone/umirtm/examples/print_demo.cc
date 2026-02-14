// SPDX-License-Identifier: MIT
/// @file
/// @brief Demonstration of rt::print / rt::println with {} placeholders.

#include <umirtm/print.hh>

int main() {
    // Basic types
    rt::println("integer: {}", 42);
    rt::println("unsigned: {}", 100u);
    rt::println("float: {}", 3.14);
    rt::println("char: {}", 'A');
    rt::println("string: {}", "hello");

    // Multiple arguments
    rt::println("{} + {} = {}", 1, 2, 3);

    // Escaped braces
    rt::println("use {{}} for literal braces");

    // Format override (hex)
    rt::println("hex: {0:x}", 255);

    // Print without newline
    rt::print("no newline ");
    rt::print("here ");
    rt::println(""); // flush with newline

    // Bare println
    rt::println();

    return 0;
}
