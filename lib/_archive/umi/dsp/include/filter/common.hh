#pragma once

#if __has_include(<numbers>)
    #include <numbers>
#endif
#include <type_traits>

// Common coefficient policy helpers for filters

constexpr float calc_wc(float fc, float dt) {
    return fc * dt;
}

constexpr float calc_cutoff_rc(float r1, float c1) {
    constexpr float pi =
#if defined(__cpp_lib_math_constants)
        std::numbers::pi_v<float>;
#else
        3.14159265358979323846f;
#endif
    return 1.0f / (2.0f * pi * r1 * c1);
}

template <typename Coeffs, auto Calc>
struct InlineCoeffs {
    static constexpr bool needs_params = true;

    template <typename... Args>
    const Coeffs& coeffs(Args... args) {
        temp_ = Calc(args...);
        return temp_;
    }

  private:
    Coeffs temp_{};
};

template <typename Coeffs, auto Calc>
struct OwnCoeffs {
    static constexpr bool needs_params = false;

    template <typename... Args>
    void set_params(Args... args) {
        coeffs_ = Calc(args...);
    }

    template <typename... Args>
    const Coeffs& coeffs(Args...) const {
        return coeffs_;
    }

  private:
    Coeffs coeffs_{};
};

template <typename Coeffs>
struct SharedCoeffs {
    static constexpr bool needs_params = false;

    explicit SharedCoeffs(const Coeffs& coeffs) : coeffs_(&coeffs) {}

    template <typename... Args>
    const Coeffs& coeffs(Args...) const {
        return *coeffs_;
    }

  private:
    const Coeffs* coeffs_ = nullptr;
};
