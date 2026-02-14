// SPDX-License-Identifier: MIT
/// @file
/// @brief Negative compile test: Monitor with zero up buffers must fail.
/// @details Triggers static_assert "At least one up buffer required".

#include <umirtm/rtm.hh>

/// @brief Compile-fail test entrypoint.
int main() {
    using BadMonitor = rt::Monitor<0, 1, 64, 64>;
    BadMonitor::init("FAIL");
    return 0;
}
