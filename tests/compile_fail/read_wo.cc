// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: reading from a write-only register must fail.
/// @author Shota Moriguchi @tekitounix
/// @details Triggers requires clause failure: Readable<Reg> not satisfied.

#include <umimmio/ops.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<> {};
struct DataReg : Register<TestDevice, 0x08, bits32, WO, 0> {};

/// @brief Mock transport for compile-time test.
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

/// @brief Compile-fail test entrypoint.
int main() {
    MockTransport hw;
    [[maybe_unused]] auto val = hw.read(DataReg{}); // ERROR: Cannot read from write-only register
    return 0;
}
