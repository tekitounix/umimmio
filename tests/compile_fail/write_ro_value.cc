// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: write() with value on RO register must be rejected.
/// @author Shota Moriguchi @tekitounix
/// @details value() itself succeeds on RO registers (it only creates a value object).
///          The write() call is rejected because WritableValue concept is not satisfied.

#include <umimmio/ops.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<> {};
struct StatusReg : Register<TestDevice, 0x00, bits32, RO, 0> {};

struct MockTransport : private RegOps<> {
  public:
    using RegOps<>::write;
    using RegOps<>::read;
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
    hw.write(StatusReg::value(42U)); // ERROR: WritableValue not satisfied (RO register)
    return 0;
}
