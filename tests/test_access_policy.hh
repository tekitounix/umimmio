// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Access policy and error policy tests.
/// @author Shota Moriguchi @tekitounix
/// @details Verifies Bit constants, Block hierarchy, and
///          compile-time properties of the MMIO framework.
#pragma once

#include "test_mock.hh"

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
// W1S/W1T access policies
// =============================================================================

void test_w1s_policy(TestContext& t) {
    t.is_true(!W1S::can_read);
    t.is_true(W1S::can_write);
    t.eq(static_cast<uint8_t>(W1S::write_behavior), static_cast<uint8_t>(WriteBehavior::ONE_TO_SET));
}

void test_w1t_policy(TestContext& t) {
    t.is_true(!W1T::can_read);
    t.is_true(W1T::can_write);
    t.eq(static_cast<uint8_t>(W1T::write_behavior), static_cast<uint8_t>(WriteBehavior::ONE_TO_TOGGLE));
}

// =============================================================================
// W1S/W1T concepts
// =============================================================================

struct W1sDevice : Device<> {};
struct W1sReg : Register<W1sDevice, 0x00, bits32, WO> {};
struct W1sField : Field<W1sReg, 0, 1, W1S> {};
struct W1tField : Field<W1sReg, 1, 1, W1T> {};
struct NormalField : Field<W1sReg, 2, 1> {};

void test_w1s_w1t_concepts(TestContext& t) {
    // IsW1S
    t.is_true(IsW1S<W1sField>);
    t.is_true(!IsW1S<W1tField>);
    t.is_true(!IsW1S<NormalField>);

    // IsW1T
    t.is_true(IsW1T<W1tField>);
    t.is_true(!IsW1T<W1sField>);
    t.is_true(!IsW1T<NormalField>);

    // NormalWrite
    t.is_true(NormalWrite<NormalField>);
    t.is_true(!NormalWrite<W1sField>);
    t.is_true(!NormalWrite<W1tField>);
    t.is_true(!NormalWrite<W1cOvr>); // W1C is also not NormalWrite

    // IsW1C unchanged
    t.is_true(IsW1C<W1cOvr>);
    t.is_true(!IsW1C<W1sField>);
    t.is_true(!IsW1C<W1tField>);
}

// =============================================================================
// W1S/W1T 1-bit aliases
// =============================================================================

void test_w1s_one_bit_aliases(TestContext& t) {
    // W1S 1-bit field gets Set/Reset (same as NORMAL)
    using SetVal = W1sField::Set;
    using ResetVal = W1sField::Reset;
    t.eq(static_cast<uint32_t>(SetVal::value), 1U);
    t.eq(static_cast<uint32_t>(ResetVal::value), 0U);
}

void test_w1t_one_bit_aliases(TestContext& t) {
    // W1T 1-bit field gets Toggle (not Set/Reset)
    using ToggleVal = W1tField::Toggle;
    t.eq(static_cast<uint32_t>(ToggleVal::value), 1U);
}

// =============================================================================
// W1S/W1T write operations
// =============================================================================

void test_w1s_write(TestContext& t) {
    MockTransport hw;
    hw.clear_memory();
    // W1S field can be written via write()
    hw.write(W1sField::Set{});
    auto raw = hw.peek<uint32_t>(0x00);
    t.eq(raw, 1U);
}

void test_w1t_write(TestContext& t) {
    MockTransport hw;
    hw.clear_memory();
    // W1T field can be written via write()
    hw.write(W1tField::Toggle{});
    auto raw = hw.peek<uint32_t>(0x00);
    t.eq(raw, 2U); // bit 1
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
    t.is_true(hw.is(UartCtrlEn::Set{}));
    t.is_true(hw.is(UartCtrlTxEn::Set{}));
    t.is_true(hw.is(UartCtrlRxEn::Set{}));
    t.is_true(hw.is(ParityNone{}));

    // Change parity to EVEN via modify (preserve other bits)
    hw.modify(ParityEven{});
    t.is_true(hw.is(ParityEven{}));
    t.is_true(hw.is(UartCtrlEn::Set{}));

    // Write data
    hw.write(UartData::value(0x41U)); // 'A'
    t.eq(hw.read(UartData{}).bits(), 0x41U);
}

} // namespace

inline void register_access_policy_tests(umi::test::Suite& suite) {
    suite.section("Bit constants");
    suite.run("width values", test_bit_constants_width_values);
    suite.run("W1C policy", test_w1c_policy);
    suite.run("W1S policy", test_w1s_policy);
    suite.run("W1T policy", test_w1t_policy);

    suite.section("W1S/W1T concepts and aliases");
    suite.run("IsW1S/IsW1T/NormalWrite concepts", test_w1s_w1t_concepts);
    suite.run("W1S 1-bit aliases", test_w1s_one_bit_aliases);
    suite.run("W1T 1-bit aliases", test_w1t_one_bit_aliases);
    suite.run("W1S write", test_w1s_write);
    suite.run("W1T write", test_w1t_write);

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
