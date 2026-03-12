#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief CsrTransport — RISC-V CSR access transport.
/// @author Shota Moriguchi @tekitounix
/// @details CSR numbers are treated as Register::address (with base_address=0).
///          CsrAccessor concept allows injecting any accessor implementation.

#include <concepts>
#include <cstdint>
#include <type_traits>

#include "../ops.hh"
#include "../policy.hh" // IWYU pragma: keep  — AssertOnError, Csr

namespace umi::mmio {

// ===========================================================================
// CsrAccessor concept — customization point for CSR read/write
// ===========================================================================

/// @brief Concept for CSR accessor implementations.
/// @details An accessor must provide `csr_read<Addr>()` and `csr_write<Addr>(value)`.
///          The address is a compile-time constant (CSR number).
template <typename T>
concept CsrAccessor = requires(const T& accessor, std::uint32_t val) {
    { accessor.template csr_read<0x300>() } -> std::same_as<std::uint32_t>;
    { accessor.template csr_write<0x300>(val) };
};

// ===========================================================================
// CsrTransport — type-safe CSR transport
// ===========================================================================

/// @brief RISC-V CSR access transport.
///
/// Uses the CsrAccessor to perform actual CSR reads and writes.
/// CSR number is encoded as Register::address (Device base_address = 0).
///
/// @tparam Accessor     CSR accessor implementation (must satisfy CsrAccessor concept).
/// @tparam CheckPolicy  Enable checks (std::true_type or std::false_type).
/// @tparam ErrorPolicy  Error handler (default: AssertOnError).
template <CsrAccessor Accessor, typename CheckPolicy = std::true_type, typename ErrorPolicy = AssertOnError>
class CsrTransport : private RegOps<CheckPolicy, ErrorPolicy> {
  public:
    using RegOps<CheckPolicy, ErrorPolicy>::write;
    using RegOps<CheckPolicy, ErrorPolicy>::read;
    using RegOps<CheckPolicy, ErrorPolicy>::modify;
    using RegOps<CheckPolicy, ErrorPolicy>::is;
    using RegOps<CheckPolicy, ErrorPolicy>::flip;
    using RegOps<CheckPolicy, ErrorPolicy>::clear;
    using RegOps<CheckPolicy, ErrorPolicy>::reset;
    using RegOps<CheckPolicy, ErrorPolicy>::read_variant;
    using TransportTag = Csr;

    /// @brief Construct CsrTransport with an accessor instance.
    constexpr explicit CsrTransport(Accessor accessor) noexcept : accessor(accessor) {}

    /// @brief Default construct (for stateless accessors).
    constexpr CsrTransport() noexcept
        requires std::is_default_constructible_v<Accessor>
    = default;

    /// @brief Read a CSR register.
    template <typename Reg>
    [[nodiscard]] auto reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        return static_cast<typename Reg::RegValueType>(
            accessor.template csr_read<static_cast<std::uint32_t>(Reg::address)>());
    }

    /// @brief Write a CSR register.
    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType value) const noexcept {
        accessor.template csr_write<static_cast<std::uint32_t>(Reg::address)>(static_cast<std::uint32_t>(value));
    }

  private:
    [[no_unique_address]] Accessor accessor{};
};

} // namespace umi::mmio
