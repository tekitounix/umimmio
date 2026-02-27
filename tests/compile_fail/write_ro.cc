// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: writing to a read-only register must fail.
/// @author Shota Moriguchi @tekitounix
/// @details Triggers static_assert "Cannot write to read-only register".

#include <cstdint>

#include <umimmio/register.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<RW, DirectTransportTag> {};
struct StatusReg : Register<TestDevice, 0x00, bits32, RO, 0> {};

/// @brief Mock transport for compile-time test.
class MockTransport : private RegOps<MockTransport> {
    friend class RegOps<MockTransport>;

  public:
    using RegOps<MockTransport>::write;
    using RegOps<MockTransport>::read;
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
    hw.write(StatusReg::value(42u)); // ERROR: Cannot write to read-only register
    return 0;
}
