// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: modify() on W1C field must be rejected.
/// @author Shota Moriguchi @tekitounix
/// @details W1C field does not satisfy ModifiableValue concept.

#include <umimmio/ops.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<> {};
struct SR : Register<TestDevice, 0x00, bits32, RW, 0, /*W1cMask=*/0x01> {};
struct OVR : Field<SR, 0, 1, W1C> {};

struct MockTransport : private RegOps<> {
  public:
    using RegOps<>::modify;
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
    hw.modify(OVR::Clear{}); // ERROR: W1C field not ModifiableValue
    return 0;
}
