// Frequency Sidechain (SpectralDucker) tests - synthetic kick / bass / drone signals.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <ambient/dsp/SpectralDucker.h>
#include <cmath>
#include <vector>

using namespace ambient::dsp;
using Catch::Approx;

namespace
{
constexpr double kPi = 3.14159265358979323846;

struct Stereo { std::vector<float> l, r; explicit Stereo(size_t n = 0) : l(n, 0.0f), r(n, 0.0f) {} size_t size() const { return l.size(); } };

/** Psytrance-style kick: pitch sweep f0 -> fEnd, exponential decay, one hit per period. */
std::vector<float> kicks(double sr, size_t n, double bpm, float amp, double fEnd = 50.0, double f0 = 140.0, double decayMs = 160.0)
{
    std::vector<float> x(n, 0.0f);
    const size_t period = size_t(sr * 60.0 / bpm);
    for (size_t start = 0; start < n; start += period)
    {
        double phase = 0.0;
        for (size_t i = 0; i < period && start + i < n; ++i)
        {
            const double t = double(i) / sr;
            const double f = fEnd + (f0 - fEnd) * std::exp(-t / 0.025);
            phase += 2.0 * kPi * f / sr;
            x[start + i] = amp * float(std::exp(-t / (decayMs * 0.001)) * std::sin(phase));
        }
    }
    return x;
}

std::vector<float> tone(double sr, size_t n, double f, float amp, double phase0 = 0.0)
{
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) x[i] = amp * float(std::sin(2.0 * kPi * f * double(i) / sr + phase0));
    return x;
}

std::vector<float> add(std::vector<float> a, const std::vector<float>& b) { for (size_t i = 0; i < a.size(); ++i) a[i] += b[i]; return a; }

/** Level (dB) of frequency f in x[from, to) - Goertzel, Hann window, amplitude-calibrated. */
float levelDb(const std::vector<float>& x, size_t from, size_t to, double f, double sr)
{
    const size_t n = to - from;
    double re = 0, im = 0, wsum = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * double(i) / double(n - 1));
        re += w * x[from + i] * std::cos(2.0 * kPi * f * double(i) / sr);
        im -= w * x[from + i] * std::sin(2.0 * kPi * f * double(i) / sr);
        wsum += w;
    }
    return float(20.0 * std::log10(2.0 * std::sqrt(re * re + im * im) / wsum + 1e-12));
}

/** Runs the ducker over whole signals in blocks; returns the processed internal signal. */
Stereo run(SpectralDucker& d, const Stereo& internal, const std::vector<float>* refL, const std::vector<float>* refR, int block = 256)
{
    Stereo out = internal;
    for (size_t s = 0; s < out.size(); s += size_t(block))
    {
        const int n = int(std::min<size_t>(size_t(block), out.size() - s));
        d.process(out.l.data() + s, out.r.data() + s,
                  refL ? refL->data() + s : nullptr, refR ? refR->data() + s : nullptr, n);
    }
    return out;
}

SpectralDucker::Params manualParams(float bandFreq)
{
    SpectralDucker::Params p;
    p.automatic = false;
    p.depth = 1.0f;
    p.sensitivity = 0.6f;
    for (auto& b : p.bands) b.enabled = false;
    p.bands[0].enabled = true;
    p.bands[0].freq = bandFreq;
    p.bands[0].q = 1.5f;
    p.bands[0].threshold = -36.0f;
    p.bands[0].maxReduction = 12.0f;
    return p;
}

Stereo drone(double sr, size_t n, double fLow, double fHigh, float amp = 0.25f)
{
    Stereo s(n);
    s.l = add(tone(sr, n, fLow, amp), tone(sr, n, fHigh, amp));
    s.r = add(tone(sr, n, fLow, amp, 0.3), tone(sr, n, fHigh, amp, 1.1)); // slightly different phase per side
    return s;
}
} // namespace

TEST_CASE("Sidechain: no external input -> internal signal untouched")
{
    const double sr = 48000.0; const size_t n = size_t(sr * 2);
    SpectralDucker d; d.prepare(sr, 512);
    auto in = drone(sr, n, 55.0, 880.0);
    auto out = run(d, in, nullptr, nullptr);
    for (size_t i = 0; i < n; ++i) REQUIRE(out.l[i] == in.l[i]);
    REQUIRE(d.telemetry().maxReductionDb.load() == 0.0f);
    REQUIRE_FALSE(d.telemetry().referencePresent.load());
}

