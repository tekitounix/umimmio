// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: multi-field write across different registers must fail.
/// @author Shota Moriguchi @tekitounix
/// @details All values in a multi-field write() must target the same register.

#include <umimmio/ops.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<RW, DirectTransportTag> {};
struct RegA : Register<TestDevice, 0x00, bits32, RW, 0> {};
struct RegB : Register<TestDevice, 0x04, bits32, RW, 0> {};
struct FieldA : Field<RegA, 0, 1> {};
struct FieldB : Field<RegB, 0, 1> {};

struct MockTransport : private RegOps<> {
  public:
    using RegOps<>::write;
    using TransportTag = DirectTransportTag;

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
    hw.write(FieldA::Set{}, FieldB::Set{}); // ERROR: fields from different registers
    return 0;
}
