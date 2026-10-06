// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: write() on RO CSR register must be rejected.
/// @author Shota Moriguchi @tekitounix
/// @details Mcause is defined as RO — writing via CsrTransport must fail.

#include <umimmio/transport/csr.hh>

namespace {

using namespace umi::mmio;

struct MockCsrAccessor {
    template <std::uint32_t CsrNum>
    [[nodiscard]] auto csr_read() const noexcept -> std::uint32_t {
        return 0;
    }

    template <std::uint32_t CsrNum>
    void csr_write(std::uint32_t /*value*/) const noexcept {}
};

struct RiscvMachine : Device<RW, Csr> {
    static constexpr Addr base_address = 0;
};

struct Mcause : Register<RiscvMachine, 0x342, bits32, RO> {};

} // namespace

int main() {
    const CsrTransport<MockCsrAccessor> csr;
    csr.write(Mcause::value(42U)); // ERROR: Cannot write to read-only register
    return 0;
}
