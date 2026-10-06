#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief BufferMmio — typed access to fixed-base buffer memory regions.
/// @author Shota Moriguchi @tekitounix
/// @details For memory-mapped buffer regions where the *whole region is volatile
///          MMIO* but there is no fixed register layout: USB DPRAM, codec sample
///          buffers, OTG FIFO data registers, etc. Provides word and byte access
///          at a dynamic offset relative to a compile-time base, with
///          alignment/bounds assertions in CheckPolicy::true mode.

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "../policy.hh" // IWYU pragma: keep  — AssertOnError, Addr

namespace umi::mmio {

/// @brief Typed access to a fixed-base, fixed-size MMIO buffer region.
///
/// Use this transport for memory-mapped buffer regions that cannot be modeled
/// as a Register set (e.g. USB DPRAM, where the entire 4 KiB region is volatile
/// host-shared memory containing endpoint control words, buffer control words,
/// SETUP packet, and arbitrary endpoint data interleaved at runtime-computed
/// offsets).
///
/// All accessors are const because the transport itself is stateless; side
/// effects occur through volatile pointer dereference, not through transport
/// member mutation.
///
/// @tparam Base         Buffer base address (must be word-aligned for word_at()).
/// @tparam Size         Buffer size in bytes. Runtime asserts use this bound.
/// @tparam CheckPolicy  Enable alignment + bounds asserts (default std::true_type).
/// @tparam ErrorPolicy  Error handler (default AssertOnError).
template <Addr Base, std::size_t Size, typename CheckPolicy = std::true_type, typename ErrorPolicy = AssertOnError>
class BufferMmio {
  public:
    static constexpr Addr base_address = Base;
    static constexpr std::size_t size_bytes = Size;
    using TransportTag = Direct;

    /// @brief Volatile 32-bit word reference at byte offset.
    ///
    /// Offset must be word-aligned (4-byte) and offset + 4 <= Size. CheckPolicy
    /// enforces alignment at compile time when offset is constant, and via
    /// ErrorPolicy::on_error at runtime otherwise.
    [[nodiscard]] volatile std::uint32_t& word_at(std::uint32_t offset) const noexcept {
        if constexpr (CheckPolicy::value) {
            if (((offset % alignof(std::uint32_t)) != 0) || (offset + sizeof(std::uint32_t) > Size)) {
                ErrorPolicy::on_error("BufferMmio::word_at: offset misaligned or out of bounds");
            }
        }
        // NOLINTNEXTLINE(performance-no-int-to-ptr,clang-analyzer-core.FixedAddressDereference) — MMIO
        return *reinterpret_cast<volatile std::uint32_t*>(Base + offset);
    }

    /// @brief Volatile 16-bit halfword reference at byte offset.
    [[nodiscard]] volatile std::uint16_t& halfword_at(std::uint32_t offset) const noexcept {
        if constexpr (CheckPolicy::value) {
            if (((offset % alignof(std::uint16_t)) != 0) || (offset + sizeof(std::uint16_t) > Size)) {
                ErrorPolicy::on_error("BufferMmio::halfword_at: offset misaligned or out of bounds");
            }
        }
        // NOLINTNEXTLINE(performance-no-int-to-ptr,clang-analyzer-core.FixedAddressDereference) — MMIO
        return *reinterpret_cast<volatile std::uint16_t*>(Base + offset);
    }

    /// @brief Volatile byte pointer at byte offset (writable).
    [[nodiscard]] volatile std::uint8_t* bytes_at(std::uint32_t offset) const noexcept {
        if constexpr (CheckPolicy::value) {
            if (offset >= Size) {
                ErrorPolicy::on_error("BufferMmio::bytes_at: offset out of bounds");
            }
        }
        // NOLINTNEXTLINE(performance-no-int-to-ptr,clang-analyzer-core.FixedAddressDereference) — MMIO
        return reinterpret_cast<volatile std::uint8_t*>(Base + offset);
    }

    /// @brief Const byte pointer at byte offset (read-only view).
    ///
    /// The returned pointer is non-volatile by design — callers that need a
    /// volatile read use bytes_at(). const_bytes_at() exists for notify_*()
    /// style upcalls that copy out of the buffer once and treat the snapshot
    /// as immutable for the duration of the callback.
    [[nodiscard]] const std::uint8_t* const_bytes_at(std::uint32_t offset) const noexcept {
        if constexpr (CheckPolicy::value) {
            if (offset >= Size) {
                ErrorPolicy::on_error("BufferMmio::const_bytes_at: offset out of bounds");
            }
        }
        // NOLINTNEXTLINE(performance-no-int-to-ptr,clang-analyzer-core.FixedAddressDereference) — MMIO
        return reinterpret_cast<const std::uint8_t*>(Base + offset);
    }
};

} // namespace umi::mmio
