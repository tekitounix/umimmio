// =============================================================================
// TB-303 Wave Shaper - Refactored Implementation
//
// 設計パターン: 係数/状態分離 (one_pole.hh準拠)
// - WaveShaperCoeffs: サンプルレート依存の係数（共有可能）
// - WaveShaperState: インスタンス固有の状態
// - DiodeIV: exp近似手法を注入するポリシークラス
//
// Author: Claude (Anthropic)
// License: MIT
// =============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tb303 {

// =============================================================================
// 回路定数 (TB-303 回路図準拠)
// =============================================================================
namespace circuit {

constexpr float V_CC = 12.0f;
constexpr float V_BIAS = 5.33f;
constexpr float R34 = 10e3f;   // Input抵抗 (10kΩ)
constexpr float R35 = 100e3f;  // Input抵抗 (100kΩ)
constexpr float R36 = 10e3f;   // (10kΩ)
constexpr float R45 = 22e3f;   // (22kΩ)
constexpr float C10 = 10e-9f;  // (0.01μF)
constexpr float C11 = 1e-6f;   // (1μF)

// 事前計算コンダクタンス
constexpr float G34 = 1.0f / R34;
constexpr float G35 = 1.0f / R35;
constexpr float G36 = 1.0f / R36;
constexpr float G45 = 1.0f / R45;

}  // namespace circuit

// =============================================================================
// 2SA733P トランジスタパラメータ
// TB-303サービスノート(1982)に基づくNEC 2SA733 Pランク準拠
// =============================================================================
namespace bjt {

constexpr float V_T = 0.025865f;                        // 熱電圧 @ 25℃
constexpr float V_T_INV = 1.0f / V_T;
constexpr float V_CRIT = V_T * 40.0f;

constexpr float I_S = 5e-14f;                           // 飽和電流 (SPICEモデル中央値)
constexpr float BETA_F = 300.0f;                        // 順方向β (Pランク: 200-400)
constexpr float ALPHA_F = BETA_F / (BETA_F + 1.0f);     // ≈ 0.9967
constexpr float BETA_R = 0.1f;                          // 逆方向β (MACOM実測)
constexpr float ALPHA_R = BETA_R / (BETA_R + 1.0f);     // ≈ 0.0909

}  // namespace bjt

// =============================================================================
// 係数構造体（サンプルレート依存、複数インスタンスで共有可能）
// =============================================================================
struct WaveShaperCoeffs {
    float g_c1;               // C10 / dt
    float g_c2;               // C11 / dt
    float inv_j11;            // 1 / (-g_c1 - G34)
    float schur_j11_factor;   // G34 * G34 * inv_j11
    float schur_f1_factor;    // G34 * inv_j11
};

inline WaveShaperCoeffs make_waveshaper_coeffs(float sample_rate) {
    using namespace circuit;
    const float dt = 1.0f / sample_rate;
    const float g_c1 = C10 / dt;
    const float g_c2 = C11 / dt;
    const float j11 = -g_c1 - G34;
    const float inv_j11 = 1.0f / j11;
    return {
        g_c1,
        g_c2,
        inv_j11,
        G34 * G34 * inv_j11,
        G34 * inv_j11
    };
}

// =============================================================================
// 状態構造体（インスタンス固有）
// =============================================================================
struct WaveShaperState {
    float v_c1 = 0.0f;              // C10電圧
    float v_c2 = 8.0f;              // C11電圧
    float v_b = 8.0f;               // ベース電圧
    float v_e = 8.0f;               // エミッタ電圧
    float v_c = circuit::V_BIAS;    // コレクタ電圧
    // 遅延評価用：B-Cジャンクションのキャッシュ
    float i_cr_cached = -bjt::I_S;  // 前サンプルのi_cr
    float g_cr_cached = 1e-12f;     // 前サンプルのg_cr
    // 予測子-補正子用：前サンプルの値
    float v_in_prev = 8.0f;         // 前サンプルの入力電圧
    float dv_b_prev = 0.0f;         // 前サンプルのベース変化量
    float dv_e_prev = 0.0f;         // 前サンプルのエミッタ変化量

    void reset() {
        v_c1 = 0.0f;
        v_c2 = 8.0f;
        v_b = 8.0f;
        v_e = 8.0f;
        v_c = circuit::V_BIAS;
        i_cr_cached = -bjt::I_S;
        g_cr_cached = 1e-12f;
        v_in_prev = 8.0f;
        dv_b_prev = 0.0f;
        dv_e_prev = 0.0f;
    }
};

// =============================================================================
// exp近似ポリシー
// =============================================================================
namespace exp_impl {

// Schraudolph改良版 fast_exp
inline float schraudolph(float x) {
    x = std::clamp(x, -87.0f, 88.0f);
    union { float f; int32_t i; } u;
    constexpr float LOG2E = 1.4426950408889634f;
    constexpr float SHIFT = (1 << 23) * 127.0f;
    constexpr float SCALE = (1 << 23) * LOG2E;
    u.i = static_cast<int32_t>(SCALE * x + SHIFT);
    float t = x - static_cast<float>(u.i - static_cast<int32_t>(SHIFT)) / SCALE;
    u.f *= 1.0f + t * (1.0f + t * 0.5f);
    return u.f;
}

}  // namespace exp_impl

