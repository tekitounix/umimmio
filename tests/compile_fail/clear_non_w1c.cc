// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: clear() on non-W1C field must fail.
/// @author Shota Moriguchi @tekitounix
/// @details clear() requires IsW1C — normal fields do not satisfy this.

#include <umimmio/ops.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<> {};
struct CReg : Register<TestDevice, 0x00, bits32, RW, 0> {};
struct Enable : Field<CReg, 0, 1> {};

struct MockTransport : private RegOps<> {
  public:
    using RegOps<>::clear;
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
    hw.clear(Enable{}); // ERROR: IsW1C not satisfied (normal field)
    return 0;
}
