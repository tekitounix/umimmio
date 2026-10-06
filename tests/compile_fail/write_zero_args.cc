// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Compile-fail guard: write() with zero arguments must not compile.
/// @author Shota Moriguchi @tekitounix

#include <umimmio/mmio.hh>

struct Dev : umi::mmio::Device<> {};
struct Reg : umi::mmio::Register<Dev, 0x00, 32> {};

struct Transport : private umi::mmio::RegOps<> {
    using umi::mmio::RegOps<>::write;
    using TransportTag = umi::mmio::Direct;

    template <typename R>
    auto reg_read(R) const noexcept -> typename R::RegValueType {
        return {};
    }
    template <typename R>
    void reg_write(R, typename R::RegValueType) const noexcept {}
};

int main() {
    Transport hw;
    hw.write(); // ERROR: static_assert "write() requires at least one value"
}
