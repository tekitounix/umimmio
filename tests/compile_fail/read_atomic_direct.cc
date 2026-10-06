// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: read() on AtomicDirectTransport must be rejected.
/// @author Shota Moriguchi @tekitounix
/// @details AtomicDirectTransport has no reg_read(), so Readable concept is false.
///          read() requires reg_read() and is therefore a compile error.

#include <umimmio/transport/atomic_direct.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<> {};
struct ConfigReg : Register<TestDevice, 0x04, bits32, RW, 0> {};

} // namespace

int main() {
    const AtomicDirectTransport<0x2000> atomic_set;
    (void)atomic_set.read(ConfigReg{}); // ERROR: AtomicDirectTransport has no reg_read
    return 0;
}
