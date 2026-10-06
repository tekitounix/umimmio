// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: flip() on read-only field must fail.
/// @author Shota Moriguchi @tekitounix
/// @details flip() requires ReadWritable — RO fields do not satisfy this.

#include <umimmio/ops.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<> {};
struct SReg : Register<TestDevice, 0x00, bits32, RO, 0> {};
struct Flag : Field<SReg, 0, 1> {};

struct MockTransport : private RegOps<> {
  public:
    using RegOps<>::flip;
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
    hw.flip(Flag{}); // ERROR: ReadWritable not satisfied (RO field)
    return 0;
}
