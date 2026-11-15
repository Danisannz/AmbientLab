// Stereo correlation / mono compatibility per stage.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <ambient/dsp/Reverb.h>
#include <ambient/dsp/SynthEngine.h>
#include <ambient/dsp/Granular.h>
#include "StereoMetrics.h"
#include <cstdio>

using namespace ambient::dsp;
using stereo_test::measure;

namespace
{
constexpr double kSr = 48000.0;
/** Mono-centred ambient pad: detuned saws (in phase on both sides) - a mono source. */
void pad(std::vector<float>& L, std::vector<float>& R, double seconds, double gateSeconds)
{
    const size_t n = size_t(kSr * seconds);
    L.assign(n, 0.0f); R.assign(n, 0.0f);
    const double f[] = { 110.0, 164.8, 220.5, 277.2 };
    for (size_t i = 0; i < size_t(kSr * gateSeconds) && i < n; ++i)
    {
        double x = 0;
        for (double fr : f) { const double ph = std::fmod(fr * double(i) / kSr, 1.0); x += 2.0 * ph - 1.0; }
        L[i] = R[i] = float(0.12 * x);
    }
}
void print(const char* name, const stereo_test::Metrics& m)
{
    std::printf("  %-26s corr %+5.2f  low<200Hz %+5.2f  mono-sum %+6.2f dB  S/M %+6.1f dB  neg-windows %4.0f %%  worst %+5.2f\n",
                name, m.corr, m.lowCorr, m.monoLossDb, m.sideToMidDb, 100.0 * m.negFraction, m.minWinCorr);
}
} // namespace

TEST_CASE("Stereo: per-stage correlation report", "[stereo][report]")
{
    std::printf("\nPer-stage stereo report (48 kHz, mono pad in; tail measured after the pad stops):\n");
    // Synth: default patch, 4-note chord
    {
        SynthEngine s; s.prepare(kSr, 512);
        SynthEngine::Params p; s.setParams(p);
        for (int nn : { 48, 55, 60, 64 }) s.noteOn(nn, 0.8f);
        std::vector<float> L(size_t(kSr * 3)), R(size_t(kSr * 3));
        for (size_t i = 0; i < L.size(); i += 512) s.render(L.data(), R.data(), int(i), int(std::min(L.size(), i + 512)));
        print("synth (A unison 3)", measure(L, R, kSr, size_t(kSr)));
    }
    // Granular default
    {
        Granular g; g.prepare(kSr, 512);
        Granular::Params p; p.on = true; p.mix = 1.0f; g.setParams(p);
        std::vector<float> L, R; pad(L, R, 4.0, 4.0);
        for (size_t i = 0; i < L.size(); i += 512) g.process(L.data() + i, R.data() + i, int(std::min<size_t>(512, L.size() - i)));
        print("granular (spread 50%)", measure(L, R, kSr, size_t(kSr)));
    }
    // Reverb: each algorithm, defaults, 100 % wet
    const char* names[] = { "Hall", "Room", "Cathedral", "Plate", "Spring", "Shimmer", "Infinite" };
    for (int a = 0; a < 7; ++a)
    {
        Reverb r; r.prepare(kSr, 512);
        Reverb::Params p; p.on = true; p.algo = Reverb::Algo(a); p.mix = 1.0f;
        r.setParams(p); r.reset();
        std::vector<float> L, R; pad(L, R, 5.0, 1.5);
        for (size_t i = 0; i < L.size(); i += 512) r.process(L.data() + i, R.data() + i, int(std::min<size_t>(512, L.size() - i)));
        char nm[64]; std::snprintf(nm, sizeof nm, "reverb %s (wet)", names[a]);
        print(nm, measure(L, R, kSr, size_t(kSr * 0.3)));
    }
}

namespace
{
stereo_test::Metrics reverbMetrics(int algo, float width, double seconds = 5.0)
{
    Reverb r; r.prepare(kSr, 512);
    Reverb::Params p; p.on = true; p.algo = Reverb::Algo(algo); p.mix = 1.0f; p.width = width;
    r.setParams(p); r.reset();
    std::vector<float> L, R; pad(L, R, seconds, 1.5);
    for (size_t i = 0; i < L.size(); i += 512) r.process(L.data() + i, R.data() + i, int(std::min<size_t>(512, L.size() - i)));
    return measure(L, R, kSr, size_t(kSr * 0.3));
}
} // namespace