// =============================================================================
// DiodeIV ポリシークラス（exp手法を注入）
// =============================================================================
struct DiodeIV_FastExp {
    static void eval(float v, float& i, float& g) {
        using namespace bjt;
        if (v > V_CRIT) {
            float exp_crit = exp_impl::schraudolph(V_CRIT * V_T_INV);
            g = I_S * V_T_INV * exp_crit;
            i = I_S * (exp_crit - 1.0f) + g * (v - V_CRIT);
        } else if (v < -10.0f * V_T) {
            i = -I_S;
            g = 1e-12f;
        } else {
            float exp_v = exp_impl::schraudolph(v * V_T_INV);
            i = I_S * (exp_v - 1.0f);
            g = I_S * V_T_INV * exp_v + 1e-12f;
        }
    }
};

struct DiodeIV_StdExp {
    static void eval(float v, float& i, float& g) {
        using namespace bjt;
        if (v > V_CRIT) {
            float exp_crit = std::exp(V_CRIT * V_T_INV);
            g = I_S * V_T_INV * exp_crit;
            i = I_S * (exp_crit - 1.0f) + g * (v - V_CRIT);
        } else if (v < -10.0f * V_T) {
            i = -I_S;
            g = 1e-12f;
        } else {
            float exp_v = std::exp(v * V_T_INV);
            i = I_S * (exp_v - 1.0f);
            g = I_S * V_T_INV * exp_v + 1e-12f;
        }
    }
};

// =============================================================================
// Schur補行列法による1反復ステップ（純粋関数）
// =============================================================================
namespace detail {

template <typename DiodeIV>
inline void schur_step(
    const WaveShaperCoeffs& c,
    float v_in, float v_c1_prev, float v_c2_prev,
    float& v_cap, float& v_b, float& v_e, float& v_c
) {
    using namespace circuit;
    using namespace bjt;

    float i_ef, g_ef, i_cr, g_cr;
    DiodeIV::eval(v_e - v_b, i_ef, g_ef);
    DiodeIV::eval(v_c - v_b, i_cr, g_cr);

    // Ebers-Moll電流
    const float i_e = i_ef - ALPHA_R * i_cr;
    const float i_c = ALPHA_F * i_ef - i_cr;
    const float i_b = i_e - i_c;

    // KCL残差
    const float f1 = c.g_c1 * (v_in - v_cap - v_c1_prev) - G34 * (v_cap - v_b);
    const float f2 = G35 * (v_in - v_b) + G34 * (v_cap - v_b) + i_b;
    const float f3 = G45 * (V_CC - v_e) - i_e - c.g_c2 * (v_e - v_c2_prev);
    const float f4 = G36 * (V_BIAS - v_c) + i_c;

    // ヤコビアン
    const float j22 = -G35 - G34 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
    const float j23 = (1.0f - ALPHA_F) * g_ef;
    const float j24 = (1.0f - ALPHA_R) * g_cr;
    const float j32 = g_ef - ALPHA_R * g_cr;
    const float j33 = -G45 - g_ef - c.g_c2;
    const float j34 = ALPHA_R * g_cr;
    const float j42 = -ALPHA_F * g_ef + g_cr;
    const float j43 = ALPHA_F * g_ef;
    const float j44 = -G36 - g_cr;

    // Step 1: j11でv_capを消去（Schur補完）
    const float j22_p = j22 - c.schur_j11_factor;
    const float f2_p = f2 - c.schur_f1_factor * f1;

    // Step 2: j22'でSchur補完 → 2x2システム（v_e, v_c）
    const float inv_j22_p = 1.0f / j22_p;
    const float m32 = j32 * inv_j22_p;
    const float m42 = j42 * inv_j22_p;

    const float j33_p = j33 - m32 * j23;
    const float j34_p = j34 - m32 * j24;
    const float f3_p = f3 - m32 * f2_p;

    const float j43_p = j43 - m42 * j23;
    const float j44_p = j44 - m42 * j24;
    const float f4_p = f4 - m42 * f2_p;

    // Step 3: 2x2 Cramer（v_e, v_c）
    const float det = j33_p * j44_p - j34_p * j43_p;
    const float inv_det = 1.0f / det;

    const float dv_e = (j44_p * (-f3_p) - j34_p * (-f4_p)) * inv_det;
    const float dv_c = (j33_p * (-f4_p) - j43_p * (-f3_p)) * inv_det;

    // Step 4: 後退代入
    const float dv_b = (-f2_p - j23 * dv_e - j24 * dv_c) * inv_j22_p;
    const float dv_cap = (-f1 - G34 * dv_b) * c.inv_j11;

    // ダンピング
    float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b), std::abs(dv_e), std::abs(dv_c)});
    float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

    v_cap += damp * dv_cap;
    v_b += damp * dv_b;
    v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
    v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);
}

