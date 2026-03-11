#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief DirectTransport — volatile pointer access for memory-mapped peripherals.
/// @author Shota Moriguchi @tekitounix

#include <type_traits>

#include "../ops.hh"
#include "../policy.hh" // IWYU pragma: keep  — AssertOnError, Direct

namespace umi::mmio {

/// @brief DirectTransport — volatile pointer read/write for memory-mapped peripherals.
///
/// All register operations are const because the transport itself is stateless.
/// Side effects occur on hardware through volatile pointer dereference,
/// which is not considered mutable object state.
///
/// @tparam CheckPolicy  Enable alignment checks (std::true_type or std::false_type).
/// @tparam ErrorPolicy  Error handler (default: AssertOnError).
template <typename CheckPolicy = std::true_type, typename ErrorPolicy = AssertOnError>
class DirectTransport : private RegOps<CheckPolicy, ErrorPolicy> {
  public:
    using RegOps<CheckPolicy, ErrorPolicy>::write;
    using RegOps<CheckPolicy, ErrorPolicy>::read;
    using RegOps<CheckPolicy, ErrorPolicy>::modify;
    using RegOps<CheckPolicy, ErrorPolicy>::is;
    using RegOps<CheckPolicy, ErrorPolicy>::flip;
    using RegOps<CheckPolicy, ErrorPolicy>::clear;
    using RegOps<CheckPolicy, ErrorPolicy>::reset;
    using RegOps<CheckPolicy, ErrorPolicy>::read_variant;
    using TransportTag = Direct;

    /// @brief Read a register via volatile pointer dereference.
    template <typename Reg>
    [[nodiscard]] auto reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        using T = typename Reg::RegValueType;
        if constexpr (CheckPolicy::value) {
            static_assert((Reg::address % alignof(T)) == 0, "Misaligned register access");
        }
        return *reinterpret_cast<volatile const T*>(Reg::address);
    }

    /// @brief Write a value to a register via volatile pointer dereference.
    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType value) const noexcept {
        using T = typename Reg::RegValueType;
        if constexpr (CheckPolicy::value) {
            static_assert((Reg::address % alignof(T)) == 0, "Misaligned register access");
        }
        *reinterpret_cast<volatile T*>(Reg::address) = value;
    }
};

} // namespace umi::mmio
