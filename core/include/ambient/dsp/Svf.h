#pragma once
// Topology-preserving state-variable filter (A. Simper / Cytomic formulation).
// Stable under per-sample coefficient changes, so it can be modulated without
// zipper noise. Used for band detection (band-pass) and dynamic bells.

#include <cmath>

namespace ambient::dsp
{
class Svf
{
public:
    void reset() noexcept { ic1 = ic2 = 0.0f; }

    /** Generic setup: g = tan(pi f / fs), k = damping (1/Q, or 1/(Q*A) for bells). */
    void setGK(float g_, float k_) noexcept
    {
        g = g_; k = k_;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    static float prewarp(double sampleRate, double freq) noexcept
    {
        const double f = std::fmin(freq, sampleRate * 0.49);
        return float(std::tan(3.14159265358979323846 * f / sampleRate));
    }

    /** Processes one sample; returns band-pass (v1) and low-pass (v2). */
    inline void tick(float v0, float& bp, float& lp) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        bp = v1; lp = v2;
    }

    /** Unity-peak-gain band-pass. */
    inline float bandpass(float x) noexcept { float bp, lp; tick(x, bp, lp); return k * bp; }
    inline float lowpass(float x) noexcept  { float bp, lp; tick(x, bp, lp); return lp; }

    float getK() const noexcept { return k; }

private:
    float ic1 = 0.0f, ic2 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, g = 0.0f, k = 1.0f;
};

/** Dynamic cut ("bell" for ducking): y = x - c * BP(x), BP = unity-peak band-pass.
    The cut amount c (0..1) is an audio-rate input, so it can be interpolated per
    sample with NO coefficient changes: no zipper noise. At f0 the gain is exactly
    (1 - c); far from f0 it is unity. Coefficients change only with f0 / Q. */
class Bell
{
public:
    void reset() noexcept { svf.reset(); }

    void set(double sampleRate, float freq, float q) noexcept
    {
        svf.setGK(Svf::prewarp(sampleRate, freq), 1.0f / q);
    }

    inline float process(float x, float cut) noexcept
    {
        return x - cut * svf.bandpass(x);
    }

    static float cutForDb(float reductionDb) noexcept { return 1.0f - std::pow(10.0f, -reductionDb / 20.0f); }

    /** Analytic magnitude (dB) at f of a cut of `reductionDb` at f0 (for displays/tests). */
    static float magnitudeDb(double sampleRate, float f0, float q, float reductionDb, float f) noexcept;

private:
    Svf svf;
};

/** Peak envelope follower with separate attack / release (one-pole). */
class EnvelopeFollower
{
public:
    void setTimes(double sampleRate, float attackMs, float releaseMs) noexcept
    {
        att = coeff(sampleRate, attackMs);
        rel = coeff(sampleRate, releaseMs);
    }
    void reset(float v = 0.0f) noexcept { env = v; }
    inline float process(float x) noexcept
    {
        const float a = std::fabs(x);
        env = a + (a > env ? att : rel) * (env - a);
        return env;
    }
    float get() const noexcept { return env; }

    static float coeff(double sampleRate, float ms) noexcept
    {
        const double n = std::fmax(1.0, sampleRate * double(ms) * 0.001);
        return float(std::exp(-1.0 / n));
    }

private:
    float env = 0.0f, att = 0.0f, rel = 0.0f;
};
} // namespace ambient::dsp
