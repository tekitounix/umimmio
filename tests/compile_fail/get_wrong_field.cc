// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: RegionValue::get() with field from different register must fail.
/// @author Shota Moriguchi @tekitounix

#include <umimmio/ops.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<> {};
struct RegA : Register<TestDevice, 0x00, bits32, RW, 0> {};
struct RegB : Register<TestDevice, 0x04, bits32, RW, 0> {};
struct FieldA : Field<RegA, 0, 8> {};
struct FieldB : Field<RegB, 0, 8> {};

} // namespace

int main() {
    auto rv = RegionValue<RegA>{0x1234};
    (void)rv.get(FieldB{}); // ERROR: FieldB belongs to RegB, not RegA
    return 0;
}
