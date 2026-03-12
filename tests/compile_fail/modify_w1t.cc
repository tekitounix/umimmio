// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: modify() on W1T field must be rejected.
/// @author Shota Moriguchi @tekitounix
/// @details W1T field does not satisfy NormalWrite — RMW is unsafe for W1T semantics.

#include <umimmio/ops.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<> {};
struct SR : Register<TestDevice, 0x00, bits32, WO> {};
struct FLAG : Field<SR, 0, 1, W1T> {};

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
    hw.modify(FLAG::Toggle{}); // ERROR: W1T field not ModifiableValue (NormalWrite)
    return 0;
}
