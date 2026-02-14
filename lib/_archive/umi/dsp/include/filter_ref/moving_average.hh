#pragma once

#include <vector>
#include "dsp/math.hh"

namespace mo::dsp {
inline namespace filter {

class MovingAverage {
 public:
  MovingAverage(size_t size = 2) { setWindowSize(size); }

  MovingAverage& setWindowSize(size_t new_size) noexcept {
    window_size = std::max(new_size, size_t(1));
    buffer.clear();
    buffer.reserve(window_size);
    sum = float(0);
    return *this;
  }

  float process(float x) noexcept {
    if (buffer.size() < window_size) {
      buffer.push_back(x);
      sum += x;
    } else {
      sum -= buffer.front();
      buffer.erase(buffer.begin());
      buffer.push_back(x);
      sum += x;
    }
    return sum / static_cast<float>(buffer.size());
  }

  //  private:
 protected:
  size_t window_size = 1;
  std::vector<float> buffer;
  float sum = float(0);
  float dt = float(0);
};

}  // namespace filter
}  // namespace mo::dsp
