#pragma once

#include <cmath>
#include <algorithm>
#include "dsp/math.hh"
#include "dsp/filter/halfband.hh"
#include "dsp/clip/adaa.hh"

class K35 {
 private:
  float yl1, yl2;  // 積分器の状態

  float dt;  // サンプルレート
  float wc;  // カットオフ周波数
  float q;   // レゾナンス値
  mo::Tanh1 tanh;

  float tanhXdX(float x) {
    float a = x * x;
    // IIRC I got this as Pade-approx for tanh(sqrt(x))/sqrt(x)
    return ((a + 105) * a + 945) / ((15 * a + 420) * a + 945);
  }

 public:
  K35(float sampleRate = 50000.0f)
    : dt(1.0f / (sampleRate * 2.0f)),
      yl1(0.0f),
      yl2(0.0f),
      wc(1000.0f),
      q(0.0f),
      tanh(sampleRate * 2.0f) {
  }
  void reset() {
    yl1 = yl2 = 0.0f;
  }

  float normFreq(float f) { return f * dt; }

  K35& setNormalizedCutoff(float wc) {
    this->wc = std::clamp(wc, 0.002f, 0.5f - 0.04f);
    return *this;
  }

  K35& setCutoff(float fc) {
    setNormalizedCutoff(fc * dt);
    return *this;
  }

  K35& setResonance(float q) {
    this->q = q;
    return *this;
  }

  // 単一サンプル処理
  float processImpl(float input) {
    input = input * 0.5f;  // 入力ゲインを下げるとレゾナンスが強くなる
    // BLTのための周波数ワーピング
    float f = mo::tan(M_PI * wc);

    // レゾナンスパラメーター
    float r = 3.0f * q;

    float t = tanhXdX(r * yl2);

    float f_plus_1 = f + 1.0f;
    float ff = f * f;
    float denom = ff * r * t + f_plus_1 * f_plus_1;
    // float y1 = (-f * r * t * yl2 + f_plus_1 * yl1 + f * f_plus_1 * input) / denom;
    float y2 = (ff * input + f * yl1 + f_plus_1 * yl2) / denom;
    float y1 = (f_plus_1 * y2 - yl2) / f;

    yl1 += 2.0f * f * (input - y1 - r * t * y2);
    yl2 += 2.0f * f * (y1 + r * t * y2 - y2);

    return y2;
  }

  float process(float x) {
    anti_alias[1].process(processImpl(anti_alias[0].process(x)));
    return anti_alias[1].process(processImpl(anti_alias[0].process(0)));
  }

  mo::dsp::filter::Halfband anti_alias[2];
};

// class K35Hpf {
//  private:
//   float yl1, yl2;  // 積分器の状態

//   float dt;        // サンプルレート
//   float wc;        // カットオフ周波数
//   float q;         // レゾナンス値
//   mo::Tanh1 tanh;  // NOTE: K35Hpf will need its own Tanh1 instance if K35 is also used

//   float tanhXdX(float x) {
//     float a = x * x;
//     // IIRC I got this as Pade-approx for tanh(sqrt(x))/sqrt(x)
//     return ((a + 105) * a + 945) / ((15 * a + 420) * a + 945);
//   }

//  public:
//   K35Hpf(float sampleRate = 50000.0f)
//     : dt(1.0f / (sampleRate * 2.0f)),
//       yl1(0.0f),
//       yl2(0.0f),
//       wc(1000.0f),
//       q(0.0f),
//       tanh(sampleRate * 2.0f) {
//   }
//   void reset() {
//     yl1 = yl2 = 0.0f;
//   }

//   float normFreq(float f) { return f * dt; }

//   K35Hpf& setNormalizedCutoff(float wc) {
//     this->wc = std::clamp(wc, 0.002f, 0.5f - 0.04f);
//     return *this;
//   }

//   K35Hpf& setCutoff(float fc) {
//     setNormalizedCutoff(fc * dt);
//     return *this;
//   }

//   K35Hpf& setResonance(float q) {
//     this->q = q;
//     return *this;
//   }

//   // 単一サンプル処理 (High-pass version)
//   float processImpl(float input) {
//     input = input * 0.25f;
//     // BLTのための周波数ワーピング
//     float f = mo::tan(M_PI * wc);

//     // レゾナンスパラメーター
//     float r = 3.0f * q;

//     float t = tanhXdX(r * yl2);

//     float f_plus_1 = f + 1.0f;
//     float ff = f * f;
//     float denom = ff * r * t + f_plus_1 * f_plus_1;
//     // float y1 = (-f * r * t * yl2 + f_plus_1 * yl1 + f * f_plus_1 * input) / denom;
//     float y2 = (ff * input + f * yl1 + f_plus_1 * yl2) / denom;
//     float y1 = (f_plus_1 * y2 - yl2) / f;

//     yl1 += 2.0f * f * (input - y1 - r * t * y2);
//     yl2 += 2.0f * f * (y1 + r * t * y2 - y2);

//     // High-pass output calculation
//     float y_hp = input - y1 - r * t * y2;

//     return y_hp * 2.0f;
//   }

//   float process(float x) {
//     anti_alias[1].process(processImpl(anti_alias[0].process(x)));
//     return anti_alias[1].process(processImpl(anti_alias[0].process(0)));
//   }

//   mo::dsp::filter::Halfband anti_alias[2];
// };