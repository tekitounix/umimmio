// SPDX-License-Identifier: MIT
/// @file
/// @brief Shared test definitions for umimmio tests.
/// @details Provides MockTransport and device/register definitions that simulate
///          a real hardware register map for host-based testing.
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <umimmio/register.hh>
#include <umitest/test.hh>

namespace umimmio::test {

// =============================================================================
// MockTransport — simulates direct memory mapped I/O with a RAM buffer
// =============================================================================

using namespace umi::mmio;

/// @brief RAM-backed mock transport for host testing.
/// Simulates hardware register access using a flat memory array.
class MockTransport : private RegOps<MockTransport> {
    friend class RegOps<MockTransport>;

  public:
    using RegOps<MockTransport>::write;
    using RegOps<MockTransport>::read;
    using RegOps<MockTransport>::modify;
    using RegOps<MockTransport>::is;
    using RegOps<MockTransport>::flip;
    using TransportTag = DirectTransportTag;

    MockTransport() { reset(); }

    void reset() { std::memset(memory.data(), 0, memory.size()); }

    /// Direct buffer access for test verification
    template <typename T>
    T peek(std::size_t offset) const {
        T val{};
        std::memcpy(&val, &memory[offset], sizeof(T));
        return val;
    }

    template <typename T>
    void poke(std::size_t offset, T val) {
        std::memcpy(&memory[offset], &val, sizeof(T));
    }

    // RegOps interface
    template <typename Reg>
    auto reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        using T = typename Reg::RegValueType;
        T val{};
        std::memcpy(&val, &memory[Reg::address], sizeof(T));
        return val;
    }

    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType value) const noexcept {
        using T = typename Reg::RegValueType;
        std::memcpy(const_cast<uint8_t*>(&memory[Reg::address]), &value, sizeof(T));
    }

  private:
    mutable std::array<std::uint8_t, 256> memory{};
};

// =============================================================================
// Mock device register map — simulates a realistic peripheral
// =============================================================================

/// @brief Mock device with direct transport only
struct MockDevice : Device<RW, DirectTransportTag> {};

// --- Registers at various offsets ---

/// @brief Status register (32-bit, read-only, reset = 0x0001)
struct StatusReg : Register<MockDevice, 0x00, bits32, RO, 0x0001> {};

/// @brief Config register (32-bit, read-write, reset = 0xFF00)
struct ConfigReg : Register<MockDevice, 0x04, bits32, RW, 0xFF00> {};

/// @brief Data register (32-bit, read-write, reset = 0)
struct DataReg : Register<MockDevice, 0x08, bits32, RW, 0> {};

/// @brief Control register (16-bit, read-write, reset = 0)
struct CtrlReg : Register<MockDevice, 0x0C, bits16, RW, 0> {};

/// @brief Write-only command register
struct CmdReg : Register<MockDevice, 0x10, bits32, WO, 0> {};

// --- Fields within ConfigReg ---

/// @brief Enable bit (bit 0, 1-bit)
struct ConfigEnable : Field<ConfigReg, 0, 1> {};

/// @brief Mode field (bits 1-2, 2-bit)
struct ConfigMode : Field<ConfigReg, 1, 2> {};

/// @brief Prescaler field (bits 8-15, 8-bit)
struct ConfigPrescaler : Field<ConfigReg, 8, 8> {};

// --- Fields within CtrlReg ---

/// @brief Start bit (bit 0, 1-bit)
struct CtrlStart : Field<CtrlReg, 0, 1> {};

/// @brief IRQ enable bit (bit 1, 1-bit)
struct CtrlIrqEn : Field<CtrlReg, 1, 1> {};

/// @brief Channel select (bits 4-7, 4-bit)
struct CtrlChannel : Field<CtrlReg, 4, 4> {};

// --- Enum values for Mode ---

enum class ModeVal : uint8_t { NORMAL = 0, FAST = 1, LOW_POWER = 2, TEST = 3 };

using ModeNormal = Value<ConfigMode, static_cast<uint8_t>(ModeVal::NORMAL)>;
using ModeFast = Value<ConfigMode, static_cast<uint8_t>(ModeVal::FAST)>;
using ModeLowPower = Value<ConfigMode, static_cast<uint8_t>(ModeVal::LOW_POWER)>;
using ModeTest = Value<ConfigMode, static_cast<uint8_t>(ModeVal::TEST)>;

// =============================================================================
// Test registration functions
// =============================================================================

void run_register_field_tests(umi::test::Suite& suite);
void run_transport_tests(umi::test::Suite& suite);
void run_access_policy_tests(umi::test::Suite& suite);
void run_spi_bitbang_tests(umi::test::Suite& suite);

} // namespace umimmio::test
