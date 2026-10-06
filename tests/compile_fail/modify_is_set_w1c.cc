// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: modify() on W1C field via IsSet alias must be rejected.
/// @author Shota Moriguchi @tekitounix
/// @details Mirrors `modify_w1c.cc`, but uses the new `IsSet` alias to prove
///          the W1C no-modify policy applies regardless of the polling-friendly
///          alias name. W1C does not satisfy `ModifiableValue`.

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
    hw.modify(OVR::IsSet{}); // ERROR: W1C field not ModifiableValue (same as OVR::Clear{})
    return 0;
}
