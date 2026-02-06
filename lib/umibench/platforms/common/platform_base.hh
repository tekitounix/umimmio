// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief Shared platform composition helpers.

namespace umi::bench::target::common {

/// @brief Generic platform composed from timer and output backends.
/// @tparam TimerT Timer backend type.
/// @tparam OutputT Output backend type.
template <typename TimerT, typename OutputT>
struct PlatformBase {
    /// @brief Timer backend alias.
    using Timer = TimerT;
    /// @brief Output backend alias.
    using Output = OutputT;

    /// @brief Initialize timer and output backends.
    static void init() {
        Timer::enable();
        Output::init();
    }

    /// @brief Default halt hook (no-op).
    static void halt() {}
};

/// @brief RAII helper that initializes a platform during static initialization.
/// @tparam PlatformT Platform type.
template <typename PlatformT>
struct PlatformAutoInit {
    /// @brief Construct and initialize the platform.
    PlatformAutoInit() { PlatformT::init(); }
};

} // namespace umi::bench::target::common
