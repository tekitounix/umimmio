#pragma once

#include <cstdint>
#include <array>
#include <vector>

namespace mo::dsp {
inline namespace filter {
class Biquad {
 private:
  static constexpr unsigned NUM_STAGES = 4;
  std::array<float, NUM_STAGES> b0, b1, b2, a1, a2;
  std::array<float, NUM_STAGES> z1, z2;

 public:
  Biquad(const std::array<int32_t, 5 * NUM_STAGES>& coeffs) {
    setCoefficients(coeffs);
    reset();
  }

  void setCoefficients(const std::array<int32_t, 5 * NUM_STAGES>& coeffs) {
    for (unsigned stage = 0; stage < NUM_STAGES; ++stage) {
      b0[stage] = coeffs[stage * 5 + 0] / float(1 << 29);
      b1[stage] = coeffs[stage * 5 + 1] / float(1 << 30);
      b2[stage] = coeffs[stage * 5 + 2] / float(1 << 30);
      a1[stage] = coeffs[stage * 5 + 3] / float(1 << 30);
      a2[stage] = coeffs[stage * 5 + 4] / float(1 << 30);
    }
  }

  void reset() {
    z1.fill(0.0f);
    z2.fill(0.0f);
  }

  float process(float input) {
    float output = input;

    for (unsigned stage = 0; stage < NUM_STAGES; ++stage) {
      float v = output * b0[stage] + z1[stage];
      output = v + z2[stage];
      z2[stage] = b2[stage] * v - a2[stage] * output;
      z1[stage] = b1[stage] * v - a1[stage] * output;
    }

    return output;
  }
};
}  // namespace filter
}  // namespace mo::dsp