TEST_CASE("Sidechain: kick reference reduces the conflicting low band, keeps upper harmonics")
{
    const double sr = 48000.0; const size_t n = size_t(sr * 4);
    SpectralDucker d; d.prepare(sr, 512);
    d.setParams(manualParams(55.0f));
    auto in = drone(sr, n, 55.0, 880.0);
    auto ref = kicks(sr, n, 145.0, 0.8f);
    auto out = run(d, in, &ref, &ref);

    const size_t a = size_t(sr * 2), b = n; // steady state
    const float low = levelDb(out.l, a, b, 55.0, sr) - levelDb(in.l, a, b, 55.0, sr);
    const float high = levelDb(out.l, a, b, 880.0, sr) - levelDb(in.l, a, b, 880.0, sr);
    INFO("55 Hz change " << low << " dB, 880 Hz change " << high << " dB");
    REQUIRE(low < -2.5f);                 // conflicting region attenuated (on average over kick cycles)
    REQUIRE(std::abs(high) < 0.3f);       // upper content preserved
    REQUIRE(d.telemetry().maxReductionDb.load() <= 12.0f + 1e-3f);
}

TEST_CASE("Sidechain: band isolation - only the band at the conflict frequency acts")
{
    const double sr = 48000.0; const size_t n = size_t(sr * 3);
    SpectralDucker d; d.prepare(sr, 512);
    d.setParams(manualParams(100.0f));
    auto in = drone(sr, n, 100.0, 400.0);
    auto ref = tone(sr, n, 100.0, 0.5f); // steady bass note at 100 Hz
    auto out = run(d, in, &ref, &ref);
    const size_t a = size_t(sr * 1.5);
    REQUIRE(levelDb(out.l, a, n, 100.0, sr) - levelDb(in.l, a, n, 100.0, sr) < -6.0f);
    REQUIRE(std::abs(levelDb(out.l, a, n, 400.0, sr) - levelDb(in.l, a, n, 400.0, sr)) < 0.5f);
}

TEST_CASE("Sidechain: overlap is not conflict - a much quieter internal band is left alone")
{
    const double sr = 48000.0; const size_t n = size_t(sr * 3);
    SpectralDucker d; d.prepare(sr, 512);
    auto p = manualParams(60.0f);
    p.sensitivity = 0.0f; // masking window 12 dB
    d.setParams(p);
    Stereo in(n);
    in.l = tone(sr, n, 60.0, 0.003f); in.r = in.l;   // internal ~ -50 dBFS at 60 Hz
    auto ref = tone(sr, n, 60.0, 0.7f);              // reference ~ -3 dBFS
    auto out = run(d, in, &ref, &ref);
    REQUIRE(d.telemetry().maxReductionDb.load() < 0.05f);
    REQUIRE(std::abs(levelDb(out.l, size_t(sr), n, 60.0, sr) - levelDb(in.l, size_t(sr), n, 60.0, sr)) < 0.1f);
}

TEST_CASE("Sidechain: reference below threshold -> no reduction")
{
    const double sr = 48000.0; const size_t n = size_t(sr * 2);
    SpectralDucker d; d.prepare(sr, 512);
    auto p = manualParams(55.0f);
    p.bands[0].threshold = -12.0f;
    d.setParams(p);
    auto in = drone(sr, n, 55.0, 880.0);
    auto ref = kicks(sr, n, 145.0, 0.05f); // ~ -26 dBFS peaks
    run(d, in, &ref, &ref);
    REQUIRE(d.telemetry().maxReductionDb.load() == 0.0f);
}

