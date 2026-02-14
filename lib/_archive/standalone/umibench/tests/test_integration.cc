// SPDX-License-Identifier: MIT
/// @file
/// @brief End-to-end workflow integration tests.

#include "test_fixture.hh"

namespace umibench::test {
namespace {

using umi::bench::Runner;
using umi::test::TestContext;

bool test_full_benchmark_workflow(TestContext& t) {
    using Platform = TestPlatform;

    Runner<Platform::Timer> runner;
    runner.calibrate<64>();

    volatile int counter = 0;
    const auto stats = runner.run<32>(10, [&] { counter += 1; });

    bool ok = true;
    ok &= t.assert_eq(stats.samples, 32u);
    ok &= t.assert_eq(stats.iterations, 10u);
    ok &= t.assert_le(stats.min, stats.median);
    ok &= t.assert_le(stats.median, stats.max);
    return ok;
}

} // namespace

/// @brief Register integration test cases.
/// @param suite Test suite to register into.
void run_integration_tests(umi::test::Suite& suite) {
    umi::test::Suite::section("Integration");
    suite.run("full_benchmark_workflow", test_full_benchmark_workflow);
}

} // namespace umibench::test
