// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: modify() on write-only register must fail.
/// @author Shota Moriguchi @tekitounix
/// @details modify() requires read for RMW — WO registers cannot be read.

#include <umimmio/ops.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<RW, DirectTransportTag> {};
struct WOReg : Register<TestDevice, 0x00, bits32, WO, 0> {};
struct WOField : Field<WOReg, 0, 8, Numeric> {};

struct MockTransport : private RegOps<> {
  public:
    using RegOps<>::modify;
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
    hw.modify(WOField::value(static_cast<uint8_t>(1))); // ERROR: cannot read WO register for RMW
    return 0;
}