TEST_CASE("Sidechain: automatic detection finds different fundamentals")
{
    for (double f : { 42.0, 55.0, 70.0, 98.0 })
    {
        const double sr = 48000.0; const size_t n = size_t(sr * 3);
        SpectralDucker d; d.prepare(sr, 512);
        SpectralDucker::Params p;
        p.automatic = true;
        p.rangeLow = 30.0f; p.rangeHigh = 250.0f;
        for (auto& b : p.bands) b.enabled = false;
        p.bands[0].enabled = true;
        p.bands[0].freq = 150.0f; // deliberately wrong start
        d.setParams(p);
        auto ref = kicks(sr, n, 145.0, 0.8f, f, f * 2.2);
        Stereo in = drone(sr, n, f, 880.0);
        run(d, in, &ref, &ref);
        const float found = d.getBandFrequency(0);
        INFO("fundamental " << f << " Hz -> band at " << found << " Hz");
        REQUIRE(std::abs(std::log2(found / float(f))) < 1.0f / 6.0f); // within 1/6 octave
    }
}

TEST_CASE("Sidechain: automatic detection follows changing bass notes and multiple bands")
{
    const double sr = 48000.0; const size_t half = size_t(sr * 2.5);
    SpectralDucker d; d.prepare(sr, 512);
    SpectralDucker::Params p;
    p.automatic = true;
    p.rangeLow = 30.0f; p.rangeHigh = 400.0f;
    p.bands[0].enabled = p.bands[1].enabled = true;
    p.bands[2].enabled = p.bands[3].enabled = false;
    d.setParams(p);

    // kick at 50 Hz + bass note at 110 Hz, then the bass moves to 165 Hz
    auto k1 = kicks(sr, half, 145.0, 0.7f, 50.0, 120.0);
    auto ref1 = add(k1, tone(sr, half, 110.0, 0.3f));
    auto ref2 = add(k1, tone(sr, half, 165.0, 0.3f));
    Stereo in = drone(sr, half, 60.0, 880.0);
    run(d, in, &ref1, &ref1);
    float lo = std::min(d.getBandFrequency(0), d.getBandFrequency(1)), hi = std::max(d.getBandFrequency(0), d.getBandFrequency(1));
    INFO("bands at " << lo << " / " << hi);
    REQUIRE(std::abs(std::log2(lo / 50.0f)) < 0.25f);
    REQUIRE(std::abs(std::log2(hi / 110.0f)) < 0.25f);

    run(d, in, &ref2, &ref2);
    lo = std::min(d.getBandFrequency(0), d.getBandFrequency(1)); hi = std::max(d.getBandFrequency(0), d.getBandFrequency(1));
    INFO("after note change: " << lo << " / " << hi);
    REQUIRE(std::abs(std::log2(lo / 50.0f)) < 0.25f);
    REQUIRE(std::abs(std::log2(hi / 165.0f)) < 0.25f);
}

TEST_CASE("Sidechain: mono and stereo references give the same result")
{
    const double sr = 48000.0; const size_t n = size_t(sr * 2);
    auto ref = kicks(sr, n, 145.0, 0.8f);
    auto in = drone(sr, n, 55.0, 880.0);
    SpectralDucker a; a.prepare(sr, 512); a.setParams(manualParams(55.0f));
    SpectralDucker b; b.prepare(sr, 512); b.setParams(manualParams(55.0f));
    auto outMono = run(a, in, &ref, nullptr);  // mono sidechain bus (refR = refL)
    auto outSt   = run(b, in, &ref, &ref);     // stereo bus with identical channels
    for (size_t i = 0; i < n; ++i) REQUIRE(outMono.l[i] == outSt.l[i]);
}

TEST_CASE("Sidechain: transient detection counts kicks and orders strengths")
{
    const double sr = 48000.0; const size_t n = size_t(sr * 60.0 / 145.0 * 8); // 8 kicks
    auto strong = kicks(sr, n, 145.0, 0.9f), weak = kicks(sr, n, 145.0, 0.05f);
    auto in = drone(sr, n, 55.0, 880.0);

    SpectralDucker a; a.prepare(sr, 512); a.setParams(manualParams(55.0f));
    run(a, in, &strong, &strong);
    REQUIRE(a.telemetry().transientCount.load() == 8u);
    const float sStrong = a.telemetry().lastTransientStrength.load();

    SpectralDucker b; b.prepare(sr, 512); b.setParams(manualParams(55.0f));
    run(b, in, &weak, &weak);
    REQUIRE(b.telemetry().transientCount.load() == 8u);
    REQUIRE(b.telemetry().lastTransientStrength.load() < sStrong);

    // steady bass (no attacks) must not trigger
    SpectralDucker c; c.prepare(sr, 512); c.setParams(manualParams(55.0f));
    auto bass = tone(sr, n, 55.0, 0.5f);
    const size_t onset = size_t(sr * 0.5);
    std::vector<float> first(bass.begin(), bass.begin() + long(onset)), rest(bass.begin() + long(onset), bass.end());
    Stereo inA(onset), inB(n - onset);
    run(c, inA, &first, &first);                       // note onset may count once
    const auto afterOnset = c.telemetry().transientCount.load();
    REQUIRE(afterOnset <= 1u);
    run(c, inB, &rest, &rest);                          // sustained note: no further triggers
    REQUIRE(c.telemetry().transientCount.load() == afterOnset);
}

