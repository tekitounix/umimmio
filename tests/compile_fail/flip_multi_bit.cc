// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: flip() on multi-bit field must fail.
/// @author Shota Moriguchi @tekitounix

#include <umimmio/ops.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<> {};
struct TestReg : Register<TestDevice, 0x00, bits32, RW, 0> {};
struct MultiBitField : Field<TestReg, 0, 4> {}; // 4-bit field — not 1-bit

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
    hw.flip(MultiBitField{}); // ERROR: flip() requires bit_width == 1
    return 0;
}