TEST_CASE("Stereo: reverb width report", "[stereo][report]")
{
    const char* names[] = { "Hall", "Room", "Cathedral", "Plate", "Spring", "Shimmer", "Infinite" };
    for (float w : { 1.0f, 0.7f })
    {
        std::printf("\nReverb wet, width %.0f %%:\n", 100.0 * w);
        for (int a = 0; a < 7; ++a) print(names[a], reverbMetrics(a, w));
    }
}

TEST_CASE("Stereo: reverb is mono-compatible at the default width and never anti-phase at max width")
{
    for (int a = 0; a < 7; ++a)
    {
        const auto d = reverbMetrics(a, 0.7f);
        INFO("algo " << a << " default: corr " << d.corr << " low " << d.lowCorr << " mono " << d.monoLossDb << " neg " << d.negFraction);
        REQUIRE(d.corr > 0.2);
        REQUIRE(d.lowCorr > 0.7);           // wet bass close to mono (was -0.77 for Plate before the fix)
        REQUIRE(d.monoLossDb > -2.0);
        REQUIRE(d.negFraction < 0.1);
        const auto m = reverbMetrics(a, 1.0f);
        INFO("algo " << a << " max: corr " << m.corr << " low " << m.lowCorr << " neg " << m.negFraction);
        REQUIRE(m.corr > 0.1);              // wide but never systematically anti-phase
        REQUIRE(m.lowCorr > 0.45);
        REQUIRE(m.negFraction < 0.25);      // short bursts only; the output safety handles those
        REQUIRE(m.monoLossDb > -3.5);
        const auto z = reverbMetrics(a, 0.0f, 3.0);
        REQUIRE(z.corr > 0.999);            // width 0 = mono wet
    }
}

TEST_CASE("Stereo: reverb still has a real stereo image at the default width")
{
    for (int a = 0; a < 7; ++a)
    {
        const auto d = reverbMetrics(a, 0.7f);
        INFO("algo " << a << " corr " << d.corr << " S/M " << d.sideToMidDb);
        REQUIRE(d.corr < 0.85);            // not collapsed to mono
        REQUIRE(d.sideToMidDb > -12.0);
    }
}

#include <ambient/dsp/StereoSafety.h>

namespace
{
void noiseStereo(std::vector<float>& L, std::vector<float>& R, size_t n, float midAmt, float sideAmt, uint32_t seed = 1)
{
    L.assign(n, 0.0f); R.assign(n, 0.0f);
    for (size_t i = 0; i < n; ++i)
    {
        seed = seed * 1664525u + 1013904223u; const float a = float(int(seed >> 8) - (1 << 23)) / float(1 << 23);
        seed = seed * 1664525u + 1013904223u; const float b = float(int(seed >> 8) - (1 << 23)) / float(1 << 23);
        L[i] = 0.3f * (midAmt * a + sideAmt * b);
        R[i] = 0.3f * (midAmt * a - sideAmt * b);
    }
}
} // namespace

TEST_CASE("StereoSafety: transparent when neutral, zero latency")
{
    std::vector<float> L, R; noiseStereo(L, R, size_t(kSr), 1.0f, 0.6f);
    auto L0 = L, R0 = R;
    StereoSafety s; s.prepare(kSr);
    StereoSafety::Params p; p.bassMono = false; p.safety = true; p.width = 1.0f; // corr ~ +0.47: no correction
    s.setParams(p); s.reset();
    s.process(L.data(), R.data(), int(L.size()));
    float err = 0; for (size_t i = 0; i < L.size(); ++i) err = std::max({ err, std::fabs(L[i] - L0[i]), std::fabs(R[i] - R0[i]) });
    REQUIRE(err < 1.0e-5f);                     // bass mono off, width 1: exact, no delay
    REQUIRE(s.telemetry().correctionDb.load() < 0.01f);
    REQUIRE(StereoSafety::getLatencySamples() == 0);
}

