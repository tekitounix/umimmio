// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: IndexedArray::Entry out-of-range must be rejected.
/// @author Shota Moriguchi @tekitounix
/// @details IndexedArray::Entry<N> static_assert: "IndexedArray::Entry index out of range".

#include <umimmio/region.hh>

using namespace umi::mmio;

struct TestDevice : Device<> {
    static constexpr Addr base_address = 0x1000;
};

using Arr = IndexedArray<TestDevice, 0x100, 4>;

int main() {
    (void)Arr::Entry<4>{}; // ERROR: index 4 >= Count(4)
    return 0;
}
