#pragma once

#include "dsp/math.hh"

namespace mo::dsp {
inline namespace filter {

class SlewLimiter {
 public:
  SlewLimiter(float fs /* [Hz] */,
              float slew_rate /* [V/s] */,
              float voltage_swing /* [V], Voltage Swing */)
    : slew(slew_rate / (fs * voltage_swing)) {}

  SlewLimiter& reset() {
    s = 0.0f;
    return *this;
  }

  float process(float x) {  // { -1.0 <= input <= +1.0 }
    if (const auto dx = x - s;
        slew < std::abs(dx)) {
      s += (0 < dx ? slew : -slew);
    } else {
      s = x;
    }
    return s;
  }

 private:
  float s = 0.0f;
  float slew;
};

}  // namespace filter
}  // namespace mo::dsp
