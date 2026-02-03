#pragma once

#include <algorithm>

namespace mo::dsp {
inline namespace filter {

class Halfband {
 public:
  float process(float x)  // 7-tap, 82 cycles
  {
    z[6] = z[5];
    z[5] = z[4];
    z[4] = z[3];
    z[3] = z[2];
    z[2] = z[1];
    z[1] = z[0];
    z[0] = x;
    auto y = (z[0] + z[6]) * g[0];
    y += (z[2] + z[4]) * g[1];
    y += z[3] * g[2];
    return y;
  }

  Halfband& reset() {
    std::ranges::fill(z, 0.0f);
    return *this;
  }

 private:
  float g[3] = {-0.114779080944552331f, 0.344337242833656965f, 0.540883676221790677f};
  float z[7] = {0.0f};
};

}  // namespace filter
}  // namespace mo::dsp
