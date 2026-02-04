// SPDX-License-Identifier: MIT
#pragma once

#include "bench/core/runner.hh"
#include "bench/output/uart.hh"
#include "bench/timer/dwt.hh"

namespace umi::bench {

/// STM32F4 platform configuration
struct Stm32f4 {
    using Timer = DwtTimer;
    using Output = UartOutput;

    static void init() {
        Timer::enable();
        Output::init();
    }

    static void halt() {
        // BKPT triggers debug halt - Renode can detect this
        asm volatile("bkpt #0");
        // Fallback: infinite loop if debugger continues
        while (true) {
            asm volatile("wfi");
        }
    }
};

} // namespace umi::bench
