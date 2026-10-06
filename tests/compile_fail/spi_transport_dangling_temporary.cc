// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: returning SpiTransport bound to a local
///        SPI driver must fail under Clang's lifetimebound diagnostic.
///        See the SpiTransport constructor comment in
///        include/umimmio/transport/spi.hh.
///        Note: GCC 14.2 silently ignores `[[clang::lifetimebound]]`, so this
///        test is meaningful only on Clang. Host build uses Clang.
/// @author Shota Moriguchi @tekitounix

#include <cstdint>
#include <span>

#include <umimmio/transport/spi.hh>

namespace {

using namespace umi::mmio;

struct FakeSpi {
    struct Result {
        explicit operator bool() const { return success; }
        bool success = true;
    };
    Result transfer(std::span<const std::uint8_t>, std::span<std::uint8_t>) const { return {true}; }
};

SpiTransport<FakeSpi> make_dangling() {
    FakeSpi local_driver;
    // `dev` parameter is `[[clang::lifetimebound]]`, so returning a transport
    // that captures a reference to `local_driver` must trigger -Wdangling
    // diagnostic under Clang.
    return SpiTransport<FakeSpi>{local_driver};
}

} // namespace

int main() {
    auto t = make_dangling();
    (void)t;
    return 0;
}
