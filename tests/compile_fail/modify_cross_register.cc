// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: modify() with fields from different registers must fail.
/// @author Shota Moriguchi @tekitounix

#include <umimmio/ops.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<> {};
struct RegA : Register<TestDevice, 0x00, bits32, RW, 0> {};
struct RegB : Register<TestDevice, 0x04, bits32, RW, 0> {};
struct FieldA : Field<RegA, 0, 1> {};
struct FieldB : Field<RegB, 0, 1> {};

struct MockTransport : private RegOps<> {
  public:
    using RegOps<>::modify;
    using TransportTag = Direct;

    template <typename Reg>
    auto reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        return 0;
    }

    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType /*val*/) noexcept {}
};

} // namespace

int main() {
    MockTransport hw;
    hw.modify(FieldA::Set{}, FieldB::Set{}); // ERROR: fields from different registers
    return 0;
}
