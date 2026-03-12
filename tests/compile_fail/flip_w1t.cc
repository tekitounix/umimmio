// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: flip() on W1T field must be rejected.
/// @author Shota Moriguchi @tekitounix
/// @details W1T field does not satisfy NormalWrite — flip() (RMW) causes double-toggle.

#include <umimmio/ops.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<> {};
struct SR : Register<TestDevice, 0x00, bits32, RW> {};
struct FLAG : Field<SR, 0, 1, W1T> {};

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
    hw.flip(FLAG{}); // ERROR: W1T field not NormalWrite
    return 0;
}
