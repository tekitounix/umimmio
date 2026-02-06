// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief Shared fixtures and helper platform/output types for umibench tests.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <umibench/bench.hh>
#include <umibench/core/measure.hh>
#include <umibench/core/runner.hh>
#include <umibench/core/stats.hh>
#include <umibench/output/null.hh>
#include <umibench/platform.hh>
#include <umibench/timer/concept.hh>
#include <umitest/test.hh>

namespace umibench::test {

/// @brief Platform type selected for host-side tests.
using TestPlatform = umi::bench::Platform;
/// @brief Timer backend used in tests.
using TestTimer = TestPlatform::Timer;

/// @brief Platform variant that suppresses output.
struct NullPlatform {
    /// @brief Timer backend.
    using Timer = TestTimer;
    /// @brief Output backend.
    using Output = umi::bench::NullOutput;

    /// @brief Reported target name.
    static constexpr const char* target_name() { return "test"; }
    /// @brief Reported timer unit.
    static constexpr const char* timer_unit() { return "ns"; }

    /// @brief Initialize platform (no-op).
    static void init() {}
    /// @brief Halt platform (no-op).
    static void halt() {}
};

/// @brief Output backend that captures emitted text into an in-memory buffer.
struct CaptureOutput {
    /// @brief Reset capture buffer.
    static void init() { buffer().clear(); }

    /// @brief Append one character to capture buffer.
    /// @param c Character to append.
    static void putc(char c) { buffer().append_char(c); }

    /// @brief Append a C-string to capture buffer.
    /// @param s Null-terminated string to append.
    static void puts(const char* s) {
        if (s != nullptr) {
            while (*s != '\0') {
                putc(*s++);
            }
        }
    }

    /// @brief Append an unsigned integer in decimal format.
    /// @param value Value to append.
    static void print_uint(std::uint64_t value) {
        if (value == 0) {
            putc('0');
            return;
        }
        char digits[21];
        std::size_t i = 0;
        while (value > 0) {
            digits[i++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }
        while (i > 0) {
            putc(digits[--i]);
        }
    }

    /// @brief Append a floating-point value with two decimals.
    /// @param value Value to append.
    static void print_double(double value) {
        if (value < 0.0) {
            putc('-');
            value = -value;
        }
        const auto integer_part = static_cast<std::uint64_t>(value);
        print_uint(integer_part);
        putc('.');
        const double frac = value - static_cast<double>(integer_part);
        const auto frac_int = static_cast<std::uint64_t>(frac * 100.0);
        if (frac_int < 10) {
            putc('0');
        }
        print_uint(frac_int);
    }

    /// @brief Clear capture buffer contents.
    static void clear() { buffer().clear(); }

    /// @brief Check whether captured text contains a substring.
    /// @param needle Substring to search.
    /// @return `true` when substring is present.
    static bool contains(const char* needle) { return std::strstr(buffer().data, needle) != nullptr; }

  private:
    /// @brief Fixed-size text buffer used for output capture.
    struct FixedBuffer {
        static constexpr std::size_t capacity = 4096; ///< Maximum buffer bytes.
        char data[capacity]{};                        ///< Null-terminated captured text storage.
        std::size_t len = 0;                          ///< Current text length.

        /// @brief Reset buffer to empty state.
        void clear() {
            len = 0;
            data[0] = '\0';
        }

        /// @brief Append one character if capacity allows.
        /// @param c Character to append.
        void append_char(char c) {
            if (len + 1 < capacity) {
                data[len++] = c;
                data[len] = '\0';
            }
        }
    };

    /// @brief Access singleton capture buffer.
    /// @return Buffer instance reference.
    static FixedBuffer& buffer() {
        static FixedBuffer text;
        return text;
    }
};

/// @brief Platform variant that uses `CaptureOutput`.
struct CapturePlatform {
    /// @brief Timer backend.
    using Timer = TestTimer;
    /// @brief Output backend.
    using Output = CaptureOutput;

    /// @brief Reported target name.
    static constexpr const char* target_name() { return "capture"; }
    /// @brief Reported timer unit.
    static constexpr const char* timer_unit() { return "ns"; }

    /// @brief Initialize platform output.
    static void init() { Output::init(); }
    /// @brief Halt platform (no-op).
    static void halt() {}
};

/// @brief Register timer and measure tests.
void run_timer_measure_tests(umi::test::Suite& suite);
/// @brief Register stats and runner tests.
void run_stats_runner_tests(umi::test::Suite& suite);
/// @brief Register platform/output/report tests.
void run_platform_output_report_tests(umi::test::Suite& suite);
/// @brief Register integration tests.
void run_integration_tests(umi::test::Suite& suite);

} // namespace umibench::test