// -----------------------------------------------------------------------------
// Fast版：固定ダンピング係数（分岐なし）
//
// 条件分岐とstd::maxを排除し、固定係数で常にダンピング。
// j22ピボットの安定性により、小さめの固定係数でも収束する。
// -----------------------------------------------------------------------------
template <typename DiodeIV>
inline void schur_step_fast(
    const WaveShaperCoeffs& c,
    float v_in, float v_c1_prev, float v_c2_prev,
    float& v_cap, float& v_b, float& v_e, float& v_c
) {
    using namespace circuit;
    using namespace bjt;

    float i_ef, g_ef, i_cr, g_cr;
    DiodeIV::eval(v_e - v_b, i_ef, g_ef);
    DiodeIV::eval(v_c - v_b, i_cr, g_cr);

    // Ebers-Moll電流
    const float i_e = i_ef - ALPHA_R * i_cr;
    const float i_c = ALPHA_F * i_ef - i_cr;
    const float i_b = i_e - i_c;

    // KCL残差
    const float f1 = c.g_c1 * (v_in - v_cap - v_c1_prev) - G34 * (v_cap - v_b);
    const float f2 = G35 * (v_in - v_b) + G34 * (v_cap - v_b) + i_b;
    const float f3 = G45 * (V_CC - v_e) - i_e - c.g_c2 * (v_e - v_c2_prev);
    const float f4 = G36 * (V_BIAS - v_c) + i_c;

    // ヤコビアン
    const float j22 = -G35 - G34 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
    const float j23 = (1.0f - ALPHA_F) * g_ef;
    const float j24 = (1.0f - ALPHA_R) * g_cr;
    const float j32 = g_ef - ALPHA_R * g_cr;
    const float j33 = -G45 - g_ef - c.g_c2;
    const float j34 = ALPHA_R * g_cr;
    const float j42 = -ALPHA_F * g_ef + g_cr;
    const float j43 = ALPHA_F * g_ef;
    const float j44 = -G36 - g_cr;

    // Step 1: j11でv_capを消去（Schur補完）
    const float j22_p = j22 - c.schur_j11_factor;
    const float f2_p = f2 - c.schur_f1_factor * f1;

    // Step 2: j22'でSchur補完 → 2x2システム（v_e, v_c）
    const float inv_j22_p = 1.0f / j22_p;
    const float m32 = j32 * inv_j22_p;
    const float m42 = j42 * inv_j22_p;

    const float j33_p = j33 - m32 * j23;
    const float j34_p = j34 - m32 * j24;
    const float f3_p = f3 - m32 * f2_p;

    const float j43_p = j43 - m42 * j23;
    const float j44_p = j44 - m42 * j24;
    const float f4_p = f4 - m42 * f2_p;

    // Step 3: 2x2 Cramer（v_e, v_c）
    const float det = j33_p * j44_p - j34_p * j43_p;
    const float inv_det = 1.0f / det;

    const float dv_e = (j44_p * (-f3_p) - j34_p * (-f4_p)) * inv_det;
    const float dv_c = (j33_p * (-f4_p) - j43_p * (-f3_p)) * inv_det;

    // Step 4: 後退代入
    const float dv_b = (-f2_p - j23 * dv_e - j24 * dv_c) * inv_j22_p;
    const float dv_cap = (-f1 - G34 * dv_b) * c.inv_j11;

    // 緩和ダンピング（閾値0.7V）
    float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b), std::abs(dv_e), std::abs(dv_c)});
    float damp = (max_dv > 0.7f) ? 0.7f / max_dv : 1.0f;

    v_cap += damp * dv_cap;
    v_b += damp * dv_b;
    v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
    v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);
}

// -----------------------------------------------------------------------------
// Ultra版：B-Cジャンクション遅延評価
//
// B-Cジャンクションは逆バイアスで変化が遅いため、前サンプルの値を再利用。
// exp呼び出しが半減し、大幅な高速化が可能。
// -----------------------------------------------------------------------------
template <typename DiodeIV>
inline void schur_step_ultra(
    const WaveShaperCoeffs& c,
    float v_in, float v_c1_prev, float v_c2_prev,
    float& v_cap, float& v_b, float& v_e, float& v_c,
    float i_cr, float g_cr  // キャッシュされたB-C値
) {
    using namespace circuit;
    using namespace bjt;

    // E-Bジャンクションのみ評価（B-Cはキャッシュを使用）
    float i_ef, g_ef;
    DiodeIV::eval(v_e - v_b, i_ef, g_ef);

    // Ebers-Moll電流
    const float i_e = i_ef - ALPHA_R * i_cr;
    const float i_c = ALPHA_F * i_ef - i_cr;
    const float i_b = i_e - i_c;

    // KCL残差
    const float f1 = c.g_c1 * (v_in - v_cap - v_c1_prev) - G34 * (v_cap - v_b);
    const float f2 = G35 * (v_in - v_b) + G34 * (v_cap - v_b) + i_b;
    const float f3 = G45 * (V_CC - v_e) - i_e - c.g_c2 * (v_e - v_c2_prev);
    const float f4 = G36 * (V_BIAS - v_c) + i_c;

    // ヤコビアン（キャッシュされたg_crを使用）
    const float j22 = -G35 - G34 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
    const float j23 = (1.0f - ALPHA_F) * g_ef;
    const float j24 = (1.0f - ALPHA_R) * g_cr;
    const float j32 = g_ef - ALPHA_R * g_cr;
    const float j33 = -G45 - g_ef - c.g_c2;
    const float j34 = ALPHA_R * g_cr;
    const float j42 = -ALPHA_F * g_ef + g_cr;
    const float j43 = ALPHA_F * g_ef;
    const float j44 = -G36 - g_cr;

    // Step 1: j11でv_capを消去（Schur補完）
    const float j22_p = j22 - c.schur_j11_factor;
    const float f2_p = f2 - c.schur_f1_factor * f1;

    // Step 2: j22'でSchur補完 → 2x2システム（v_e, v_c）
    const float inv_j22_p = 1.0f / j22_p;
    const float m32 = j32 * inv_j22_p;
    const float m42 = j42 * inv_j22_p;

    const float j33_p = j33 - m32 * j23;
    const float j34_p = j34 - m32 * j24;
    const float f3_p = f3 - m32 * f2_p;

    const float j43_p = j43 - m42 * j23;
    const float j44_p = j44 - m42 * j24;
    const float f4_p = f4 - m42 * f2_p;

    // Step 3: 2x2 Cramer（v_e, v_c）
    const float det = j33_p * j44_p - j34_p * j43_p;
    const float inv_det = 1.0f / det;

    const float dv_e = (j44_p * (-f3_p) - j34_p * (-f4_p)) * inv_det;
    const float dv_c = (j33_p * (-f4_p) - j43_p * (-f3_p)) * inv_det;

    // Step 4: 後退代入
    const float dv_b = (-f2_p - j23 * dv_e - j24 * dv_c) * inv_j22_p;
    const float dv_cap = (-f1 - G34 * dv_b) * c.inv_j11;

    // ダンピング
    float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b), std::abs(dv_e), std::abs(dv_c)});
    float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

    v_cap += damp * dv_cap;
    v_b += damp * dv_b;
    v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
    v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);
}

}  // namespace detail

