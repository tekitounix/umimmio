#pragma once

#include <cmath>
#include "dsp/math.hh"

namespace mo::dsp {
inline namespace filter {

class Skf {
 public:
  Skf(float fs) {
    setSampleRate(fs);
    setCutoff(1.0f);
    processCoef();
  }

  Skf& reset() {
    s1 = 0;
    s2 = 0;
    return *this;
  }

  Skf& setSampleRate(float fs) {
    dt = 1.0f / fs;
    return *this;
  }

  Skf& setNormalizedCutoff(float wc) {  // {0 <= wc < 0.5}
    this->wc = wc;
    return *this;
  }

  Skf& setCutoff(float fc) {
    setNormalizedCutoff(fc * dt);
    return *this;
  }

  Skf& setResonance(float q) {
    this->q = q;
    return *this;
  }

  static std::pair<float, float> calcCutoff(float r1, float r2, float c1, float c2) {
    const auto fc = 1.0f / (std::sqrt(r1 * r2 * c1 * c2));
    const auto q = std::sqrt(r1 * r2 * c1 * c2) / (c2 * (r1 + r2));
    return {fc, q};
  }

  Skf& setRC(float r1, float r2, float c1, float c2) {
    const auto [fc, q] = calcCutoff(r1, r2, c1, c2);
    setCutoff(fc);
    setResonance(q);
    return *this;
  }

  Skf& processCoef() {
    const auto g = tan(pi<float> * wc);
    const auto g_plus1 = 1.0f + g;
    k = 2.0f * q;
    a0 = 1.0f / ((g_plus1 * g_plus1) - (g * k));
    a1 = k * a0;
    a2 = g_plus1 * a0;
    a3 = g * a2;
    a4 = 1.0f / g_plus1;  // A: 155 cycles
    a5 = g * a4;
    return *this;
  }

  float processOut(float x) {
    // A: 74 cycles / Total: 232 cycles
    const auto v1 = a1 * s2 + a2 * s1 + a3 * x;
    const auto v2 = a4 * s2 + a5 * v1;
    s1 = 2.f * (v1 - k * v2) - s1;
    s2 = 2.f * v2 - s2;
    return v2;
  }

  float process(float x) {
    const auto g = tan(pi<float> * wc);
    const auto g_plus1 = 1.0f + g;
    const auto k = 2.0f * q;
    const auto a0 = 1.0f / ((g_plus1 * g_plus1) - (g * k));
    const auto a1 = k * a0;
    const auto a2 = g_plus1 * a0;
    const auto a3 = g * a2;
    const auto a4 = g * a0;  // B: 145 cycles
    const auto a5 = g * a4;

    // B: 79 cycles / Total: 224 cycles
    const auto v1 = a1 * s2 + a2 * s1 + a3 * x;
    const auto v2 = a2 * s2 + a4 * s1 + a5 * x;
    s1 = 2.f * (v1 - k * v2) - s1;
    s2 = 2.f * v2 - s2;
    return v2;
  }

 private:
  float dt = 0, wc = 0;
  float s1 = 0, s2 = 0;
  float g = 0, q = 0, k = 0;
  float a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0;
};
}  // namespace filter
}  // namespace mo::dsp
