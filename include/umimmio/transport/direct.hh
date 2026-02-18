#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file direct.hh
/// @brief Direct memory-mapped I/O transport implementation.
/// @author Shota Moriguchi @tekitounix

#include <concepts>
#include <type_traits>

#include "../register.hh" // RegOps and DirectTransportTag

namespace umi::mmio {

/// @brief DirectTransport — volatile pointer read/write for memory-mapped peripherals.
/// @tparam CheckPolicy  Enable alignment checks (std::true_type or std::false_type).
/// @tparam ErrorPolicy  Error handler (default: AssertOnError).
template <typename CheckPolicy = std::true_type, typename ErrorPolicy = AssertOnError>
class DirectTransport : private RegOps<DirectTransport<CheckPolicy, ErrorPolicy>, CheckPolicy, ErrorPolicy> {
    friend class RegOps<DirectTransport<CheckPolicy, ErrorPolicy>, CheckPolicy, ErrorPolicy>;

  public:
    using RegOps<DirectTransport<CheckPolicy, ErrorPolicy>, CheckPolicy, ErrorPolicy>::write;
    using RegOps<DirectTransport<CheckPolicy, ErrorPolicy>, CheckPolicy, ErrorPolicy>::read;
    using RegOps<DirectTransport<CheckPolicy, ErrorPolicy>, CheckPolicy, ErrorPolicy>::modify;
    using RegOps<DirectTransport<CheckPolicy, ErrorPolicy>, CheckPolicy, ErrorPolicy>::is;
    using RegOps<DirectTransport<CheckPolicy, ErrorPolicy>, CheckPolicy, ErrorPolicy>::flip;
    using TransportTag = DirectTransportTag;

    /// @brief Read a register via volatile pointer dereference.
    /// @tparam Reg Register type (provides address and RegValueType).
    /// @return Current register value.
    template <typename Reg>
    auto reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        using T = typename Reg::RegValueType;
        return *reinterpret_cast<volatile const T*>(Reg::address);
    }

    /// @brief Write a value to a register via volatile pointer dereference.
    /// @tparam Reg Register type (provides address and RegValueType).
    /// @param value Value to write.
    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType value) const noexcept {
        using T = typename Reg::RegValueType;

        // Alignment check (compile-time controlled)
        if constexpr (CheckPolicy::value) {
            static_assert((Reg::address % alignof(T)) == 0, "Misaligned register access");
        }

        *reinterpret_cast<volatile T*>(Reg::address) = value;
    }
};

/// @brief Convenience alias for DirectTransport with default policies.
template <typename CheckPolicy = std::true_type, typename ErrorPolicy = AssertOnError>
using DirectTransportT = DirectTransport<CheckPolicy, ErrorPolicy>;

/// @brief Concept matching any DirectTransport specialization.
template <typename T>
concept DirectTransportType =
    std::same_as<T, DirectTransport<std::true_type, AssertOnError>> ||
    std::same_as<T, DirectTransport<std::false_type, AssertOnError>> || std::same_as<T, DirectTransportT<>>;

} // namespace umi::mmio