TEST_CASE("StereoSafety: anti-phase material is pulled up to the correlation floor, not collapsed")
{
    for (float floor : { 0.0f, 0.3f })
    {
        std::vector<float> L, R; noiseStereo(L, R, size_t(kSr * 2), 0.4f, 1.0f);   // corr ~ -0.72
        const double before = stereo_test::pearson(L.data(), R.data(), 0, L.size());
        StereoSafety s; s.prepare(kSr);
        StereoSafety::Params p; p.bassMono = false; p.floor = floor; s.setParams(p); s.reset();
        s.process(L.data(), R.data(), int(L.size()));
        const auto m = measure(L, R, kSr, size_t(kSr * 0.5));
        INFO("floor " << floor << ": " << before << " -> " << m.corr << ", correction " << s.telemetry().correctionDb.load() << " dB");
        REQUIRE(before < -0.6);
        REQUIRE(m.corr > floor - 0.08);
        REQUIRE(m.corr < floor + 0.15);           // only as much as needed
        REQUIRE(s.telemetry().antiPhase.load());
    }
}

TEST_CASE("StereoSafety: bass mono, width, mono check")
{
    StereoSafety s; s.prepare(kSr);
    // 60 Hz in anti-phase + 2 kHz in anti-phase: bass mono removes only the low side.
    std::vector<float> L(static_cast<size_t>(kSr)), R(static_cast<size_t>(kSr));
    for (size_t i = 0; i < L.size(); ++i)
    {
        const float lo = 0.3f * float(std::sin(2 * 3.14159265358979 * 60.0 * double(i) / kSr));
        const float hi = 0.3f * float(std::sin(2 * 3.14159265358979 * 2000.0 * double(i) / kSr));
        L[i] = lo + hi; R[i] = -lo - hi;
    }
    StereoSafety::Params p; p.safety = false; p.bassMono = true; p.bassMonoHz = 150.0f; s.setParams(p); s.reset();
    auto l = L, r = R;
    s.process(l.data(), r.data(), int(l.size()));
    std::vector<float> side(l.size()); for (size_t i = 0; i < l.size(); ++i) side[i] = 0.5f * (l[i] - r[i]);
    auto tone = [&](const std::vector<float>& x, double f) { double re = 0, im = 0; for (size_t i = x.size() / 2; i < x.size(); ++i) { re += x[i] * std::cos(2 * 3.14159265358979 * f * double(i) / kSr); im += x[i] * std::sin(2 * 3.14159265358979 * f * double(i) / kSr); } return 2.0 * std::sqrt(re * re + im * im) / double(x.size() / 2); };
    REQUIRE(tone(side, 60.0) < 0.3 * 0.2);       // > 14 dB less low side
    REQUIRE(tone(side, 2000.0) > 0.3 * 0.95);    // highs untouched

    p.bassMono = false; p.width = 0.5f; s.setParams(p); s.reset();
    l = L; r = R; s.process(l.data(), r.data(), int(l.size()));
    for (size_t i = 0; i < l.size(); ++i) side[i] = 0.5f * (l[i] - r[i]);
    REQUIRE(tone(side, 2000.0) == Catch::Approx(0.15).margin(0.01));

    p.width = 1.0f; p.monoCheck = true; s.setParams(p); s.reset();
    l = L; r = R; s.process(l.data(), r.data(), int(l.size()));
    for (size_t i = size_t(kSr * 0.2); i < l.size(); ++i) REQUIRE(l[i] == r[i]);
}

TEST_CASE("StereoSafety: stable and finite at all rates and block sizes")
{
    for (double rate : { 44100.0, 96000.0, 192000.0 })
        for (int bs : { 1, 512 })
        {
            StereoSafety s; s.prepare(rate);
            StereoSafety::Params p; p.width = 1.5f; p.bassMonoHz = 400.0f; p.floor = 0.5f; s.setParams(p); s.reset();
            std::vector<float> L, R; noiseStereo(L, R, size_t(rate), 0.0f, 3.0f);   // pure anti-phase, loud
            for (size_t i = 0; i < L.size(); i += size_t(bs)) s.process(L.data() + i, R.data() + i, int(std::min<size_t>(size_t(bs), L.size() - i)));
            for (size_t i = 0; i < L.size(); ++i) { REQUIRE(std::isfinite(L[i])); REQUIRE(std::fabs(L[i]) < 2.0f); }
        }
}
