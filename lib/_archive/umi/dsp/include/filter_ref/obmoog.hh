#pragma once

#include <cmath>
#include "dsp/math.hh"
#include "dsp/clip/adaa.hh"

// https://github.com/ddiakopoulos/MoogLadders/blob/master/src/OberheimVariationModel.h
// https://forum.audulus.com/uploads/default/original/2X/8/82ea1a4ef055962ff4a50bcdf5e21f7aa895bebd.pdf

namespace mo::dsp {
inline namespace filter {

class ObMoogLadder {
 public:
  ObMoogLadder(float fs) : tanh_clipper(fs), hard_clipper(fs) {
    setSampleRate(fs);
    setCutoff(1.0f);
    processCoef();
  }

  float normFreq(float f) { return f * dt; }

  ObMoogLadder& reset() {
    return *this;
  }

  ObMoogLadder& setSampleRate(float fs) {
    dt = 1.0f / fs;
    return *this;
  }

  ObMoogLadder& setNormalizedCutoff(float wc) {  // {0 <= wc < 0.5}
    this->wc = clamp(wc, 0.002f, 0.5f - 0.04f);
    return *this;
  }

  ObMoogLadder& setCutoff(float fc) {
    setNormalizedCutoff(fc * dt);
    return *this;
  }

  ObMoogLadder& setResonance(float q) {
    // k = 4.0f * q;
    k = 3.2f * q;
    return *this;
  }

  ObMoogLadder& processCoef() {
    const auto g = tan(pi<float> * wc);
    G = g / (1.0f + g);
    return *this;
  }

  float process(float x) {
    processCoef();
    const auto GG = G * G;
    const auto GGGG = GG * GG;
    const auto one_minus_G = 1.0f - G;
    auto S = GG * G * (s[0] * one_minus_G)
             + GG * (s[1] * one_minus_G)
             + G * (s[2] * one_minus_G)
             + (s[3] * one_minus_G);

    // S = clamp(S, -2.f, 2.f);
    S = hard_clipper.process(S * 0.5f) * 2.0f;
    // S = tanh_clipper.process(S);
    // S = clamp(S, -1.f, 1.f);
    // if (S > 1.f)
    //   S = tanh_clipper.process(S - 1.f) + 1.f;
    // else if (S < -2.f)
    //   S = tanh_clipper.process(S + 1.f) - 1.f;

    auto y = (x - k * S) / (1.0f + k * GGGG);

    const auto v1 = (y - s[0]) * G;
    const auto v2 = (s[0] - s[1]) * G;
    const auto v3 = (s[1] - s[2]) * G;
    const auto v4 = (s[2] - s[3]) * G;

    const auto lp1 = v1 + s[0];
    const auto lp2 = v2 + s[1];
    const auto lp3 = v3 + s[2];
    const auto lp4 = v4 + s[3];

    s[0] = v1 + lp1;
    s[1] = v2 + lp2;
    s[2] = v3 + lp3;
    s[3] = v4 + lp4;

    return lp4;
  }

 private:
  float dt = 0, wc = 0;
  float g = 0, G = 0, k = 0;
  float s[4];
  Tanh1 tanh_clipper;
  Hardclip hard_clipper;
};

// class VAOnePole {
//  public:
//   VAOnePole(float sr) : sampleRate(sr) {
//     Reset();
//   }

//   void Reset() {
//     alpha = 1.0f;
//     beta = 0.0f;
//     G4 = 1.0f;
//     delta = 0.0f;
//     epsilon = 0.0f;
//     a0 = 1.0f;
//     feedback = 0.0f;
//     z1 = 0.0f;
//   }

//   float Tick(float s) {
//     s = s * G4 + feedback + epsilon * GetFeedbackOutput();
//     float vn = (a0 * s - z1) * alpha;
//     float out = vn + z1;
//     z1 = vn + out;
//     return out;
//   }

//   void SetFeedback(float fb) { feedback = fb; }
//   float GetFeedbackOutput() { return beta * (z1 + feedback * delta); }
//   void SetAlpha(float a) { alpha = a; };
//   void SetBeta(float b) { beta = b; };

//  private:
//   float sampleRate;
//   float alpha;
//   float beta;
//   float G4;
//   float delta;
//   float epsilon;
//   float a0;
//   float feedback;
//   float z1;
// };

// class ObMoogLadder {
//  public:
//   ObMoogLadder(float fs) : LPF1(fs), LPF2(fs), LPF3(fs), LPF4(fs), tanh_clipper(fs) {
//     setSampleRate(fs);
//     setCutoff(1.0f);
//     processCoef();
//   }

//   float normFreq(float f) { return f * dt; }

//   ObMoogLadder& reset() {
//     return *this;
//   }

//   ObMoogLadder& setSampleRate(float fs) {
//     dt = 1.0f / fs;
//     return *this;
//   }

//   ObMoogLadder& setNormalizedCutoff(float wc) {  // {0 <= wc < 0.5}
//     this->wc = clamp(wc, 0.002f, 0.5f - 0.04f);
//     return *this;
//   }

//   ObMoogLadder& setCutoff(float fc) {
//     setNormalizedCutoff(fc * dt);
//     return *this;
//   }

//   ObMoogLadder& setResonance(float q) {
//     // this->q = q * 10.0f;
//     // k = (4.0f) * (this->q - 1.0f)/(10.0f - 1.0f);
//     this->q = q;
//     k = 10.0f * q;
//     return *this;
//   }

//   ObMoogLadder& processCoef() {
//     const auto g = tan(pi<float> * wc);
//     const auto g_plus1 = 1.0f + g;
//     const auto div_g_plus1 = 1.0f / g_plus1;
//     const auto G = g * div_g_plus1;
//     const auto G2 = G * G;

//     LPF1.SetAlpha(G);
//     LPF2.SetAlpha(G);
//     LPF3.SetAlpha(G);
//     LPF4.SetAlpha(G);

//     LPF1.SetBeta(G2 * G * div_g_plus1);
//     LPF2.SetBeta(G2 * div_g_plus1);
//     LPF3.SetBeta(G * div_g_plus1);
//     LPF4.SetBeta(div_g_plus1);

//     G4 = G2 * G2;
//     a0 = 1.0f / (1.0f + k * G4);

//     // Oberheim variations / LPF4
//     // oberheimCoefs[0] = 0.0f;
//     // oberheimCoefs[1] = 0.0f;
//     // oberheimCoefs[2] = 0.0f;
//     // oberheimCoefs[3] = 0.0f;
//     // oberheimCoefs[4] = 1.0f;
//     return *this;
//   }

//   float process(float x) {
//     processCoef();

//     float input = x;

//     float sigma = LPF1.GetFeedbackOutput()
//                   + LPF2.GetFeedbackOutput()
//                   + LPF3.GetFeedbackOutput()
//                   + LPF4.GetFeedbackOutput();

//     input *= 1.0f + k;

//     // calculate input to first filter
//     float u = (input - k * sigma) * a0;

//     // u = tanh(u);
//     u = tanh_clipper.process(u);

//     float stage1 = LPF1.Tick(u);
//     float stage2 = LPF2.Tick(stage1);
//     float stage3 = LPF3.Tick(stage2);
//     float stage4 = LPF4.Tick(stage3);

//     return stage4;

//     // Oberheim variations
//     // const auto output = oberheimCoefs[0] * u
//     //                     + oberheimCoefs[1] * stage1
//     //                     + oberheimCoefs[2] * stage2
//     //                     + oberheimCoefs[3] * stage3
//     //                     + oberheimCoefs[4] * stage4;
//     // return output;
//   }

//  private:
//   float dt = 0, wc = 0;
//   float g = 0, q = 0, k = 0;
//   float G4;
//   float a0;

//   VAOnePole LPF1;
//   VAOnePole LPF2;
//   VAOnePole LPF3;
//   VAOnePole LPF4;

//   float oberheimCoefs[5];

//   Tanh1 tanh_clipper;
// };
}  // namespace filter
}  // namespace mo::dsp
