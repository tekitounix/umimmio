// =============================================================================
// TB-303 WaveShaper Python Bindings (pybind11)
//
// C++実装をPythonから直接使用可能にするバインディング
// ビルド: clang++ -std=c++20 -O3 -shared -fPIC -undefined dynamic_lookup \
//         $(python3 -m pybind11 --includes) waveshaper_pybind.cpp \
//         -o tb303_waveshaper$(python3-config --extension-suffix)
//
// 使用法: import tb303_waveshaper as ws
//
// Author: Claude (Anthropic)
// License: MIT
// =============================================================================

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "tb303_waveshaper.hpp"
#include "tb303_oscillator.hpp"

namespace py = pybind11;

// =============================================================================
// NumPy配列処理用ヘルパー
// =============================================================================
template <typename WaveShaperT>
py::array_t<float> process_array(WaveShaperT& ws, py::array_t<float> input) {
    auto buf = input.request();
    if (buf.ndim != 1) {
        throw std::runtime_error("Input must be a 1-dimensional array");
    }

    size_t n = buf.size;
    float* in_ptr = static_cast<float*>(buf.ptr);

    auto result = py::array_t<float>(n);
    auto result_buf = result.request();
    float* out_ptr = static_cast<float*>(result_buf.ptr);

    for (size_t i = 0; i < n; ++i) {
        out_ptr[i] = ws.process(in_ptr[i]);
    }

    return result;
}

