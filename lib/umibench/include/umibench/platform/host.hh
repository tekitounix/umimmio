// SPDX-License-Identifier: MIT
#pragma once

#include "umibench/core/runner.hh"
#include "umibench/output/stdout.hh"
#include "umibench/timer/chrono.hh"

namespace umi::bench {

/// Host platform configuration
struct Host {
    using Timer = ChronoTimer;
    using Output = StdoutOutput;

    static void init() { Output::init(); }

    static void halt() {}
};

} // namespace umi::bench
