#pragma once
// Polyphase IIR half-band decimators (two parallel allpass chains, elliptic design after
// L. de Soras' HIIR). Near-zero latency (a few samples of low-frequency group delay; no
// reported plugin latency), ~-100 dB stopband. Used by the synth's High Quality mode (4x).
#include <array>
#include <cmath>

namespace ambient::dsp
{
namespace halfband
{
/** Allpass coefficients for nbr coefficients and normalised transition bandwidth (0..0.5). */
template <int N>
inline std::array<double, N> design(double transition) noexcept
{
    const double pi = 3.14159265358979323846;
    double k = std::tan((1.0 - transition * 2.0) * pi / 4.0);
    k *= k;
    const double kksqrt = std::pow(1.0 - k * k, 0.25);
    const double e = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
    const double e4 = e * e * e * e;
    const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
    const int order = N * 2 + 1;
    std::array<double, N> c {};
    for (int index = 0; index < N; ++index)
    {
        const int cc = index + 1;
        double num = 0.0, den = 0.0, term;
        int i = 0, sgn = 1;
        do { term = std::pow(q, double(i * (i + 1))) * std::sin(double((i * 2 + 1) * cc) * pi / order) * sgn; num += term; sgn = -sgn; ++i; } while (std::fabs(term) > 1e-100 && i < 100);
        i = 1; sgn = -1;
        do { term = std::pow(q, double(i * i)) * std::cos(double(i * 2 * cc) * pi / order) * sgn; den += term; sgn = -sgn; ++i; } while (std::fabs(term) > 1e-100 && i < 100);
        num *= std::pow(q, 0.25);
        den += 0.5;
        const double ww = num / den, wwsq = ww * ww;
        const double x = std::sqrt((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
        c[size_t(index)] = (1.0 - x) / (1.0 + x);
    }
    return c;
}
} // namespace halfband

/** 2x -> 1x decimator with N allpass coefficients. */
template <int N>
class Downsampler2x
{
public:
    void setCoefficients(const std::array<double, N>& c) noexcept { for (int i = 0; i < N; ++i) coef[size_t(i)] = float(c[size_t(i)]); }
    void reset() noexcept { x.fill(0.0f); y.fill(0.0f); }
    /** Consumes two input samples (in[0] earlier, in[1] later), returns one output sample. */
    inline float process(const float* in) noexcept
    {
        float s0 = in[1], s1 = in[0];
        for (int i = 0; i < N; i += 2)
        {
            const float t0 = (s0 - y[size_t(i)]) * coef[size_t(i)] + x[size_t(i)];
            x[size_t(i)] = s0; y[size_t(i)] = t0; s0 = t0;
            if (i + 1 < N)
            {
                const float t1 = (s1 - y[size_t(i + 1)]) * coef[size_t(i + 1)] + x[size_t(i + 1)];
                x[size_t(i + 1)] = s1; y[size_t(i + 1)] = t1; s1 = t1;
            }
        }
        return 0.5f * (s0 + s1);
    }
    void flushDenormals() noexcept { for (auto* a : { &x, &y }) for (auto& v : *a) if (std::fabs(v) < 1e-20f) v = 0.0f; }
private:
    std::array<float, N> coef {}, x {}, y {};
};

/** 4x -> 1x: 4x->2x (wide transition, 6 coefs) then 2x->1x (steep, 12 coefs). */
class Decimator4x
{
public:
    Decimator4x()
    {
        stage1.setCoefficients(halfband::design<6>(0.19));    // 192k->96k: protects 0-20 kHz from content above 76 kHz
        stage2.setCoefficients(halfband::design<12>(0.0417)); // 96k->48k: passband 0-20 kHz, stopband from 28 kHz
    }
    void reset() noexcept { stage1.reset(); stage2.reset(); }
    /** n output samples from 4n input samples. In-place safe (out may alias in). */
    void process(const float* in, float* out, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            float mid[2];
            mid[0] = stage1.process(in + 4 * i);
            mid[1] = stage1.process(in + 4 * i + 2);
            out[i] = stage2.process(mid);
        }
        stage1.flushDenormals(); stage2.flushDenormals();
    }
private:
    Downsampler2x<6> stage1;
    Downsampler2x<12> stage2;
};
} // namespace ambient::dsp
