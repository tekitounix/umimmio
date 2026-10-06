// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: RegionValue cannot be compared with raw integer.
/// @author Shota Moriguchi @tekitounix
/// @details RegionValue returned from read(Field) and RegionValue::get() has
///          no operator== with integer types. Use .bits() for explicit escape,
///          or compare with named Value<F, V> types.

#include <umimmio/region.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<> {};
struct Reg : Register<TestDevice, 0x00, bits32, RW, 0> {};
struct Mode : Field<Reg, 4, 2> {};

} // namespace

int main() {
    RegionValue<Mode> val{1};
    bool ok = (val == 1); // ERROR: no operator== with integer
    (void)ok;
    return 0;
}
