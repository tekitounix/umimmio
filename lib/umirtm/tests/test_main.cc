// SPDX-License-Identifier: MIT
/// @file
/// @brief Test runner entry point for umirtm tests.

#include "test_fixture.hh"

int main() {
    umi::test::Suite suite("umirtm");

    umirtm::test::run_monitor_tests(suite);
    umirtm::test::run_printf_tests(suite);
    umirtm::test::run_print_tests(suite);
    umirtm::test::run_printf_extended_tests(suite);
    umirtm::test::run_monitor_extended_tests(suite);
    umirtm::test::run_print_extended_tests(suite);

    return suite.summary();
}
