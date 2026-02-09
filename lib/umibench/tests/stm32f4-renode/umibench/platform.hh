// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief STM32F4 Renode benchmark platform adapter.
/// @details Delegates Timer and Output::init/putc to umi::port::Platform,
///          retaining umibench-specific formatted print methods.

#include <cstdint>
#include <umiport/board/stm32f4-renode/platform.hh>

namespace umi::bench::target {

/// @brief Extended output backend for benchmarks.
///
/// Delegates init/putc to umi::port::Platform::Output and adds
/// formatted print methods required by the benchmark framework.
struct BenchOutput {
    /// @brief Initialize the output backend.
    static void init() { umi::port::Platform::Output::init(); }

    /// @brief Transmit one character.
    /// @param c Character to transmit.
    static void putc(char c) { umi::port::Platform::Output::putc(c); }

    /// @brief Transmit a null-terminated string.
    /// @param s String to transmit.
    static void puts(const char* s) {
        while (*s != '\0') {
            putc(*s++);
        }
    }

    /// @brief Print an unsigned integer in decimal.
    /// @param value Value to print.
    static void print_uint(std::uint64_t value) {
        if (value == 0) {
            putc('0');
            return;
        }
        char buf[21];
        int i = 0;
        while (value > 0) {
            buf[i++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }
        while (i-- > 0) {
            putc(buf[i]);
        }
    }

    /// @brief Print a floating-point value with two decimals.
    /// @param value Value to print.
    static void print_double(double value) {
        if (value < 0.0) {
            putc('-');
            value = -value;
        }
        auto integer_part = static_cast<std::uint64_t>(value);
        print_uint(integer_part);
        putc('.');
        double frac = value - static_cast<double>(integer_part);
        auto frac_int = static_cast<std::uint64_t>((frac * 100.0) + 0.5);
        if (frac_int < 10) {
            putc('0');
        }
        print_uint(frac_int);
    }
};

/// @brief STM32F4 Renode benchmark platform definition.
struct Platform {
    /// @brief Timer backend — delegated from umi::port::Platform.
    using Timer = umi::port::Platform::Timer;
    /// @brief Output backend with benchmark-specific extensions.
    using Output = BenchOutput;

    /// @brief Platform name shown in reports.
    /// @return `"stm32f4"`.
    static constexpr const char* target_name() { return "stm32f4"; }
    /// @brief Timer unit shown in reports.
    /// @return `"cy"`.
    static constexpr const char* timer_unit() { return "cy"; }

    /// @brief Initialize timer and output backend.
    static void init() {
        Timer::enable();
        Output::init();
    }

    /// @brief Halt the CPU in low-power wait-for-interrupt loop.
    [[noreturn]] static void halt() {
        while (true) {
            asm volatile("wfi");
        }
    }
};

} // namespace umi::bench::target

namespace umi::bench {
/// @brief Convenience alias to the selected target platform type.
using Platform = target::Platform;
} // namespace umi::bench
