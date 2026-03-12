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
// W1S/W1T helper types
// =============================================================================

struct W1sDevice : Device<> {};
struct W1sReg : Register<W1sDevice, 0x00, bits32, WO> {};
struct W1sField : Field<W1sReg, 0, 1, W1S> {};
struct W1tField : Field<W1sReg, 1, 1, W1T> {};
struct NormalField : Field<W1sReg, 2, 1> {};

// =============================================================================
// Block hierarchy helper types
// =============================================================================

struct MyDevice : Device<> {};
struct MyBlock : Block<MyDevice, 0x100, RO> {};
struct MyReg : Register<MyBlock, 0x04, bits32, Inherit> {};
struct MyField : Field<MyReg, 8, 4> {};

struct TopDevice : Device<> {};
struct SubBlock1 : Block<TopDevice, 0x1000> {};
struct SubBlock2 : Block<SubBlock1, 0x200> {};
struct DeepReg : Register<SubBlock2, 0x10, bits16> {};

// =============================================================================
// UART helper types
// =============================================================================

struct UartDevice : Device<> {};
struct UartCtrl : Register<UartDevice, 0x00, bits32, RW, 0> {};
struct UartStatus : Register<UartDevice, 0x04, bits32, RO, 0> {};
struct UartData : Register<UartDevice, 0x08, bits32, RW, 0> {};
struct UartBaud : Register<UartDevice, 0x0C, bits32, RW, 9600> {};

struct UartCtrlEn : Field<UartCtrl, 0, 1> {};
struct UartCtrlTxEn : Field<UartCtrl, 1, 1> {};
struct UartCtrlRxEn : Field<UartCtrl, 2, 1> {};
struct UartCtrlParity : Field<UartCtrl, 4, 2> {};

enum class Parity : std::uint8_t { NONE = 0, EVEN = 1, ODD = 2 };
using ParityNone = Value<UartCtrlParity, static_cast<std::uint8_t>(Parity::NONE)>;
using ParityEven = Value<UartCtrlParity, static_cast<std::uint8_t>(Parity::EVEN)>;
using ParityOdd = Value<UartCtrlParity, static_cast<std::uint8_t>(Parity::ODD)>;

} // namespace

inline void run_access_policy_tests(umi::test::Suite& suite) {
    suite.section("Bit constants");

    suite.run("width values", [](auto& t) {
        t.eq(bits8, 8U);
        t.eq(bits16, 16U);
        t.eq(bits32, 32U);
        t.eq(bits64, 64U);
    });

    suite.run("W1C policy", [](auto& t) {
        t.is_true(W1C::can_read && W1C::can_write);
        t.eq(static_cast<std::uint8_t>(W1C::write_behavior), static_cast<std::uint8_t>(WriteBehavior::ONE_TO_CLEAR));
        t.eq(static_cast<std::uint8_t>(RW::write_behavior), static_cast<std::uint8_t>(WriteBehavior::NORMAL));
    });

    suite.run("W1S policy", [](auto& t) {
        t.is_true(!W1S::can_read);
        t.is_true(W1S::can_write);
        t.eq(static_cast<std::uint8_t>(W1S::write_behavior), static_cast<std::uint8_t>(WriteBehavior::ONE_TO_SET));
    });

    suite.run("W1T policy", [](auto& t) {
        t.is_true(!W1T::can_read);
        t.is_true(W1T::can_write);
        t.eq(static_cast<std::uint8_t>(W1T::write_behavior), static_cast<std::uint8_t>(WriteBehavior::ONE_TO_TOGGLE));
    });

    suite.section("W1S/W1T concepts and aliases");

    suite.run("IsW1S/IsW1T/NormalWrite concepts", [](auto& t) {
        t.is_true(IsW1S<W1sField>);
        t.is_true(!IsW1S<W1tField>);
        t.is_true(!IsW1S<NormalField>);

        t.is_true(IsW1T<W1tField>);
        t.is_true(!IsW1T<W1sField>);
        t.is_true(!IsW1T<NormalField>);

        t.is_true(NormalWrite<NormalField>);
        t.is_true(!NormalWrite<W1sField>);
        t.is_true(!NormalWrite<W1tField>);
        t.is_true(!NormalWrite<W1cOvr>);

        t.is_true(IsW1C<W1cOvr>);
        t.is_true(!IsW1C<W1sField>);
        t.is_true(!IsW1C<W1tField>);
    });

    suite.run("W1S 1-bit aliases", [](auto& t) {
        using SetVal = W1sField::Set;
        using ResetVal = W1sField::Reset;
        t.eq(static_cast<std::uint32_t>(SetVal::value), 1U);
        t.eq(static_cast<std::uint32_t>(ResetVal::value), 0U);
    });

    suite.run("W1T 1-bit aliases", [](auto& t) {
        using ToggleVal = W1tField::Toggle;
        t.eq(static_cast<std::uint32_t>(ToggleVal::value), 1U);
    });

    suite.run("W1S write", [](auto& t) {
        MockTransport hw;
        hw.clear_memory();
        hw.write(W1sField::Set{});
        auto raw = hw.peek<std::uint32_t>(0x00);
        t.eq(raw, 1U);
    });

    suite.run("W1T write", [](auto& t) {
        MockTransport hw;
        hw.clear_memory();
        hw.write(W1tField::Toggle{});
        auto raw = hw.peek<std::uint32_t>(0x00);
        t.eq(raw, 2U);
    });

    suite.section("Block hierarchy");

    suite.run("address calculation", [](auto& t) {
        t.eq(MyBlock::base_address, static_cast<Addr>(0x100));
        t.eq(MyReg::address, static_cast<Addr>(0x104));
        t.eq(MyField::address, static_cast<Addr>(0x104));
    });

    suite.run("access inheritance", [](auto& t) {
        t.is_true(MyReg::AccessType::can_read);
        t.is_true(!MyReg::AccessType::can_write);
    });

    suite.run("nested blocks", [](auto& t) {
        t.eq(DeepReg::address, static_cast<Addr>(0x1210));
        t.eq(DeepReg::bit_width, 16U);
        t.is_true(DeepReg::AccessType::can_read && DeepReg::AccessType::can_write);
    });

    suite.section("Register masks");

    suite.run("full-width masks", [](auto& t) {
        t.eq(ConfigReg::mask(), static_cast<std::uint32_t>(0xFFFF'FFFF));
        t.eq(CtrlReg::mask(), static_cast<std::uint16_t>(0xFFFF));
        t.eq(StatusReg::mask(), static_cast<std::uint32_t>(0xFFFF'FFFF));
    });

    suite.section("Practical: UART device");

    suite.run("full init and operation", [](auto& t) {
        const MockTransport hw;

        hw.write(UartBaud::value(115200U));
        hw.write(UartCtrlEn::Set{}, UartCtrlTxEn::Set{}, UartCtrlRxEn::Set{}, ParityNone{});
        t.eq(hw.read(UartBaud{}).bits(), 115200U);
        t.is_true(hw.is(UartCtrlEn::Set{}));
        t.is_true(hw.is(UartCtrlTxEn::Set{}));
        t.is_true(hw.is(UartCtrlRxEn::Set{}));
        t.is_true(hw.is(ParityNone{}));

        hw.modify(ParityEven{});
        t.is_true(hw.is(ParityEven{}));
        t.is_true(hw.is(UartCtrlEn::Set{}));

        hw.write(UartData::value(0x41U));
        t.eq(hw.read(UartData{}).bits(), 0x41U);
    });
}

} // namespace umimmio::test