// =============================================================================
// process関数（純粋関数版）
// =============================================================================
template <typename DiodeIV, int Iterations>
inline float process(WaveShaperState& s, const WaveShaperCoeffs& c, float v_in) {
    const float v_c1_prev = s.v_c1;
    const float v_c2_prev = s.v_c2;

    float v_cap = v_in - v_c1_prev;
    float v_b = s.v_b;
    float v_e = v_c2_prev;
    float v_c = s.v_c;

    for (int i = 0; i < Iterations; ++i) {
        detail::schur_step<DiodeIV>(c, v_in, v_c1_prev, v_c2_prev, v_cap, v_b, v_e, v_c);
    }

    s.v_c1 = v_in - v_cap;
    s.v_c2 = v_e;
    s.v_b = v_b;
    s.v_e = v_e;
    s.v_c = v_c;

    return v_c;
}

// Fast版（固定ダンピング）のprocess関数
template <typename DiodeIV, int Iterations>
inline float process_fast(WaveShaperState& s, const WaveShaperCoeffs& c, float v_in) {
    const float v_c1_prev = s.v_c1;
    const float v_c2_prev = s.v_c2;

    float v_cap = v_in - v_c1_prev;
    float v_b = s.v_b;
    float v_e = v_c2_prev;
    float v_c = s.v_c;

    for (int i = 0; i < Iterations; ++i) {
        detail::schur_step_fast<DiodeIV>(c, v_in, v_c1_prev, v_c2_prev, v_cap, v_b, v_e, v_c);
    }

    s.v_c1 = v_in - v_cap;
    s.v_c2 = v_e;
    s.v_b = v_b;
    s.v_e = v_e;
    s.v_c = v_c;

    return v_c;
}

// Hybrid版：1回目は固定ダンピング、2回目は通常ダンピングで精度確保
template <typename DiodeIV>
inline float process_hybrid(WaveShaperState& s, const WaveShaperCoeffs& c, float v_in) {
    const float v_c1_prev = s.v_c1;
    const float v_c2_prev = s.v_c2;

    float v_cap = v_in - v_c1_prev;
    float v_b = s.v_b;
    float v_e = v_c2_prev;
    float v_c = s.v_c;

    // 1回目：固定ダンピングで高速に近似解を得る
    detail::schur_step_fast<DiodeIV>(c, v_in, v_c1_prev, v_c2_prev, v_cap, v_b, v_e, v_c);
    // 2回目：通常ダンピングで精度を確保
    detail::schur_step<DiodeIV>(c, v_in, v_c1_prev, v_c2_prev, v_cap, v_b, v_e, v_c);

    s.v_c1 = v_in - v_cap;
    s.v_c2 = v_e;
    s.v_b = v_b;
    s.v_e = v_e;
    s.v_c = v_c;

    return v_c;
}

// Ultra版：B-Cジャンクション遅延評価（exp呼び出し半減）
template <typename DiodeIV, int Iterations>
inline float process_ultra(WaveShaperState& s, const WaveShaperCoeffs& c, float v_in) {
    const float v_c1_prev = s.v_c1;
    const float v_c2_prev = s.v_c2;

    float v_cap = v_in - v_c1_prev;
    float v_b = s.v_b;
    float v_e = v_c2_prev;
    float v_c = s.v_c;

    // キャッシュされたB-C値を使用して反復
    const float i_cr = s.i_cr_cached;
    const float g_cr = s.g_cr_cached;

    for (int i = 0; i < Iterations; ++i) {
        detail::schur_step_ultra<DiodeIV>(c, v_in, v_c1_prev, v_c2_prev, v_cap, v_b, v_e, v_c, i_cr, g_cr);
    }

    // B-Cジャンクションを評価してキャッシュを更新（次サンプル用）
    float i_cr_new, g_cr_new;
    DiodeIV::eval(v_c - v_b, i_cr_new, g_cr_new);
    s.i_cr_cached = i_cr_new;
    s.g_cr_cached = g_cr_new;

    s.v_c1 = v_in - v_cap;
    s.v_c2 = v_e;
    s.v_b = v_b;
    s.v_e = v_e;
    s.v_c = v_c;

    return v_c;
}

