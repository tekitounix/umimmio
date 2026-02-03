#pragma once

#include "lpf.hh"
#include "skf.hh"

namespace mo::dsp {
inline namespace filter {

class Skf3 {
 public:
  Skf3(float fs)
    : lpf(fs), sk(fs) {
    setSampleRate(fs);
    setCutoff(1.0f, 1.0f);
    processCoef();
  }

  Skf3& reset() {
    lpf.reset();
    sk.reset();
    return *this;
  }

  Skf3& setSampleRate(float fs) {
    lpf.setSampleRate(fs);
    sk.setSampleRate(fs);
    return *this;
  }

  Skf3& setNormalizedCutoff(float wc1, float wc2) {  // {0 <= wc < 0.5}
    lpf.setNormalizedCutoff(wc1);
    sk.setNormalizedCutoff(wc2);
    return *this;
  }

  Skf3& setCutoff(float fc1, float fc2) {
    lpf.setCutoff(fc1);
    sk.setCutoff(fc2);
    return *this;
  }

  Skf3& setResonance(float q) {
    sk.setResonance(q);
    return *this;
  }

  Skf3& setRC(float r1, float r2, float r3, float c1, float c2, float c3) {
    lpf.setRC(r1, c1);
    sk.setRC(r2, r3, c2, c3);
    return *this;
  }

  Skf3& processCoef() {
    sk.processCoef();
    lpf.processCoef();
    return *this;
  }

  float processOut(float x) {
    return sk.processOut(lpf.processOut(x));
  }

  float process(float x) {
    return sk.process(lpf.process(x));
  }

 private:
  Lpf lpf;
  Skf sk;
};

}  // namespace filter
}  // namespace mo::dsp