TEST_CASE("Sidechain: bypass, delta, listen and no reference leakage")
{
    const double sr = 48000.0; const size_t n = size_t(sr * 2);
    auto ref = kicks(sr, n, 145.0, 0.8f);
    auto in = drone(sr, n, 55.0, 880.0);

    SECTION("disabled -> output equals input after the 10 ms crossfade")
    {
        SpectralDucker d; d.prepare(sr, 512);
        auto p = manualParams(55.0f); p.enabled = false;
        d.setParams(p); d.reset();
        auto out = run(d, in, &ref, &ref);
        for (size_t i = 0; i < n; ++i) REQUIRE(out.l[i] == Approx(in.l[i]).margin(1e-6));
    }
    SECTION("delta + processed == dry")
    {
        SpectralDucker a; a.prepare(sr, 512); a.setParams(manualParams(55.0f));
        auto p = manualParams(55.0f); p.delta = true;
        SpectralDucker b; b.prepare(sr, 512); b.setParams(p); b.reset();
        auto wet = run(a, in, &ref, &ref);
        auto del = run(b, in, &ref, &ref);
        for (size_t i = 0; i < n; ++i) REQUIRE(wet.l[i] + del.l[i] == Approx(in.l[i]).margin(1e-5));
    }
    SECTION("internal silent, loud reference -> output silent (no leakage)")
    {
        SpectralDucker d; d.prepare(sr, 512); d.setParams(manualParams(55.0f));
        Stereo silent(n);
        auto out = run(d, silent, &ref, &ref);
        float peak = 0.0f;
        for (size_t i = 0; i < n; ++i) peak = std::max({ peak, std::abs(out.l[i]), std::abs(out.r[i]) });
        REQUIRE(peak == 0.0f);
    }
    SECTION("listen -> output is the reference")
    {
        SpectralDucker d; d.prepare(sr, 512);
        auto p = manualParams(55.0f); p.listen = true;
        d.setParams(p); d.reset();
        auto out = run(d, in, &ref, &ref);
        for (size_t i = 0; i < n; ++i) REQUIRE(out.l[i] == Approx(ref[i]).margin(1e-6));
    }
}

TEST_CASE("Sidechain: no clicks - attenuation never creates steps faster than the signal itself")
{
    const double sr = 48000.0; const size_t n = size_t(sr * 3);
    SpectralDucker d; d.prepare(sr, 512);
    auto p = manualParams(50.0f);
    p.bands[0].attackMs = 0.5f;   // most aggressive settings
    p.bands[0].releaseMs = 10.0f;
    p.bands[0].maxReduction = 24.0f;
    p.sensitivity = 1.0f;
    d.setParams(p);
    Stereo in(n);
    in.l = tone(sr, n, 50.0, 0.5f); in.r = in.l;
    auto ref = kicks(sr, n, 175.0, 0.9f);
    auto out = run(d, in, &ref, &ref, 64);
    // Max slope of the input sine is 2*pi*f*A/fs; allow modest headroom for gain motion.
    const float inSlope = float(2.0 * kPi * 50.0 * 0.5 / sr);
    float maxStep = 0.0f;
    for (size_t i = 1; i < n; ++i) maxStep = std::max(maxStep, std::abs(out.l[i] - out.l[i - 1]));
    INFO("max step " << maxStep << " vs input slope " << inSlope);
    REQUIRE(maxStep < inSlope * 1.2f); // ducking adds no steps of its own
}

