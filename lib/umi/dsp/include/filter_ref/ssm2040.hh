#pragma once

#include <cmath>
#include <algorithm>
#include "dsp/math.hh"
#include "dsp/filter/halfband.hh"
#include "dsp/clip/adaa.hh"

class Prophet5Filter {
 private:
  // フィルターの状態変数
  float s1, s2, s3, s4;  // 4つの積分器の状態
  float zi;              // 入力遅延（半サンプル計算用）
  float last_output;     // 前回の出力（非線形フィードバック用）
  float prev_in;         // 前の入力
  float prev_out;        // 前の出力

  float dt;  // サンプルレート
  float wc;  // カットオフ周波数
  float q;   // レゾナンス値
  mo::Tanh1 tanh;

  // SSM2040の非線形トランジスタ特性をモデル化するtanhXdX関数
  float tanhXdX(float x) {
    // 数値的な安定性のための特殊ケース
    if (std::abs(x) < 1e-6f) {
      return 1.0f;
    }

    // 大きな値の場合（飽和）
    if (std::abs(x) > 4.0f) {
      return 1.0f / std::abs(x);
    }

    // SSM2040の特性に合わせた近似
    // トランジスタラダーフィルターに基づく特性
    float x2 = x * x;
    return ((x2 + 100.0f) * x2 + 900.0f) / ((18.0f * x2 + 400.0f) * x2 + 900.0f);
  }

  // Prophet-5のVCFで使用されるソフトクリッパー
  float softClip(float x) {
    // SSM2040の出力段飽和をモデル化
    if (x > 1.0f) {
      return 1.0f - mo::exp(-x + 1.0f);
    } else if (x < -1.0f) {
      return -1.0f + mo::exp(x + 1.0f);
    }
    return x;
  }

 public:
  Prophet5Filter(float sampleRate = 50000.0f)
    : dt(1.0f / (sampleRate)),
      s1(0.0f),
      s2(0.0f),
      s3(0.0f),
      s4(0.0f),
      zi(0.0f),
      last_output(0.0f),
      wc(1000.0f),
      q(0.0f),
      tanh(sampleRate) {
  }

  void reset() {
    s1 = s2 = s3 = s4 = zi = last_output = 0.0f;
  }

  float normFreq(float f) { return f * dt; }

  Prophet5Filter& setNormalizedCutoff(float wc) {
    this->wc = std::clamp(wc, 0.002f, 0.5f - 0.04f);
    return *this;
  }

  Prophet5Filter& setCutoff(float fc) {
    setNormalizedCutoff(fc * dt);
    return *this;
  }

  Prophet5Filter& setResonance(float q) {
    this->q = q;
    return *this;
  }

  // 単一サンプル処理
  float process(float input) {
    // float processImpl(float input) {
    input = input * 0.5f;
    // BLTのための周波数ワーピング
    float f = mo::tan(M_PI * wc);

    // レゾナンス（Prophet-5は0.9付近でセルフオシレーション）
    float r = 3.9f * q;

    // 半サンプル遅延入力
    float ih = 0.5f * (input + prev_in);
    prev_in = input;

    // 非線形ゲイン評価
    float t = tanhXdX(r * prev_out);

    // 線形化されたシステムを解く - 4極の場合
    float denom = f * f * f * f + 4 * f * f * f + 6 * f * f + 4 * f + 1 + r * t * f * f * f * f;
    float y1 = (s1 + f * ih - r * t * f * f * f * f * prev_out) / denom;
    float y2 = (s2 + f * f * ih + f * s1) / denom;
    float y3 = (s3 + f * f * f * ih + f * f * s1 + f * s2) / denom;
    float y4 = (s4 + f * f * f * f * ih + f * f * f * s1 + f * f * s2 + f * s3) / denom;

    // 状態更新
    s1 += 2 * f * (ih - y1);
    s2 += 2 * f * (f * ih + s1 - y2);
    s3 += 2 * f * (f * f * ih + f * s1 + s2 - y3);
    s4 += 2 * f * (f * f * f * ih + f * f * s1 + f * s2 + s3 - y4);

    // 出力を保存
    prev_out = y4;

    return y4;
  }

  // float process(float x) {
  //   anti_alias[1].process(processImpl(anti_alias[0].process(x)));
  //   return anti_alias[1].process(processImpl(anti_alias[0].process(0)));
  // }

  mo::dsp::filter::Halfband anti_alias[2];
};

/**
 * Prophet-5のマルチモードVCF
 *
 * Prophet-5 Rev.4では異なるフィルターモードを選択可能
 * - ローパス (SSM2040)
 * - バンドパス
 * - ハイパス
 */
// class Prophet5MultiModeFilter {
// private:
//     Prophet5Filter lpf;        // ローパスフィルター
//     float sampleRate;

//     // フィルター出力のミックス用
//     float s1_out, s2_out, s3_out, s4_out;
//     float mode;  // 0:LP, 0.5:BP, 1:HP

// public:
//     Prophet5MultiModeFilter(float sampleRate = 44100.0f)
//         : lpf(sampleRate), sampleRate(sampleRate),
//           s1_out(0.0f), s2_out(0.0f), s3_out(0.0f), s4_out(0.0f), mode(0.0f) {
//     }

//     void reset() {
//         lpf.reset();
//         s1_out = s2_out = s3_out = s4_out = 0.0f;
//     }

//     void setCutoff(float freq) {
//         lpf.setCutoff(freq);
//     }

//     void setResonance(float res) {
//         lpf.setResonance(res);
//     }

//     void setMode(float m) {
//         // 0:ローパス, 0.5:バンドパス, 1:ハイパス
//         mode = std::clamp(m, 0.0f, 1.0f);
//     }

//     // 単一サンプル処理
//     float process(float input) {
//         // 基本のローパスフィルター処理
//         float lp_out = lpf.process(input);

//         // モードによって出力を混合
//         // 注: 完全な実装では内部状態にアクセスする必要がありますが、
//         // ここでは簡易的に実装しています

//         // 0 = ローパス（通常のSSM2040出力）
//         // 0.5 = バンドパス（近似）
//         // 1 = ハイパス（近似）

//         if (mode < 0.01f) {
//             // ローパスモード
//             return lp_out;
//         } else if (mode > 0.99f) {
//             // ハイパスモード（近似）
//             return input - lp_out;
//         } else {
//             // バンドパスモード（近似）または中間ミックス
//             return input * mode + lp_out * (1.0f - mode);
//         }
//     }

//     // ブロック処理
//     void processBlock(float* input, float* output, int numSamples) {
//         for (int i = 0; i < numSamples; i++) {
//             output[i] = process(input[i]);
//         }
//     }
// };
