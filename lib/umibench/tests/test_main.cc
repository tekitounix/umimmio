// SPDX-License-Identifier: MIT
/// @file
/// @brief Test runner entry point for umibench host tests.

#include "test_fixture.hh"

/// @brief Program entry point for running all umibench test sections.
int main() {
    umi::test::Suite suite("bench");

    umibench::test::run_timer_measure_tests(suite);
    umibench::test::run_stats_runner_tests(suite);
    umibench::test::run_platform_output_report_tests(suite);
    umibench::test::run_integration_tests(suite);

    return suite.summary();
}
