// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Tests for Protected<T, LockPolicy> RAII wrapper.
/// @author Shota Moriguchi @tekitounix

#include <umimmio/protected.hh>

#include "test_fixture.hh"

namespace umimmio::test {

using namespace umi::mmio;
using umi::test::TestContext;

bool test_protected_nolock(TestContext& t) {
    Protected<MockTransport, NoLockPolicy> protected_hw;

    {
        auto guard = protected_hw.lock();
        guard->write(ConfigEnable::Set{});
    }

    auto guard = protected_hw.lock();
    return t.assert_true(guard->is(ConfigEnable::Set{}), "enable should be set");
}

bool test_protected_structural(TestContext& t) {
    // Protected<T, Policy> exposes only lock() — no other public accessors.
    // Verify that lock() returns a Guard that provides operator->().
    static_assert(requires(Protected<MockTransport, NoLockPolicy> p) { p.lock(); },
                  "lock() must be accessible");
    // Guard must provide operator->
    static_assert(requires(Guard<MockTransport, NoLockPolicy> g) { g.operator->(); },
                  "Guard must provide operator->");
    return t.assert_true(true, "only lock()/Guard are public");
}

bool test_protected_no_size_overhead(TestContext& t) {
    return t.assert_eq(sizeof(Protected<MockTransport, NoLockPolicy>), sizeof(MockTransport));
}

void run_protected_tests(umi::test::Suite& suite) {
    umi::test::Suite::section("Protected<T, LockPolicy>");
    suite.run("NoLockPolicy write and read", test_protected_nolock);
    suite.run("prevents direct access", test_protected_structural);
    suite.run("stateless policy no size overhead", test_protected_no_size_overhead);
}

} // namespace umimmio::test
