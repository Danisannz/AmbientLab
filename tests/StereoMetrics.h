#pragma once
// Stereo measurement helpers for tests: correlation, mono-sum loss, anti-phase time.
#include <cmath>
#include <vector>
#include <algorithm>

namespace stereo_test
{
struct Metrics
{
    double corr = 0;          // Pearson L/R over the whole window
    double lowCorr = 0;       // below ~200 Hz (one-pole 4th order LP)
    double monoLossDb = 0;    // energy of (L+R)/2 vs mean(L^2,R^2): 0 dB = fully mono-safe, -3 dB = uncorrelated, -inf = anti-phase
    double negFraction = 0;   // fraction of 50 ms windows with correlation < 0
    double minWinCorr = 1;    // worst 50 ms window
    double sideToMidDb = 0;   // S/M energy ratio
};

inline double pearson(const float* l, const float* r, size_t a, size_t b)
{
    double lr = 0, ll = 0, rr = 0;
    for (size_t i = a; i < b; ++i) { lr += double(l[i]) * r[i]; ll += double(l[i]) * l[i]; rr += double(r[i]) * r[i]; }
    return (ll > 1e-20 && rr > 1e-20) ? lr / std::sqrt(ll * rr) : 1.0;
}

inline Metrics measure(const std::vector<float>& L, const std::vector<float>& R, double sr, size_t from = 0)
{
    Metrics m;
    const size_t n = L.size();
    m.corr = pearson(L.data(), R.data(), from, n);
    double mid = 0, side = 0, lr2 = 0;
    for (size_t i = from; i < n; ++i)
    {
        const double M = 0.5 * (L[i] + R[i]), S = 0.5 * (L[i] - R[i]);
        mid += M * M; side += S * S; lr2 += 0.5 * (double(L[i]) * L[i] + double(R[i]) * R[i]);
    }
    m.monoLossDb = 10.0 * std::log10(std::max(1e-30, mid) / std::max(1e-30, lr2));
    m.sideToMidDb = 10.0 * std::log10(std::max(1e-30, side) / std::max(1e-30, mid));
    // low band
    std::vector<float> ll(n), rl(n);
    const float c = float(1.0 - std::exp(-2.0 * 3.14159265358979 * 200.0 / sr));
    float s1[4] {}, s2[4] {};
    for (size_t i = 0; i < n; ++i)
    {
        float x = L[i], y = R[i];
        for (int k = 0; k < 4; ++k) { s1[k] += c * (x - s1[k]); x = s1[k]; s2[k] += c * (y - s2[k]); y = s2[k]; }
        ll[i] = x; rl[i] = y;
    }
    m.lowCorr = pearson(ll.data(), rl.data(), from, n);
    const size_t w = size_t(0.05 * sr);
    int neg = 0, tot = 0;
    for (size_t a = from; a + w <= n; a += w)
    {
        double e = 0; for (size_t i = a; i < a + w; ++i) e += double(L[i]) * L[i] + double(R[i]) * R[i];
        if (e < 1e-8 * double(w)) continue;           // skip silence
        const double c2 = pearson(L.data(), R.data(), a, a + w);
        m.minWinCorr = std::min(m.minWinCorr, c2);
        ++tot; if (c2 < 0) ++neg;
    }
    m.negFraction = tot ? double(neg) / tot : 0.0;
    return m;
}
} // namespace stereo_test
