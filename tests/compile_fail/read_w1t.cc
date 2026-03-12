// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: read() on W1T field must be rejected.
/// @author Shota Moriguchi @tekitounix
/// @details W1T fields are write-only — reading is not permitted.

#include <umimmio/ops.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<> {};
struct SR : Register<TestDevice, 0x00, bits32, WO> {};
struct FLAG : Field<SR, 0, 1, W1T> {};

struct MockTransport : private RegOps<> {
  public:
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
    (void)hw.read(FLAG{}); // ERROR: W1T field is not Readable
    return 0;
}
