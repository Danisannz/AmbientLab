// Filter + envelope (Stage 3) tests.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <ambient/dsp/Envelope.h>
#include <ambient/dsp/Filter.h>
#include <ambient/dsp/SynthEngine.h>
#include <cmath>
#include <vector>

using namespace ambient::dsp;
using Catch::Approx;

namespace
{
constexpr double kPi = 3.14159265358979323846;

/** Steady-state gain (dB) of the filter for a sine at f. */
float responseDb(MultiFilter::Mode m, float fc, float reso, double f, double sr = 48000.0)
{
    MultiFilter flt;
    flt.set(sr, fc, reso, m);
    const int n = int(sr * 0.5);
    double in = 0, out = 0;
    for (int i = 0; i < n; ++i)
    {
        const float x = float(std::sin(2.0 * kPi * f * i / sr));
        const float y = flt.process(x);
        if (i > n / 2) { in += double(x) * x; out += double(y) * y; }
    }
    return float(10.0 * std::log10(out / in));
}
} // namespace

TEST_CASE("Filter: low-pass / high-pass slopes and cutoff")
{
    const float fc = 1000.0f;
    REQUIRE(responseDb(MultiFilter::Mode::LP12, fc, 0.0f, 100.0) == Approx(0.0f).margin(0.2f));
    REQUIRE(responseDb(MultiFilter::Mode::LP12, fc, 0.0f, 1000.0) == Approx(-3.0f).margin(0.5f));  // Q 0.707 at fc
    const float lp12Oct = responseDb(MultiFilter::Mode::LP12, fc, 0.0f, 4000.0) - responseDb(MultiFilter::Mode::LP12, fc, 0.0f, 8000.0);
    const float lp24Oct = responseDb(MultiFilter::Mode::LP24, fc, 0.0f, 4000.0) - responseDb(MultiFilter::Mode::LP24, fc, 0.0f, 8000.0);
    INFO("LP12 " << lp12Oct << " dB/oct, LP24 " << lp24Oct << " dB/oct");
    REQUIRE(lp12Oct == Approx(12.0f).margin(1.5f));
    REQUIRE(lp24Oct == Approx(24.0f).margin(3.0f));
    REQUIRE(responseDb(MultiFilter::Mode::HP12, fc, 0.0f, 10000.0) == Approx(0.0f).margin(0.3f));
    REQUIRE(responseDb(MultiFilter::Mode::HP24, fc, 0.0f, 250.0) < -40.0f);
}

TEST_CASE("Filter: band-pass peaks at cutoff, notch removes it")
{
    const float fc = 800.0f;
    REQUIRE(responseDb(MultiFilter::Mode::BP, fc, 0.5f, 800.0) == Approx(0.0f).margin(0.3f));
    REQUIRE(responseDb(MultiFilter::Mode::BP, fc, 0.5f, 3200.0) < -12.0f);
    REQUIRE(responseDb(MultiFilter::Mode::Notch, fc, 0.3f, 800.0) < -40.0f);
    REQUIRE(responseDb(MultiFilter::Mode::Notch, fc, 0.3f, 6400.0) == Approx(0.0f).margin(0.5f));
}

TEST_CASE("Filter: resonance raises the peak and stays stable at maximum")
{
    const float lo = responseDb(MultiFilter::Mode::LP12, 1000.0f, 0.0f, 1000.0);
    const float hi = responseDb(MultiFilter::Mode::LP12, 1000.0f, 0.8f, 1000.0);
    REQUIRE(hi > lo + 15.0f);
    // maximum resonance + drive + fast cutoff sweep: bounded and finite
    MultiFilter f;
    f.setDrive(8.0f);
    float peak = 0.0f;
    uint32_t seed = 3;
    for (int i = 0; i < 96000; ++i)
    {
        if (i % 8 == 0) f.set(48000.0, 100.0f + 9000.0f * float(0.5 + 0.5 * std::sin(i * 0.0005)), 1.0f, MultiFilter::Mode::LP24);
        seed = seed * 1664525u + 1013904223u;
        const float y = f.process(float(int(seed >> 8) - (1 << 23)) / float(1 << 23));
        REQUIRE(std::isfinite(y));
        peak = std::max(peak, std::fabs(y));
    }
    REQUIRE(peak < 50.0f);
}

TEST_CASE("Envelope: attack, decay to sustain, release timing")
{
    const double sr = 48000.0;
    Adsr e; e.setSampleRate(sr);
    e.setParams({ 10.0f, 100.0f, 0.5f, 200.0f });
    e.noteOn();
    int reachedPeak = -1;
    for (int i = 0; i < 48000; ++i)
    {
        const float v = e.next();
        if (reachedPeak < 0 && v >= 1.0f) reachedPeak = i;
    }
    REQUIRE(reachedPeak == Approx(480).margin(3));     // 10 ms attack
    REQUIRE(e.getLevel() == Approx(0.5f).margin(1e-3)); // sustain
    e.noteOff();
    int at60 = -1;
    for (int i = 0; i < 48000; ++i) if (e.next() < 0.5f * 0.001f && at60 < 0) at60 = i;
    REQUIRE(at60 == Approx(9600).margin(30));            // 200 ms to -60 dB
    for (int i = 0; i < 48000; ++i) e.next();
    REQUIRE_FALSE(e.isActive());
}

