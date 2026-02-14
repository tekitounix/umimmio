// SPDX-License-Identifier: MIT
/// @file
/// @brief Shared test declarations for umirtm tests.
#pragma once

#include <umirtm/print.hh>
#include <umirtm/printf.hh>
#include <umirtm/rtm.hh>
#include <umitest/test.hh>

namespace umirtm::test {

/// @brief Register Monitor ring buffer lifecycle tests.
void run_monitor_tests(umi::test::Suite& suite);

/// @brief Register printf/snprintf format tests.
void run_printf_tests(umi::test::Suite& suite);

/// @brief Register print/println (Rust-style) format tests.
void run_print_tests(umi::test::Suite& suite);

/// @brief Extended printf tests (configs, edge cases, flags).
void run_printf_extended_tests(umi::test::Suite& suite);

/// @brief Extended Monitor tests (multi-buffer, modes, control block).
void run_monitor_extended_tests(umi::test::Suite& suite);

/// @brief Extended print/println tests (brace escaping, overrides, edge cases).
void run_print_extended_tests(umi::test::Suite& suite);

} // namespace umirtm::test
