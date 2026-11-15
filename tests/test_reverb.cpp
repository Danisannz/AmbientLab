// Reverb engine (Stage 5) tests.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <ambient/dsp/Reverb.h>
#include <cmath>
#include <vector>

using namespace ambient::dsp;
using Catch::Approx;
using A = Reverb::Algo;

namespace
{
constexpr double kPi = 3.14159265358979323846;
struct St { std::vector<float> l, r; };

Reverb::Params wet(A algo, float decay = 2.0f)
{
    Reverb::Params p;
    p.on = true; p.algo = algo; p.decay = decay; p.predelayMs = 0.0f; p.mix = 1.0f;
    p.early = 0.0f; p.modulation = 0.0f; p.feedback = 0.0f; p.damping = 0.1f; p.diffusion = 0.7f;
    return p;
}

St impulse(Reverb& r, double sr, double seconds, int block = 256)
{
    const size_t n = size_t(sr * seconds);
    St s { std::vector<float>(n, 0.0f), std::vector<float>(n, 0.0f) };
    s.l[0] = s.r[0] = 1.0f;
    for (size_t i = 0; i < n; i += size_t(block)) r.process(s.l.data() + i, s.r.data() + i, int(std::min<size_t>(size_t(block), n - i)));
    return s;
}

/** RT60 from Schroeder backward integration, fitted between -5 and -25 dB (T20 x 3). */
double rt60(const St& s, double sr)
{
    const size_t n = s.l.size();
    std::vector<double> e(n);
    double acc = 0.0;
    for (size_t i = n; i-- > 0;) { acc += double(s.l[i]) * s.l[i] + double(s.r[i]) * s.r[i]; e[i] = acc; }
    const double e0 = e[0];
    size_t i5 = 0, i25 = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double db = 10.0 * std::log10(e[i] / e0 + 1e-30);
        if (! i5 && db <= -5.0) i5 = i;
        if (! i25 && db <= -25.0) { i25 = i; break; }
    }
    return 3.0 * double(i25 - i5) / sr;
}

double rms(const std::vector<float>& x, size_t a, size_t b)
{
    double e = 0; for (size_t i = a; i < b; ++i) e += double(x[i]) * x[i];
    return std::sqrt(e / double(b - a));
}
} // namespace

TEST_CASE("Reverb: off / mix 0 leave the signal bit-exact; zero latency")
{
    Reverb r; r.prepare(48000.0, 256);
    auto p = wet(A::Hall); p.mix = 0.0f;
    r.setParams(p); r.reset();
    St s { std::vector<float>(48000), std::vector<float>(48000) };
    for (size_t i = 0; i < s.l.size(); ++i) s.l[i] = s.r[i] = 0.3f * float(std::sin(i * 0.01));
    const auto ref = s;
    r.process(s.l.data(), s.r.data(), int(s.l.size()));
    for (size_t i = 0; i < s.l.size(); ++i) REQUIRE(s.l[i] == ref.l[i]);
    REQUIRE(Reverb::getLatencySamples() == 0);
}

TEST_CASE("Reverb: FDN decay times follow the Decay parameter")
{
    const double sr = 48000.0;
    for (A algo : { A::Hall, A::Room, A::Cathedral })
        for (float rt : { 1.0f, 4.0f })
        {
            Reverb r; r.prepare(sr, 256); r.setParams(wet(algo, rt)); r.reset();
            const double m = rt60(impulse(r, sr, rt * 2.0 + 1.0), sr);
            INFO("algo " << int(algo) << " RT60 set " << rt << " measured " << m);
            REQUIRE(m == Approx(rt).epsilon(0.3));
        }
}

TEST_CASE("Reverb: plate and spring decay grow with the Decay parameter")
{
    const double sr = 48000.0;
    for (A algo : { A::Plate, A::Spring })
    {
        Reverb a; a.prepare(sr, 256); a.setParams(wet(algo, 1.0f)); a.reset();
        Reverb b; b.prepare(sr, 256); b.setParams(wet(algo, 5.0f)); b.reset();
        const double ra = rt60(impulse(a, sr, 4.0), sr), rb = rt60(impulse(b, sr, 12.0), sr);
        INFO("algo " << int(algo) << " RT60 1 s -> " << ra << ", 5 s -> " << rb);
        REQUIRE(rb > ra * 2.5);
        REQUIRE(ra == Approx(1.0).epsilon(0.6));
    }
}

