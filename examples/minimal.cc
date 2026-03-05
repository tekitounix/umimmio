// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Minimal Device/Register/Field definition example.
/// @author Shota Moriguchi @tekitounix

#include <cstdio>

#include <umimmio/region.hh>

// Define a device with direct transport
struct MyDevice : umi::mmio::Device<umi::mmio::RW, umi::mmio::DirectTransportTag> {};

// Define an 8-bit register at offset 0x00
struct StatusReg : umi::mmio::Register<MyDevice, 0x00, umi::mmio::bits8, umi::mmio::RO> {};

// Define bit fields within the register
struct Ready : umi::mmio::Field<StatusReg, 0, 1> {}; // bit 0
struct Error : umi::mmio::Field<StatusReg, 1, 1> {}; // bit 1
struct Mode : umi::mmio::Field<StatusReg, 4, 2> {};  // bits 4-5

// Define named values for the Mode field
using ModeIdle = umi::mmio::Value<Mode, 0>;
using ModeActive = umi::mmio::Value<Mode, 1>;

int main() {
    // Static properties — all evaluated at compile time
    static_assert(StatusReg::address == 0x00);
    static_assert(Ready::mask() == 0x01);
    static_assert(Mode::mask() == 0x30);
    static_assert(ModeActive::value == 1);

    std::printf("StatusReg address: 0x%08lX\n", static_cast<unsigned long>(StatusReg::address));
    std::printf("Ready mask: 0x%02X\n", Ready::mask());
    std::printf("Mode  mask: 0x%02X\n", Mode::mask());
    std::printf("ModeActive shifted: 0x%02X\n", ModeActive::shifted_value);

    return 0;
}
