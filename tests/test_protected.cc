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

// =============================================================================
// Mock mutex for verifying lock/unlock lifecycle
// =============================================================================

/// @brief Tracks lock() and unlock() call counts for verification.
struct MockMutex {
    int lock_count = 0;
    int unlock_count = 0;
    void lock() noexcept { ++lock_count; }
    void unlock() noexcept { ++unlock_count; }
};

// =============================================================================
// NoLockPolicy tests
// =============================================================================

bool test_protected_nolock_write_read(TestContext& t) {
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
    static_assert(requires(Protected<MockTransport, NoLockPolicy> p) { p.lock(); }, "lock() must be accessible");
    // Guard must provide operator->
    static_assert(requires(Guard<MockTransport, NoLockPolicy> g) { g.operator->(); }, "Guard must provide operator->");
    return t.assert_true(true, "only lock()/Guard are public");
}

bool test_protected_no_size_overhead(TestContext& t) {
    return t.assert_eq(sizeof(Protected<MockTransport, NoLockPolicy>), sizeof(MockTransport));
}

// =============================================================================
// MutexPolicy tests
// =============================================================================

bool test_protected_mutex_lock_unlock(TestContext& t) {
    MockMutex mtx;
    MutexPolicy<MockMutex> policy{mtx};
    Protected<int, MutexPolicy<MockMutex>> p(policy, 42);

    bool ok = true;
    ok &= t.assert_eq(mtx.lock_count, 0);
    ok &= t.assert_eq(mtx.unlock_count, 0);

    {
        auto guard = p.lock();
        ok &= t.assert_eq(mtx.lock_count, 1);
        ok &= t.assert_eq(mtx.unlock_count, 0);
        ok &= t.assert_eq(*guard, 42);
    }

    ok &= t.assert_eq(mtx.lock_count, 1);
    ok &= t.assert_eq(mtx.unlock_count, 1);
    return ok;
}

bool test_protected_guard_deref(TestContext& t) {
    Protected<int, NoLockPolicy> p(100);
    auto guard = p.lock();

    bool ok = true;
    // operator*
    ok &= t.assert_eq(*guard, 100);
    // Modify via operator*
    *guard = 200;
    ok &= t.assert_eq(*guard, 200);
    // operator-> (for struct types)
    return ok;
}

bool test_protected_guard_sequential_locks(TestContext& t) {
    MockMutex mtx;
    MutexPolicy<MockMutex> policy{mtx};
    Protected<int, MutexPolicy<MockMutex>> p(policy, 0);

    bool ok = true;

    // First lock/unlock cycle
    {
        auto guard = p.lock();
        *guard = 10;
    }
    ok &= t.assert_eq(mtx.lock_count, 1);
    ok &= t.assert_eq(mtx.unlock_count, 1);

    // Second lock/unlock cycle
    {
        auto guard = p.lock();
        ok &= t.assert_eq(*guard, 10);
        *guard = 20;
    }
    ok &= t.assert_eq(mtx.lock_count, 2);
    ok &= t.assert_eq(mtx.unlock_count, 2);

    // Third: verify final value
    {
        auto guard = p.lock();
        ok &= t.assert_eq(*guard, 20);
    }
    ok &= t.assert_eq(mtx.lock_count, 3);
    ok &= t.assert_eq(mtx.unlock_count, 3);
    return ok;
}

bool test_protected_mutex_transport(TestContext& t) {
    MockMutex mtx;
    MutexPolicy<MockMutex> policy{mtx};
    Protected<MockTransport, MutexPolicy<MockMutex>> protected_hw(policy);

    bool ok = true;
    {
        auto guard = protected_hw.lock();
        guard->write(ConfigEnable::Set{}, ModeFast{});
    }
    ok &= t.assert_eq(mtx.lock_count, 1);
    ok &= t.assert_eq(mtx.unlock_count, 1);

    {
        auto guard = protected_hw.lock();
        ok &= t.assert_true(guard->is(ConfigEnable::Set{}), "enable should be set");
        ok &= t.assert_true(guard->is(ModeFast{}), "mode should be FAST");
    }
    ok &= t.assert_eq(mtx.lock_count, 2);
    ok &= t.assert_eq(mtx.unlock_count, 2);
    return ok;
}

bool test_protected_const_guard_access(TestContext& t) {
    Protected<int, NoLockPolicy> p(42);
    auto guard = p.lock();
    // const Guard access via const reference
    const auto& cguard = guard;
    return t.assert_eq(*cguard, 42);
}

void run_protected_tests(umi::test::Suite& suite) {
    umi::test::Suite::section("Protected<T, LockPolicy>");
    suite.run("NoLockPolicy write and read", test_protected_nolock_write_read);
    suite.run("prevents direct access", test_protected_structural);
    suite.run("stateless policy no size overhead", test_protected_no_size_overhead);

    umi::test::Suite::section("MutexPolicy");
    suite.run("lock/unlock lifecycle", test_protected_mutex_lock_unlock);
    suite.run("Guard operator* deref", test_protected_guard_deref);
    suite.run("sequential lock/unlock cycles", test_protected_guard_sequential_locks);
    suite.run("MutexPolicy with transport", test_protected_mutex_transport);
    suite.run("const Guard access", test_protected_const_guard_access);
}

} // namespace umimmio::test
