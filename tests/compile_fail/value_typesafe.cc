// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: raw value() on safe (non-Numeric) field must fail.
/// @author Shota Moriguchi @tekitounix
/// @details Fields without mm::Numeric trait do not expose value().
///          Only named Value<> types or mm::raw<>() escape are allowed.

#include <cstdint>

#include <umimmio/register.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<RW, DirectTransportTag> {};
struct CtrlReg : Register<TestDevice, 0x00, bits32, RW, 0> {};

// Field without Numeric — safe by default, raw value() must be rejected
struct MODE : Field<CtrlReg, 4, 2> {};

/// @brief Mock transport for compile-time test.
class MockTransport : private RegOps<MockTransport> {
    friend class RegOps<MockTransport>;

  public:
    using RegOps<MockTransport>::write;
    using RegOps<MockTransport>::modify;
    using TransportTag = DirectTransportTag;

    template <typename Reg>
    auto reg_read(Reg) const noexcept -> typename Reg::RegValueType {
        return 0;
    }

    template <typename Reg>
    void reg_write(Reg, typename Reg::RegValueType) const noexcept {}
};

} // namespace

/// @brief Compile-fail test entrypoint.
int main() {
    MockTransport hw;
    hw.modify(MODE::value(1)); // ERROR: value() unavailable on non-Numeric field
    return 0;
}