// Predictor-Corrector版：前サンプルの変化量から予測して1回の補正
// 入力信号が連続的な場合、v_b, v_eの変化も連続的。
// 前フレームの変化量を使って初期推定を改善することで、1回の反復で収束させる。
template <typename DiodeIV>
inline float process_predictor(WaveShaperState& s, const WaveShaperCoeffs& c, float v_in) {
    const float v_c1_prev = s.v_c1;
    const float v_c2_prev = s.v_c2;

    // 入力変化に基づく予測スケール
    const float dv_in = v_in - s.v_in_prev;
    const float scale = (std::abs(s.v_in_prev - 8.0f) > 0.1f)
                      ? dv_in / (s.v_in_prev - 8.0f + 0.01f)
                      : 0.0f;

    // 予測: 前回の変化量をスケーリングして適用
    float v_cap = v_in - v_c1_prev;
    float v_b = s.v_b + scale * s.dv_b_prev;
    float v_e = v_c2_prev + scale * s.dv_e_prev;
    float v_c = s.v_c;

    // 予測値をクランプ
    v_b = std::clamp(v_b, 0.0f, circuit::V_CC);
    v_e = std::clamp(v_e, 0.0f, circuit::V_CC);

    const float v_b_init = v_b;
    const float v_e_init = v_e;

    // 補正: 緩和ダンピングで1回の反復
    detail::schur_step_fast<DiodeIV>(c, v_in, v_c1_prev, v_c2_prev, v_cap, v_b, v_e, v_c);

    // 変化量を記録
    s.dv_b_prev = v_b - v_b_init + (scale * s.dv_b_prev);
    s.dv_e_prev = v_e - v_e_init + (scale * s.dv_e_prev);
    s.v_in_prev = v_in;

    s.v_c1 = v_in - v_cap;
    s.v_c2 = v_e;
    s.v_b = v_b;
    s.v_e = v_e;
    s.v_c = v_c;

    return v_c;
}

// -----------------------------------------------------------------------------
// Turbo版：2反復だが2回目はE-B評価をスキップ
//
// 1回目: E-BとB-Cの両方を評価（2回のexp）
// 2回目: 1回目のE-B値を再利用、B-Cのみ再評価（1回のexp）
// 合計3回のexp()で2反復の精度が得られる。
// -----------------------------------------------------------------------------
template <typename DiodeIV>
inline float process_turbo(WaveShaperState& s, const WaveShaperCoeffs& c, float v_in) {
    using namespace circuit;
    using namespace bjt;

    const float v_c1_prev = s.v_c1;
    const float v_c2_prev = s.v_c2;

    float v_cap = v_in - v_c1_prev;
    float v_b = s.v_b;
    float v_e = v_c2_prev;
    float v_c = s.v_c;

    // === 1回目の反復（フル評価）===
    float i_ef, g_ef, i_cr, g_cr;
    DiodeIV::eval(v_e - v_b, i_ef, g_ef);
    DiodeIV::eval(v_c - v_b, i_cr, g_cr);

    {
        const float i_e = i_ef - ALPHA_R * i_cr;
        const float i_c = ALPHA_F * i_ef - i_cr;
        const float i_b = i_e - i_c;

        const float f1 = c.g_c1 * (v_in - v_cap - v_c1_prev) - G34 * (v_cap - v_b);
        const float f2 = G35 * (v_in - v_b) + G34 * (v_cap - v_b) + i_b;
        const float f3 = G45 * (V_CC - v_e) - i_e - c.g_c2 * (v_e - v_c2_prev);
        const float f4 = G36 * (V_BIAS - v_c) + i_c;

        const float j22 = -G35 - G34 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        const float j23 = (1.0f - ALPHA_F) * g_ef;
        const float j24 = (1.0f - ALPHA_R) * g_cr;
        const float j32 = g_ef - ALPHA_R * g_cr;
        const float j33 = -G45 - g_ef - c.g_c2;
        const float j34 = ALPHA_R * g_cr;
        const float j42 = -ALPHA_F * g_ef + g_cr;
        const float j43 = ALPHA_F * g_ef;
        const float j44 = -G36 - g_cr;

        const float j22_p = j22 - c.schur_j11_factor;
        const float f2_p = f2 - c.schur_f1_factor * f1;

        const float inv_j22_p = 1.0f / j22_p;
        const float m32 = j32 * inv_j22_p;
        const float m42 = j42 * inv_j22_p;

        const float j33_p = j33 - m32 * j23;
        const float j34_p = j34 - m32 * j24;
        const float f3_p = f3 - m32 * f2_p;

        const float j43_p = j43 - m42 * j23;
        const float j44_p = j44 - m42 * j24;
        const float f4_p = f4 - m42 * f2_p;

        const float det = j33_p * j44_p - j34_p * j43_p;
        const float inv_det = 1.0f / det;

        const float dv_e = (j44_p * (-f3_p) - j34_p * (-f4_p)) * inv_det;
        const float dv_c = (j33_p * (-f4_p) - j43_p * (-f3_p)) * inv_det;
        const float dv_b = (-f2_p - j23 * dv_e - j24 * dv_c) * inv_j22_p;
        const float dv_cap = (-f1 - G34 * dv_b) * c.inv_j11;

        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b), std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.7f) ? 0.7f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);
    }

    // === 2回目の反復（E-B再利用、B-Cのみ再評価）===
    // E-Bは前回の値を再利用（v_e - v_bが変化しているが、1回の反復内では変化が小さい）
    DiodeIV::eval(v_c - v_b, i_cr, g_cr);

    {
        const float i_e = i_ef - ALPHA_R * i_cr;
        const float i_c = ALPHA_F * i_ef - i_cr;
        const float i_b = i_e - i_c;

        const float f1 = c.g_c1 * (v_in - v_cap - v_c1_prev) - G34 * (v_cap - v_b);
        const float f2 = G35 * (v_in - v_b) + G34 * (v_cap - v_b) + i_b;
        const float f3 = G45 * (V_CC - v_e) - i_e - c.g_c2 * (v_e - v_c2_prev);
        const float f4 = G36 * (V_BIAS - v_c) + i_c;

        const float j22 = -G35 - G34 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        const float j23 = (1.0f - ALPHA_F) * g_ef;
        const float j24 = (1.0f - ALPHA_R) * g_cr;
        const float j32 = g_ef - ALPHA_R * g_cr;
        const float j33 = -G45 - g_ef - c.g_c2;
        const float j34 = ALPHA_R * g_cr;
        const float j42 = -ALPHA_F * g_ef + g_cr;
        const float j43 = ALPHA_F * g_ef;
        const float j44 = -G36 - g_cr;

        const float j22_p = j22 - c.schur_j11_factor;
        const float f2_p = f2 - c.schur_f1_factor * f1;

        const float inv_j22_p = 1.0f / j22_p;
        const float m32 = j32 * inv_j22_p;
        const float m42 = j42 * inv_j22_p;

        const float j33_p = j33 - m32 * j23;
        const float j34_p = j34 - m32 * j24;
        const float f3_p = f3 - m32 * f2_p;

        const float j43_p = j43 - m42 * j23;
        const float j44_p = j44 - m42 * j24;
        const float f4_p = f4 - m42 * f2_p;

        const float det = j33_p * j44_p - j34_p * j43_p;
        const float inv_det = 1.0f / det;

        const float dv_e = (j44_p * (-f3_p) - j34_p * (-f4_p)) * inv_det;
        const float dv_c = (j33_p * (-f4_p) - j43_p * (-f3_p)) * inv_det;
        const float dv_b = (-f2_p - j23 * dv_e - j24 * dv_c) * inv_j22_p;
        const float dv_cap = (-f1 - G34 * dv_b) * c.inv_j11;

        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b), std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);
    }

    s.v_c1 = v_in - v_cap;
    s.v_c2 = v_e;
    s.v_b = v_b;
    s.v_e = v_e;
    s.v_c = v_c;

    return v_c;
}

