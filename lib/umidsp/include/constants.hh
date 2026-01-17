// SPDX-License-Identifier: MIT
// UMI-OS - DSP Constants

#pragma once

#include <numbers>

namespace umi::dsp {

inline constexpr float Pi = std::numbers::pi_v<float>;
inline constexpr float TwoPi = Pi * 2.0f;

} // namespace umi::dsp
