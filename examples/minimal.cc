// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Minimal Device/Register/Field definition example (hierarchical style).
///
/// The nested struct style Device > Register > Field > Value mirrors the
/// physical hardware structure: registers belong to a device, fields belong
/// to a register, and named values belong to a field.
///
/// @author Shota Moriguchi @tekitounix

#include <cstdio>

#include <umimmio/region.hh>

using namespace umi::mmio;

// Hierarchical register map — Device contains Register, Register contains Field.
struct MyDevice : Device<> {

    /// 8-bit status register at offset 0x00 (read-only)
    struct SR : Register<MyDevice, 0x00, bits8, RO> {
        struct READY : Field<SR, 0, 1> {};  // bit 0
        struct ERROR : Field<SR, 1, 1> {};  // bit 1
        struct MODE : Field<SR, 4, 2> {     // bits 4-5
            using Idle   = Value<MODE, 0>;
            using Active = Value<MODE, 1>;
        };
    };
};

int main() {
    // Static properties — all evaluated at compile time.
    // The type path MyDevice::SR::MODE::Active reads like the datasheet.
    static_assert(MyDevice::SR::address == 0x00);
    static_assert(MyDevice::SR::READY::mask() == 0x01);
    static_assert(MyDevice::SR::MODE::mask() == 0x30);
    static_assert(MyDevice::SR::MODE::Active::value == 1);

    std::printf("SR address:     0x%08lX\n", static_cast<unsigned long>(MyDevice::SR::address));
    std::printf("READY mask:     0x%02X\n", MyDevice::SR::READY::mask());
    std::printf("MODE  mask:     0x%02X\n", MyDevice::SR::MODE::mask());
    std::printf("Active shifted: 0x%02X\n", MyDevice::SR::MODE::Active::shifted_value);

    return 0;
}
