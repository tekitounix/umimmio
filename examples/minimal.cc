// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Minimal Region/Field definition example.

#include <cstdio>

#include <umimmio/register.hh>

// Define a simple 8-bit register at address 0x4000'0000
using StatusReg = umi::mmio::Region<0x4000'0000, std::uint8_t>;

// Define bit fields within the register
using Ready = umi::mmio::Field<StatusReg, 0, 1>; // bit 0
using Error = umi::mmio::Field<StatusReg, 1, 1>; // bit 1
using Mode = umi::mmio::Field<StatusReg, 4, 2>;  // bits 4-5

// Define named values for the Mode field
using ModeIdle = umi::mmio::Value<Mode, 0>;
using ModeActive = umi::mmio::Value<Mode, 1>;

int main() {
    // Static properties — all evaluated at compile time
    static_assert(StatusReg::address == 0x4000'0000);
    static_assert(Ready::mask == 0x01);
    static_assert(Mode::mask == 0x30);
    static_assert(ModeActive::field_value == 1);

    std::printf("StatusReg address: 0x%08X\n", StatusReg::address);
    std::printf("Ready mask: 0x%02X\n", Ready::mask);
    std::printf("Mode  mask: 0x%02X\n", Mode::mask);
    std::printf("ModeActive shifted: 0x%02X\n", ModeActive::shifted_value);

    return 0;
}
