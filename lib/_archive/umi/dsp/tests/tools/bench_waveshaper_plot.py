#!/usr/bin/env python3
"""
TB-303 WaveShaper Benchmark Visualization

Compares different WaveShaper implementations:
- WaveShaperSchur (baseline, fast_exp)
- WaveShaperSchurLambertW (omega_fast2)
- Reference waveforms

Also plots benchmark results from Cortex-M4 measurements.
"""

import numpy as np
import matplotlib.pyplot as plt
from dataclasses import dataclass
from typing import Tuple

# Circuit constants (TB-303)
V_CC = 12.0
V_COLL = 5.33
R2 = 100e3
R3 = 10e3
R4 = 22e3
R5 = 10e3
C1 = 10e-9
C2 = 1e-6

# Transistor parameters
V_T = 0.025865
V_T_INV = 1.0 / V_T
I_S = 1e-13
BETA_F = 100.0
ALPHA_F = BETA_F / (BETA_F + 1.0)
ALPHA_R = 0.5 / 1.5
V_CRIT = V_T * 40.0

# Conductances
G2 = 1.0 / R2
G3 = 1.0 / R3
G4 = 1.0 / R4
G5 = 1.0 / R5


def fast_exp(x: np.ndarray) -> np.ndarray:
    """Fast exp approximation (clipped)"""
    x = np.clip(x, -87.0, 88.0)
    return np.exp(x)


def diode_iv(v: float) -> Tuple[float, float]:
    """Diode I-V using fast_exp (baseline)"""
    if v > V_CRIT:
        exp_crit = fast_exp(V_CRIT * V_T_INV)
        g = I_S * V_T_INV * exp_crit
        i = I_S * (exp_crit - 1.0) + g * (v - V_CRIT)
    elif v < -10.0 * V_T:
        i = -I_S
        g = 1e-12
    else:
        exp_v = fast_exp(v * V_T_INV)
        i = I_S * (exp_v - 1.0)
        g = I_S * V_T_INV * exp_v + 1e-12
    return i, g


def omega3(x: float) -> float:
    """Wright Omega polynomial approximation (DAFx2019 style)"""
    if x < -3.341459552768620:
        return np.exp(x)
    elif x < 8.0:
        y = x + 1.0
        return 0.6314 + y * (0.3632 + y * (0.04776 + y * (-0.001314)))
    else:
        return x - np.log(x)


def omega4(x: float) -> float:
    """Wright Omega with Newton-Raphson correction"""
    w = omega3(x)
    # One Newton iteration
    lnw = np.log(max(w, 1e-10))
    r = x - w - lnw
    return w * (1.0 + r / (1.0 + w))


def omega_fast2(x: float) -> float:
    """
    Optimized Wright Omega for diode applications.
    Uses omega4 with Newton correction for accuracy.
    """
    return omega4(x)


def diode_iv_lambertw(v: float) -> Tuple[float, float]:
    """Diode I-V using Lambert W / omega_fast2 (no Newton iteration)"""
    Is_Vt_ln = -29.9  # ln(1e-13 / 0.025865)
    x = Is_Vt_ln + v * V_T_INV

    if x < -10.0:
        return -I_S, 1e-12

    w = omega_fast2(x)
    i = V_T * w - I_S
    g = (w / (1.0 + w) + 1e-12) if w > 1e-10 else 1e-12
    return i, g


@dataclass
class WaveShaperState:
    v_c1: float = 0.0
    v_c2: float = 8.0
    v_b: float = 8.0
    v_e: float = 8.0
    v_c: float = V_COLL


