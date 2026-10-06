#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief AtomicTransport — wrapper transport that brackets MMIO accesses with memory barriers.
/// @author Shota Moriguchi @tekitounix
/// @details Wraps an inner transport (typically DirectTransport) and emits
///          compiler + hardware memory barriers around `reg_read` / `reg_write`
///          to provide MMIO ordering guarantees in dual-core, DMA, or IRQ
///          contexts. The std::memory_order template parameter selects the
///          fence strength; for ARM Cortex-M targets, DMB / DSB instructions
///          are emitted at the appropriate boundaries. On host builds, only
///          the compiler fence is emitted (hardware barriers are noops).
///
///          Minimal viable shape (v1): Direct inner transport, seq_cst /
///          release / acquire / relaxed memory orders. Ergonomics extensions
///          (free function helpers, driver-side adoption templates) are
///          follow-up work — see ADR umimmio-atomic-transport.md.

#include <atomic>
#include <type_traits>
#include <utility>

#include "../ops.hh"
#include "../policy.hh" // IWYU pragma: keep  — AssertOnError, Direct

namespace umi::mmio {

#if defined(__GNUC__) || defined(__clang__)
#define UMI_MMIO_ALWAYS_INLINE [[gnu::always_inline]] inline
#else
#define UMI_MMIO_ALWAYS_INLINE inline
#endif

namespace detail {

/// @brief Emit a hardware data memory barrier (DMB) on ARM Cortex-M targets.
/// On host builds, this is a noop — only the compiler fence is required.
UMI_MMIO_ALWAYS_INLINE void hw_dmb() noexcept {
#if defined(__thumb__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_8M_BASE__) ||    \
    defined(__ARM_ARCH_8M_MAIN__)
    asm volatile("dmb" ::: "memory");
#endif
}

/// @brief Emit a hardware data synchronization barrier (DSB) on ARM Cortex-M targets.
/// On host builds, this is a noop — only the compiler fence is required.
UMI_MMIO_ALWAYS_INLINE void hw_dsb() noexcept {
#if defined(__thumb__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_8M_BASE__) ||    \
    defined(__ARM_ARCH_8M_MAIN__)
    asm volatile("dsb" ::: "memory");
#endif
}

/// @brief Pre-read barrier dispatched on std::memory_order.
template <std::memory_order Order>
UMI_MMIO_ALWAYS_INLINE void pre_read_barrier() noexcept {
    if constexpr (Order == std::memory_order::seq_cst) {
        std::atomic_signal_fence(std::memory_order::seq_cst);
        hw_dsb();
    } else if constexpr (Order == std::memory_order::acquire || Order == std::memory_order::acq_rel) {
        std::atomic_signal_fence(std::memory_order::acquire);
    } else {
        // Release/relaxed/consume are not meaningful pre-read orderings.
        std::atomic_signal_fence(std::memory_order::relaxed);
    }
}

/// @brief Post-read barrier dispatched on std::memory_order.
template <std::memory_order Order>
UMI_MMIO_ALWAYS_INLINE void post_read_barrier() noexcept {
    if constexpr (Order == std::memory_order::seq_cst) {
        hw_dmb();
        std::atomic_signal_fence(std::memory_order::seq_cst);
    } else if constexpr (Order == std::memory_order::acquire || Order == std::memory_order::acq_rel) {
        hw_dmb();
        std::atomic_signal_fence(std::memory_order::acquire);
    } else {
        // Release/relaxed/consume are not meaningful post-read orderings.
        std::atomic_signal_fence(std::memory_order::relaxed);
    }
}

/// @brief Pre-write barrier dispatched on std::memory_order.
template <std::memory_order Order>
UMI_MMIO_ALWAYS_INLINE void pre_write_barrier() noexcept {
    if constexpr (Order == std::memory_order::seq_cst) {
        std::atomic_signal_fence(std::memory_order::seq_cst);
        hw_dsb();
    } else if constexpr (Order == std::memory_order::release || Order == std::memory_order::acq_rel) {
        std::atomic_signal_fence(std::memory_order::release);
        hw_dmb();
    } else {
        // Acquire/relaxed/consume are not meaningful pre-write orderings.
        std::atomic_signal_fence(std::memory_order::relaxed);
    }
}

/// @brief Post-write barrier dispatched on std::memory_order.
template <std::memory_order Order>
UMI_MMIO_ALWAYS_INLINE void post_write_barrier() noexcept {
    if constexpr (Order == std::memory_order::seq_cst) {
        hw_dsb();
        std::atomic_signal_fence(std::memory_order::seq_cst);
    } else if constexpr (Order == std::memory_order::release || Order == std::memory_order::acq_rel) {
        hw_dsb();
        std::atomic_signal_fence(std::memory_order::release);
    } else {
        // Acquire/relaxed/consume are not meaningful post-write orderings.
        std::atomic_signal_fence(std::memory_order::relaxed);
    }
}

} // namespace detail

/// @brief AtomicTransport — wrapper that brackets reg_read / reg_write with memory barriers.
///
/// Wraps an inner transport (typically `DirectTransport`) and emits compiler +
/// hardware memory barriers around register accesses. Selects barrier strength
/// from the `Order` template parameter (`std::memory_order`).
///
/// Default `Order` is `seq_cst`, which emits compiler `seq_cst` fence + DSB on
/// ARM Cortex-M and provides the strongest ordering guarantee. Use `release`
/// for store-side publication, `acquire` for load-side consumption, and
/// `relaxed` to emit only the compiler fence (no hardware barrier).
///
/// The TransportTag is inherited from the inner transport, so register access
/// constraints (`AllowedTransports`) are preserved transparently.
///
/// @tparam Inner  Inner transport (must satisfy TransportLike). No default — explicit to keep
///                AtomicTransport free of any direct dependency on DirectTransport.
/// @tparam Order  std::memory_order strength. Default `seq_cst`.
///
/// @code
///   // Default: seq_cst barrier on every access (DSB-bracketed on ARM)
///   AtomicTransport<DirectTransport<>> io;
///   io.write(GPIO::OUT::value(0xFF));     // full barrier before + after
///   auto v = io.read(GPIO::IN{}).bits();  // full barrier before + after
///
///   // Release-only for publication
///   AtomicTransport<DirectTransport<>, std::memory_order::release> pub;
///   pub.write(SHARED::FLAG::Set{});
/// @endcode
template <typename Inner, std::memory_order Order = std::memory_order::seq_cst>
class AtomicTransport : private RegOps<std::true_type, AssertOnError> {
  public:
    using RegOps<std::true_type, AssertOnError>::write;
    using RegOps<std::true_type, AssertOnError>::read;
    using RegOps<std::true_type, AssertOnError>::modify;
    using RegOps<std::true_type, AssertOnError>::is;
    using RegOps<std::true_type, AssertOnError>::flip;
    using RegOps<std::true_type, AssertOnError>::clear;
    using RegOps<std::true_type, AssertOnError>::reset;
    using RegOps<std::true_type, AssertOnError>::read_variant;

