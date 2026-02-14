// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief WebAssembly platform binding for umibench.

#include "../../host/chrono.hh"
#include "../../host/stdout.hh"
#include "../../common/platform_base.hh"

namespace umi::bench::target {

/// @brief WebAssembly platform configuration.
struct Platform : common::PlatformBase<ChronoTimer, StdoutOutput> {

    /// @brief Platform name shown in reports.
    /// @return `"wasm"`.
    static constexpr const char* target_name() { return "wasm"; }
    /// @brief Timer unit shown in reports.
    /// @return `"ns"`.
    static constexpr const char* timer_unit() { return "ns"; }
};

namespace detail {
/// @brief Ensure platform initialization runs during static initialization.
inline common::PlatformAutoInit<Platform> platform_auto_init;
} // namespace detail

} // namespace umi::bench::target

namespace umi::bench {
/// @brief Convenience alias to the selected target platform type.
using Platform = target::Platform;
} // namespace umi::bench