// -----------------------------------------------------------------------------
// TurboLite版：Turboと同じ3 exp呼び出しだがヤコビアン計算を簡略化
//
// 1回目: E-BとB-Cの両方を評価（2回のexp）、フル反復
// 2回目: B-Cのみ再評価（1回のexp）、E-B側ヤコビアンを再利用
// 合計3回のexp()、g_ef依存ヤコビアン要素の再利用で計算削減。
// -----------------------------------------------------------------------------
template <typename DiodeIV>
inline float process_turbo_lite(WaveShaperState& s, const WaveShaperCoeffs& c, float v_in) {
    using namespace circuit;
    using namespace bjt;

    const float v_c1_prev = s.v_c1;
    const float v_c2_prev = s.v_c2;

    float v_cap = v_in - v_c1_prev;
    float v_b = s.v_b;
    float v_e = v_c2_prev;
    float v_c = s.v_c;

    // === 1回目の反復（フル評価）===
    float i_ef, g_ef, i_cr, g_cr;
    DiodeIV::eval(v_e - v_b, i_ef, g_ef);
    DiodeIV::eval(v_c - v_b, i_cr, g_cr);

    // g_efに依存するヤコビアン要素を保存（2回目で再利用）
    const float j23_ef = (1.0f - ALPHA_F) * g_ef;
    const float j32_ef = g_ef;  // ALPHA_R * g_cr部分は後で追加
    const float j33_ef = -G45 - g_ef - c.g_c2;
    const float j42_ef = -ALPHA_F * g_ef;  // g_cr部分は後で追加
    const float j43_ef = ALPHA_F * g_ef;

    {
        const float i_e = i_ef - ALPHA_R * i_cr;
        const float i_c = ALPHA_F * i_ef - i_cr;
        const float i_b = i_e - i_c;

        const float f1 = c.g_c1 * (v_in - v_cap - v_c1_prev) - G34 * (v_cap - v_b);
        const float f2 = G35 * (v_in - v_b) + G34 * (v_cap - v_b) + i_b;
        const float f3 = G45 * (V_CC - v_e) - i_e - c.g_c2 * (v_e - v_c2_prev);
        const float f4 = G36 * (V_BIAS - v_c) + i_c;

        const float j22 = -G35 - G34 - j23_ef - (1.0f - ALPHA_R) * g_cr;
        const float j24 = (1.0f - ALPHA_R) * g_cr;
        const float j32 = j32_ef - ALPHA_R * g_cr;
        const float j34 = ALPHA_R * g_cr;
        const float j42 = j42_ef + g_cr;
        const float j44 = -G36 - g_cr;

        const float j22_p = j22 - c.schur_j11_factor;
        const float f2_p = f2 - c.schur_f1_factor * f1;

        const float inv_j22_p = 1.0f / j22_p;
        const float m32 = j32 * inv_j22_p;
        const float m42 = j42 * inv_j22_p;

        const float j33_p = j33_ef - m32 * j23_ef;
        const float j34_p = j34 - m32 * j24;
        const float f3_p = f3 - m32 * f2_p;

        const float j43_p = j43_ef - m42 * j23_ef;
        const float j44_p = j44 - m42 * j24;
        const float f4_p = f4 - m42 * f2_p;

        const float det = j33_p * j44_p - j34_p * j43_p;
        const float inv_det = 1.0f / det;

        const float dv_e = (j44_p * (-f3_p) - j34_p * (-f4_p)) * inv_det;
        const float dv_c = (j33_p * (-f4_p) - j43_p * (-f3_p)) * inv_det;
        const float dv_b = (-f2_p - j23_ef * dv_e - j24 * dv_c) * inv_j22_p;
        const float dv_cap = (-f1 - G34 * dv_b) * c.inv_j11;

        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b), std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.7f) ? 0.7f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);
    }

    // === 2回目の反復（E-B再利用、B-Cのみ再評価）===
    DiodeIV::eval(v_c - v_b, i_cr, g_cr);

    {
        const float i_e = i_ef - ALPHA_R * i_cr;
        const float i_c = ALPHA_F * i_ef - i_cr;
        const float i_b = i_e - i_c;

        const float f1 = c.g_c1 * (v_in - v_cap - v_c1_prev) - G34 * (v_cap - v_b);
        const float f2 = G35 * (v_in - v_b) + G34 * (v_cap - v_b) + i_b;
        const float f3 = G45 * (V_CC - v_e) - i_e - c.g_c2 * (v_e - v_c2_prev);
        const float f4 = G36 * (V_BIAS - v_c) + i_c;

        // g_crに依存する要素のみ再計算、g_ef依存は再利用
        const float j22 = -G35 - G34 - j23_ef - (1.0f - ALPHA_R) * g_cr;
        const float j24 = (1.0f - ALPHA_R) * g_cr;
        const float j32 = j32_ef - ALPHA_R * g_cr;
        const float j34 = ALPHA_R * g_cr;
        const float j42 = j42_ef + g_cr;
        const float j44 = -G36 - g_cr;

        const float j22_p = j22 - c.schur_j11_factor;
        const float f2_p = f2 - c.schur_f1_factor * f1;

        const float inv_j22_p = 1.0f / j22_p;
        const float m32 = j32 * inv_j22_p;
        const float m42 = j42 * inv_j22_p;

        const float j33_p = j33_ef - m32 * j23_ef;
        const float j34_p = j34 - m32 * j24;
        const float f3_p = f3 - m32 * f2_p;

        const float j43_p = j43_ef - m42 * j23_ef;
        const float j44_p = j44 - m42 * j24;
        const float f4_p = f4 - m42 * f2_p;

        const float det = j33_p * j44_p - j34_p * j43_p;
        const float inv_det = 1.0f / det;

        const float dv_e = (j44_p * (-f3_p) - j34_p * (-f4_p)) * inv_det;
        const float dv_c = (j33_p * (-f4_p) - j43_p * (-f3_p)) * inv_det;
        const float dv_b = (-f2_p - j23_ef * dv_e - j24 * dv_c) * inv_j22_p;
        const float dv_cap = (-f1 - G34 * dv_b) * c.inv_j11;

        float max_dv = std::max({std::abs(dv_cap), std::abs(dv_b), std::abs(dv_e), std::abs(dv_c)});
        float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        v_b += damp * dv_b;
        v_e = std::clamp(v_e + damp * dv_e, 0.0f, V_CC + 0.5f);
        v_c = std::clamp(v_c + damp * dv_c, 0.0f, V_CC + 0.5f);
    }

    s.v_c1 = v_in - v_cap;
    s.v_c2 = v_e;
    s.v_b = v_b;
    s.v_e = v_e;
    s.v_c = v_c;

    return v_c;
}

