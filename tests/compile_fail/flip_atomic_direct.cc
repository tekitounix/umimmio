// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: flip() on AtomicDirectTransport must be rejected.
/// @author Shota Moriguchi @tekitounix
/// @details AtomicDirectTransport has no reg_read(), so flip() (read-modify-write)
///          is rejected at compile time.

#include <umimmio/transport/atomic_direct.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<> {};
struct ConfigReg : Register<TestDevice, 0x04, bits32, RW, 0> {};
struct Enable : Field<ConfigReg, 0, 1> {};

} // namespace

int main() {
    const AtomicDirectTransport<0x2000> atomic_set;
    atomic_set.flip(Enable{}); // ERROR: AtomicDirectTransport has no reg_read
    return 0;
}