    /// @brief Inherit the inner transport's tag so AllowedTransports constraints transparent.
    using TransportTag = typename Inner::TransportTag;

    /// @brief Default construct (for stateless inner transports).
    constexpr AtomicTransport() noexcept
        requires std::is_default_constructible_v<Inner>
    = default;

    /// @brief Construct AtomicTransport with an inner transport instance.
    constexpr explicit AtomicTransport(Inner inner) noexcept : inner(std::move(inner)) {}

    /// @brief Read a register through the inner transport, bracketed with memory barriers.
    template <typename Reg>
    [[nodiscard]] UMI_MMIO_ALWAYS_INLINE auto reg_read(Reg reg) const noexcept -> typename Reg::RegValueType {
        detail::pre_read_barrier<Order>();
        auto val = inner.reg_read(reg);
        detail::post_read_barrier<Order>();
        return val;
    }

    /// @brief Write a register through the inner transport, bracketed with memory barriers.
    template <typename Reg>
    UMI_MMIO_ALWAYS_INLINE void reg_write(Reg reg, typename Reg::RegValueType value) const noexcept {
        detail::pre_write_barrier<Order>();
        inner.reg_write(reg, value);
        detail::post_write_barrier<Order>();
    }

  private:
    [[no_unique_address]] Inner inner{};
};

#undef UMI_MMIO_ALWAYS_INLINE

} // namespace umi::mmio
