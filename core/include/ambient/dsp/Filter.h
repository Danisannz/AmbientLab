#pragma once
// Multimode voice filter on the TPT state-variable core (stable under modulation).
//   LP24 / HP24 = two cascaded 2-pole stages (resonance on the second stage)
//   LP12 / HP12 / BP / Notch = one 2-pole stage
// Drive: ADAA soft saturation into the filter (no oversampling, no latency).
// Resonance 0..1 maps Q 0.707 .. ~25 (near self-oscillation, always stable).

#include "Oscillator.h"
#include "Svf.h"

namespace ambient::dsp
{
class MultiFilter
{
public:
    enum class Mode { LP24 = 0, LP12, BP, HP12, HP24, Notch };

    void reset() noexcept { s1.reset(); s2.reset(); sat.reset(); }

    /** cutoffHz is clamped to [20, 0.45 fs]. */
    void set(double fs, float cutoffHz, float resonance, Mode m) noexcept
    {
        mode = m;
        const double fc = std::fmin(std::fmax(20.0, double(cutoffHz)), fs * 0.45);
        const float g = Svf::prewarp(fs, fc);
        const float q = 0.70710678f * std::pow(35.0f, std::fmin(1.0f, std::fmax(0.0f, resonance)));
        s1.setGK(g, isFourPole() ? 1.41421356f : 1.0f / q);
        s2.setGK(g, 1.0f / q);
        k1 = isFourPole() ? 1.41421356f : 1.0f / q;
        k2 = 1.0f / q;
    }

    void setDrive(float linearGain) noexcept { drive = linearGain; }

    inline float process(float x) noexcept
    {
        if (drive > 1.001f) x = sat.process(x * drive, Saturator::Mode::Soft) / std::sqrt(drive) * 1.5f;
        float y = stage(s1, k1, x);
        if (isFourPole()) y = stage(s2, k2, y);
        return y;
    }

private:
    bool isFourPole() const noexcept { return mode == Mode::LP24 || mode == Mode::HP24; }

    inline float stage(Svf& s, float k, float x) noexcept
    {
        float bp, lp;
        s.tick(x, bp, lp);
        switch (mode)
        {
            case Mode::LP24: case Mode::LP12: return lp;
            case Mode::HP24: case Mode::HP12: return x - k * bp - lp;
            case Mode::BP:    return k * bp;          // unity peak
            case Mode::Notch: return x - k * bp;
        }
        return lp;
    }

    Svf s1, s2;
    Saturator sat;
    Mode mode = Mode::LP24;
    float k1 = 1.414f, k2 = 1.414f, drive = 1.0f;
};
} // namespace ambient::dsp
