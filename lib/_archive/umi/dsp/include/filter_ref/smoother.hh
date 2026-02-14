#pragma once
// https://cytomic.com/files/dsp/DynamicSmoothing.pdf

#include <algorithm>
#include "dsp/math.hh"

namespace mo::dsp {

inline namespace filter {
class Smoother {
 public:
  Smoother(float fs) {
    setSampleRate(fs);
    setCutoff(2.0f);
    setSensitivity(0.5f);
    processCoef();
  }

  Smoother& reset() {
    s1 = 0.0f;
    s2 = 0.0f;
    return *this;
  }

  Smoother& setSampleRate(float fs) {
    dt = 1.f / fs;
    return *this;
  }
  Smoother& setSensitivity(float sense) {
    this->sense = sense;
    return *this;
  }
  Smoother& setCutoff(float fc) {
    this->fc = fc;
    return *this;
  }

  Smoother& processCoef() {
    g0 = calcCoef(fc, dt);
    return *this;
  }

  float processOut(float x) {
    const auto y1 = s1;
    const auto y2 = s2;
    const auto bandz = y1 - y2;
    const auto g = mo::min(g0 + sense * std::abs(bandz), 1.0f);
    s1 = y1 + g * (x - y1);
    s2 = y2 + g * (s1 - y2);
    return s2;
  }

  float process(float x) {
    const auto y1 = s1;
    const auto y2 = s2;
    const auto bandz = y1 - y2;
    const auto g0 = calcCoef(fc, dt);
    const auto g = mo::min(g0 + sense * std::abs(bandz), 1.0f);
    s1 = y1 + g * (x - y1);
    s2 = y2 + g * (s1 - y2);
    return s2;
  }

 private:
  float calcCoef(float fc, float dt) {
    const auto wc = fc * dt;
    // Third-order polynomial approximation of:
    // gc = tan (pi * wc)
    // g0 = 2 * gc / (1 + gc)
    return ((15.952062f * wc - 11.969296f) * wc + 5.9948827f) * wc;
  }

  float dt = 0.0f, fc = 0.0f, sense = 0.0f, g0 = 0.0f;
  float s1 = 0.0f, s2 = 0.0f;
};

}  // namespace filter
}  // namespace mo::dsp
