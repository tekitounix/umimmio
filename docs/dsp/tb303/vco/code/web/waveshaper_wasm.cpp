// waveshaper_wasm.cpp - WaveShaperSchur for WebAssembly
// Compile: emcc waveshaper_wasm.cpp -O3 -s WASM=1 -s EXPORTED_FUNCTIONS='["_ws_create","_ws_destroy","_ws_reset","_ws_set_sample_rate","_ws_process_block"]' -s EXPORTED_RUNTIME_METHODS='["cwrap"]' -o waveshaper.js

#include <cmath>
#include <cstdint>
#include <algorithm>

extern "C" {

// 回路定数
constexpr float V_CC = 12.0f;
constexpr float V_COLL = 5.33f;
constexpr float R2 = 100e3f;
constexpr float R3 = 10e3f;
constexpr float R4 = 22e3f;
constexpr float R5 = 10e3f;
constexpr float C1 = 10e-9f;
constexpr float C2 = 1e-6f;

constexpr float V_T = 0.025865f;
constexpr float V_T_INV = 1.0f / V_T;
constexpr float I_S = 1e-13f;
constexpr float BETA_F = 100.0f;
constexpr float ALPHA_F = BETA_F / (BETA_F + 1.0f);
constexpr float ALPHA_R = 0.5f / 1.5f;
constexpr float V_CRIT = V_T * 40.0f;

constexpr float G2 = 1.0f / R2;
constexpr float G3 = 1.0f / R3;
constexpr float G4 = 1.0f / R4;
constexpr float G5 = 1.0f / R5;

inline float clamp(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

inline float fast_exp(float x) {
    x = clamp(x, -87.0f, 88.0f);
    union { float f; int32_t i; } u;
    constexpr float LOG2E = 1.4426950408889634f;
    constexpr float SHIFT = (1 << 23) * 127.0f;
    constexpr float SCALE = (1 << 23) * LOG2E;
    u.i = static_cast<int32_t>(SCALE * x + SHIFT);
    float t = x - static_cast<float>(u.i - static_cast<int32_t>(SHIFT)) / SCALE;
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

struct WaveShaperSchur {
    float v_c1 = 0.0f, v_c2 = 8.0f;
    float v_b = 8.0f, v_e = 8.0f, v_c = V_COLL;
    float dt = 1.0f / 48000.0f;
    float g_c1, g_c2;
    float inv_j11, schur_j11_factor, schur_f1_factor;

    void setSampleRate(float sr) {
        dt = 1.0f / sr;
        g_c1 = C1 / dt;
        g_c2 = C2 / dt;
        float j11 = -g_c1 - G3;
        inv_j11 = 1.0f / j11;
        schur_j11_factor = G3 * G3 * inv_j11;
        schur_f1_factor = G3 * inv_j11;
    }

    void reset() {
        v_c1 = 0.0f; v_c2 = 8.0f;
        v_b = 8.0f; v_e = 8.0f; v_c = V_COLL;
    }

    float process(float v_in) {
        float v_c1_prev = v_c1;
        float v_c2_prev = v_c2;
        float v_cap = v_in - v_c1_prev;
        float vb = v_b, ve = v_c2_prev, vc = v_c;

        float v_eb = ve - vb;
        float v_cb = vc - vb;
        float i_ef, g_ef, i_cr, g_cr;
        diode_iv(v_eb, i_ef, g_ef);
        diode_iv(v_cb, i_cr, g_cr);

        float i_e = i_ef - ALPHA_R * i_cr;
        float i_c = ALPHA_F * i_ef - i_cr;
        float i_b = i_e - i_c;

        float f1 = g_c1 * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - vb);
        float f2 = G2 * (v_in - vb) + G3 * (v_cap - vb) + i_b;
        float f3 = G4 * (V_CC - ve) - i_e - g_c2 * (ve - v_c2_prev);
        float f4 = G5 * (V_COLL - vc) + i_c;

        float j22 = -G2 - G3 - (1.0f - ALPHA_F) * g_ef - (1.0f - ALPHA_R) * g_cr;
        float j23 = (1.0f - ALPHA_F) * g_ef;
        float j24 = (1.0f - ALPHA_R) * g_cr;
        float j32 = g_ef - ALPHA_R * g_cr;
        float j33 = -G4 - g_ef - g_c2;
        float j34 = ALPHA_R * g_cr;
        float j42 = -ALPHA_F * g_ef + g_cr;
        float j43 = ALPHA_F * g_ef;
        float j44 = -G5 - g_cr;

        float j22_p = j22 - schur_j11_factor;
        float f2_p = f2 - schur_f1_factor * f1;

        float inv_j44 = 1.0f / j44;
        float j24_inv_j44 = j24 * inv_j44;
        float j22_pp = j22_p - j24_inv_j44 * j42;
        float j23_pp = j23 - j24_inv_j44 * j43;
        float f2_pp = f2_p + j24_inv_j44 * f4;

        float j34_inv_j44 = j34 * inv_j44;
        float j32_pp = j32 - j34_inv_j44 * j42;
        float j33_pp = j33 - j34_inv_j44 * j43;
        float f3_pp = f3 + j34_inv_j44 * f4;

        float det = j22_pp * j33_pp - j23_pp * j32_pp;
        if (det > -1e-15f && det < 1e-15f) {
            v_c1 = v_in - v_cap;
            v_c2 = ve;
            return vc;
        }

        float inv_det = 1.0f / det;
        float dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det;
        float dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det;
        float dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44;
        float dv_cap = (-f1 - G3 * dv_b) * inv_j11;

        float max_dv = dv_cap; if (max_dv < 0) max_dv = -max_dv;
        float t = dv_b < 0 ? -dv_b : dv_b; if (t > max_dv) max_dv = t;
        t = dv_e < 0 ? -dv_e : dv_e; if (t > max_dv) max_dv = t;
        t = dv_c < 0 ? -dv_c : dv_c; if (t > max_dv) max_dv = t;
        float damp = max_dv > 0.5f ? 0.5f / max_dv : 1.0f;

        v_cap += damp * dv_cap;
        vb += damp * dv_b;
        ve = clamp(ve + damp * dv_e, 0.0f, V_CC + 0.5f);
        vc = clamp(vc + damp * dv_c, 0.0f, V_CC + 0.5f);

        v_c1 = v_in - v_cap;
        v_c2 = ve;
        v_b = vb;
        v_e = ve;
        v_c = vc;
        return vc;
    }
};

WaveShaperSchur* ws_create() {
    auto* ws = new WaveShaperSchur();
    ws->setSampleRate(48000.0f);
    return ws;
}

void ws_destroy(WaveShaperSchur* ws) {
    delete ws;
}

void ws_reset(WaveShaperSchur* ws) {
    ws->reset();
}

void ws_set_sample_rate(WaveShaperSchur* ws, float sr) {
    ws->setSampleRate(sr);
}

void ws_process_block(WaveShaperSchur* ws, float* input, float* output, int len) {
    for (int i = 0; i < len; i++) {
        output[i] = ws->process(input[i]);
    }
}

// Simple allocator for WASM
static char heap[65536];
static int heap_offset = 0;

void* ws_malloc(int size) {
    size = (size + 7) & ~7; // 8-byte align
    if (heap_offset + size > 65536) return nullptr;
    void* ptr = &heap[heap_offset];
    heap_offset += size;
    return ptr;
}

void ws_free(void* ptr) {
    // no-op for this simple allocator
}

} // extern "C"