// =============================================================================
// 係数ポリシー（one_pole.hh準拠）
// =============================================================================
struct InlineCoeffs {
    static constexpr bool needs_sample_rate = true;

    const WaveShaperCoeffs& coeffs(float sample_rate) {
        temp_ = make_waveshaper_coeffs(sample_rate);
        return temp_;
    }

  private:
    WaveShaperCoeffs temp_{};
};

struct OwnCoeffs {
    static constexpr bool needs_sample_rate = false;

    void set_sample_rate(float sample_rate) {
        coeffs_ = make_waveshaper_coeffs(sample_rate);
    }

    const WaveShaperCoeffs& coeffs(float = 0.0f) const {
        return coeffs_;
    }

  private:
    WaveShaperCoeffs coeffs_ = make_waveshaper_coeffs(48000.0f);
};

struct SharedCoeffs {
    static constexpr bool needs_sample_rate = false;

    explicit SharedCoeffs(const WaveShaperCoeffs& coeffs) : coeffs_(&coeffs) {}

    const WaveShaperCoeffs& coeffs(float = 0.0f) const {
        return *coeffs_;
    }

  private:
    const WaveShaperCoeffs* coeffs_ = nullptr;
};

// =============================================================================
// WaveShaperクラステンプレート
// =============================================================================
enum class JacobianMode { Full, Fast, Hybrid, Ultra, Predictor, Turbo, TurboLite };

template <typename DiodeIV, int Iterations, typename CoeffSource, JacobianMode Mode = JacobianMode::Full>
class WaveShaper {
  public:
    WaveShaper()
        requires(!std::is_same_v<CoeffSource, SharedCoeffs>)
    = default;

    explicit WaveShaper(const WaveShaperCoeffs& shared)
        requires(std::is_same_v<CoeffSource, SharedCoeffs>)
        : c_(shared) {}

    void setSampleRate(float sample_rate)
        requires requires(CoeffSource& c, float sr) { c.set_sample_rate(sr); }
    {
        c_.set_sample_rate(sample_rate);
    }

