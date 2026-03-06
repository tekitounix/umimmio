// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief RAM-backed mock transport for host-side register testing.
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <cstdio>
#include <cstring>

#include <umimmio/ops.hh>

// =============================================================================
// Mock transport: RAM-backed register I/O for testing
// =============================================================================

using namespace umi::mmio;

/// @brief A trivial RAM-backed transport suitable for host-side testing.
class MockTransport : private RegOps<> {
  public:
    using RegOps<>::write;
    using RegOps<>::read;
    using RegOps<>::modify;
    using RegOps<>::is;
    using TransportTag = Direct;

    MockTransport() { std::memset(ram.data(), 0, ram.size()); }

    template <typename Reg>
    auto reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        using T = typename Reg::RegValueType;
        T val{};
        std::memcpy(&val, &ram[Reg::address], sizeof(T));
        return val;
    }

    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType value) const noexcept {
        using T = typename Reg::RegValueType;
        std::memcpy(&ram[Reg::address], &value, sizeof(T));
    }

  private:
    mutable std::array<std::uint8_t, 256> ram{};
};

// =============================================================================
// Device register definitions (hierarchical style)
// =============================================================================

struct MockDevice : Device<> {
    /// CTRL: 8-bit control register at offset 0x00
    struct CTRL : Register<MockDevice, 0x00, bits8> {
        struct EN : Field<CTRL, 0, 1> {};     // Enable
        struct SPEED : Field<CTRL, 1, 2> {    // Speed selection
            using Fast = Value<SPEED, 2>;
        };
    };

    /// SR: 8-bit status register at offset 0x01 (read-only)
    struct SR : Register<MockDevice, 0x01, bits8, RO> {
        struct READY : Field<SR, 0, 1> {};
    };
};

int main() {
    MockTransport const io;

    // Write EN=1, SPEED=2 using typed API
    io.write(MockDevice::CTRL::EN::Set{}, MockDevice::CTRL::SPEED::Fast{});

    auto reader = io.read(MockDevice::CTRL{});
    std::printf("CTRL = 0x%02X (expect EN=1, SPEED=2 -> 0x05)\n", reader.bits());

    // Verify using field read
    auto enable_val = reader.get(MockDevice::CTRL::EN{}).bits();
    auto speed_val = reader.get(MockDevice::CTRL::SPEED{}).bits();
    std::printf("EN = %u, SPEED = %u\n", enable_val, speed_val);

    return (reader.bits() == 0x05) ? 0 : 1;
}