// =============================================================================
// Python Module Definition
// =============================================================================
PYBIND11_MODULE(tb303_waveshaper, m) {
    m.doc() = R"pbdoc(
        TB-303 WaveShaper DSP Module
        ----------------------------

        C++ implementation of the TB-303 wave shaper circuit.

        Classes:
            WaveShaperReference: High-precision reference (100 iterations, std::exp)
            WaveShaperNewton1/2/3: Newton solver with N iterations
            WaveShaperFast1/2: Fast solver with relaxed damping
            WaveShaperTurbo: 2 iterations with E-B junction reuse (3 exp calls)
            WaveShaperTurboLite: Turbo with Jacobian element reuse

        Example:
            >>> import tb303_waveshaper as ws
            >>> shaper = ws.WaveShaperTurbo()
            >>> shaper.set_sample_rate(48000.0)
            >>> shaper.reset()
            >>> output = shaper.process(9.0)
    )pbdoc";

    // =========================================================================
    // 回路定数エクスポート (TB-303 回路図準拠)
    // =========================================================================
    m.attr("V_T") = tb303::bjt::V_T;
    m.attr("I_S") = tb303::bjt::I_S;
    m.attr("BETA_F") = tb303::bjt::BETA_F;
    m.attr("ALPHA_F") = tb303::bjt::ALPHA_F;
    m.attr("BETA_R") = tb303::bjt::BETA_R;
    m.attr("ALPHA_R") = tb303::bjt::ALPHA_R;
    m.attr("V_CC") = tb303::circuit::V_CC;
    m.attr("V_BIAS") = tb303::circuit::V_BIAS;
    m.attr("R34") = tb303::circuit::R34;
    m.attr("R35") = tb303::circuit::R35;
    m.attr("R36") = tb303::circuit::R36;
    m.attr("R45") = tb303::circuit::R45;
    m.attr("C10") = tb303::circuit::C10;
    m.attr("C11") = tb303::circuit::C11;

    // =========================================================================
    // WaveShaperReference (高精度リファレンス: 100回反復, std::exp)
    // =========================================================================
    py::class_<tb303::WaveShaperReference>(m, "WaveShaperReference",
        "High-precision reference solver (100 iterations, std::exp)")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::WaveShaperReference::setSampleRate,
             py::arg("sample_rate"),
             "Set the sample rate in Hz")
        .def("reset", &tb303::WaveShaperReference::reset,
             "Reset internal state to initial values")
        .def("process", py::overload_cast<float>(&tb303::WaveShaperReference::process),
             py::arg("v_in"),
             "Process a single sample, returns output voltage")
        .def("process_array",
             [](tb303::WaveShaperReference& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"),
             "Process an array of samples, returns output array");

    // =========================================================================
    // WaveShaperNewton<N> (Newton法 N回反復)
    // =========================================================================
    py::class_<tb303::WaveShaperNewton<1>>(m, "WaveShaperNewton1",
        "Newton solver with 1 iteration")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::WaveShaperNewton<1>::setSampleRate,
             py::arg("sample_rate"))
        .def("reset", &tb303::WaveShaperNewton<1>::reset)
        .def("process", py::overload_cast<float>(&tb303::WaveShaperNewton<1>::process),
             py::arg("v_in"))
        .def("process_array",
             [](tb303::WaveShaperNewton<1>& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"));

    py::class_<tb303::WaveShaperNewton<2>>(m, "WaveShaperNewton2",
        "Newton solver with 2 iterations")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::WaveShaperNewton<2>::setSampleRate,
             py::arg("sample_rate"))
        .def("reset", &tb303::WaveShaperNewton<2>::reset)
        .def("process", py::overload_cast<float>(&tb303::WaveShaperNewton<2>::process),
             py::arg("v_in"))
        .def("process_array",
             [](tb303::WaveShaperNewton<2>& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"));

    py::class_<tb303::WaveShaperNewton<3>>(m, "WaveShaperNewton3",
        "Newton solver with 3 iterations")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::WaveShaperNewton<3>::setSampleRate,
             py::arg("sample_rate"))
        .def("reset", &tb303::WaveShaperNewton<3>::reset)
        .def("process", py::overload_cast<float>(&tb303::WaveShaperNewton<3>::process),
             py::arg("v_in"))
        .def("process_array",
             [](tb303::WaveShaperNewton<3>& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"));

    // =========================================================================
    // WaveShaperSchur<N> はNewton<N>と同じ型なので登録不要
    // エイリアスとしてSchur2を追加（推奨設定）
    // =========================================================================
    m.attr("WaveShaperSchur1") = m.attr("WaveShaperNewton1");
    m.attr("WaveShaperSchur2") = m.attr("WaveShaperNewton2");

    // SchurUltra は Newton<2> と同一型
    m.attr("WaveShaperSchurUltra") = m.attr("WaveShaperNewton2");

    // =========================================================================
    // WaveShaperFast (ダンピング除去版)
    // =========================================================================
    py::class_<tb303::WaveShaperFast1>(m, "WaveShaperFast1",
        "Fast solver (no damping, 1 iteration)")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::WaveShaperFast1::setSampleRate,
             py::arg("sample_rate"))
        .def("reset", &tb303::WaveShaperFast1::reset)
        .def("process", py::overload_cast<float>(&tb303::WaveShaperFast1::process),
             py::arg("v_in"))
        .def("process_array",
             [](tb303::WaveShaperFast1& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"));

    py::class_<tb303::WaveShaperFast2>(m, "WaveShaperFast2",
        "Fast solver (no damping, 2 iterations)")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::WaveShaperFast2::setSampleRate,
             py::arg("sample_rate"))
        .def("reset", &tb303::WaveShaperFast2::reset)
        .def("process", py::overload_cast<float>(&tb303::WaveShaperFast2::process),
             py::arg("v_in"))
        .def("process_array",
             [](tb303::WaveShaperFast2& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"));

    // =========================================================================
    // WaveShaperHybrid (1回目固定ダンピング + 2回目通常ダンピング)
    // =========================================================================
    py::class_<tb303::WaveShaperHybrid>(m, "WaveShaperHybrid",
        "Hybrid solver (fast 1st iter + normal 2nd iter)")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::WaveShaperHybrid::setSampleRate,
             py::arg("sample_rate"))
        .def("reset", &tb303::WaveShaperHybrid::reset)
        .def("process", py::overload_cast<float>(&tb303::WaveShaperHybrid::process),
             py::arg("v_in"))
        .def("process_array",
             [](tb303::WaveShaperHybrid& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"));

    // =========================================================================
    // WaveShaperTurbo (2反復だが2回目E-Bスキップ)
    // =========================================================================
    py::class_<tb303::WaveShaperTurbo>(m, "WaveShaperTurbo",
        "Turbo solver (2 iters with E-B reuse on 2nd, 3 exp calls)")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::WaveShaperTurbo::setSampleRate,
             py::arg("sample_rate"))
        .def("reset", &tb303::WaveShaperTurbo::reset)
        .def("process", py::overload_cast<float>(&tb303::WaveShaperTurbo::process),
             py::arg("v_in"))
        .def("process_array",
             [](tb303::WaveShaperTurbo& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"));

    // =========================================================================
    // WaveShaperTurboLite (ヤコビアン再利用による高速化)
    // =========================================================================
    py::class_<tb303::WaveShaperTurboLite>(m, "WaveShaperTurboLite",
        "TurboLite solver (2 iters with E-B reuse, Jacobian reuse, 3 exp calls)")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::WaveShaperTurboLite::setSampleRate,
             py::arg("sample_rate"))
        .def("reset", &tb303::WaveShaperTurboLite::reset)
        .def("process", py::overload_cast<float>(&tb303::WaveShaperTurboLite::process),
             py::arg("v_in"))
        .def("process_array",
             [](tb303::WaveShaperTurboLite& self, py::array_t<float> input) {
                 return process_array(self, input);
             },
             py::arg("input"));

    // =========================================================================
    // ユーティリティ関数
    // =========================================================================
    m.def("fast_exp", &tb303::exp_impl::schraudolph,
          py::arg("x"),
          "Fast exponential approximation (Schraudolph improved)");

    // =========================================================================
    // PolyBLEP Sawtooth Oscillator
    // =========================================================================
    py::class_<tb303::vco::SawOscillator>(m, "SawOscillator",
        "PolyBLEP anti-aliased sawtooth oscillator (12V -> 5.5V)")
        .def(py::init<>())
        .def("set_sample_rate", &tb303::vco::SawOscillator::setSampleRate,
             py::arg("sample_rate"))
        .def("set_frequency", &tb303::vco::SawOscillator::setFrequency,
             py::arg("freq"))
        .def("get_frequency", &tb303::vco::SawOscillator::getFrequency)
        .def("reset", &tb303::vco::SawOscillator::reset)
        .def("process", &tb303::vco::SawOscillator::process,
             "Process one sample (2nd order PolyBLEP)")
        .def("process_hq", &tb303::vco::SawOscillator::processHQ,
             "Process one sample (4th order PolyBLEP, higher quality)")
        .def("process_naive", &tb303::vco::SawOscillator::processNaive,
             "Process one sample (no antialiasing, for comparison)")
        .def("process_array",
             [](tb303::vco::SawOscillator& self, int n_samples) {
                 auto result = py::array_t<float>(n_samples);
                 auto buf = result.request();
                 float* ptr = static_cast<float*>(buf.ptr);
                 for (int i = 0; i < n_samples; ++i) {
                     ptr[i] = self.process();
                 }
                 return result;
             },
             py::arg("n_samples"),
             "Generate n_samples of sawtooth wave (2nd order PolyBLEP)")
        .def("process_naive_array",
             [](tb303::vco::SawOscillator& self, int n_samples) {
                 auto result = py::array_t<float>(n_samples);
                 auto buf = result.request();
                 float* ptr = static_cast<float*>(buf.ptr);
                 for (int i = 0; i < n_samples; ++i) {
                     ptr[i] = self.processNaive();
                 }
                 return result;
             },
             py::arg("n_samples"),
             "Generate n_samples of sawtooth wave (no antialiasing)")
        .def("process_hq_array",
             [](tb303::vco::SawOscillator& self, int n_samples) {
                 auto result = py::array_t<float>(n_samples);
                 auto buf = result.request();
                 float* ptr = static_cast<float*>(buf.ptr);
                 for (int i = 0; i < n_samples; ++i) {
                     ptr[i] = self.processHQ();
                 }
                 return result;
             },
             py::arg("n_samples"),
             "Generate n_samples of sawtooth wave (4th order PolyBLEP)");
}
