#pragma once

#include <cmath>
#include "dsp/math.hh"

namespace mo::dsp {
inline namespace filter {

class MoogLadder {
 public:
  MoogLadder(float fs) {
    setSampleRate(fs);
    setCutoff(1.0f);
    processCoef();
  }

  float normFreq(float f) { return f * dt; }

  MoogLadder& reset() {
    return *this;
  }

  MoogLadder& setSampleRate(float fs) {
    dt = 1.0f / fs;
    return *this;
  }

  MoogLadder& setNormalizedCutoff(float wc) {  // {0 <= wc < 0.5}
    this->wc = clamp(wc, 0.002f, 0.5f - 0.04f);
    return *this;
  }

  MoogLadder& setCutoff(float fc) {
    setNormalizedCutoff(fc * dt);
    return *this;
  }

  MoogLadder& setResonance(float q) {
    this->q = q;
    k = q * 4.0f;
    return *this;
  }

  MoogLadder& processCoef() {
    const auto x = pi<float> * wc;
    // const auto x = tan(pi<float> * wc);
    g = 4.0f * x * VT * (1.0f - x) / (dt + x * dt);
    return *this;
  }

  float process(float x) {
    processCoef();

    constexpr auto div2vt = 1.0f / (2.0f * VT);
    const auto half_dt = 0.5f * dt;

    const auto dV0 = -g * (tanh((x + k * V[3]) * div2vt) + tV[0]);
    V[0] += (dV0 + dV[0]) * half_dt;
    dV[0] = dV0;
    tV[0] = tanh(V[0] * div2vt);

    const auto dV1 = g * (tV[0] - tV[1]);
    V[1] += (dV1 + dV[1]) * half_dt;
    dV[1] = dV1;
    tV[1] = tanh(V[1] * div2vt);

    const auto dV2 = g * (tV[1] - tV[2]);
    V[2] += (dV2 + dV[2]) * half_dt;
    dV[2] = dV2;
    tV[2] = tanh(V[2] * div2vt);

    const auto dV3 = g * (tV[2] - tV[3]);
    V[3] += (dV3 + dV[3]) * half_dt;
    dV[3] = dV3;
    tV[3] = tanh(V[3] * div2vt);

    return V[3];
  }

 private:
  float dt = 0, wc = 0;
  float g = 0, q = 0, k = 0;

  static constexpr float VT = 0.312f;
  float V[4];
  float dV[4];
  float tV[4];
};
}  // namespace filter
}  // namespace mo::dsp