class WaveShaperSchur:
    """Baseline Schur complement WaveShaper with fast_exp"""

    def __init__(self, sample_rate: float = 48000.0, n_iters: int = 2):
        self.dt = 1.0 / sample_rate
        self.g_c1 = C1 / self.dt
        self.g_c2 = C2 / self.dt
        self.n_iters = n_iters

        j11 = -self.g_c1 - G3
        self.inv_j11 = 1.0 / j11
        self.schur_j11_factor = G3 * G3 * self.inv_j11
        self.schur_f1_factor = G3 * self.inv_j11

        self.state = WaveShaperState()

    def reset(self):
        self.state = WaveShaperState()

    def process(self, v_in: float) -> float:
        s = self.state
        v_c1_prev = s.v_c1
        v_c2_prev = s.v_c2

        v_cap = v_in - v_c1_prev
        v_b = s.v_b
        v_e = v_c2_prev
        v_c = s.v_c

        # Diode evaluation (fast_exp)
        v_eb = v_e - v_b
        v_cb = v_c - v_b
        i_ef, g_ef = diode_iv(v_eb)
        i_cr, g_cr = diode_iv(v_cb)

        # Ebers-Moll currents
        i_e = i_ef - ALPHA_R * i_cr
        i_c = ALPHA_F * i_ef - i_cr
        i_b = i_e - i_c

        # KCL residuals
        f1 = self.g_c1 * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b)
        f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b
        f3 = G4 * (V_CC - v_e) - i_e - self.g_c2 * (v_e - v_c2_prev)
        f4 = G5 * (V_COLL - v_c) + i_c

        # Jacobian
        j22 = -G2 - G3 - (1.0 - ALPHA_F) * g_ef - (1.0 - ALPHA_R) * g_cr
        j23 = (1.0 - ALPHA_F) * g_ef
        j24 = (1.0 - ALPHA_R) * g_cr
        j32 = g_ef - ALPHA_R * g_cr
        j33 = -G4 - g_ef - self.g_c2
        j34 = ALPHA_R * g_cr
        j42 = -ALPHA_F * g_ef + g_cr
        j43 = ALPHA_F * g_ef
        j44 = -G5 - g_cr

        # Schur complement reduction
        j22_p = j22 - self.schur_j11_factor
        f2_p = f2 - self.schur_f1_factor * f1

        inv_j44 = 1.0 / j44
        j24_inv = j24 * inv_j44
        j34_inv = j34 * inv_j44

        j22_pp = j22_p - j24_inv * j42
        j23_pp = j23 - j24_inv * j43
        f2_pp = f2_p + j24_inv * f4

        j32_pp = j32 - j34_inv * j42
        j33_pp = j33 - j34_inv * j43
        f3_pp = f3 + j34_inv * f4

        # 2x2 Cramer's rule
        det = j22_pp * j33_pp - j23_pp * j32_pp
        inv_det = 1.0 / det

        dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det
        dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det

        # Back substitution
        dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44
        dv_cap = (-f1 - G3 * dv_b) * self.inv_j11

        # Damping
        max_dv = max(abs(dv_cap), abs(dv_b), abs(dv_e), abs(dv_c))
        damp = 0.5 / max_dv if max_dv > 0.5 else 1.0

        v_cap += damp * dv_cap
        v_b += damp * dv_b
        v_e = np.clip(v_e + damp * dv_e, 0.0, V_CC + 0.5)
        v_c = np.clip(v_c + damp * dv_c, 0.0, V_CC + 0.5)

        # Update state
        s.v_c1 = v_in - v_cap
        s.v_c2 = v_e
        s.v_b = v_b
        s.v_e = v_e
        s.v_c = v_c

        return v_c


