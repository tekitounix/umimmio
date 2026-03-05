// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief RAM-backed mock transport for host-side register testing.
/// @author Shota Moriguchi @tekitounix

#include <array>
#include <cstdio>
#include <cstring>

#include <umimmio/register.hh>

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
    using TransportTag = DirectTransportTag;

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
// Device register definitions
// =============================================================================

struct MockDevice : Device<RW, DirectTransportTag> {};

struct CtrlReg : Register<MockDevice, 0x00, bits8> {};
struct StatusReg : Register<MockDevice, 0x01, bits8, RO> {};

struct Enable : Field<CtrlReg, 0, 1> {};
struct Speed : Field<CtrlReg, 1, 2> {};
struct Ready : Field<StatusReg, 0, 1> {};

// Named values for Speed
using SpeedFast = Value<Speed, 2>;

int main() {
    MockTransport const io;

    // Write Enable=1, Speed=2 using typed API
    io.write(Enable::Set{}, SpeedFast{});

    auto reader = io.read(CtrlReg{});
    std::printf("CtrlReg = 0x%02X (expect Enable=1, Speed=2 -> 0x05)\n", reader.bits());

    // Verify using field read
    auto enable_val = reader.get(Enable{}).bits();
    auto speed_val = reader.get(Speed{}).bits();
    std::printf("Enable = %u, Speed = %u\n", enable_val, speed_val);

    return (reader.bits() == 0x05) ? 0 : 1;
}
