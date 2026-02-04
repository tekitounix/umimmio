// SPDX-License-Identifier: MIT
#pragma once

#include "bench/core/runner.hh"
#include "bench/output/stdout.hh"
#include "bench/timer/chrono.hh"

namespace umi::bench {

/// Host platform configuration
struct Host {
    using Timer = ChronoTimer;
    using Output = StdoutOutput;

    static void init() { Output::init(); }

    static void halt() {}
};

} // namespace umi::bench