class WaveShaperSchurLambertW:
    """Optimized Schur complement WaveShaper with omega_fast2 (Lambert W)"""

    def __init__(self, sample_rate: float = 48000.0):
        self.dt = 1.0 / sample_rate
        self.g_c1 = C1 / self.dt
        self.g_c2 = C2 / self.dt

        j11 = -self.g_c1 - G3
        self.inv_j11 = 1.0 / j11
        self.schur_j11_factor = G3 * G3 * self.inv_j11
        self.schur_f1_factor = G3 * self.inv_j11

        self.state = WaveShaperState()

    def reset(self):
        self.state = WaveShaperState()

    def process(self, v_in: float) -> float:
        s = self.state
        v_c1_prev = s.v_c1
        v_c2_prev = s.v_c2

        v_cap = v_in - v_c1_prev
        v_b = s.v_b
        v_e = v_c2_prev
        v_c = s.v_c

        # Diode evaluation (omega_fast2 / Lambert W)
        v_eb = v_e - v_b
        v_cb = v_c - v_b
        i_ef, g_ef = diode_iv_lambertw(v_eb)
        i_cr, g_cr = diode_iv_lambertw(v_cb)

        # Ebers-Moll currents
        i_e = i_ef - ALPHA_R * i_cr
        i_c = ALPHA_F * i_ef - i_cr
        i_b = i_e - i_c

        # KCL residuals
        f1 = self.g_c1 * (v_in - v_cap - v_c1_prev) - G3 * (v_cap - v_b)
        f2 = G2 * (v_in - v_b) + G3 * (v_cap - v_b) + i_b
        f3 = G4 * (V_CC - v_e) - i_e - self.g_c2 * (v_e - v_c2_prev)
        f4 = G5 * (V_COLL - v_c) + i_c

        # Jacobian
        j22 = -G2 - G3 - (1.0 - ALPHA_F) * g_ef - (1.0 - ALPHA_R) * g_cr
        j23 = (1.0 - ALPHA_F) * g_ef
        j24 = (1.0 - ALPHA_R) * g_cr
        j32 = g_ef - ALPHA_R * g_cr
        j33 = -G4 - g_ef - self.g_c2
        j34 = ALPHA_R * g_cr
        j42 = -ALPHA_F * g_ef + g_cr
        j43 = ALPHA_F * g_ef
        j44 = -G5 - g_cr

        # Schur complement reduction
        j22_p = j22 - self.schur_j11_factor
        f2_p = f2 - self.schur_f1_factor * f1

        inv_j44 = 1.0 / j44
        j24_inv = j24 * inv_j44
        j34_inv = j34 * inv_j44

        j22_pp = j22_p - j24_inv * j42
        j23_pp = j23 - j24_inv * j43
        f2_pp = f2_p + j24_inv * f4

        j32_pp = j32 - j34_inv * j42
        j33_pp = j33 - j34_inv * j43
        f3_pp = f3 + j34_inv * f4

        # 2x2 Cramer's rule
        det = j22_pp * j33_pp - j23_pp * j32_pp
        inv_det = 1.0 / det

        dv_b = (j33_pp * (-f2_pp) - j23_pp * (-f3_pp)) * inv_det
        dv_e = (j22_pp * (-f3_pp) - j32_pp * (-f2_pp)) * inv_det

        # Back substitution
        dv_c = (-f4 - j42 * dv_b - j43 * dv_e) * inv_j44
        dv_cap = (-f1 - G3 * dv_b) * self.inv_j11

        # Damping
        max_dv = max(abs(dv_cap), abs(dv_b), abs(dv_e), abs(dv_c))
        damp = 0.5 / max_dv if max_dv > 0.5 else 1.0

        v_cap += damp * dv_cap
        v_b += damp * dv_b
        v_e = np.clip(v_e + damp * dv_e, 0.0, V_CC + 0.5)
        v_c = np.clip(v_c + damp * dv_c, 0.0, V_CC + 0.5)

        # Update state
        s.v_c1 = v_in - v_cap
        s.v_c2 = v_e
        s.v_b = v_b
        s.v_e = v_e
        s.v_c = v_c

        return v_c


def generate_sawtooth(freq: float, sample_rate: float, duration: float) -> np.ndarray:
    """Generate sawtooth wave (TB-303 VCO style)"""
    n_samples = int(sample_rate * duration)
    t = np.arange(n_samples) / sample_rate
    # Sawtooth from 12V to 5.5V (TB-303 style)
    phase = (t * freq) % 1.0
    return 12.0 - phase * 6.5


