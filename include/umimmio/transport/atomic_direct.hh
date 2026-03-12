#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief AtomicDirectTransport — write-only transport with address offset.
/// @author Shota Moriguchi @tekitounix
/// @details Write operations target (register_address + AliasOffset) via volatile
///          pointer. No reg_read() — Readable concept is false, so modify() and
///          flip() are naturally rejected at compile time.
///
///          Primary use case: MCUs with atomic register aliases (e.g. SET, CLR, XOR)
///          where write-only aliases at fixed offsets from base registers enable
///          lock-free bit manipulation.

#include <type_traits>

#include "../ops.hh"
#include "../policy.hh" // IWYU pragma: keep  — AssertOnError, Direct

namespace umi::mmio {

/// @brief Write-only transport that applies a fixed address offset to all writes.
///
/// Writes target `(Reg::address + AliasOffset)` via volatile pointer.
/// No read capability — `modify()` and `flip()` fail at compile time
/// because the `Readable` concept is not satisfied.
///
/// @tparam AliasOffset  Byte offset added to register address for writes.
/// @tparam CheckPolicy  Enable alignment checks (std::true_type or std::false_type).
/// @tparam ErrorPolicy  Error handler (default: AssertOnError).
template <Addr AliasOffset, typename CheckPolicy = std::true_type, typename ErrorPolicy = AssertOnError>
class AtomicDirectTransport : private RegOps<CheckPolicy, ErrorPolicy> {
  public:
    using RegOps<CheckPolicy, ErrorPolicy>::write;
    using RegOps<CheckPolicy, ErrorPolicy>::reset;
    using TransportTag = Direct;

    /// @brief Write a value to (register_address + AliasOffset) via volatile pointer.
    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType value) const noexcept {
        using T = typename Reg::RegValueType;
        if constexpr (CheckPolicy::value) {
            static_assert(((Reg::address + AliasOffset) % alignof(T)) == 0, "Misaligned register access");
        }
        // NOLINTNEXTLINE(performance-no-int-to-ptr,clang-analyzer-core.FixedAddressDereference) — MMIO: atomic alias
        // write via offset
        *reinterpret_cast<volatile T*>(Reg::address + AliasOffset) = value;
    }

    // No reg_read() — this is a write-only transport.
    // modify(), flip(), read() are compile errors via Readable concept.
};

} // namespace umi::mmio
