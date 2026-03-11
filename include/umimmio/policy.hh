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
    NORMAL,       ///< Standard write: written value replaces current value.
    ONE_TO_CLEAR, ///< Write-1-to-clear: writing 1 clears the bit, writing 0 has no effect.
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

struct Inherit {};

/// @name Transport tags
/// @{
struct Direct {};
struct I2c {};
struct Spi {};
/// @}

/// @name Error policies
/// @{
struct AssertOnError {
    static void on_range_error([[maybe_unused]] const char* msg) noexcept { assert(false && msg); }
    static void on_transport_error([[maybe_unused]] const char* msg) noexcept { assert(false && msg); }
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
/// @}

} // namespace umi::mmio
