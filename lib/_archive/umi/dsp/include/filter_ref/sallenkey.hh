#pragma once

#include "util/frequency.hh"
#include "util/math.hh"

namespace mo::dsp::filter::zdf {
class SallenKeyLpf {
  // From https://cytomic.com/files/dsp/SkfLinearTrapOptimised2.pdf
 public:
  SallenKeyLpf(frequency::hertzf sample_rate) { setSampleRate(sample_rate); }

  SallenKeyLpf& setSampleRate(frequency::hertzf sample_rate) {
    dt = 1.f / sample_rate.count();
    return *this;
  }

  SallenKeyLpf& setCutoff(frequency::hertzf cutoff) {
    g = tan(PI * cutoff.count() * dt);
    // a0 = 1.f / (1.f + g * (2.f - g * k));
    const auto g_plus1 = 1.f + g;
    a0 = 1.f / ((g_plus1 * g_plus1) - (g * k));
    a1 = k * a0;
    a2 = g_plus1 * a0;
    a3 = g * a2;
    a4 = g * a0;
    a5 = g * a4;
    return *this;
  }

  SallenKeyLpf& setResonance(float res)  // k = 0...2
  {
    k = 1.9f * res;
    return *this;
  }

  auto process(float v0) -> float {
    float v1 = a1 * ic2eq + a2 * ic1eq + a3 * v0;
    float v2 = a2 * ic2eq + a4 * ic1eq + a5 * v0;
    ic1eq = 2.f * (v1 - k * v2) - ic1eq;
    ic2eq = 2.f * v2 - ic2eq;
    return v2;
  }

 private:
  float dt = 0, b0 = 0, y = 0;
  float ic1eq = 0, ic2eq = 0;
  float g = 0, k = 0;
  float a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0;
};
}  // namespace mo::dsp::filter::zdf
