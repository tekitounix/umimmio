// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Transport layer tests — MockTransport, ByteAdapter with mock I2C.
/// @author Shota Moriguchi @tekitounix
/// @details Tests both direct (RAM-backed) and byte-oriented transports.

#include <array>
#include <cstring>
#include <span>

#include <umimmio/transport/i2c.hh>

#include "test_fixture.hh"

namespace umimmio::test {
namespace {

using umi::test::TestContext;

// =============================================================================
// Type traits and compile-time properties
// =============================================================================

bool test_uint_fit_sizes(TestContext& t) {
    bool ok = true;
    ok &= t.assert_eq(sizeof(UintFit<8>), 1U);
    ok &= t.assert_eq(sizeof(UintFit<16>), 2U);
    ok &= t.assert_eq(sizeof(UintFit<32>), 4U);
    ok &= t.assert_eq(sizeof(UintFit<64>), 8U);
    // Edge: 1-bit fits in uint8_t
    ok &= t.assert_eq(sizeof(UintFit<1>), 1U);
    ok &= t.assert_eq(sizeof(UintFit<7>), 1U);
    ok &= t.assert_eq(sizeof(UintFit<9>), 2U);
    ok &= t.assert_eq(sizeof(UintFit<33>), 8U);
    return ok;
}

bool test_access_policy_traits(TestContext& t) {
    bool ok = true;
    ok &= t.assert_true(RW::can_read && RW::can_write);
    ok &= t.assert_true(RO::can_read && !RO::can_write);
    ok &= t.assert_true(!WO::can_read && WO::can_write);
    return ok;
}

bool test_register_static_properties(TestContext& t) {
    bool ok = true;

    // StatusReg: offset 0x00, 32-bit, reset = 0x0001
    ok &= t.assert_eq(StatusReg::address, static_cast<Addr>(0x00));
    ok &= t.assert_eq(StatusReg::bit_width, 32U);
    ok &= t.assert_eq(StatusReg::reset_value(), static_cast<uint32_t>(0x0001));
    ok &= t.assert_true(StatusReg::AccessType::can_read);
    ok &= t.assert_true(!StatusReg::AccessType::can_write);

    // ConfigReg: offset 0x04, 32-bit, reset = 0xFF00
    ok &= t.assert_eq(ConfigReg::address, static_cast<Addr>(0x04));
    ok &= t.assert_eq(ConfigReg::reset_value(), static_cast<uint32_t>(0xFF00));

    // CtrlReg: offset 0x0C, 16-bit
    ok &= t.assert_eq(CtrlReg::address, static_cast<Addr>(0x0C));
    ok &= t.assert_eq(CtrlReg::bit_width, 16U);

    return ok;
}

bool test_field_static_properties(TestContext& t) {
    bool ok = true;

    // ConfigEnable: bit 0, width 1
    ok &= t.assert_eq(ConfigEnable::shift, 0U);
    ok &= t.assert_eq(ConfigEnable::bit_width, 1U);
    ok &= t.assert_eq(ConfigEnable::mask(), static_cast<uint32_t>(0x1));

    // ConfigMode: bit 1, width 2
    ok &= t.assert_eq(ConfigMode::shift, 1U);
    ok &= t.assert_eq(ConfigMode::bit_width, 2U);
    ok &= t.assert_eq(ConfigMode::mask(), static_cast<uint32_t>(0x6));

    // ConfigPrescaler: bit 8, width 8
    ok &= t.assert_eq(ConfigPrescaler::shift, 8U);
    ok &= t.assert_eq(ConfigPrescaler::bit_width, 8U);
    ok &= t.assert_eq(ConfigPrescaler::mask(), static_cast<uint32_t>(0xFF00));

    // CtrlChannel: bit 4, width 4
    ok &= t.assert_eq(CtrlChannel::shift, 4U);
    ok &= t.assert_eq(CtrlChannel::bit_width, 4U);
    ok &= t.assert_eq(CtrlChannel::mask(), static_cast<uint16_t>(0xF0));

    return ok;
}

bool test_value_static_properties(TestContext& t) {
    bool ok = true;
    ok &= t.assert_eq(ConfigEnable::Set::value, static_cast<uint8_t>(1));
    ok &= t.assert_eq(ConfigEnable::Reset::value, static_cast<uint8_t>(0));
    ok &= t.assert_eq(ModeNormal::value, static_cast<uint8_t>(ModeVal::NORMAL));
    ok &= t.assert_eq(ModeFast::value, static_cast<uint8_t>(ModeVal::FAST));
    return ok;
}

// =============================================================================
// MockTransport direct buffer access
// =============================================================================

bool test_mock_transport_peek_poke(TestContext& t) {
    MockTransport hw;

    hw.poke<uint32_t>(0x00, 0x12345678U);
    bool ok = t.assert_eq(hw.peek<uint32_t>(0x00), 0x12345678U);

    hw.poke<uint16_t>(0x10, uint16_t{0xABCD});
    ok &= t.assert_eq(hw.peek<uint16_t>(0x10), static_cast<uint16_t>(0xABCD));

    hw.reset();
    ok &= t.assert_eq(hw.peek<uint32_t>(0x00), 0U);
    return ok;
}

// =============================================================================
// Mock I2C driver for ByteAdapter testing
// =============================================================================

/// @brief Minimal I2C driver mock that stores register data in RAM.
struct MockI2C {
    mutable std::array<uint8_t, 256> memory{};

