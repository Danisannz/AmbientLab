#pragma once
// Band-limited oscillator primitives (header-only, allocation-free).
//   BlepOsc   - sine / triangle / saw / square / pulse with PolyBLEP / PolyBLAMP
//               anti-aliasing; accepts per-sample increment (FM) and phase offset (PM).
//   Noise     - white (xorshift) and pink (Paul Kellet "refined" filter).
//   Drift     - Ornstein-Uhlenbeck random walk for analog-style pitch drift
//               (mean-reverting, band-limited; NOT white noise on the pitch).
//   Saturator - tanh / asymmetric / wavefold with first-order antiderivative
//               anti-aliasing (ADAA), no oversampling, no latency.

#include <cmath>
#include <cstdint>

namespace ambient::dsp
{
enum class Wave { Sine = 0, Triangle, Saw, Square, Pulse };

/** Deterministic fast RNG (xorshift32). */
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    explicit Rng(uint32_t seed = 0x9E3779B9u) : s(seed ? seed : 1u) {}
    inline uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float uni() noexcept { return float(next() >> 8) * (1.0f / 16777216.0f); }       // [0,1)
    inline float bi() noexcept { return uni() * 2.0f - 1.0f; }                                // [-1,1)
    inline float gauss() noexcept { return (uni() + uni() + uni() + uni() - 2.0f) * 1.7320508f; } // ~N(0,1)
};

class BlepOsc
{
public:
    void reset(double startPhase = 0.0) noexcept { phase = startPhase - std::floor(startPhase); }
    double getPhase() const noexcept { return phase; }

    /** One sample. inc = f / fs (may be negative under through-zero FM),
        pmCycles = phase offset in cycles, pw = pulse width 0..1. */
    inline float next(double inc, Wave w, float pw, double pmCycles = 0.0) noexcept
    {
        double t = phase + pmCycles;
        t -= std::floor(t);
        const double dt = std::fabs(inc) < 1.0e-9 ? 1.0e-9 : std::fmin(std::fabs(inc), 0.5);
        float y = 0.0f;
        switch (w)
        {
            case Wave::Sine:
                y = float(std::sin(6.283185307179586 * t));
                break;
            case Wave::Saw:
                y = float(2.0 * t - 1.0) - blep(t, dt);
                break;
            case Wave::Square:
                y = (t < 0.5 ? 1.0f : -1.0f) + blep(t, dt) - blep(frac(t + 0.5), dt);
                break;
            case Wave::Pulse:
            {
                const double p = std::fmin(0.95, std::fmax(0.05, double(pw)));
                y = (t < p ? 1.0f : -1.0f) + blep(t, dt) - blep(frac(t + 1.0 - p), dt);
                y += float(1.0 - 2.0 * p); // remove DC so pulse width changes do not shift the level
                break;
            }
            case Wave::Triangle:
            {
                // naive: -1 at t=0, +1 at t=0.5; slope +4 -> -4 at 0.5 and back at 0 (change of 8)
                y = float(1.0 - 4.0 * std::fabs(t - 0.5));
                y += float(4.0 * dt) * (blamp(t, dt) - blamp(frac(t + 0.5), dt));
                break;
            }
        }
        phase += inc;
        phase -= std::floor(phase);
        return y;
    }

private:
    static inline double frac(double x) noexcept { return x - std::floor(x); }

    static inline float blep(double t, double dt) noexcept
    {
        if (t < dt) { const double x = t / dt; return float(x + x - x * x - 1.0); }
        if (t > 1.0 - dt) { const double x = (t - 1.0) / dt; return float(x * x + x + x + 1.0); }
        return 0.0f;
    }

    static inline float blamp(double t, double dt) noexcept
    {
        if (t < dt) { const double x = t / dt - 1.0; return float(-x * x * x / 3.0); }
        if (t > 1.0 - dt) { const double x = (t - 1.0) / dt + 1.0; return float(x * x * x / 3.0); }
        return 0.0f;
    }

    double phase = 0.0;
};

class Noise
{
public:
    explicit Noise(uint32_t seed = 12345u) : rng(seed) {}
    inline float white() noexcept { return rng.bi(); }
    /** Pink (-3 dB/octave), Paul Kellet's refined method; ~unity-ish level. */
    inline float pink() noexcept
    {
        const float w = rng.bi();
        b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f;
        b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
        b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
        const float p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
        b6 = w * 0.115926f;
        return p * 0.11f;
    }
    void reset() noexcept { b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0f; }

private:
    Rng rng;
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
};

/** Ornstein-Uhlenbeck drift: dx = -x/tau dt + sqrt(2/tau) dW  (unit variance, mean 0). */
class Drift
{
public:
    explicit Drift(uint32_t seed = 777u) : rng(seed) {}
    void setTimeConstant(float seconds) noexcept { tau = seconds; }
    /** Advance by dtSeconds; returns current value (~N(0,1) stationary). */
    inline float step(float dtSeconds) noexcept
    {
        x += -x / tau * dtSeconds + std::sqrt(2.0f / tau * dtSeconds) * rng.gauss();
        if (x > 4.0f) x = 4.0f; else if (x < -4.0f) x = -4.0f;
        return x;
    }
    float get() const noexcept { return x; }
    void reset() noexcept { x = 0.0f; }

private:
    Rng rng;
    float x = 0.0f, tau = 1.5f;
};

/** Saturation / waveshaping with first-order ADAA. */
class Saturator
{
public:
    enum class Mode { Soft = 0, Asym, Fold };

    void reset() noexcept { xPrev = 0.0f; }

    inline float process(float x, Mode m) noexcept
    {
        const float dx = x - xPrev;
        float y;
        if (std::fabs(dx) > 1.0e-4f) y = float((F(x, m) - F(xPrev, m)) / double(dx));
        else y = f(0.5f * (x + xPrev), m);
        xPrev = x;
        return y;
    }

    static inline float f(float x, Mode m) noexcept
    {
        switch (m)
        {
            case Mode::Soft: return std::tanh(x);
            case Mode::Asym: return std::tanh(x + kBias) - std::tanh(kBias);
            case Mode::Fold: return std::sin(1.5707963f * x);
        }
        return x;
    }

    /** Antiderivative (double precision for the difference quotient). */
    static inline double F(float xf, Mode m) noexcept
    {
        const double x = xf;
        switch (m)
        {
            case Mode::Soft: return logCosh(x);
            case Mode::Asym: return logCosh(x + kBias) - x * std::tanh(double(kBias));
            case Mode::Fold: return -std::cos(1.5707963267948966 * x) / 1.5707963267948966;
        }
        return 0.5 * x * x;
    }

private:
    static inline double logCosh(double x) noexcept
    {
        const double a = std::fabs(x);
        return a + std::log1p(std::exp(-2.0 * a)) - 0.6931471805599453;
    }
    static constexpr float kBias = 0.35f;
    float xPrev = 0.0f;
};
} // namespace ambient::dsp