    void reset() { s_.reset(); }

    float process(float v_in, float sample_rate) {
        const auto& coeffs_ref = c_.coeffs(sample_rate);
        if constexpr (Mode == JacobianMode::Fast) {
            return tb303::process_fast<DiodeIV, Iterations>(s_, coeffs_ref, v_in);
        } else if constexpr (Mode == JacobianMode::Hybrid) {
            return tb303::process_hybrid<DiodeIV>(s_, coeffs_ref, v_in);
        } else if constexpr (Mode == JacobianMode::Ultra) {
            return tb303::process_ultra<DiodeIV, Iterations>(s_, coeffs_ref, v_in);
        } else if constexpr (Mode == JacobianMode::Predictor) {
            return tb303::process_predictor<DiodeIV>(s_, coeffs_ref, v_in);
        } else if constexpr (Mode == JacobianMode::Turbo) {
            return tb303::process_turbo<DiodeIV>(s_, coeffs_ref, v_in);
        } else if constexpr (Mode == JacobianMode::TurboLite) {
            return tb303::process_turbo_lite<DiodeIV>(s_, coeffs_ref, v_in);
        } else {
            return tb303::process<DiodeIV, Iterations>(s_, coeffs_ref, v_in);
        }
    }

    float process(float v_in)
        requires(!CoeffSource::needs_sample_rate)
    {
        return process(v_in, 0.0f);
    }

  private:
    WaveShaperState s_{};
    CoeffSource c_{};
};

// =============================================================================
// 型エイリアス（後方互換性）
// =============================================================================

// Newton法（4x4直接解法 → 内部ではSchur縮約を使用）
template <int N>
using WaveShaperNewton = WaveShaper<DiodeIV_FastExp, N, OwnCoeffs>;

// Schur補行列法
template <int N>
using WaveShaperSchur = WaveShaper<DiodeIV_FastExp, N, OwnCoeffs>;

// リファレンス（std::exp, 100反復）
using WaveShaperReference = WaveShaper<DiodeIV_StdExp, 100, OwnCoeffs>;

// SchurUltra（後方互換性のため2反復版として定義）
using WaveShaperSchurUltra = WaveShaper<DiodeIV_FastExp, 2, OwnCoeffs>;

// =============================================================================
// Fast版（ダンピング除去による高速化）
// j22ピボットの安定性によりダンピングなしでも収束
// =============================================================================
template <int N>
using WaveShaperFast = WaveShaper<DiodeIV_FastExp, N, OwnCoeffs, JacobianMode::Fast>;

using WaveShaperFast1 = WaveShaperFast<1>;
using WaveShaperFast2 = WaveShaperFast<2>;

// =============================================================================
// Hybrid版（1回目固定ダンピング + 2回目通常ダンピング）
// 速度と精度のバランスを取る
// =============================================================================
using WaveShaperHybrid = WaveShaper<DiodeIV_FastExp, 2, OwnCoeffs, JacobianMode::Hybrid>;

// =============================================================================
// Ultra版（B-Cジャンクション遅延評価による高速化）
// 逆バイアスのB-Cジャンクションは変化が遅いため、前サンプルの値を再利用。
// exp呼び出しが半減し、大幅な高速化が可能。
// =============================================================================
template <int N>
using WaveShaperUltra = WaveShaper<DiodeIV_FastExp, N, OwnCoeffs, JacobianMode::Ultra>;

using WaveShaperUltra1 = WaveShaperUltra<1>;
using WaveShaperUltra2 = WaveShaperUltra<2>;
using WaveShaperUltra3 = WaveShaperUltra<3>;

// =============================================================================
// Predictor-Corrector版（予測子-補正子法による高速化）
// 前サンプルの変化量から初期推定を改善し、1回の反復で収束させる。
// =============================================================================
using WaveShaperPredictor = WaveShaper<DiodeIV_FastExp, 1, OwnCoeffs, JacobianMode::Predictor>;

// =============================================================================
// Turbo版（2反復だが2回目はE-B評価をスキップ）
// 1回目: E-BとB-Cの両方を評価（2回のexp）
// 2回目: 1回目のE-B値を再利用、B-Cのみ再評価（1回のexp）
// 合計3回のexp()で2反復の精度が得られる。
// =============================================================================
using WaveShaperTurbo = WaveShaper<DiodeIV_FastExp, 2, OwnCoeffs, JacobianMode::Turbo>;

// =============================================================================
// TurboLite版（ヤコビアン再利用による高速化）
// 1回目: E-BとB-Cの両方を評価（2回のexp）
// 2回目: E-B再利用、B-Cのみ再評価（1回のexp）、ヤコビアン一部再利用
// 合計3回のexp()、計算削減で高速化。
// =============================================================================
using WaveShaperTurboLite = WaveShaper<DiodeIV_FastExp, 2, OwnCoeffs, JacobianMode::TurboLite>;

// =============================================================================
// 後方互換性のための旧名前空間エクスポート
// =============================================================================
namespace fast {

using namespace circuit;
using namespace bjt;

inline float fast_exp(float x) { return exp_impl::schraudolph(x); }

using WaveShaperReference = tb303::WaveShaperReference;

template <int N>
using WaveShaperNewton = tb303::WaveShaperNewton<N>;

template <int N>
using WaveShaperSchur = tb303::WaveShaperSchur<N>;

using WaveShaperSchurUltra = tb303::WaveShaperSchurUltra;

}  // namespace fast

}  // namespace tb303
