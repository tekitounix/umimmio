// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: returning I2cTransport bound to a local
///        I2C driver must fail under Clang's lifetimebound diagnostic.
///        See the I2cTransport constructor comment in
///        include/umimmio/transport/i2c.hh.
///        Note: GCC 14.2 silently ignores `[[clang::lifetimebound]]`, so this
///        test is meaningful only on Clang. Host build uses Clang.
/// @author Shota Moriguchi @tekitounix

#include <cstdint>
#include <span>

#include <umimmio/transport/i2c.hh>

namespace {

using namespace umi::mmio;

struct FakeI2C {
    struct Result {
        explicit operator bool() const { return success; }
        bool success = true;
    };
    Result write(std::uint8_t, std::span<const std::uint8_t>) const { return {true}; }
    Result write_read(std::uint8_t, std::span<const std::uint8_t>, std::span<std::uint8_t>) const { return {true}; }
};

I2cTransport<FakeI2C> make_dangling() {
    FakeI2C local_bus;
    // The `bus` parameter is `[[clang::lifetimebound]]`, so returning a
    // transport that captures a reference to `local_bus` must trigger
    // -Wdangling diagnostic under Clang.
    return I2cTransport<FakeI2C>{local_bus, 0x50};
}

} // namespace

int main() {
    auto t = make_dangling();
    (void)t;
    return 0;
}
