#pragma once
#include <cmath>
#include <cstddef>

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
  #include <xmmintrin.h>
  #include <pmmintrin.h>
  #define AMBIENT_HAS_SSE 1
#endif

namespace ambient
{
/** RAII: enable flush-to-zero / denormals-are-zero for the scope of a block. */
class ScopedNoDenormals
{
public:
    ScopedNoDenormals() noexcept
    {
#if AMBIENT_HAS_SSE
        saved = _mm_getcsr();
        _mm_setcsr(saved | 0x8040); // FTZ | DAZ
#endif
    }
    ~ScopedNoDenormals() noexcept
    {
#if AMBIENT_HAS_SSE
        _mm_setcsr(saved);
#endif
    }
private:
    unsigned int saved = 0;
};

/** Replaces NaN/Inf with silence. Returns true if anything was repaired. */
inline bool sanitize(float* data, std::size_t n) noexcept
{
    bool bad = false;
    for (std::size_t i = 0; i < n; ++i)
        if (! std::isfinite(data[i])) { data[i] = 0.0f; bad = true; }
    return bad;
}

/** Zero-latency soft clipper used as output safety (no lookahead). */
inline float softClip(float x) noexcept
{
    // Transparent below ~-3 dBFS, smooth saturation to +-1.
    constexpr float knee = 0.7f;
    const float a = std::abs(x);
    if (a <= knee) return x;
    const float over = (a - knee) / (1.0f - knee);
    const float y = knee + (1.0f - knee) * std::tanh(over);
    return std::copysign(y, x);
}

/** First-order DC blocker. */
class DcBlocker
{
public:
    void prepare(double sampleRate) noexcept { r = static_cast<float>(1.0 - (2.0 * 3.14159265358979 * 10.0 / sampleRate)); x1 = y1 = 0.0f; }
    float process(float x) noexcept { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
private:
    float r = 0.995f, x1 = 0.0f, y1 = 0.0f;
};
} // namespace ambient