def plot_waveform_comparison():
    """Plot waveform comparison - single implementation showing TB-303 characteristic"""
    sample_rate = 48000.0
    freq = 40.0  # 40 Hz
    duration = 0.1  # 100ms (4 cycles)

    # Generate input
    v_in = generate_sawtooth(freq, sample_rate, duration)
    n_samples = len(v_in)
    t = np.arange(n_samples) / sample_rate * 1000  # ms

    # Process with WaveShaper
    ws = WaveShaperSchur(sample_rate)
    out = np.zeros(n_samples)

    for i in range(n_samples):
        out[i] = ws.process(v_in[i])

    # Plot
    fig, axes = plt.subplots(2, 1, figsize=(12, 8))

    # Input waveform
    axes[0].plot(t, v_in, 'b-', linewidth=1.5)
    axes[0].set_ylabel('Input (V)')
    axes[0].set_title('TB-303 WaveShaper: Input Sawtooth (40 Hz)')
    axes[0].set_xlim([0, t[-1]])
    axes[0].grid(True, alpha=0.3)
    axes[0].set_ylim([5, 13])

    # Output waveform
    axes[1].plot(t, out, 'r-', linewidth=1.5)
    axes[1].set_xlabel('Time (ms)')
    axes[1].set_ylabel('Output (V)')
    axes[1].set_title('Collector Voltage Output (Ebers-Moll BJT Model)')
    axes[1].set_xlim([0, t[-1]])
    axes[1].grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig('build/waveshaper_waveform_comparison.png', dpi=150)
    plt.close()
    print("Saved: build/waveshaper_waveform_comparison.png")


def plot_benchmark_results():
    """Plot benchmark results from Cortex-M4 measurements"""
    # Benchmark data from Renode measurements
    implementations = [
        'SquareShaper\n(PNP approx)',
        'WaveShaperFast\n(Forward Euler)',
        'SchurLambertW\n(omega_fast2)',
        'WaveShaperSchur\n(baseline)',
        'WaveShaperSchurMo\n(mo::pow2)',
        'SchurUltra\n(BC delayed)',
        'WDFFull\n(Lambert W)',
        'WaveShaperWDF\n(DiodePair)',
        'SchurTable\n(Meijer LUT)',
    ]

    cycles = [174, 208, 320, 364, 370, 372, 379, 384, 413]

    # Color coding
    colors = []
    for impl in implementations:
        if 'SquareShaper' in impl or 'Fast' in impl:
            colors.append('#888888')  # Gray for non-Ebers-Moll
        elif 'LambertW' in impl:
            colors.append('#2ecc71')  # Green for optimized
        elif 'baseline' in impl:
            colors.append('#3498db')  # Blue for baseline
        elif 'WDF' in impl:
            colors.append('#9b59b6')  # Purple for WDF
        else:
            colors.append('#e74c3c')  # Red for slower

    fig, ax = plt.subplots(figsize=(12, 7))

    bars = ax.barh(implementations, cycles, color=colors)

    # Add value labels
    for bar, cyc in zip(bars, cycles):
        ax.text(bar.get_width() + 5, bar.get_y() + bar.get_height()/2,
                f'{cyc}', va='center', fontsize=10)

    # Add baseline reference line
    ax.axvline(x=364, color='#3498db', linestyle='--', alpha=0.7, label='Baseline (364 cycles)')

    # Add RT ratio axis on top
    ax2 = ax.twiny()
    xlim = ax.get_xlim()
    ax2.set_xlim([3500/max(xlim[1], 1), 3500/max(xlim[0], 1)])
    ax2.set_xlabel('Real-time Ratio @168MHz/48kHz')

    ax.set_xlabel('Cycles per Sample')
    ax.set_title('TB-303 WaveShaper Benchmark (Cortex-M4, 168MHz)')
    ax.legend(loc='lower right')
    ax.set_xlim([0, 450])
    ax.grid(True, axis='x', alpha=0.3)

    plt.tight_layout()
    plt.savefig('build/waveshaper_benchmark.png', dpi=150)
    plt.close()
    print("Saved: build/waveshaper_benchmark.png")


