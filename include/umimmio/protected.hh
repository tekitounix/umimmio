#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file protected.hh
/// @brief RAII-based exclusive access control for transport objects.
/// @author Shota Moriguchi @tekitounix

#include <type_traits>
#include <utility>

namespace umi::mmio {

// ===========================================================================
// Lock Policies
// ===========================================================================

/// @brief Bare-metal critical section via interrupt disable.
///
/// ARM (Cortex-M) ターゲット専用。テンプレート遅延インスタンス化により、
/// ヘッダを include するだけではエラーにならず、実際に lock()/unlock() を
/// 呼び出した時点で static_assert が発火する。
/// `__aarch64__` は対象外 (`cpsid i` は A32/T32 命令)。
struct CriticalSectionPolicy {
    template <bool IsArm =
#if defined(__arm__)
                  true
#else
                  false
#endif
              >
    static void lock() noexcept {
        static_assert(IsArm,
                      "CriticalSectionPolicy is ARM (Cortex-M) only. "
                      "Use NoLockPolicy or MutexPolicy on other platforms.");
        __asm volatile("cpsid i" ::: "memory");
    }

    template <bool IsArm =
#if defined(__arm__)
                  true
#else
                  false
#endif
              >
    static void unlock() noexcept {
        static_assert(IsArm,
                      "CriticalSectionPolicy is ARM (Cortex-M) only. "
                      "Use NoLockPolicy or MutexPolicy on other platforms.");
        __asm volatile("cpsie i" ::: "memory");
    }
};

/// @brief RTOS mutex wrapper.
/// @tparam MutexT Mutex type with lock()/unlock() methods.
template <typename MutexT>
struct MutexPolicy {
    MutexT& mtx;
    void lock() noexcept { mtx.lock(); }
    void unlock() noexcept { mtx.unlock(); }
};

/// @brief No-op lock for single-threaded or test contexts.
struct NoLockPolicy {
    static void lock() noexcept {}
    static void unlock() noexcept {}
};

// ===========================================================================
// Guard — RAII scoped access to the protected value
// ===========================================================================

/// @brief RAII guard providing exclusive access to a Protected<T> value.
///
/// The only way to access the inner T of a Protected<T, Policy>.
/// Lock is acquired on construction and released on destruction.
///
/// @tparam T          Protected value type.
/// @tparam LockPolicy Lock policy type.
template <typename T, typename LockPolicy>
class Guard {
    T& ref;
    LockPolicy& policy;

  public:
    /// @brief Acquire lock and bind to the protected value.
    explicit Guard(T& r, LockPolicy& p) noexcept : ref(r), policy(p) { policy.lock(); }

    /// @brief Release lock.
    ~Guard() { policy.unlock(); }

    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    Guard(Guard&&) = delete;
    Guard& operator=(Guard&&) = delete;

    /// @brief Access the protected value.
    T& operator*() noexcept { return ref; }
    T* operator->() noexcept { return &ref; }
    const T& operator*() const noexcept { return ref; }
    const T* operator->() const noexcept { return &ref; }
};

// ===========================================================================
// Protected — wrapper preventing direct access without lock
// ===========================================================================

/// @brief Wraps a value, exposing it only through a locked Guard.
///
/// Provides the same safety guarantee as Rust's Mutex<T>:
/// you cannot access the inner value without holding the lock.
///
/// @note This is an opt-in pattern. DirectTransport remains stateless and
///       accessible without Protected. Enforce Protected usage via project
///       conventions and code review.
///
/// @tparam T          Value type (typically a transport object).
/// @tparam LockPolicy Lock policy (CriticalSectionPolicy, MutexPolicy, NoLockPolicy).
template <typename T, typename LockPolicy>
class Protected {
    T inner;
    [[no_unique_address]] LockPolicy policy;

  public:
    /// @brief Construct with a pre-built lock policy and forwarded args for T.
    template <typename... Args>
    explicit Protected(LockPolicy p, Args&&... args) noexcept
        : inner(std::forward<Args>(args)...), policy(std::move(p)) {}

    /// @brief Construct with default-constructed lock policy.
    ///        Available only when LockPolicy is default-constructible (e.g., CriticalSectionPolicy).
    template <typename... Args>
        requires std::is_default_constructible_v<LockPolicy>
    explicit Protected(Args&&... args) noexcept : inner(std::forward<Args>(args)...), policy{} {}

    /// @brief Acquire the lock and return a Guard for exclusive access.
    [[nodiscard]] Guard<T, LockPolicy> lock() noexcept { return Guard<T, LockPolicy>{inner, policy}; }

    // No direct access to inner — this is the entire point.
};

} // namespace umi::mmio
