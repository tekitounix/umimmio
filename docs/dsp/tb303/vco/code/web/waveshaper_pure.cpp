// waveshaper_pure.cpp - Pure WASM (no emscripten runtime)
// Based on WaveShaper3Var<2> from tb303_waveshaper_fast.hpp
// Build: emcc waveshaper_pure.cpp -O2 -s WASM=1 -s EXPORTED_FUNCTIONS="['_ws_init','_ws_set_sample_rate','_ws_set_c2','_ws_reset','_ws_get_input_ptr','_ws_get_output_ptr','_ws_process_block']" -s SIDE_MODULE=0 --no-entry -o waveshaper.wasm

#define WASM_EXPORT __attribute__((visibility("default")))

extern "C" {

// 回路定数
constexpr float V_CC = 12.0f;
constexpr float V_COLL = 5.33f;
constexpr float C1 = 10e-9f;

// 2SA733P トランジスタパラメータ (TB-303サービスノート準拠)
// 注: I_S, BETA_F のバラつきは音色への影響が知覚困難なレベルのため固定値を採用
constexpr float V_T = 0.025865f;                        // 熱電圧 @ 25℃
constexpr float V_T_INV = 1.0f / V_T;
constexpr float V_CRIT = V_T * 40.0f;

constexpr float I_S = 5e-14f;                           // 飽和電流 (SPICEモデル中央値)
constexpr float BETA_F = 300.0f;                        // 順方向β (Pランク: 200-400)
constexpr float ALPHA_F = BETA_F / (BETA_F + 1.0f);     // ≈ 0.9967
constexpr float BETA_R = 0.1f;                          // 逆方向β (MACOM実測)
constexpr float ALPHA_R = BETA_R / (BETA_R + 1.0f);     // ≈ 0.0909

constexpr float G2 = 1.0f / 100e3f;
constexpr float G3 = 1.0f / 10e3f;
constexpr float G4 = 1.0f / 22e3f;
constexpr float G5 = 1.0f / 10e3f;

inline float clamp(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

inline float fabsf(float x) {
    return x < 0 ? -x : x;
}

inline float maxf(float a, float b) {
    return a > b ? a : b;
}

inline float fast_exp(float x) {
    x = clamp(x, -87.0f, 88.0f);
    union { float f; int i; } u;
    constexpr float LOG2E = 1.4426950408889634f;
    constexpr float SHIFT = (1 << 23) * 127.0f;
    constexpr float SCALE = (1 << 23) * LOG2E;
    u.i = (int)(SCALE * x + SHIFT);
    float t = x - (float)(u.i - (int)SHIFT) / SCALE;
    u.f *= 1.0f + t * (1.0f + t * 0.5f);
    return u.f;
}

inline void diode_iv(float v, float& i, float& g) {
    if (v > V_CRIT) {
        float exp_crit = fast_exp(V_CRIT * V_T_INV);
        g = I_S * V_T_INV * exp_crit;
        i = I_S * (exp_crit - 1.0f) + g * (v - V_CRIT);
    } else if (v < -10.0f * V_T) {
        i = -I_S;
        g = 1e-12f;
    } else {
        float exp_v = fast_exp(v * V_T_INV);
        i = I_S * (exp_v - 1.0f);
        g = I_S * V_T_INV * exp_v + 1e-12f;
    }
}

// WaveShaper3Var: 3変数Newton法 (v_capはv_bの線形関数として消去)
struct WaveShaper3Var {
    float v_c1, v_c2, v_b, v_e, v_c;
    float dt, g_c1, g_c2;
    float den, inv_den, k, j22_linear;
    float c2;

    void init() {
        v_c1 = 0.0f; v_c2 = 8.0f;
        v_b = 8.0f; v_e = 8.0f; v_c = V_COLL;
        dt = 1.0f / 48000.0f;
        c2 = 1e-6f;
        updateCoeffs();
    }

    void updateCoeffs() {
        g_c1 = C1 / dt;
        g_c2 = c2 / dt;

        // v_cap消去用定数
        // v_cap = (g_c1*(v_in - v_c1_prev) + G3*v_b) / (g_c1 + G3)
        den = g_c1 + G3;
        inv_den = 1.0f / den;
        k = G3 * inv_den;  // dv_cap/dv_b

        // f2の定数部分: df2/dv_b の線形部分 = -G2 + G3*(k - 1)
        j22_linear = -G2 + G3 * (k - 1.0f);
    }

    void setSampleRate(float sr) {
        dt = 1.0f / sr;
        updateCoeffs();
    }

    void setC2(float c2_uF) {
        c2 = c2_uF * 1e-6f;
        updateCoeffs();
    }

    void reset() {
        v_c1 = 0.0f; v_c2 = 8.0f;
        v_b = 8.0f; v_e = 8.0f; v_c = V_COLL;
    }

    void newton_step(float v_in, float v_c2_prev, float B,
                     float& vb, float& ve, float& vc) {
        // ダイオード評価
        float i_ef, g_ef, i_cr, g_cr;
        diode_iv(ve - vb, i_ef, g_ef);
        diode_iv(vc - vb, i_cr, g_cr);

        // Ebers-Moll電流
        const float i_e = i_ef - ALPHA_R * i_cr;
        const float i_c = ALPHA_F * i_ef - i_cr;
        const float i_b = i_e - i_c;

        // 3変数残差
        const float f2 = G2 * v_in + G3 * B * inv_den + j22_linear * vb + i_b;
        const float f3 = G4 * (V_CC - ve) - i_e - g_c2 * (ve - v_c2_prev);
        const float f4 = G5 * (V_COLL - vc) + i_c;

        // 3x3 ヤコビアン
        const float j22 = j22_linear - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        const float j23 = (1.0f - ALPHA_F) * g_ef;
        const float j24 = (1.0f - ALPHA_R) * g_cr;
        const float j32 = g_ef - ALPHA_R * g_cr;
        const float j33 = -G4 - g_ef - g_c2;
        const float j34 = ALPHA_R * g_cr;
        const float j42 = -ALPHA_F * g_ef + g_cr;
        const float j43 = ALPHA_F * g_ef;
        const float j44 = -G5 - g_cr;

        // 3x3 Gauss消去
        const float inv_j22 = 1.0f / j22;
        const float m32 = j32 * inv_j22;
        const float m42 = j42 * inv_j22;

        const float j33_p = j33 - m32 * j23;
        const float j34_p = j34 - m32 * j24;
        const float f3_p = f3 + m32 * f2;

        const float j43_p = j43 - m42 * j23;
        const float j44_p = j44 - m42 * j24;
        const float f4_p = f4 + m42 * f2;

        // 2x2 Cramer
        const float det = j33_p * j44_p - j34_p * j43_p;
        const float inv_det = 1.0f / det;

        const float dv_e = (j44_p * (-f3_p) - j34_p * (-f4_p)) * inv_det;
        const float dv_c = (j33_p * (-f4_p) - j43_p * (-f3_p)) * inv_det;
        const float dv_b = (-f2 - j23 * dv_e - j24 * dv_c) * inv_j22;

        // ダンピング
        float max_dv = maxf(maxf(fabsf(dv_b), fabsf(dv_e)), fabsf(dv_c));
        float damp = (max_dv > 0.5f) ? 0.5f / max_dv : 1.0f;

        vb = clamp(vb + damp * dv_b, 0.0f, V_CC + 0.5f);
        ve = clamp(ve + damp * dv_e, 0.0f, V_CC + 0.5f);
        vc = clamp(vc + damp * dv_c, 0.0f, V_CC + 0.5f);
    }

    float process(float v_in) {
        const float v_c1_prev = v_c1;
        const float v_c2_prev = v_c2;

        float vb = v_b;
        float ve = v_c2_prev;
        float vc = v_c;

        // B = g_c1 * (v_in - v_c1_prev)
        const float B = g_c1 * (v_in - v_c1_prev);

        // 2回Newton反復
        newton_step(v_in, v_c2_prev, B, vb, ve, vc);
        newton_step(v_in, v_c2_prev, B, vb, ve, vc);

        // v_cap更新（新しいv_bから再計算）
        const float v_cap_new = (B + G3 * vb) * inv_den;

        // 状態更新
        v_c1 = v_in - v_cap_new;
        v_c2 = ve;
        v_b = vb;
        v_e = ve;
        v_c = vc;

        return vc;
    }
};

// Global instance and buffers
static WaveShaper3Var ws;
static float inputBuf[128];
static float outputBuf[128];

WASM_EXPORT void ws_init() {
    ws.init();
}

WASM_EXPORT void ws_set_sample_rate(float sr) {
    ws.setSampleRate(sr);
}

WASM_EXPORT void ws_set_c2(float c2_uF) {
    ws.setC2(c2_uF);
}

WASM_EXPORT void ws_reset() {
    ws.reset();
}

WASM_EXPORT float* ws_get_input_ptr() {
    return inputBuf;
}

WASM_EXPORT float* ws_get_output_ptr() {
    return outputBuf;
}

WASM_EXPORT void ws_process_block(int len) {
    for (int i = 0; i < len; i++) {
        outputBuf[i] = ws.process(inputBuf[i]);
    }
}

} // extern "C"
