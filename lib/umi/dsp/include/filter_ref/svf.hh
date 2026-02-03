#pragma once

#include "dsp/math.hh"

namespace mo::dsp {
inline namespace filter {
template <std::floating_point F>
class Svf {
  // From https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
  // Same Result
  // https://github.com/juce-framework/JUCE/blob/master/modules/juce_dsp/processors/juce_StateVariableFilter.h
 public:
  Svf(F sample_rate) {
    setSampleRate(sample_rate);
  }

  Svf& setSampleRate(F sample_rate) {
    dt = F(1) / sample_rate;
    return *this;
  }

  Svf& setNormalizedCutoff(float wc) noexcept {  // {0 <= wc < 0.5}
    this->wc = mo::clamp(wc, 0.002f, 0.5f - 0.04f);
    // this->wc = wc;
    return *this;
  }

  Svf& setCutoff(float cutoff) {
    setNormalizedCutoff(cutoff * dt);
    // constexpr auto pi = std::numbers::pi_v<F>;
    // g = mo::tan(pi * cutoff * dt);
    // a1 = F(1.0) / (F(1.0) + g * (g + k));
    // a2 = g * a1;
    // a3 = g * a2;
    // m0 = 0;
    // m1 = 0;
    // m2 = 1;

    // c1 = static_cast<F>(2 * (g + k));
    // c2 = static_cast<F>(g / (1 + g * (g + k)));
    return *this;
  }

  Svf& setResonance(F resonance)  // k = 1...0
  {
    // k = static_cast<F>(1.0 / res);
    // k = static_cast<F>(2.f - 2.f * resonance);
    // k = F(1.0) - F(0.96) * resonance;
    k = F(2) - F(2) * resonance;
    // k = static_cast<F>(2 * cos(pow(res, 0.1) * std::numbers::pi *
    // 0.5)); k = static_cast<F>(min(k, min(2.0, 2 / g - g * 0.5)));
    // qnorm = static_cast<F>(sqrt(fabs(q) / 2.0 + 0.001));
    return *this;
  }

  Svf& calcCoef() noexcept {
    constexpr auto pi = std::numbers::pi_v<F>;
    g = mo::tan(pi * wc);
    a1 = F(1) / (F(1) + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
    return *this;
  }

  F process(F x) {
    const auto v3 = x - s2;
    const auto v1 = a1 * s1 + a2 * v3;
    const auto v2 = s2 + a2 * s1 + a3 * v3;
    s1 = F(2) * v1 - s1;
    s2 = F(2) * v2 - s2;
    // return m0 * v0 + m1 * v1 + m2 * v2;
    // float low = v2;
    // float band = v1;
    // float high = v0 - k * v1 - v2;
    // float notch = low + high = v0 - k * v1;
    // float peak = low - high = v0 - k*v1-2*v2;
    // float all = low + high - k* band = v0 - 2 * k * v1;

    // const auto v1 = v1z + g * (v0 + v0z - 2 * (g + k) * v1z - 2 * v2z) / (1 +
    // g * (g + k)); const auto v2 = v2z + g * (v1 + v1z); v[1] += c2 * (v0 +
    // v[0] - c1 * v[1] - 2 * v[2]); v[2] += g * (v[1] + v[1]); v[0] = v0; v[1]
    // = v1; v[2] = v2;
    return v2;
  }

 private:
  F dt = 0;
  F wc = 0;
  F s1 = 0, s2 = 0;
  F g = 0, k = 0;
  F a1 = 0, a2 = 0, a3 = 0;
  F c1 = 0, c2 = 0;
  F v1 = 0, v2 = 0;
  F v[3] = {0, 0, 0};
  // F m0 = 0, m1 = 0, m2 = 0;
};
}  // namespace filter
}  // namespace mo::dsp
