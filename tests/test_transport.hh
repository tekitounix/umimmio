// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Transport layer tests — MockTransport, ByteAdapter with mock I2C.
/// @author Shota Moriguchi @tekitounix
/// @details Tests both direct (RAM-backed) and byte-oriented transports.
#pragma once

#include <array>
#include <cstring>
#include <span>
#include <variant>

#include <umimmio/transport/csr.hh>
#include <umimmio/transport/i2c.hh>

#include "test_mock.hh"

namespace umimmio::test {
namespace {

// =============================================================================
// MockCsrAccessor — RAM-backed CSR accessor for host testing
// =============================================================================

/// @brief Mock CSR accessor that stores CSR values in a flat array.
struct MockCsrAccessor {
    mutable std::array<std::uint32_t, 4096> csrs{};

    template <std::uint32_t CsrNum>
    [[nodiscard]] auto csr_read() const noexcept -> std::uint32_t {
        return csrs[CsrNum];
    }

    template <std::uint32_t CsrNum>
    void csr_write(std::uint32_t value) const noexcept {
        csrs[CsrNum] = value;
    }
};

static_assert(CsrAccessor<MockCsrAccessor>);

// =============================================================================
// CSR Device definitions for testing
// =============================================================================

struct RiscvMachine : Device<RW, Csr> {
    static constexpr Addr base_address = 0;
};

struct Mstatus : Register<RiscvMachine, 0x300, bits32> {
    struct MIE : Field<Mstatus, 3, 1> {};
    struct MPIE : Field<Mstatus, 7, 1> {};
    struct MPP : Field<Mstatus, 11, 2> {
        using MACHINE = Value<MPP, 3>;
        using SUPERVISOR = Value<MPP, 1>;
        using USER = Value<MPP, 0>;
    };
};

struct Mtvec : Register<RiscvMachine, 0x305, bits32> {
    struct MODE : Field<Mtvec, 0, 2> {
        using DIRECT = Value<MODE, 0>;
        using VECTORED = Value<MODE, 1>;
    };
    struct BASE : Field<Mtvec, 2, 30, Numeric> {};
};

struct Mcause : Register<RiscvMachine, 0x342, bits32, RO> {};

using umi::test::TestContext;

// =============================================================================
// Mock I2C driver for ByteAdapter testing
// =============================================================================

/// @brief Minimal I2C driver mock that stores register data in RAM.
struct MockI2C {
    mutable std::array<std::uint8_t, 256> memory{};

    struct Result {
        explicit operator bool() const { return success; }
        bool success = true;
    };

    Result write(std::uint8_t /*dev_addr*/, std::span<const std::uint8_t> data) const {
        if (data.size() < 2) {
            return {false};
        }
        std::uint8_t const reg_addr = data[0];
        std::memcpy(&memory[reg_addr], data.data() + 1, data.size() - 1);
        return {true};
    }

    Result write_read(std::uint8_t /*dev_addr*/, std::span<const std::uint8_t> tx, std::span<std::uint8_t> rx) const {
        if (tx.empty()) {
            return {false};
        }
        std::uint8_t const reg_addr = tx[0];
        std::memcpy(rx.data(), &memory[reg_addr], rx.size());
        return {true};
    }
};

/// @brief I2C device for testing — allows I2c
struct I2CDevice : Device<RW, I2c> {};

/// @brief Register on I2C device at offset 0x10
struct I2CReg : Register<I2CDevice, 0x10, bits32, RW, 0> {};
struct I2CField : Field<I2CReg, 0, 8, Numeric> {};

// =============================================================================
// read_variant() helper
// =============================================================================

/// @brief Visitor helper for std::visit (C++17 pattern).
template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

// =============================================================================
// CustomErrorHandler support
// =============================================================================

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) — test-only
// mutable state, required at namespace scope for CustomErrorHandler<> template argument.
bool custom_handler_called = false;
void custom_handler(const char* /*msg*/) noexcept {
    custom_handler_called = true;
}
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

/// @brief MockTransport variant that uses CustomErrorHandler for range errors.
struct CustomErrTransport : private RegOps<std::true_type, CustomErrorHandler<custom_handler>> {
  public:
    using RegOps<std::true_type, CustomErrorHandler<custom_handler>>::write;
    using RegOps<std::true_type, CustomErrorHandler<custom_handler>>::read;
    using RegOps<std::true_type, CustomErrorHandler<custom_handler>>::is;
    using TransportTag = Direct;

