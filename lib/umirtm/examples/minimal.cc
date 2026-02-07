// SPDX-License-Identifier: MIT
/// @file
/// @brief Minimal Monitor init + write example.

#include <cstdio>
#include <umirtm/rtm.hh>

int main() {
    // Initialize the default monitor (rtm is an alias for rt::Monitor<>)
    rtm::init("EXAMPLE");

    // Write a string to up buffer 0
    auto written = rtm::write("Hello from RTM!\n");
    std::printf("Wrote %zu bytes to RTM buffer\n", written);

    // Log (fire-and-forget, ignores return value)
    rtm::log("This is a log message\n");

    // Check available data
    auto avail = rtm::get_available();
    std::printf("Available in buffer: %zu bytes\n", avail);

    return 0;
}