TEST_CASE("Reverb: pre-delay delays the onset")
{
    const double sr = 48000.0;
    Reverb r; r.prepare(sr, 256);
    auto p = wet(A::Hall); p.predelayMs = 120.0f;
    r.setParams(p); r.reset();
    auto s = impulse(r, sr, 0.5);
    size_t first = 0;
    for (size_t i = 0; i < s.l.size(); ++i) if (std::fabs(s.l[i]) > 1e-6f || std::fabs(s.r[i]) > 1e-6f) { first = i; break; }
    REQUIRE(first >= size_t(0.12 * sr));
}

TEST_CASE("Reverb: freeze sustains the tail, infinite never decays, both stay bounded")
{
    const double sr = 48000.0;
    for (A algo : { A::Hall, A::Plate, A::Cathedral })
    {
        Reverb r; r.prepare(sr, 256);
        auto p = wet(algo, 1.0f);
        r.setParams(p); r.reset();
        St s { std::vector<float>(size_t(sr * 6), 0.0f), std::vector<float>(size_t(sr * 6), 0.0f) };
        for (size_t i = 0; i < size_t(sr * 0.3); ++i) s.l[i] = s.r[i] = 0.5f * float(std::sin(2 * kPi * 330.0 * double(i) / sr));
        for (size_t i = 0; i < s.l.size(); i += 256)
        {
            if (! p.freeze && i >= size_t(sr * 0.5)) { p.freeze = true; r.setParams(p); }
            r.process(s.l.data() + i, s.r.data() + i, int(std::min<size_t>(256, s.l.size() - i)));
        }
        const double early = rms(s.l, size_t(sr * 1.0), size_t(sr * 1.5)), late = rms(s.l, size_t(sr * 5.0), size_t(sr * 5.5));
        INFO("algo " << int(algo) << " frozen tail: " << 20 * std::log10(late / early) << " dB over 4 s");
        REQUIRE(late > early * 0.5);   // within 6 dB after 4 s (1 s RT60 would be -240 dB)
        REQUIRE(late < early * 2.0);
    }
    Reverb r; r.prepare(sr, 256); r.setParams(wet(A::Infinite)); r.reset();
    St s { std::vector<float>(size_t(sr * 12), 0.0f), std::vector<float>(size_t(sr * 12), 0.0f) };
    for (size_t i = 0; i < s.l.size(); ++i) s.l[i] = s.r[i] = (i < size_t(sr * 6) ? 0.8f : 0.0f) * float(std::sin(2 * kPi * 220.0 * double(i) / sr)); // loud and long input
    r.process(s.l.data(), s.r.data(), int(s.l.size()));
    float peak = 0.0f; for (float v : s.l) { REQUIRE(std::isfinite(v)); peak = std::max(peak, std::fabs(v)); }
    REQUIRE(peak < 8.0f);                                                     // limiter keeps it bounded
    REQUIRE(rms(s.l, size_t(sr * 11), size_t(sr * 12)) > 0.3 * rms(s.l, size_t(sr * 6.5), size_t(sr * 7.5))); // still ringing 5 s later
}

TEST_CASE("Reverb: damping makes highs decay faster")
{
    const double sr = 48000.0;
    auto hfRatio = [&](float damping)
    {
        Reverb r; r.prepare(sr, 256); auto p = wet(A::Hall, 3.0f); p.damping = damping; r.setParams(p); r.reset();
        auto s = impulse(r, sr, 2.0);
        double hf = 0, all = 0;
        for (size_t i = size_t(sr * 1.0) + 1; i < size_t(sr * 1.5); ++i) { hf += std::pow(double(s.l[i] - s.l[i - 1]), 2); all += double(s.l[i]) * s.l[i]; }
        return hf / all;
    };
    REQUIRE(hfRatio(0.9f) < 0.5 * hfRatio(0.0f));
}