    mutable std::array<std::uint8_t, 256> memory{};

    template <typename Reg>
    auto reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        typename Reg::RegValueType val{};
        std::memcpy(&val, &memory[Reg::address], sizeof(val));
        return val;
    }

    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType val) const noexcept {
        std::memcpy(&memory[Reg::address], &val, sizeof(val));
    }
};

// =============================================================================
// Non-zero base_address Device
// =============================================================================

struct MmioPeripheral : Device<> {
    static constexpr Addr base_address = 0x4001'3000;
};
struct MmioCtrl : Register<MmioPeripheral, 0x00, bits32, RW, 0> {};
struct MmioStatus : Register<MmioPeripheral, 0x04, bits32, RO, 0> {};
struct MmioCtrlEn : Field<MmioCtrl, 0, 1> {};

struct MmioPeripheralBlock : Block<MmioPeripheral, 0x100> {};
struct MmioBlockReg : Register<MmioPeripheralBlock, 0x10, bits16> {};

// =============================================================================
// Multi-transport device
// =============================================================================

struct DualTransportDevice : Device<RW, Direct, I2c> {};
struct DualReg : Register<DualTransportDevice, 0x00, bits32, RW, 0> {};

} // namespace

inline void run_transport_tests(umi::test::Suite& suite) {
    suite.section("Type traits");
    suite.run("UintFit sizes", [](TestContext& t) {
        t.eq(sizeof(UintFit<8>), 1U);
        t.eq(sizeof(UintFit<16>), 2U);
        t.eq(sizeof(UintFit<32>), 4U);
        t.eq(sizeof(UintFit<64>), 8U);
        // Edge: 1-bit fits in uint8_t
        t.eq(sizeof(UintFit<1>), 1U);
        t.eq(sizeof(UintFit<7>), 1U);
        t.eq(sizeof(UintFit<9>), 2U);
        t.eq(sizeof(UintFit<33>), 8U);
    });
    suite.run("access policy flags", [](TestContext& t) {
        t.is_true(RW::can_read && RW::can_write);
        t.is_true(RO::can_read && !RO::can_write);
        t.is_true(!WO::can_read && WO::can_write);
    });

    suite.section("Static properties");
    suite.run("register addresses/reset", [](TestContext& t) {
        // StatusReg: offset 0x00, 32-bit, reset = 0x0001
        t.eq(StatusReg::address, static_cast<Addr>(0x00));
        t.eq(StatusReg::bit_width, 32U);
        t.eq(StatusReg::reset_value(), static_cast<std::uint32_t>(0x0001));
        t.is_true(StatusReg::AccessType::can_read);
        t.is_true(!StatusReg::AccessType::can_write);

        // ConfigReg: offset 0x04, 32-bit, reset = 0xFF00
        t.eq(ConfigReg::address, static_cast<Addr>(0x04));
        t.eq(ConfigReg::reset_value(), static_cast<std::uint32_t>(0xFF00));

        // CtrlReg: offset 0x0C, 16-bit
        t.eq(CtrlReg::address, static_cast<Addr>(0x0C));
        t.eq(CtrlReg::bit_width, 16U);
    });
    suite.run("field shift/width/mask", [](TestContext& t) {
        // ConfigEnable: bit 0, width 1
        t.eq(ConfigEnable::shift, 0U);
        t.eq(ConfigEnable::bit_width, 1U);
        t.eq(ConfigEnable::mask(), static_cast<std::uint32_t>(0x1));

        // ConfigMode: bit 1, width 2
        t.eq(ConfigMode::shift, 1U);
        t.eq(ConfigMode::bit_width, 2U);
        t.eq(ConfigMode::mask(), static_cast<std::uint32_t>(0x6));

        // ConfigPrescaler: bit 8, width 8
        t.eq(ConfigPrescaler::shift, 8U);
        t.eq(ConfigPrescaler::bit_width, 8U);
        t.eq(ConfigPrescaler::mask(), static_cast<std::uint32_t>(0xFF00));

        // CtrlChannel: bit 4, width 4
        t.eq(CtrlChannel::shift, 4U);
        t.eq(CtrlChannel::bit_width, 4U);
        t.eq(CtrlChannel::mask(), static_cast<std::uint16_t>(0xF0));
    });
    suite.run("Value constants", [](TestContext& t) {
        t.eq(ConfigEnable::Set::value, static_cast<std::uint8_t>(1));
        t.eq(ConfigEnable::Reset::value, static_cast<std::uint8_t>(0));
        t.eq(ModeNormal::value, static_cast<std::uint8_t>(ModeVal::NORMAL));
        t.eq(ModeFast::value, static_cast<std::uint8_t>(ModeVal::FAST));
    });

    suite.section("MockTransport internals");
    suite.run("peek/poke/reset", [](TestContext& t) {
        MockTransport hw;

        hw.poke<std::uint32_t>(0x00, 0x12345678U);
        t.eq(hw.peek<std::uint32_t>(0x00), 0x12345678U);

        hw.poke<std::uint16_t>(0x10, std::uint16_t{0xABCD});
        t.eq(hw.peek<std::uint16_t>(0x10), static_cast<std::uint16_t>(0xABCD));

        hw.clear_memory();
        t.eq(hw.peek<std::uint32_t>(0x00), 0U);
    });

    suite.section("I2C transport (mock)");
    suite.run("write/read", [](TestContext& t) {
        MockI2C i2c;
        const I2cTransport<MockI2C> transport(i2c, 0x50);

        // Write a value through I2C transport
        transport.write(I2CReg::value(0xDEAD'BEEFU));

        // Read it back
        auto val = transport.read(I2CReg{});

        t.eq(val.bits(), static_cast<std::uint32_t>(0xDEAD'BEEF));
    });
    suite.run("field operations", [](TestContext& t) {
        MockI2C i2c;
        const I2cTransport<MockI2C> transport(i2c, 0x50);

        // Write via register
        transport.write(I2CReg::value(0x00000042U));

        // Read field
        auto field_val = transport.read(I2CField{});
        t.eq(field_val.bits(), static_cast<std::uint8_t>(0x42));
    });
    suite.run("modify (RMW)", [](TestContext& t) {
        MockI2C i2c;
        const I2cTransport<MockI2C> transport(i2c, 0x50);

        // Pre-load value
        transport.write(I2CReg::value(0xFF00'0000U));

        // Modify just the low 8 bits
        transport.modify(I2CField::value(static_cast<std::uint8_t>(0xAB)));
        auto reg_val = transport.read(I2CReg{});
        // Low byte should be 0xAB, high bytes preserved
        t.eq(reg_val.bits() & 0xFFU, 0xABU);
        t.eq(reg_val.bits() & 0xFF00'0000U, 0xFF00'0000U);
    });

    suite.section("W1C / reset / read_variant");
    suite.run("W1C clear()", [](TestContext& t) {
        MockTransport hw;

        hw.poke<std::uint32_t>(0x14, 0x0103U); // OVR=1, EOC=1, Enable=1
        hw.clear(W1cOvr{});
        // Mixed register (W1C + non-W1C): clear() uses RMW.
        // Read 0x0103, mask W1C bits (&~0x03 = 0x0100), set OVR (|0x01 = 0x0101).
        // Enable (bit 8) preserved. EOC (bit 1, W1C) not accidentally cleared.
        t.eq(hw.peek<std::uint32_t>(0x14), 0x0101U);
    });
    suite.run("register reset()", [](TestContext& t) {
        MockTransport hw;

        hw.poke<std::uint32_t>(0x04, 0xAAAA'AAAA);
        hw.reset(ConfigReg{});
        t.eq(hw.peek<std::uint32_t>(0x04), static_cast<std::uint32_t>(0xFF00));
    });
    suite.run("W1C modify() safety", [](TestContext& t) {
        MockTransport hw;

        // Set OVR=1, EOC=1, Enable=1
        hw.poke<std::uint32_t>(0x14, 0x0103U);

        // Modify the Enable field (bit 8) — W1C bits (0,1) should be masked to 0
        hw.modify(W1cRegEnable::Set{});

        auto result = hw.peek<std::uint32_t>(0x14);
        // Enable (bit 8) should be set
        t.is_true((result & 0x0100U) != 0);
        // W1C bits (0,1) should be 0 (masked by w1c_mask)
        t.eq(result & 0x03U, 0U);
    });
    suite.run("read_variant() match", [](TestContext& t) {
        MockTransport hw;

        // Mode = FAST (bits 1-2 = 01) → value at offset 0x04 = 0x02
        hw.poke<std::uint32_t>(0x04, 0x02U);
        auto mode = hw.read_variant<ConfigMode, ModeNormal, ModeFast, ModeLowPower, ModeTest>();

        bool matched = false;
        std::visit(Overloaded{
                       [&](ModeFast) { matched = true; },
                       [&](auto) { matched = false; },
                   },
                   mode);
        t.is_true(matched);
    });
    suite.run("read_variant() unknown", [](TestContext& t) {
        MockTransport hw;

        // Set mode to a value not in the variant list
        hw.poke<std::uint32_t>(0x04, 0x06U); // bits 1-2 = 0b11 = TEST
        // Only list Normal and Fast — TEST should be UnknownValue
        auto mode = hw.read_variant<ConfigMode, ModeNormal, ModeFast>();

        bool is_unknown = false;
        std::visit(Overloaded{
                       [&](UnknownValue<ConfigMode> u) {
                           is_unknown = true;
                           // value should be 3 (TEST)
                           (void)u;
                       },
                       [&](auto) { is_unknown = false; },
                   },
                   mode);
        t.is_true(is_unknown);
    });

    suite.section("CustomErrorHandler");
    suite.run("callback invoked on range error", [](TestContext& t) {
        const CustomErrTransport hw;
        // ConfigPrescaler is 8-bit (bits 8-15). Value 256 exceeds max (255).
        custom_handler_called = false;
        hw.write(DynamicValue<ConfigPrescaler, std::uint16_t>{256});
        t.is_true(custom_handler_called);
    });

    suite.section("Multi-field modify with W1C");
    suite.run("W1C bits masked in multi-modify", [](TestContext& t) {
        MockTransport hw;

        // Set OVR=1, EOC=1, Enable=1, Mode=0
        hw.poke<std::uint32_t>(0x14, 0x0103U);

        // Modify both Enable and Mode at once — W1C bits (0,1) must be masked
        hw.modify(W1cRegEnable::Set{}, W1cRegMode::Set{});

        auto result = hw.peek<std::uint32_t>(0x14);
        t.is_true((result & 0x0100U) != 0);
        t.is_true((result & 0x0200U) != 0);
        t.eq(result & 0x03U, 0U);
    });

    suite.section("8-bit register");
    suite.run("write/read/modify/reset", [](TestContext& t) {
        const MockTransport hw;

        // Write full register
        hw.write(ByteReg::value(static_cast<std::uint8_t>(0x5A)));
        t.eq(hw.peek<std::uint8_t>(0x18), static_cast<std::uint8_t>(0x5A));

        // Read field
        auto low = hw.read(ByteLow{});
        t.eq(low.bits(), static_cast<std::uint8_t>(0x0A));
        auto high = hw.read(ByteHigh{});
        t.eq(high.bits(), static_cast<std::uint8_t>(0x05));

        // Modify single field (preserves other nibble)
        hw.modify(ByteLow::value(static_cast<std::uint8_t>(0x0F)));
        t.eq(hw.peek<std::uint8_t>(0x18), static_cast<std::uint8_t>(0x5F));

        // Reset
        hw.reset(ByteReg{});
        t.eq(hw.peek<std::uint8_t>(0x18), static_cast<std::uint8_t>(0xA5));
    });

    suite.section("flip() in W1C register");
    suite.run("W1C bits masked during flip", [](TestContext& t) {
        MockTransport hw;

        // OVR=1, EOC=1, Enable=0 → raw = 0x0003
        hw.poke<std::uint32_t>(0x14, 0x0003U);

        // flip(Enable) should:
        //   1. Read 0x0003
        //   2. XOR Enable mask (0x0100) → 0x0103
        //   3. Mask W1C bits (&~0x03) → 0x0100
        //   4. Write 0x0100
        hw.flip(W1cRegEnable{});
        auto result = hw.peek<std::uint32_t>(0x14);
        t.is_true((result & 0x0100U) != 0);
        t.is_true((result & 0x03U) == 0U);
    });
    suite.run("flip toggle back with W1C mask", [](TestContext& t) {
        MockTransport hw;

        // Enable=1, Mode=1, OVR=1
        hw.poke<std::uint32_t>(0x14, 0x0301U);

        // flip(Enable) should:
        //   1. Read 0x0301
        //   2. XOR Enable mask → 0x0201
        //   3. Mask W1C (&~0x03) → 0x0200
        //   4. Write 0x0200
        hw.flip(W1cRegEnable{});
        auto result = hw.peek<std::uint32_t>(0x14);
        t.is_true((result & 0x0100U) == 0U);
        t.is_true((result & 0x0200U) != 0);
        t.is_true((result & 0x03U) == 0U);
    });
    suite.run("flip 16-bit W1C register", [](TestContext& t) {
        MockTransport hw;

        // Ctrl16: W1C bits 0-1 set, Enable=0
        hw.poke<std::uint16_t>(0x20, std::uint16_t{0x0003});

        // flip(Enable) — W1C bits should be masked
        hw.flip(Ctrl16Enable{});
        auto result = hw.peek<std::uint16_t>(0x20);
        t.is_true((result & 0x0100U) != 0);
        t.is_true((result & 0x03U) == std::uint16_t{0});
    });

    suite.section("clear() edge cases");
    suite.run("all-W1C register direct write", [](TestContext& t) {
        MockTransport hw;

        // All flags set: 0x0007 (bits 0,1,2)
        hw.poke<std::uint16_t>(0x1C, std::uint16_t{0x0007});

        // clear(Flag1) should directly write Flag1::mask() = 0x0002
        // because all bits are W1C → no non-W1C fields to preserve.
        hw.clear(AllW1cFlag1{});
        auto result = hw.peek<std::uint16_t>(0x1C);
        // Direct write: only Flag1 bit is written (0x0002), NOT RMW.
        // In real HW: writing 1 to bit 1 clears it. But in RAM mock,
        // reg_write overwrites, so result = 0x0002.
        t.eq(result, std::uint16_t{0x0002});
    });
    suite.run("selective clear one of multiple W1C", [](TestContext& t) {
        MockTransport hw;

        // OVR=1, EOC=1, Enable=1 → raw = 0x0103
        hw.poke<std::uint32_t>(0x14, 0x0103U);

        // clear(EOC) should:
        //   1. Read 0x0103
        //   2. Mask W1C bits (&~0x03) → 0x0100
        //   3. Set EOC mask (|0x02) → 0x0102
        //   4. Write 0x0102
        hw.clear(W1cEoc{});
        auto result = hw.peek<std::uint32_t>(0x14);
        t.is_true(result == 0x0102U);
    });
    suite.run("preserves all non-W1C fields", [](TestContext& t) {
        MockTransport hw;

        // OVR=1, EOC=1, Enable=1, Mode=1 → raw = 0x0303
        hw.poke<std::uint32_t>(0x14, 0x0303U);

        hw.clear(W1cOvr{});
        auto result = hw.peek<std::uint32_t>(0x14);
        // RMW: read 0x0303, mask W1C (&~0x03) → 0x0300, set OVR (|0x01) → 0x0301
        t.is_true((result & 0x0100U) != 0);
        t.is_true((result & 0x0200U) != 0);
        t.is_true((result & 0x03U) == 0x01U);
    });
    suite.run("clear already-cleared W1C bit", [](TestContext& t) {
        MockTransport hw;

        // OVR=0, EOC=0, Enable=1 → only non-W1C field set
        hw.poke<std::uint32_t>(0x14, 0x0100U);

        // clear(OVR) even though OVR is already 0 — should still write the bit
        hw.clear(W1cOvr{});
        auto result = hw.peek<std::uint32_t>(0x14);
        // RMW: read 0x0100, mask W1C (&~0x03) → 0x0100, set OVR (|0x01) → 0x0101
        t.is_true(result == 0x0101U);
    });

    suite.section("write() semantics");
    suite.run("single field resets others to reset_value", [](TestContext& t) {
        MockTransport hw;

        // Pre-fill with something else
        hw.poke<std::uint32_t>(0x04, 0x0000'0000U);

        // write(Enable::Set) should write reset_value(0xFF00) | Enable(0x01) = 0xFF01
        hw.write(ConfigEnable::Set{});
        auto result = hw.peek<std::uint32_t>(0x04);
        t.is_true((result & 0x01U) == 1U);
        // Prescaler should be 0xFF (from reset_value), NOT 0x00
        t.is_true(((result >> 8) & 0xFFU) == 0xFFU);
        t.is_true(result == 0xFF01U);
    });

    suite.section("DynamicValue boundary");
    suite.run("max boundary value", [](TestContext& t) {
        const MockTransport hw;

        // ConfigPrescaler: 8-bit field → max = 255
        hw.write(ConfigPrescaler::value(static_cast<std::uint8_t>(255)));
        t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<std::uint8_t>(255));
    });
    suite.run("zero value preserves neighbors", [](TestContext& t) {
        MockTransport hw;

        hw.poke<std::uint32_t>(0x04, 0xFFFF'FFFFU);
        hw.modify(ConfigPrescaler::value(static_cast<std::uint8_t>(0)));
        t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<std::uint8_t>(0));
        // Other fields (enable, mode) should be preserved
        t.is_true(hw.is(ConfigEnable::Set{}));
        t.is_true(hw.is(ModeTest{}));
    });

    suite.section("modify() with DynamicValue");
    suite.run("DynamicValue on W1C register", [](TestContext& t) {
        MockTransport hw;

        // Ctrl16: W1C bits set, Enable=0, Mode=0
        hw.poke<std::uint16_t>(0x20, std::uint16_t{0x0003});

        // modify() with DynamicValue — W1C bits must be masked
        hw.modify(Ctrl16Mode::value(static_cast<std::uint8_t>(0x02)));
        auto result = hw.peek<std::uint16_t>(0x20);
        t.is_true(((result >> 4) & 0x03) == std::uint16_t{0x02});
        t.is_true((result & 0x03U) == std::uint16_t{0});
    });
    suite.run("mixed Value + DynamicValue", [](TestContext& t) {
        MockTransport hw;

        hw.poke<std::uint32_t>(0x04, 0U);

        // Modify: Set enable (Value) + prescaler 0x42 (DynamicValue) in one RMW
        hw.modify(ConfigEnable::Set{}, ConfigPrescaler::value(static_cast<std::uint8_t>(0x42)));
        t.is_true(hw.is(ConfigEnable::Set{}));
        t.eq(hw.read(ConfigPrescaler{}).bits(), static_cast<std::uint8_t>(0x42));
        t.is_true(hw.is(ModeNormal{}));
    });

    suite.section("RegionValue comparisons");
    suite.run("field == Value", [](TestContext& t) {
        MockTransport hw;

        hw.poke<std::uint32_t>(0x04, 0x02U); // Mode = FAST (bits 1-2 = 01)
        auto mode_val = hw.read(ConfigMode{});
        t.is_true(mode_val == ModeFast{});
        t.is_true(!(mode_val == ModeNormal{}));
    });
    suite.run("field == DynamicValue", [](TestContext& t) {
        MockTransport hw;

        hw.poke<std::uint32_t>(0x04, 0x42U << 8); // Prescaler = 0x42
        auto presc_val = hw.read(ConfigPrescaler{});
        // DynamicValue comparison uses .bits() since value() returns DynamicValue
        // with base Field type parameter, not the derived type alias.
        t.is_true(presc_val.bits() == static_cast<std::uint8_t>(0x42));
        t.is_true(presc_val.bits() != static_cast<std::uint8_t>(0x43));
        // is() at RegOps level handles DynamicValue correctly
        t.is_true(hw.is(ConfigPrescaler::value(static_cast<std::uint8_t>(0x42))));
        t.is_true(!hw.is(ConfigPrescaler::value(static_cast<std::uint8_t>(0x43))));
    });
    suite.run("is() out-of-range triggers handler", [](TestContext& t) {
        custom_handler_called = false;
        const CustomErrTransport hw;

        // ConfigPrescaler is 8-bit (max 255). Compare with 256.
        (void)hw.is(DynamicValue<ConfigPrescaler, std::uint16_t>{256});

        t.is_true(custom_handler_called);
    });

    suite.section("64-bit register (direct)");
    suite.run("write/read", [](TestContext& t) {
        const MockTransport hw;

        hw.write(Reg64Direct::value(0x0102'0304'0506'0708ULL));
        auto val = hw.read(Reg64Direct{});
        t.eq(val.bits(), 0x0102'0304'0506'0708ULL);
    });
    suite.run("mask computation", [](TestContext& t) {
        t.eq(Reg64Direct::mask(), ~std::uint64_t{0});
        t.eq(Field64Low32::mask(), static_cast<std::uint64_t>(0xFFFF'FFFF));
        t.eq(Field64High32::mask(), 0xFFFF'FFFF'0000'0000ULL);
    });
    suite.run("field read", [](TestContext& t) {
        const MockTransport hw;

        hw.write(Reg64Direct::value(0xAAAA'BBBB'CCCC'DDDDULL));
        t.eq(hw.read(Field64Low32{}).bits(), 0xCCCC'DDDDU);
        t.eq(hw.read(Field64High32{}).bits(), 0xAAAA'BBBBU);
    });
    suite.run("modify (RMW)", [](TestContext& t) {
        MockTransport hw;

        hw.poke<std::uint64_t>(0x28, 0xFFFF'FFFF'0000'0000ULL);
        hw.modify(Field64Low32::value(0xDEAD'BEEFU));
        t.eq(hw.read(Field64Low32{}).bits(), 0xDEAD'BEEFU);
        t.eq(hw.read(Field64High32{}).bits(), 0xFFFF'FFFFU);
    });
    suite.run("reset", [](TestContext& t) {
        MockTransport hw;

        hw.poke<std::uint64_t>(0x28, 0xFFFF'FFFF'FFFF'FFFFULL);
        hw.reset(Reg64Direct{});
        t.eq(hw.peek<std::uint64_t>(0x28), 0ULL);
    });

    suite.section("Non-zero base_address");
    suite.run("MMIO peripheral addresses", [](TestContext& t) {
        t.eq(MmioCtrl::address, static_cast<Addr>(0x4001'3000));
        t.eq(MmioStatus::address, static_cast<Addr>(0x4001'3004));
        t.eq(MmioCtrlEn::address, static_cast<Addr>(0x4001'3000));
    });
    suite.run("block within MMIO peripheral",
              [](TestContext& t) { t.eq(MmioBlockReg::address, static_cast<Addr>(0x4001'3110)); });

    suite.section("RegionValue edge cases");
    suite.run("RegionValue == RegionValue", [](TestContext& t) {
        MockTransport hw;

        hw.poke<std::uint32_t>(0x04, 0x1234U);
        auto val1 = hw.read(ConfigReg{});
        auto val2 = hw.read(ConfigReg{});
        t.is_true(val1 == val2);

        hw.poke<std::uint32_t>(0x04, 0x5678U);
        auto val3 = hw.read(ConfigReg{});
        t.is_true(!(val1 == val3));
    });
    suite.run("field == DynamicValue (operator)", [](TestContext& t) {
        MockTransport hw;

        hw.poke<std::uint32_t>(0x04, 0x42U << 8); // Prescaler = 0x42
        auto presc_val = hw.read(ConfigPrescaler{});
        // Direct operator== with DynamicValue (requires exact type match)
        // Field::value() returns DynamicValue<Field<...base...>, T>, so we
        // construct DynamicValue<ConfigPrescaler, T> manually for the operator.
        t.is_true(presc_val == DynamicValue<ConfigPrescaler, std::uint8_t>{0x42});
        t.is_true(!(presc_val == DynamicValue<ConfigPrescaler, std::uint8_t>{0x43}));
    });
    suite.run("is() with field DynamicValue", [](TestContext& t) {
        MockTransport hw;

        hw.poke<std::uint32_t>(0x04, 0x42U << 8); // Prescaler = 0x42
        // RegOps::is() with field-level DynamicValue (the primary API path)
        t.is_true(hw.is(ConfigPrescaler::value(static_cast<std::uint8_t>(0x42))));
        t.is_true(!hw.is(ConfigPrescaler::value(static_cast<std::uint8_t>(0x43))));
    });
    suite.run("is() with register DynamicValue", [](TestContext& t) {
        MockTransport hw;

        hw.poke<std::uint32_t>(0x04, 0x4200U);
        // RegOps::is() with register-level DynamicValue
        t.is_true(hw.is(ConfigReg::value(0x4200U)));
        t.is_true(!hw.is(ConfigReg::value(0x4201U)));
    });
    suite.run("field RegionValue == field RegionValue", [](TestContext& t) {
        MockTransport hw;

        hw.poke<std::uint32_t>(0x04, 0x02U); // Mode = FAST (bits 1-2 = 01)
        auto mode1 = hw.read(ConfigMode{});
        auto mode2 = hw.read(ConfigMode{});
        t.is_true(mode1 == mode2);
    });

    suite.section("Value::shifted_value");
    suite.run("shifted values match expected", [](TestContext& t) {
        // ConfigEnable: bit 0, width 1 → shift = 0 → shifted_value = 1 << 0 = 1
        t.eq(ConfigEnable::Set::shifted_value, static_cast<std::uint32_t>(1));
        t.eq(ConfigEnable::Reset::shifted_value, static_cast<std::uint32_t>(0));

        // ConfigMode: shift = 1 → FAST(1) shifted = 1 << 1 = 2
        t.eq(ModeFast::shifted_value, static_cast<std::uint32_t>(2));
        // LOW_POWER(2) shifted = 2 << 1 = 4
        t.eq(ModeLowPower::shifted_value, static_cast<std::uint32_t>(4));
        // TEST(3) shifted = 3 << 1 = 6
        t.eq(ModeTest::shifted_value, static_cast<std::uint32_t>(6));
    });

    suite.section("Multi-transport device");
    suite.run("dual transport allowed", [](TestContext& t) {
        // DualTransportDevice allows both Direct and I2c
        using Allowed = DualTransportDevice::AllowedTransportsType;
        t.is_true((std::tuple_size_v<Allowed> == 2));
    });

    suite.section("CsrTransport (mock)");
    suite.run("write/read", [](TestContext& t) {
        const CsrTransport<MockCsrAccessor> csr;
        csr.write(Mstatus::MIE::Set{});
        t.is_true(csr.is(Mstatus::MIE::Set{}));
    });
    suite.run("field modify", [](TestContext& t) {
        const CsrTransport<MockCsrAccessor> csr;
        // Set MPP to MACHINE (bits 12:11 = 0b11)
        csr.modify(Mstatus::MPP::MACHINE{});
        t.is_true(csr.is(Mstatus::MPP::MACHINE{}));

        // Change to SUPERVISOR (bits 12:11 = 0b01)
        csr.modify(Mstatus::MPP::SUPERVISOR{});
        t.is_true(csr.is(Mstatus::MPP::SUPERVISOR{}));
    });
    suite.run("multi-field write", [](TestContext& t) {
        const CsrTransport<MockCsrAccessor> csr;
        csr.write(Mstatus::MIE::Set{}, Mstatus::MPP::MACHINE{});

        auto cfg = csr.read(Mstatus{});
        t.is_true(cfg.is(Mstatus::MIE::Set{}));
        t.is_true(cfg.is(Mstatus::MPP::MACHINE{}));
    });
    suite.run("mtvec numeric field", [](TestContext& t) {
        const CsrTransport<MockCsrAccessor> csr;
        csr.write(Mtvec::MODE::VECTORED{}, Mtvec::BASE::value(0x2000'0000U >> 2));

        t.is_true(csr.is(Mtvec::MODE::VECTORED{}));
        auto base_val = csr.read(Mtvec::BASE{});
        t.eq(base_val.bits(), static_cast<std::uint32_t>(0x2000'0000U >> 2));
    });
    suite.run("flip", [](TestContext& t) {
        const CsrTransport<MockCsrAccessor> csr;
        csr.write(Mstatus::MIE::Reset{});
        t.is_true(csr.is(Mstatus::MIE::Reset{}));

        csr.flip(Mstatus::MIE{});
        t.is_true(csr.is(Mstatus::MIE::Set{}));

        csr.flip(Mstatus::MIE{});
        t.is_true(csr.is(Mstatus::MIE::Reset{}));
    });
    suite.run("CsrAccessor concept", [](TestContext& t) { t.is_true(CsrAccessor<MockCsrAccessor>); });
    suite.run("transport tag", [](TestContext& t) {
        // Verify CsrTransport uses Csr tag
        using Tag = CsrTransport<MockCsrAccessor>::TransportTag;
        t.is_true((std::is_same_v<Tag, Csr>));
    });
}

} // namespace umimmio::test
