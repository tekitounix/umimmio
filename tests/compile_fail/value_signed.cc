// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: signed value must be rejected.
/// @author Shota Moriguchi @tekitounix
/// @details value() requires std::unsigned_integral.

#include <umimmio/register.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<RW, DirectTransportTag> {};
struct CtrlReg : Register<TestDevice, 0x00, bits32, RW, 0> {};
struct Prescaler : Field<CtrlReg, 8, 8, Numeric> {};

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
    hw.write(Prescaler::value(-1)); // ERROR: requires unsigned_integral
    return 0;
}
