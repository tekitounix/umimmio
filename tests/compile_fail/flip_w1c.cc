// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: flip() on W1C field must be rejected.
/// @author Shota Moriguchi @tekitounix
/// @details NotW1C constraint prevents flip on W1C fields.

#include <umimmio/ops.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<> {};
struct SR : Register<TestDevice, 0x00, bits32, RW, 0, /*W1cMask=*/0x01> {};
struct OVR : Field<SR, 0, 1, W1C> {};

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
    hw.flip(OVR{}); // ERROR: NotW1C constraint
    return 0;
}
