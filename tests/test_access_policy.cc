// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Access policy and error policy tests.
/// @author Shota Moriguchi @tekitounix
/// @details Verifies Bit constants, Block hierarchy, and
///          compile-time properties of the MMIO framework.

#include "test_fixture.hh"

namespace umimmio::test {
namespace {

using umi::test::TestContext;

// =============================================================================
// Bit width constants
// =============================================================================

bool test_bit_constants_width_values(TestContext& t) {
    bool ok = true;
    ok &= t.assert_eq(bits8, 8U);
    ok &= t.assert_eq(bits16, 16U);
    ok &= t.assert_eq(bits32, 32U);
    ok &= t.assert_eq(bits64, 64U);
    return ok;
}

// =============================================================================
// W1C access policy
// =============================================================================

bool test_w1c_policy(TestContext& t) {
    bool ok = true;
    ok &= t.assert_true(W1C::can_read && W1C::can_write);
    ok &= t.assert_eq(static_cast<uint8_t>(W1C::write_behavior), static_cast<uint8_t>(WriteBehavior::ONE_TO_CLEAR));
    ok &= t.assert_eq(static_cast<uint8_t>(RW::write_behavior), static_cast<uint8_t>(WriteBehavior::NORMAL));
    return ok;
}

// =============================================================================
// Block hierarchy
// =============================================================================

struct MyDevice : Device<RW, DirectTransportTag> {};
struct MyBlock : Block<MyDevice, 0x100, RO> {};
struct MyReg : Register<MyBlock, 0x04, bits32, Inherit> {};
struct MyField : Field<MyReg, 8, 4> {};

bool test_block_address_calculation(TestContext& t) {
    bool ok = true;
    // MyDevice base = 0
    // MyBlock base = 0 + 0x100 = 0x100
    ok &= t.assert_eq(MyBlock::base_address, static_cast<Addr>(0x100));
    // MyReg address = 0x100 + 0x04 = 0x104
    ok &= t.assert_eq(MyReg::address, static_cast<Addr>(0x104));
    // MyField shares the same address as parent register
    ok &= t.assert_eq(MyField::address, static_cast<Addr>(0x104));
    return ok;
}

bool test_block_access_inheritance(TestContext& t) {
    bool ok = true;
    // Block is RO, register inherits → should be RO
    ok &= t.assert_true(MyReg::AccessType::can_read);
    ok &= t.assert_true(!MyReg::AccessType::can_write);
    return ok;
}

// =============================================================================
// Nested block hierarchy
// =============================================================================

struct TopDevice : Device<RW, DirectTransportTag> {};
struct SubBlock1 : Block<TopDevice, 0x1000> {};
struct SubBlock2 : Block<SubBlock1, 0x200> {};
struct DeepReg : Register<SubBlock2, 0x10, bits16> {};

bool test_nested_blocks_address_calc(TestContext& t) {
    bool ok = true;
    // 0 + 0x1000 + 0x200 + 0x10 = 0x1210
    ok &= t.assert_eq(DeepReg::address, static_cast<Addr>(0x1210));
    ok &= t.assert_eq(DeepReg::bit_width, 16U);
    // Inherits RW from TopDevice
    ok &= t.assert_true(DeepReg::AccessType::can_read && DeepReg::AccessType::can_write);
    return ok;
}

// =============================================================================
// Register mask computation
// =============================================================================

bool test_register_mask_full_width(TestContext& t) {
    // A 32-bit register with full width: mask should be 0xFFFFFFFF
    bool ok = true;
    ok &= t.assert_eq(ConfigReg::mask(), static_cast<uint32_t>(0xFFFF'FFFF));
    ok &= t.assert_eq(CtrlReg::mask(), static_cast<uint16_t>(0xFFFF));

    // StatusReg: same — full-width mask
    ok &= t.assert_eq(StatusReg::mask(), static_cast<uint32_t>(0xFFFF'FFFF));
    return ok;
}

// =============================================================================
// Practical: multi-register device scenario
// =============================================================================

/// @brief Simulates a UART-like device.
struct UartDevice : Device<RW, DirectTransportTag> {};
struct UartCtrl : Register<UartDevice, 0x00, bits32, RW, 0> {};
struct UartStatus : Register<UartDevice, 0x04, bits32, RO, 0> {};
struct UartData : Register<UartDevice, 0x08, bits32, RW, 0> {};
struct UartBaud : Register<UartDevice, 0x0C, bits32, RW, 9600> {};

struct UartCtrlEn : Field<UartCtrl, 0, 1> {};
struct UartCtrlTxEn : Field<UartCtrl, 1, 1> {};
struct UartCtrlRxEn : Field<UartCtrl, 2, 1> {};
struct UartCtrlParity : Field<UartCtrl, 4, 2> {};

enum class Parity : uint8_t { NONE = 0, EVEN = 1, ODD = 2 };
using ParityNone = Value<UartCtrlParity, static_cast<uint8_t>(Parity::NONE)>;
using ParityEven = Value<UartCtrlParity, static_cast<uint8_t>(Parity::EVEN)>;
using ParityOdd = Value<UartCtrlParity, static_cast<uint8_t>(Parity::ODD)>;

bool test_uart_device_init(TestContext& t) {
    MockTransport hw;

    // Configure UART: 115200 baud, 8N1, RX+TX enabled
    hw.write(UartBaud::value(115200U));
    hw.write(UartCtrlEn::Set{}, UartCtrlTxEn::Set{}, UartCtrlRxEn::Set{}, ParityNone{});

    bool ok = true;
    ok &= t.assert_eq(hw.read(UartBaud{}).bits(), 115200U);
    ok &= t.assert_eq(hw.read(UartCtrlEn{}).bits(), static_cast<uint8_t>(1));
    ok &= t.assert_eq(hw.read(UartCtrlTxEn{}).bits(), static_cast<uint8_t>(1));
    ok &= t.assert_eq(hw.read(UartCtrlRxEn{}).bits(), static_cast<uint8_t>(1));
    ok &= t.assert_true(hw.is(ParityNone{}), "parity is NONE");

    // Change parity to EVEN via modify (preserve other bits)
    hw.modify(ParityEven{});
    ok &= t.assert_true(hw.is(ParityEven{}), "parity is EVEN");
    ok &= t.assert_eq(hw.read(UartCtrlEn{}).bits(), static_cast<uint8_t>(1));

    // Write data
    hw.write(UartData::value(0x41U)); // 'A'
    ok &= t.assert_eq(hw.read(UartData{}).bits(), 0x41U);

    return ok;
}

} // namespace

void run_access_policy_tests(umi::test::Suite& suite) {
    umi::test::Suite::section("Bit constants");
    suite.run("width values", test_bit_constants_width_values);
    suite.run("W1C policy", test_w1c_policy);

    umi::test::Suite::section("Block hierarchy");
    suite.run("address calculation", test_block_address_calculation);
    suite.run("access inheritance", test_block_access_inheritance);
    suite.run("nested blocks", test_nested_blocks_address_calc);

    umi::test::Suite::section("Register masks");
    suite.run("full-width masks", test_register_mask_full_width);

    umi::test::Suite::section("Practical: UART device");
    suite.run("full init and operation", test_uart_device_init);
}

} // namespace umimmio::test
