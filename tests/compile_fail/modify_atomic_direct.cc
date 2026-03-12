// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief Negative compile test: modify() on AtomicDirectTransport must be rejected.
/// @author Shota Moriguchi @tekitounix
/// @details AtomicDirectTransport has no reg_read(), so Readable concept is false.
///          modify() requires read-modify-write and is therefore a compile error.

#include <umimmio/transport/atomic_direct.hh>

namespace {

using namespace umi::mmio;

struct TestDevice : Device<> {};
struct ConfigReg : Register<TestDevice, 0x04, bits32, RW, 0> {};
struct Enable : Field<ConfigReg, 0, 1> {};

} // namespace

int main() {
    const AtomicDirectTransport<0x2000> atomic_set;
    atomic_set.modify(Enable::Set{}); // ERROR: AtomicDirectTransport has no reg_read
    return 0;
}