def plot_omega_comparison():
    """Plot Wright Omega function approximations"""
    x = np.linspace(-5, 15, 1000)

    # Reference (scipy)
    from scipy.special import lambertw
    omega_ref = np.real(lambertw(np.exp(x)))

    # omega3 (polynomial only) and omega4 (with Newton)
    omega3_approx = np.array([omega3(xi) for xi in x])
    omega4_approx = np.array([omega4(xi) for xi in x])

    # Errors
    rel_error_3 = np.abs(omega3_approx - omega_ref) / (np.abs(omega_ref) + 1e-10) * 100
    rel_error_4 = np.abs(omega4_approx - omega_ref) / (np.abs(omega_ref) + 1e-10) * 100

    fig, axes = plt.subplots(2, 1, figsize=(12, 9))

    # Function comparison
    axes[0].plot(x, omega_ref, 'b-', linewidth=2.5, label='Reference (scipy)', alpha=0.7)
    axes[0].plot(x, omega3_approx, 'g--', linewidth=1.5, label='omega3 (polynomial)')
    axes[0].plot(x, omega4_approx, 'r:', linewidth=2, label='omega4 (+Newton)')
    axes[0].set_xlabel('x')
    axes[0].set_ylabel('ω(x)')
    axes[0].set_title('Wright Omega Function: ω(x) = W₀(eˣ)')
    axes[0].legend(loc='upper left')
    axes[0].grid(True, alpha=0.3)
    axes[0].set_xlim([-5, 15])

    # Relative error
    axes[1].semilogy(x, rel_error_3, 'g-', linewidth=1.5, label='omega3 (polynomial)')
    axes[1].semilogy(x, rel_error_4, 'r-', linewidth=1.5, label='omega4 (+Newton)')
    axes[1].axhline(y=1.0, color='k', linestyle='--', alpha=0.5, label='1% error')
    axes[1].set_xlabel('x')
    axes[1].set_ylabel('Relative Error (%)')
    axes[1].set_title('Approximation Error Comparison')
    axes[1].legend()
    axes[1].grid(True, alpha=0.3)
    axes[1].set_xlim([-5, 15])
    axes[1].set_ylim([1e-6, 100])

    plt.tight_layout()
    plt.savefig('build/omega_comparison.png', dpi=150)
    plt.close()
    print("Saved: build/omega_comparison.png")