TEST_CASE("Envelope: retrigger continues from the current level, zero attack still click-free")
{
    Adsr e; e.setSampleRate(48000.0);
    e.setParams({ 0.0f, 50.0f, 0.3f, 500.0f });
    e.noteOn();
    float prev = 0.0f, maxStep = 0.0f;
    for (int i = 0; i < 4800; ++i) { const float v = e.next(); maxStep = std::max(maxStep, std::fabs(v - prev)); prev = v; }
    e.noteOff();
    for (int i = 0; i < 2400; ++i) { const float v = e.next(); maxStep = std::max(maxStep, std::fabs(v - prev)); prev = v; }
    const float before = e.getLevel();
    e.noteOn(); // retrigger mid-release
    const float after = e.next();
    REQUIRE(after >= before);                     // no drop to zero
    REQUIRE(after - before < 0.08f); // steepest step of the 0.5 ms minimum exponential attack
    REQUIRE(maxStep < 0.085f);                    // 0.5 ms minimum attack: steepest step 1.3*ln(1.3/0.3)/24 = 0.079
}

TEST_CASE("Engine: amp envelope shapes notes; mod envelope sweeps cutoff and pitch")
{
    const double sr = 48000.0;
    SynthEngine e; e.prepare(sr, 256);
    SynthEngine::Params p;
    p.a.wave = Wave::Saw; p.a.unison = 1; p.b.on = false; p.subOn = false; p.drift = 0.0f;
    p.filterMode = 0; p.cutoff = 200.0f; p.resonance = 0.1f; p.keytrack = 0.0f;
    p.filterEnv = 1.0f;                                  // +6 octaves at the envelope peak
    p.modEnv = { 1.0f, 300.0f, 0.0f, 300.0f };
    p.ampEnv = { 1.0f, 50.0f, 1.0f, 100.0f };
    e.setParams(p);
    const int n = int(sr);
    std::vector<float> l(size_t(n), 0.0f), r(size_t(n), 0.0f);
    e.noteOn(45, 1.0f); // 110 Hz
    e.render(l.data(), r.data(), 0, n);
    // brightness: zero-crossing-ish high-frequency energy early vs late
    auto hfEnergy = [&](int from, int to)
    {
        double hf = 0, all = 0;
        for (int i = from + 1; i < to; ++i) { hf += std::pow(double(l[size_t(i)] - l[size_t(i - 1)]), 2); all += double(l[size_t(i)]) * l[size_t(i)]; }
        return hf / all;
    };
    REQUIRE(hfEnergy(480, 2880) > 4.0 * hfEnergy(36000, 40800)); // opens then closes

    // pitch envelope: +12 semitones at the peak decays back
    SynthEngine e2; e2.prepare(sr, 256);
    p.filterOn = false; p.a.wave = Wave::Sine; p.modEnvPitch = 12.0f; p.modEnv = { 1.0f, 2000.0f, 0.0f, 100.0f };
    e2.setParams(p);
    std::vector<float> a(size_t(n), 0.0f), b(size_t(n), 0.0f);
    e2.noteOn(57, 1.0f); // 220 Hz -> ~440 Hz at the start
    e2.render(a.data(), b.data(), 0, n);
    auto zc = [&](int from, int len) { int c = 0; for (int i = from + 1; i < from + len; ++i) if ((a[size_t(i - 1)] < 0) != (a[size_t(i)] < 0)) ++c; return c * 0.5 * sr / len; };
    REQUIRE(zc(48, 1200) > 350.0);
    REQUIRE(zc(40000, 4800) < 260.0);

    // amp: note off -> silence after release
    e.allNotesOff();
    std::vector<float> t(size_t(sr), 0.0f), tr(size_t(sr), 0.0f);
    e.render(t.data(), tr.data(), 0, int(sr));
    REQUIRE(e.activeVoices() == 0);
}

TEST_CASE("Engine: keytracking moves the cutoff with the played note")
{
    const double sr = 48000.0;
    // 3rd-harmonic / fundamental ratio (dB) of a filtered saw: with full keytracking the
    // filter moves with the note, so the ratio stays put; without it, high notes lose harmonics.
    auto h3overH1 = [&](int note, float key)
    {
        SynthEngine e; e.prepare(sr, 256);
        SynthEngine::Params p;
        p.a.wave = Wave::Saw; p.a.unison = 1; p.b.on = false; p.subOn = false; p.drift = 0.0f;
        p.filterMode = 1; p.cutoff = 500.0f; p.resonance = 0.0f; p.keytrack = key; p.ampEnv = { 1.0f, 10.0f, 1.0f, 50.0f };
        e.setParams(p);
        std::vector<float> l(48000, 0.0f), r(48000, 0.0f);
        e.noteOn(note, 1.0f);
        e.render(l.data(), r.data(), 0, 48000);
        const double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        auto level = [&](double f)
        {
            double re = 0, im = 0;
            for (size_t i = 9600; i < l.size(); ++i) { re += l[i] * std::cos(2 * kPi * f * double(i) / sr); im += l[i] * std::sin(2 * kPi * f * double(i) / sr); }
            return 10.0 * std::log10(re * re + im * im);
        };
        return level(3.0 * f0) - level(f0);
    };
    const double noKeyChange = h3overH1(84, 0.0f) - h3overH1(60, 0.0f);
    const double fullKeyChange = h3overH1(84, 1.0f) - h3overH1(60, 1.0f);
    INFO("h3/h1 change C4->C6: no keytrack " << noKeyChange << " dB, full keytrack " << fullKeyChange << " dB");
    REQUIRE(noKeyChange < -8.0);               // fixed cutoff: the high note loses its harmonics
    REQUIRE(std::abs(fullKeyChange) < 2.0);    // tracked cutoff: same timbre two octaves up
}
