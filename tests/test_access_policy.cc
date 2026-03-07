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

void test_bit_constants_width_values(TestContext& t) {
    t.eq(bits8, 8U);
    t.eq(bits16, 16U);
    t.eq(bits32, 32U);
    t.eq(bits64, 64U);
}

// =============================================================================
// W1C access policy
// =============================================================================

void test_w1c_policy(TestContext& t) {
    t.is_true(W1C::can_read && W1C::can_write);
    t.eq(static_cast<uint8_t>(W1C::write_behavior), static_cast<uint8_t>(WriteBehavior::ONE_TO_CLEAR));
    t.eq(static_cast<uint8_t>(RW::write_behavior), static_cast<uint8_t>(WriteBehavior::NORMAL));
}

// =============================================================================
// Block hierarchy
// =============================================================================

struct MyDevice : Device<> {};
struct MyBlock : Block<MyDevice, 0x100, RO> {};
struct MyReg : Register<MyBlock, 0x04, bits32, Inherit> {};
struct MyField : Field<MyReg, 8, 4> {};

void test_block_address_calculation(TestContext& t) {
    // MyDevice base = 0
    // MyBlock base = 0 + 0x100 = 0x100
    t.eq(MyBlock::base_address, static_cast<Addr>(0x100));
    // MyReg address = 0x100 + 0x04 = 0x104
    t.eq(MyReg::address, static_cast<Addr>(0x104));
    // MyField shares the same address as parent register
    t.eq(MyField::address, static_cast<Addr>(0x104));
}

void test_block_access_inheritance(TestContext& t) {
    // Block is RO, register inherits → should be RO
    t.is_true(MyReg::AccessType::can_read);
    t.is_true(!MyReg::AccessType::can_write);
}

// =============================================================================
// Nested block hierarchy
// =============================================================================

struct TopDevice : Device<> {};
struct SubBlock1 : Block<TopDevice, 0x1000> {};
struct SubBlock2 : Block<SubBlock1, 0x200> {};
struct DeepReg : Register<SubBlock2, 0x10, bits16> {};

void test_nested_blocks_address_calc(TestContext& t) {
    // 0 + 0x1000 + 0x200 + 0x10 = 0x1210
    t.eq(DeepReg::address, static_cast<Addr>(0x1210));
    t.eq(DeepReg::bit_width, 16U);
    // Inherits RW from TopDevice
    t.is_true(DeepReg::AccessType::can_read && DeepReg::AccessType::can_write);
}

// =============================================================================
// Register mask computation
// =============================================================================

void test_register_mask_full_width(TestContext& t) {
    // A 32-bit register with full width: mask should be 0xFFFFFFFF
    t.eq(ConfigReg::mask(), static_cast<uint32_t>(0xFFFF'FFFF));
    t.eq(CtrlReg::mask(), static_cast<uint16_t>(0xFFFF));

    // StatusReg: same — full-width mask
    t.eq(StatusReg::mask(), static_cast<uint32_t>(0xFFFF'FFFF));
}

// =============================================================================
// Practical: multi-register device scenario
// =============================================================================

/// @brief Simulates a UART-like device.
struct UartDevice : Device<> {};
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

void test_uart_device_init(TestContext& t) {
    const MockTransport hw;

    // Configure UART: 115200 baud, 8N1, RX+TX enabled
    hw.write(UartBaud::value(115200U));
    hw.write(UartCtrlEn::Set{}, UartCtrlTxEn::Set{}, UartCtrlRxEn::Set{}, ParityNone{});
    t.eq(hw.read(UartBaud{}).bits(), 115200U);
    t.eq(hw.read(UartCtrlEn{}).bits(), static_cast<uint8_t>(1));
    t.eq(hw.read(UartCtrlTxEn{}).bits(), static_cast<uint8_t>(1));
    t.eq(hw.read(UartCtrlRxEn{}).bits(), static_cast<uint8_t>(1));
    t.is_true(hw.is(ParityNone{}));

    // Change parity to EVEN via modify (preserve other bits)
    hw.modify(ParityEven{});
    t.is_true(hw.is(ParityEven{}));
    t.eq(hw.read(UartCtrlEn{}).bits(), static_cast<uint8_t>(1));

    // Write data
    hw.write(UartData::value(0x41U)); // 'A'
    t.eq(hw.read(UartData{}).bits(), 0x41U);
}

} // namespace

void run_access_policy_tests(umi::test::Suite& suite) {
    suite.section("Bit constants");
    suite.run("width values", test_bit_constants_width_values);
    suite.run("W1C policy", test_w1c_policy);

    suite.section("Block hierarchy");
    suite.run("address calculation", test_block_address_calculation);
    suite.run("access inheritance", test_block_access_inheritance);
    suite.run("nested blocks", test_nested_blocks_address_calc);

    suite.section("Register masks");
    suite.run("full-width masks", test_register_mask_full_width);

    suite.section("Practical: UART device");
    suite.run("full init and operation", test_uart_device_init);
}

} // namespace umimmio::test
