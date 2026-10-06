#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Foundation vocabulary types for UMI MMIO: access policies, transport tags, error policies.
/// @author Shota Moriguchi @tekitounix

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <type_traits>

// Portable `[[clang::lifetimebound]]` for reference-holding constructors and
// getters. Clang emits -Wdangling when the bound reference would outlive its
// source; GCC <= 14.2 rejects the scoped attribute under -Werror=attributes,
// so we expand to empty.
#if defined(__clang__) && __has_cpp_attribute(clang::lifetimebound)
#define UMI_LIFETIMEBOUND [[clang::lifetimebound]]
#else
#define UMI_LIFETIMEBOUND
#endif

/// @namespace umi::mmio
/// @brief UMI Memory-mapped I/O abstractions
namespace umi::mmio {

/// @typedef Addr
/// @brief Memory address type
using Addr = std::uintptr_t;

/// @name Bit width constants
/// @{
constexpr std::size_t bits8 = 8U;   ///< 8-bit width
constexpr std::size_t bits16 = 16U; ///< 16-bit width
constexpr std::size_t bits32 = 32U; ///< 32-bit width
constexpr std::size_t bits64 = 64U; ///< 64-bit width
/// @}

/// @brief Maximum register size in bytes (64-bit = 8 bytes).
constexpr std::size_t max_reg_bytes = 8U;

/// @brief Select the smallest unsigned integer type that can hold Bits
template <std::size_t Bits>
using UintFit =
    std::conditional_t<(Bits <= bits8),
                       std::uint8_t,
                       std::conditional_t<(Bits <= bits16),
                                          std::uint16_t,
                                          std::conditional_t<(Bits <= bits32), std::uint32_t, std::uint64_t>>>;

// ===========================================================================
// Access policies
// ===========================================================================

/// @brief Describes how a write operation affects the register/field.
enum class WriteBehavior : std::uint8_t {
    NORMAL,         ///< Standard write: written value replaces current value.
    ONE_TO_CLEAR,   ///< Write-1-to-clear: writing 1 clears the bit, writing 0 has no effect.
    ONE_TO_SET,     ///< Write-1-to-set: writing 1 sets the bit, writing 0 has no effect.
    ONE_TO_TOGGLE,  ///< Write-1-to-toggle: writing 1 toggles the bit, writing 0 has no effect.
    ZERO_TO_CLEAR,  ///< Write-0-to-clear: writing 0 clears the bit, writing 1 has no effect.
    ZERO_TO_SET,    ///< Write-0-to-set: writing 0 sets the bit, writing 1 has no effect.
    ZERO_TO_TOGGLE, ///< Write-0-to-toggle: writing 0 toggles the bit, writing 1 has no effect.
};

/// @brief Parameterized access policy.
/// @tparam CanRead  Whether the register/field is readable.
/// @tparam CanWrite Whether the register/field is writable.
/// @tparam Behavior Write semantics (NORMAL, ONE_TO_CLEAR, ...).
template <bool CanRead, bool CanWrite, WriteBehavior Behavior = WriteBehavior::NORMAL>
struct AccessPolicy {
    static constexpr bool can_read = CanRead;
    static constexpr bool can_write = CanWrite;
    static constexpr auto write_behavior = Behavior;
};

using RW = AccessPolicy<true, true>;
using RO = AccessPolicy<true, false>;
using WO = AccessPolicy<false, true>;
using W1C = AccessPolicy<true, true, WriteBehavior::ONE_TO_CLEAR>;
using W1S = AccessPolicy<false, true, WriteBehavior::ONE_TO_SET>;
using W1T = AccessPolicy<false, true, WriteBehavior::ONE_TO_TOGGLE>;
using Z0C = AccessPolicy<true, true, WriteBehavior::ZERO_TO_CLEAR>;
using Z0S = AccessPolicy<true, true, WriteBehavior::ZERO_TO_SET>;
using Z0T = AccessPolicy<true, true, WriteBehavior::ZERO_TO_TOGGLE>;

struct Inherit {};

/// @name Transport tags
/// @{
struct Direct {};
struct I2c {};
struct Spi {};
struct Csr {};
/// @}

/// @name Error policies
/// @{
struct AssertOnError {
    static void on_range_error([[maybe_unused]] const char* msg) noexcept {
        assert(false && msg);
        __builtin_trap(); // Unreachable with asserts enabled; provides defined trap under NDEBUG.
    }
    static void on_transport_error([[maybe_unused]] const char* msg) noexcept {
        assert(false && msg);
        __builtin_trap();
    }
};

struct TrapOnError {
    [[noreturn]] static void on_range_error([[maybe_unused]] const char* msg) noexcept { std::abort(); }
    [[noreturn]] static void on_transport_error([[maybe_unused]] const char* msg) noexcept { std::abort(); }
};

struct IgnoreError {
    static void on_range_error([[maybe_unused]] const char* msg) noexcept {}
    static void on_transport_error([[maybe_unused]] const char* msg) noexcept {}
};

template <void (*Handler)(const char*)>
struct CustomErrorHandler {
    static void on_range_error(const char* msg) noexcept { Handler(msg); }
    static void on_transport_error(const char* msg) noexcept { Handler(msg); }
};

/// @brief Error policy that latches failures instead of trapping, for
/// control-plane polling.
///
/// @details Holds per-@p Tag static state: instantiate with a distinct
/// @p Tag type (typically an empty tag struct) for each board-level device
/// instance that should track errors independently — sharing a Tag across
/// two device instances shares their latch state.
///
/// Unlike AssertOnError/TrapOnError, on_transport_error() does not trap: it
/// only latches, so a control-plane sequence (e.g. driver init, a paged
/// register scan) can run to completion and the caller polls take()
/// afterward to decide whether to retry or report failure. on_range_error()
/// still asserts in debug builds (programmer bug, same as AssertOnError),
/// but always latches first so a release build can recover instead of
/// trapping.
///
/// @warning Control-plane only. The static bool is not atomic and has no
/// ISR guard; do not call on_range_error() / on_transport_error() / take()
/// from an ISR, or concurrently from multiple threads.
///
/// @tparam Tag Distinguishes independent latch instances (one per device).
template <typename Tag>
struct LatchError {
    static inline bool latched = false;

    static void on_range_error([[maybe_unused]] const char* msg) noexcept {
        latched = true;
        assert(false && msg);
    }

    static void on_transport_error([[maybe_unused]] const char* msg) noexcept { latched = true; }

    /// @brief Read and clear the latch.
    /// @return true if an error was latched since the last take() call.
    [[nodiscard]] static bool take() noexcept {
        bool const was_latched = latched;
        latched = false;
        return was_latched;
    }
};
/// @}

} // namespace umi::mmio
