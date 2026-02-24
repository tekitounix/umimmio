// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief RAM-backed mock transport for host-side register testing.

#include <array>
#include <cstdio>
#include <cstring>

#include <umimmio/register.hh>

// =============================================================================
// Mock transport: RAM-backed register I/O for testing
// =============================================================================

/// @brief A trivial RAM-backed transport suitable for host-side testing.
class MockTransport {
    std::array<std::uint8_t, 256> ram{};

  public:
    template <typename Reg>
    auto reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        using T = typename Reg::RegValueType;
        T val{};
        std::memcpy(&val, &ram[Reg::address], sizeof(T));
        return val;
    }

    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType value) noexcept {
        using T = typename Reg::RegValueType;
        std::memcpy(&ram[Reg::address], &value, sizeof(T));
    }

    template <typename Reg>
    void reg_modify(Reg r, typename Reg::RegValueType clear_mask, typename Reg::RegValueType set_mask) noexcept {
        auto val = reg_read(r);
        val = (val & ~clear_mask) | set_mask;
        reg_write(r, val);
    }
};

// =============================================================================
// Device register definitions
// =============================================================================

using CtrlReg = umi::mmio::Region<0x00, std::uint8_t>;
using StatusReg = umi::mmio::Region<0x01, std::uint8_t>;

using Enable = umi::mmio::Field<CtrlReg, 0, 1>;
using Speed = umi::mmio::Field<CtrlReg, 1, 2>;
using Ready = umi::mmio::Field<StatusReg, 0, 1>;

int main() {
    MockTransport io;

    // Write full register
    io.reg_write(CtrlReg{}, std::uint8_t{0x00});

    // Read-modify-write: set Enable=1, Speed=2
    io.reg_modify(CtrlReg{}, Enable::mask | Speed::mask, Enable::mask | static_cast<std::uint8_t>(2u << Speed::offset));

    auto val = io.reg_read(CtrlReg{});
    std::printf("CtrlReg = 0x%02X (expect Enable=1, Speed=2 → 0x05)\n", val);

    return (val == 0x05) ? 0 : 1;
}
