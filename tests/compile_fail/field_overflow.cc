// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: field exceeding register width must be rejected.
/// @author Shota Moriguchi @tekitounix
/// @details BitRegion static_assert: "Field bit range exceeds register width".

#include <umimmio/register.hh>

using namespace umi::mmio;

struct TestDevice : Device<RW> {};
struct TestReg : Register<TestDevice, 0x00, bits32> {};
struct BadField : Field<TestReg, 30, 4> {}; // bits 30-33 → overflow

int main() {
    (void)BadField::mask(); // ERROR: BitRegion static_assert
    return 0;
}
