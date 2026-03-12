// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: bits() on non-Numeric field must fail.
/// @author Shota Moriguchi @tekitounix
/// @details Fields without Numeric trait should use named Value comparisons
///          or is(). The bits() escape hatch requires Numeric opt-in,
///          symmetric with the write-side value() gate.

#include <umimmio/region.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<> {};
struct Reg : Register<TestDevice, 0x00, bits32, RW, 0> {};
struct Mode : Field<Reg, 4, 2> {};

} // namespace

int main() {
    RegionValue<Mode> val{1};
    auto raw = val.bits(); // ERROR: bits() requires NumericAccessible
    (void)raw;
    return 0;
}
