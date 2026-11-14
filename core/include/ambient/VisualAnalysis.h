#pragma once
// JUCE-free analysis helpers used by the UI-thread visualizers.
// None of this runs on the audio thread.

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ambient::viz
{
// ---------------------------------------------------------------------------
// Stereo statistics
// ---------------------------------------------------------------------------
struct StereoStats
{
    float correlation = 0.0f; // -1 (out of phase) .. +1 (mono)
    float width       = 0.0f; // 0 = mono, 0.5 = wide/uncorrelated, 1 = pure side (phase inverted)
    float balance     = 0.0f; // -1 = left only .. +1 = right only
    float rmsL = 0.0f, rmsR = 0.0f;
};

inline StereoStats computeStereoStats(const float* l, const float* r, std::size_t n) noexcept
{
    double sll = 0, srr = 0, slr = 0, smm = 0, sss = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double a = l[i], b = r[i];
        sll += a * a; srr += b * b; slr += a * b;
        const double m = 0.5 * (a + b), s = 0.5 * (a - b);
        smm += m * m; sss += s * s;
    }
    StereoStats st;
    constexpr double eps = 1.0e-12;
    const double denom = std::sqrt(sll * srr);
    st.correlation = denom > eps ? static_cast<float>(std::clamp(slr / denom, -1.0, 1.0)) : 0.0f;
    const double ms = std::sqrt(smm) + std::sqrt(sss);
    st.width = ms > eps ? static_cast<float>(std::sqrt(sss) / ms) : 0.0f;
    const double lr = std::sqrt(sll) + std::sqrt(srr);
    st.balance = lr > eps ? static_cast<float>((std::sqrt(srr) - std::sqrt(sll)) / lr) : 0.0f;
    if (n > 0)
    {
        st.rmsL = static_cast<float>(std::sqrt(sll / double(n)));
        st.rmsR = static_cast<float>(std::sqrt(srr / double(n)));
    }
    return st;
}

/** Plain-language reading of the stereo image, used by the vectorscope. */
enum class StereoImage { Silent, Mono, LeftOnly, RightOnly, Stereo, Wide, PhaseIssue, Inverted };

/** Classification thresholds:
      silent      both RMS below -70 dBFS
      left/right  one channel >= 30 dB louder than the other (|balance| >= 0.94)
      mono        correlation >= 0.98 and width <= 0.05 (L and R practically identical)
      inverted    correlation <= -0.98 and width >= 0.95 (L = -R)
      phase issue correlation < -0.2 (mono sum loses energy)
      wide        -0.2 <= correlation < 0.3 (uncorrelated / very wide)
      stereo      otherwise (positively correlated, mono-compatible) */
inline StereoImage classifyStereo(const StereoStats& s) noexcept
{
    constexpr float silent = 3.16e-4f; // -70 dBFS
    if (s.rmsL < silent && s.rmsR < silent) return StereoImage::Silent;
    if (s.balance <= -0.94f) return StereoImage::LeftOnly;
    if (s.balance >=  0.94f) return StereoImage::RightOnly;
    if (s.correlation >= 0.98f && s.width <= 0.05f) return StereoImage::Mono;
    if (s.correlation <= -0.98f && s.width >= 0.95f) return StereoImage::Inverted;
    if (s.correlation < -0.2f) return StereoImage::PhaseIssue;
    if (s.correlation < 0.3f) return StereoImage::Wide;
    return StereoImage::Stereo;
}

inline const char* describe(StereoImage i) noexcept
{
    switch (i)
    {
        case StereoImage::Silent:     return "No signal";
        case StereoImage::Mono:       return "Mono";
        case StereoImage::LeftOnly:   return "Left channel only";
        case StereoImage::RightOnly:  return "Right channel only";
        case StereoImage::Stereo:     return "Stereo - mono compatible";
        case StereoImage::Wide:       return "Wide / uncorrelated";
        case StereoImage::PhaseIssue: return "Phase conflict - mono loss";
        case StereoImage::Inverted:   return "Phase inverted - cancels in mono";
    }
    return "";
}

// ---------------------------------------------------------------------------
// Oscilloscope trigger
// ---------------------------------------------------------------------------
enum class TriggerMode { Free, Rising, Falling };

/** Searches data[0 .. n - displayLen] for a crossing of `level`, scanning from the
    newest candidate backwards so the display shows the most recent stable cycle.
    Returns the start index of the display window. Free mode (or no crossing found)
    returns n - displayLen, i.e. the latest samples. */
inline std::size_t findTrigger(const float* data, std::size_t n, std::size_t displayLen,
                               TriggerMode mode, float level = 0.0f) noexcept
{
    if (displayLen >= n) return 0;
    const std::size_t latest = n - displayLen;
    if (mode == TriggerMode::Free) return latest;

    for (std::size_t i = latest; i >= 1; --i)
    {
        const float a = data[i - 1], b = data[i];
        const bool hit = mode == TriggerMode::Rising ? (a < level && b >= level)
                                                     : (a > level && b <= level);
        if (hit) return i;
    }
    return latest;
}

/** Reduces `n` samples to `columns` min/max pairs (for drawing one vertical
    span per pixel column, independent of sample count). */
inline void minMaxDecimate(const float* data, std::size_t n, std::size_t columns,
                           float* outMin, float* outMax) noexcept
{
    if (columns == 0) return;
    for (std::size_t c = 0; c < columns; ++c)
    {
        const std::size_t a = c * n / columns;
        std::size_t b = (c + 1) * n / columns;
        if (b <= a) b = std::min(n, a + 1);
        float lo = data[std::min(a, n - 1)], hi = lo;
        for (std::size_t i = a; i < b; ++i) { lo = std::min(lo, data[i]); hi = std::max(hi, data[i]); }
        outMin[c] = lo; outMax[c] = hi;
    }
}

// ---------------------------------------------------------------------------
// Frequency mapping
// ---------------------------------------------------------------------------
/** Maps x in [0,1] to frequency on a log axis. */
inline float logFreq(float x, float fMin, float fMax) noexcept
{
    return fMin * std::pow(fMax / fMin, x);
}

inline float logPos(float f, float fMin, float fMax) noexcept
{
    return std::log(f / fMin) / std::log(fMax / fMin);
}
} // namespace ambient::viz
