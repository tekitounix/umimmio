#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief CsrTransport — RISC-V CSR access transport.
/// @author Shota Moriguchi @tekitounix
/// @details CSR numbers are treated as Register::address (with base_address=0).
///          CSR read/write uses inline asm on RISC-V targets. On other targets,
///          a CsrAccessor concept allows injecting mock implementations for testing.

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
// DefaultCsrAccessor — RISC-V inline asm (target-conditional)
// ===========================================================================

#if defined(__riscv)

/// @brief Default CSR accessor using RISC-V inline assembly.
/// @note Only available on RISC-V targets. Uses `csrr`/`csrw` instructions.
///       CSR number must be a compile-time constant (12-bit immediate).
struct DefaultCsrAccessor {
    /// @brief Read a CSR register.
    /// @tparam CsrNum CSR number (compile-time constant).
    template <std::uint32_t CsrNum>
    [[nodiscard]] auto csr_read() const noexcept -> std::uint32_t {
        std::uint32_t val;
        // NOLINTBEGIN(hicpp-no-assembler) — CSR access requires inline asm
        if constexpr (CsrNum == 0x300) {
            asm volatile("csrr %0, mstatus" : "=r"(val));
        } else if constexpr (CsrNum == 0x301) {
            asm volatile("csrr %0, misa" : "=r"(val));
        } else if constexpr (CsrNum == 0x304) {
            asm volatile("csrr %0, mie" : "=r"(val));
        } else if constexpr (CsrNum == 0x305) {
            asm volatile("csrr %0, mtvec" : "=r"(val));
        } else if constexpr (CsrNum == 0x340) {
            asm volatile("csrr %0, mscratch" : "=r"(val));
        } else if constexpr (CsrNum == 0x341) {
            asm volatile("csrr %0, mepc" : "=r"(val));
        } else if constexpr (CsrNum == 0x342) {
            asm volatile("csrr %0, mcause" : "=r"(val));
        } else if constexpr (CsrNum == 0x343) {
            asm volatile("csrr %0, mtval" : "=r"(val));
        } else if constexpr (CsrNum == 0x344) {
            asm volatile("csrr %0, mip" : "=r"(val));
        } else {
            static_assert(false, "Unsupported CSR number — extend DefaultCsrAccessor");
        }
        // NOLINTEND(hicpp-no-assembler)
        return val;
    }

    /// @brief Write a CSR register.
    /// @tparam CsrNum CSR number (compile-time constant).
    template <std::uint32_t CsrNum>
    void csr_write(std::uint32_t value) const noexcept {
        // NOLINTBEGIN(hicpp-no-assembler) — CSR access requires inline asm
        if constexpr (CsrNum == 0x300) {
            asm volatile("csrw mstatus, %0" ::"r"(value));
        } else if constexpr (CsrNum == 0x304) {
            asm volatile("csrw mie, %0" ::"r"(value));
        } else if constexpr (CsrNum == 0x305) {
            asm volatile("csrw mtvec, %0" ::"r"(value));
        } else if constexpr (CsrNum == 0x340) {
            asm volatile("csrw mscratch, %0" ::"r"(value));
        } else if constexpr (CsrNum == 0x341) {
            asm volatile("csrw mepc, %0" ::"r"(value));
        } else {
            static_assert(false, "Unsupported CSR number — extend DefaultCsrAccessor");
        }
        // NOLINTEND(hicpp-no-assembler)
    }
};

#endif // __riscv

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
    constexpr explicit CsrTransport(Accessor accessor) noexcept : accessor_(accessor) {}

    /// @brief Default construct (for stateless accessors).
    constexpr CsrTransport() noexcept
        requires std::is_default_constructible_v<Accessor>
    = default;

    /// @brief Read a CSR register.
    template <typename Reg>
    [[nodiscard]] auto reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        return static_cast<typename Reg::RegValueType>(
            accessor_.template csr_read<static_cast<std::uint32_t>(Reg::address)>());
    }

    /// @brief Write a CSR register.
    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType value) const noexcept {
        accessor_.template csr_write<static_cast<std::uint32_t>(Reg::address)>(static_cast<std::uint32_t>(value));
    }

  private:
    [[no_unique_address]] Accessor accessor_{};
};

} // namespace umi::mmio