def plot_diode_iv_comparison():
    """Plot diode I-V characteristics comparison"""
    v = np.linspace(-0.5, 0.8, 500)

    i_baseline = np.zeros_like(v)
    i_lambertw = np.zeros_like(v)
    g_baseline = np.zeros_like(v)
    g_lambertw = np.zeros_like(v)

    for idx, vi in enumerate(v):
        i_baseline[idx], g_baseline[idx] = diode_iv(vi)
        i_lambertw[idx], g_lambertw[idx] = diode_iv_lambertw(vi)

    fig, axes = plt.subplots(2, 2, figsize=(12, 10))

    # I-V curves (linear scale, zoomed)
    axes[0, 0].plot(v, i_baseline * 1e3, 'b-', linewidth=2, label='diode_iv (fast_exp)')
    axes[0, 0].plot(v, i_lambertw * 1e3, 'r--', linewidth=1.5, label='diode_iv_lambertw')
    axes[0, 0].set_xlabel('Voltage (V)')
    axes[0, 0].set_ylabel('Current (mA)')
    axes[0, 0].set_title('Diode I-V Characteristic')
    axes[0, 0].legend()
    axes[0, 0].grid(True, alpha=0.3)
    axes[0, 0].set_xlim([0.4, 0.8])
    axes[0, 0].set_ylim([0, 5])

    # I-V curves (log scale)
    axes[0, 1].semilogy(v, np.abs(i_baseline) + 1e-15, 'b-', linewidth=2, label='diode_iv (fast_exp)')
    axes[0, 1].semilogy(v, np.abs(i_lambertw) + 1e-15, 'r--', linewidth=1.5, label='diode_iv_lambertw')
    axes[0, 1].set_xlabel('Voltage (V)')
    axes[0, 1].set_ylabel('|Current| (A)')
    axes[0, 1].set_title('Diode I-V Characteristic (Log Scale)')
    axes[0, 1].legend()
    axes[0, 1].grid(True, alpha=0.3)

    # Conductance
    axes[1, 0].semilogy(v, g_baseline, 'b-', linewidth=2, label='diode_iv (fast_exp)')
    axes[1, 0].semilogy(v, g_lambertw, 'r--', linewidth=1.5, label='diode_iv_lambertw')
    axes[1, 0].set_xlabel('Voltage (V)')
    axes[1, 0].set_ylabel('Conductance (S)')
    axes[1, 0].set_title('Diode Conductance (dI/dV)')
    axes[1, 0].legend()
    axes[1, 0].grid(True, alpha=0.3)

    # Current difference
    i_diff = (i_lambertw - i_baseline) * 1e9  # nA
    axes[1, 1].plot(v, i_diff, 'g-', linewidth=1)
    axes[1, 1].set_xlabel('Voltage (V)')
    axes[1, 1].set_ylabel('Current Difference (nA)')
    axes[1, 1].set_title(f'I_lambertw - I_baseline (max |diff| = {np.max(np.abs(i_diff)):.1f} nA)')
    axes[1, 1].grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig('build/diode_iv_comparison.png', dpi=150)
    plt.close()
    print("Saved: build/diode_iv_comparison.png")


def plot_micro_benchmark():
    """Plot micro-benchmark results"""
    functions = ['omega_fast2', 'omega_fast', 'fast_exp', 'omega4', 'DiodeLambertW', 'diode_iv']
    cycles = [38, 47, 51, 58, 59, 63]

    colors = ['#2ecc71', '#27ae60', '#3498db', '#9b59b6', '#e67e22', '#e74c3c']

    fig, ax = plt.subplots(figsize=(10, 5))

    bars = ax.bar(functions, cycles, color=colors)

    # Add value labels
    for bar, cyc in zip(bars, cycles):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 1,
                f'{cyc}', ha='center', va='bottom', fontsize=11, fontweight='bold')

    ax.set_ylabel('Cycles per Call')
    ax.set_title('Micro-benchmark: Exp/Omega Function Implementations (Cortex-M4)')
    ax.grid(True, axis='y', alpha=0.3)

    # Add speedup annotations
    baseline_cycles = 63  # diode_iv
    for i, (func, cyc) in enumerate(zip(functions, cycles)):
        speedup = baseline_cycles / cyc
        ax.text(i, cyc / 2, f'{speedup:.1f}x', ha='center', va='center',
                fontsize=9, color='white', fontweight='bold')

    plt.tight_layout()
    plt.savefig('build/micro_benchmark.png', dpi=150)
    plt.close()
    print("Saved: build/micro_benchmark.png")


def main():
    print("Generating TB-303 WaveShaper benchmark plots...")
    print()

    # Create build directory if needed
    import os
    os.makedirs('build', exist_ok=True)

    plot_waveform_comparison()
    plot_benchmark_results()
    plot_omega_comparison()
    plot_diode_iv_comparison()
    plot_micro_benchmark()

    print()
    print("All plots generated successfully!")


if __name__ == '__main__':
    main()
