// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: using I2C transport on Direct-only device must fail.
/// @author Shota Moriguchi @tekitounix

#include <umimmio/transport/i2c.hh>

namespace {

using namespace umi::mmio;

// Device only allows Direct transport (default)
struct DirectOnlyDevice : Device<> {};
struct DirectOnlyReg : Register<DirectOnlyDevice, 0x00, bits32, RW, 0> {};

struct FakeI2C {
    struct Result {
        explicit operator bool() const { return success; }
        bool success = true;
    };
    Result write(std::uint8_t, std::span<const std::uint8_t>) const { return {true}; }
    Result write_read(std::uint8_t, std::span<const std::uint8_t>, std::span<std::uint8_t>) const { return {true}; }
};

} // namespace

int main() {
    FakeI2C i2c;
    I2cTransport<FakeI2C> transport(i2c, 0x50);
    transport.write(DirectOnlyReg::value(42U)); // ERROR: I2c transport not allowed for Direct-only device
    return 0;
}