TEST_CASE("Reverb: algorithms are genuinely different; output is stereo")
{
    const double sr = 48000.0;
    std::vector<St> irs;
    for (A algo : { A::Hall, A::Room, A::Cathedral, A::Plate, A::Spring })
    {
        Reverb r; r.prepare(sr, 256); r.setParams(wet(algo, 2.0f)); r.reset();
        irs.push_back(impulse(r, sr, 1.0));
    }
    for (size_t a = 0; a < irs.size(); ++a)
    {
        // stereo decorrelation
        double lr = 0, ll = 0, rr = 0;
        for (size_t i = 0; i < irs[a].l.size(); ++i) { lr += irs[a].l[i] * irs[a].r[i]; ll += irs[a].l[i] * irs[a].l[i]; rr += irs[a].r[i] * irs[a].r[i]; }
        INFO("algo index " << a << " L/R correlation " << lr / std::sqrt(ll * rr));
        REQUIRE(std::fabs(lr / std::sqrt(ll * rr)) < 0.6);
        for (size_t b = a + 1; b < irs.size(); ++b)
        {
            double xy = 0, xx = 0, yy = 0;
            for (size_t i = 0; i < irs[a].l.size(); ++i) { xy += irs[a].l[i] * irs[b].l[i]; xx += irs[a].l[i] * irs[a].l[i]; yy += irs[b].l[i] * irs[b].l[i]; }
            INFO("algos " << a << " vs " << b << " correlation " << xy / std::sqrt(xx * yy));
            REQUIRE(std::fabs(xy / std::sqrt(xx * yy)) < 0.5);
        }
    }
}

TEST_CASE("Reverb: shimmer adds octave-up energy to the tail")
{
    const double sr = 48000.0;
    auto octaveRatio = [&](A algo)
    {
        Reverb r; r.prepare(sr, 256); auto p = wet(algo, 4.0f); p.shimmer = 1.0f; r.setParams(p); r.reset();
        St s { std::vector<float>(size_t(sr * 4), 0.0f), std::vector<float>(size_t(sr * 4), 0.0f) };
        for (size_t i = 0; i < size_t(sr * 0.5); ++i) s.l[i] = s.r[i] = 0.5f * float(std::sin(2 * kPi * 330.0 * double(i) / sr));
        r.process(s.l.data(), s.r.data(), int(s.l.size()));
        auto g = [&](double f) { double re = 0, im = 0; for (size_t i = size_t(sr * 2); i < size_t(sr * 4); ++i) { re += s.l[i] * std::cos(2 * kPi * f * i / sr); im += s.l[i] * std::sin(2 * kPi * f * i / sr); } return re * re + im * im; };
        return 10.0 * std::log10(g(660.0) / g(330.0));
    };
    const double shim = octaveRatio(A::Shimmer), hall = octaveRatio(A::Hall);
    INFO("660/330 Hz: shimmer " << shim << " dB, hall " << hall << " dB");
    REQUIRE(shim > hall + 15.0);
}

TEST_CASE("Reverb: regenerative feedback lengthens the tail and stays bounded; stable everywhere")
{
    const double sr = 48000.0;
    Reverb a; a.prepare(sr, 256); a.setParams(wet(A::Hall, 1.0f)); a.reset();
    auto pf = wet(A::Hall, 1.0f); pf.feedback = 0.9f;
    Reverb b; b.prepare(sr, 256); b.setParams(pf); b.reset();
    REQUIRE(rt60(impulse(b, sr, 6.0), sr) > rt60(impulse(a, sr, 6.0), sr) * 1.3);

    for (double rate : { 44100.0, 96000.0, 192000.0 })
        for (A algo : { A::Hall, A::Room, A::Cathedral, A::Plate, A::Spring, A::Shimmer, A::Infinite })
            for (int block : { 1, 512 })
            {
                Reverb r; r.prepare(rate, block);
                auto p = wet(algo, 30.0f); p.feedback = 0.9f; p.shimmer = 1.0f; p.modulation = 1.0f; p.size = 2.0f; p.early = 1.0f; p.damping = 0.0f; p.diffusion = 1.0f;
                r.setParams(p); r.reset();
                const size_t n = size_t(rate * 1.5);
                St s { std::vector<float>(n), std::vector<float>(n) };
                uint32_t seed = 9;
                for (size_t i = 0; i < n; ++i) { seed = seed * 1664525u + 1013904223u; s.l[i] = s.r[i] = 0.9f * (float(int(seed >> 8) - (1 << 23)) / float(1 << 23)); }
                for (size_t i = 0; i < n; i += size_t(block)) r.process(s.l.data() + i, s.r.data() + i, int(std::min<size_t>(size_t(block), n - i)));
                float peak = 0.0f; for (float v : s.l) { REQUIRE(std::isfinite(v)); peak = std::max(peak, std::fabs(v)); }
                INFO("rate " << rate << " algo " << int(algo) << " peak " << peak);
                REQUIRE(peak < 20.0f);
            }
}
