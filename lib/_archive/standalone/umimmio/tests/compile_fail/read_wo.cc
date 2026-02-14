// SPDX-License-Identifier: MIT
/// @file
/// @brief Negative compile test: reading from a write-only register must fail.
/// @details Triggers static_assert "Cannot read from write-only register".

#include <cstdint>
#include <umimmio/register.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<RW, DirectTransportTag> {};
struct DataReg : Register<TestDevice, 0x08, bits32, WO, 0> {};

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
    [[maybe_unused]] auto val = hw.read(DataReg{}); // ERROR: Cannot read from write-only register
    return 0;
}
