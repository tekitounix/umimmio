#pragma once

#include "dsp/math.hh"

namespace mo::dsp {
inline namespace filter {

class OnePole {
 public:
  OnePole(float fs) {
    setSampleRate(fs);
    setCutoff(1.0f);
    processCoef();
  }

  enum class Type {
    LPF,
    HPF,
  };

  OnePole& setSampleRate(float fs) {
    dt = div(fs);
    return *this;
  }

  OnePole& setCutoff(float fc) {
    this->fc = fc;
    return *this;
  }

  OnePole& setCoef(float b0) {
    this->b0 = b0;
    return *this;
  }

  OnePole& processCoef(float rc) {
    b0 = dt / (rc + dt);
    return *this;
  }

  OnePole& processCoef() {
    b0 = calcCoef(fc, dt);
    return *this;
  }

  template <Type type>
  float processOut(float x) {
    y += (x - y) * b0;
    if constexpr (type == Type::LPF) {
      return y;
    } else {
      return x - y;
    }
  }

  template <Type type>
  float process(float x) {
    const auto b0 = calcCoef(fc, dt);
    y += (x - y) * b0;
    if constexpr (type == Type::LPF) {
      return y;
    } else {
      return x - y;
    }
  }

 private:
  static float calcCoef(float fc, float dt) {
    const auto rc = div(tau<float> * fc);
    return dt / (rc + dt);
  }

  float dt = 0, fc = 0, b0 = 0, y = 0;
};

}  // namespace filter
}  // namespace mo::dsp