TEST_CASE("Sidechain: stable under extreme settings, sample rates and block sizes")
{
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (int block : { 1, 17, 64, 512, 2048 })
        {
            const size_t n = size_t(sr * 0.75);
            SpectralDucker d; d.prepare(sr, block);
            auto p = manualParams(30.0f);
            for (auto& b : p.bands) { b.enabled = true; b.q = 12.0f; b.maxReduction = 24.0f; b.attackMs = 0.5f; b.releaseMs = 10.0f; b.threshold = -60.0f; }
            p.bands[3].freq = 18000.0f;
            p.automatic = (block % 2) == 1;
            p.sensitivity = 1.0f;
            d.setParams(p);
            Stereo in(n);
            uint32_t seed = 7;
            for (size_t i = 0; i < n; ++i) { seed = seed * 1664525u + 1013904223u; in.l[i] = float(int(seed >> 8) - (1 << 23)) / float(1 << 23); in.r[i] = -in.l[i]; }
            auto ref = kicks(sr, n, 180.0, 1.0f);
            auto out = run(d, in, &ref, &ref, block);
            // Finite and bounded. Cut filters can raise the peak of full-scale noise by a
            // few percent through phase shift; instability would grow without bound.
            float peak = 0.0f;
            for (size_t i = 0; i < n; ++i) { REQUIRE(std::isfinite(out.l[i])); peak = std::max(peak, std::abs(out.l[i])); }
            REQUIRE(peak < 1.25f);
        }
}

TEST_CASE("Sidechain: full-band mode ducks everything, spectral mode keeps highs")
{
    const double sr = 48000.0; const size_t n = size_t(sr * 3);
    auto ref = kicks(sr, n, 145.0, 0.8f);
    auto in = drone(sr, n, 55.0, 880.0);
    auto p = manualParams(55.0f); p.fullBand = true;
    SpectralDucker d; d.prepare(sr, 512); d.setParams(p);
    auto out = run(d, in, &ref, &ref);
    const size_t a = size_t(sr * 1.5);
    REQUIRE(levelDb(out.l, a, n, 880.0, sr) - levelDb(in.l, a, n, 880.0, sr) < -1.0f);
}

TEST_CASE("Sidechain: latency is zero and bell display magnitude matches the filter")
{
    REQUIRE(SpectralDucker::getLatencySamples() == 0);
    REQUIRE(Bell::magnitudeDb(48000.0, 60.0f, 1.2f, 9.0f, 60.0f) == Approx(-9.0f).margin(0.01));
    REQUIRE(std::abs(Bell::magnitudeDb(48000.0, 60.0f, 1.2f, 9.0f, 2000.0f)) < 0.1f);

    // measured: steady 9 dB cut applied to a 60 Hz tone and a 120 Hz tone matches the display formula
    const double sr = 48000.0; const size_t n = size_t(sr);
    for (double f : { 60.0, 120.0 })
    {
        Bell b; b.set(sr, 60.0f, 1.2f);
        const float c = Bell::cutForDb(9.0f);
        auto x = tone(sr, n, f, 0.5f), y = x;
        for (auto& v : y) v = b.process(v, c);
        REQUIRE(levelDb(y, n / 2, n, f, sr) - levelDb(x, n / 2, n, f, sr)
                == Approx(Bell::magnitudeDb(sr, 60.0f, 1.2f, 9.0f, float(f))).margin(0.1));
    }
}

TEST_CASE("Sidechain: modulation outputs are bounded and stable IDs")
{
    const double sr = 48000.0; const size_t n = size_t(sr * 1.5);
    SpectralDucker d; d.prepare(sr, 512); d.setParams(manualParams(55.0f));
    auto ref = kicks(sr, n, 145.0, 0.8f);
    auto in = drone(sr, n, 55.0, 880.0);
    run(d, in, &ref, &ref);
    REQUIRE(d.getNumModOutputs() == 7);
    REQUIRE(int(d.getModOutputInfo(0).id) == 100);
    REQUIRE(int(d.getModOutputInfo(6).id) == 106);
    for (int i = 0; i < 7; ++i) { REQUIRE(d.getModOutput(i) >= 0.0f); REQUIRE(d.getModOutput(i) <= 1.0f); }
    REQUIRE(d.getModOutput(2) > d.getModOutput(4)); // kick: low energy > high energy
}