    struct Result {
        explicit operator bool() const { return success; }
        bool success = true;
    };

    Result write(uint8_t /*dev_addr*/, std::span<const uint8_t> data) const {
        if (data.size() < 2) {
            return {false};
        }
        uint8_t const reg_addr = data[0];
        std::memcpy(&memory[reg_addr], data.data() + 1, data.size() - 1);
        return {true};
    }

    Result write_read(uint8_t /*dev_addr*/, std::span<const uint8_t> tx, std::span<uint8_t> rx) const {
        if (tx.empty()) {
            return {false};
        }
        uint8_t const reg_addr = tx[0];
        std::memcpy(rx.data(), &memory[reg_addr], rx.size());
        return {true};
    }
};

/// @brief I2C device for testing — allows I2CTransportTag
struct I2CDevice : Device<RW, I2CTransportTag> {};

/// @brief Register on I2C device at offset 0x10
struct I2CReg : Register<I2CDevice, 0x10, bits32, RW, 0> {};
struct I2CField : Field<I2CReg, 0, 8> {};

bool test_i2c_transport_write_read(TestContext& t) {
    MockI2C i2c;
    I2cTransport<MockI2C> const transport(i2c, 0x50);

    // Write a value through I2C transport
    transport.write(I2CReg::value(0xDEAD'BEEFU));

    // Read it back
    auto val = transport.read(I2CReg{});

    return t.assert_eq(val, static_cast<uint32_t>(0xDEAD'BEEF));
}

bool test_i2c_transport_field_operations(TestContext& t) {
    MockI2C i2c;
    I2cTransport<MockI2C> const transport(i2c, 0x50);

    // Write via register
    transport.write(I2CReg::value(0x00000042U));

    // Read field
    auto field_val = transport.read(I2CField{});
    return t.assert_eq(field_val, static_cast<uint8_t>(0x42));
}

bool test_i2c_transport_modify(TestContext& t) {
    MockI2C i2c;
    I2cTransport<MockI2C> const transport(i2c, 0x50);

    // Pre-load value
    transport.write(I2CReg::value(0xFF00'0000U));

    // Modify just the low 8 bits
    transport.modify(I2CField::value(static_cast<uint8_t>(0xAB)));

    bool ok = true;
    auto reg_val = transport.read(I2CReg{});
    // Low byte should be 0xAB, high bytes preserved
    ok &= t.assert_eq(reg_val & 0xFFU, 0xABU);
    ok &= t.assert_eq(reg_val & 0xFF00'0000U, 0xFF00'0000U);
    return ok;
}

} // namespace

void run_transport_tests(umi::test::Suite& suite) {
    umi::test::Suite::section("Type traits");
    suite.run("UintFit sizes", test_uint_fit_sizes);
    suite.run("access policy flags", test_access_policy_traits);

    umi::test::Suite::section("Static properties");
    suite.run("register addresses/reset", test_register_static_properties);
    suite.run("field shift/width/mask", test_field_static_properties);
    suite.run("Value constants", test_value_static_properties);

    umi::test::Suite::section("MockTransport internals");
    suite.run("peek/poke/reset", test_mock_transport_peek_poke);

    umi::test::Suite::section("I2C transport (mock)");
    suite.run("write/read", test_i2c_transport_write_read);
    suite.run("field operations", test_i2c_transport_field_operations);
    suite.run("modify (RMW)", test_i2c_transport_modify);
}

} // namespace umimmio::test
