// Oscillator engine (Stage 2) tests.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <ambient/dsp/SynthEngine.h>
#include <ambient/dsp/Fft.h>
#include <cmath>
#include <complex>
#include <vector>

using namespace ambient::dsp;
using Catch::Approx;

namespace
{
constexpr double kPi = 3.14159265358979323846;

struct Spectrum { std::vector<double> power; double binHz; };

Spectrum spectrum(const std::vector<float>& x, double sr, size_t offset = 0)
{
    const int N = 1 << 15;
    Fft fft; fft.prepare(N);
    std::vector<std::complex<float>> buf(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (N - 1));
        buf[size_t(i)] = { float(x[offset + size_t(i)] * w), 0.0f };
    }
    fft.perform(buf.data());
    Spectrum s { std::vector<double>(size_t(N / 2)), sr / N };
    for (int k = 0; k < N / 2; ++k) s.power[size_t(k)] = std::norm(buf[size_t(k)]);
    return s;
}

double peakHz(const Spectrum& s, double fLo = 10.0)
{
    size_t best = size_t(fLo / s.binHz);
    for (size_t k = best; k + 1 < s.power.size(); ++k) if (s.power[k] > s.power[best]) best = k;
    const double a = std::log(s.power[best - 1] + 1e-30), b = std::log(s.power[best] + 1e-30), c = std::log(s.power[best + 1] + 1e-30);
    const double d = 0.5 * (a - c) / (a - 2 * b + c);
    return (double(best) + d) * s.binHz;
}

/** Non-harmonic / harmonic energy ratio in dB. */
double aliasDb(const Spectrum& s, double f0)
{
    double harm = 0, alias = 0;
    for (size_t k = 2; k < s.power.size(); ++k)
    {
        const double fk = double(k) * s.binHz, nh = fk / f0;
        if (std::fabs(nh - std::round(nh)) * f0 < 6.0 * s.binHz) harm += s.power[k]; else alias += s.power[k];
    }
    return 10.0 * std::log10(alias / harm);
}

std::vector<float> oscillator(Wave w, double f, double sr, int n, bool naive, float pw = 0.3f)
{
    std::vector<float> y(static_cast<size_t>(n));
    BlepOsc o;
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        if (! naive) { y[size_t(i)] = o.next(f / sr, w, pw); continue; }
        const double t = ph - std::floor(ph);
        switch (w)
        {
            case Wave::Saw: y[size_t(i)] = float(2 * t - 1); break;
            case Wave::Square: y[size_t(i)] = t < 0.5 ? 1.0f : -1.0f; break;
            case Wave::Triangle: y[size_t(i)] = float(1 - 4 * std::fabs(t - 0.5)); break;
            default: y[size_t(i)] = t < pw ? 1.0f : -1.0f; break;
        }
        ph += f / sr;
    }
    return y;
}

/** Renders the engine for `seconds`, one note held. */
struct Render { std::vector<float> l, r; };
Render play(SynthEngine& e, int note, double sr, double seconds, int block = 256, float vel = 1.0f)
{
    const size_t n = size_t(sr * seconds);
    Render out { std::vector<float>(n, 0.0f), std::vector<float>(n, 0.0f) };
    e.noteOn(note, vel);
    for (size_t s = 0; s < n; s += size_t(block))
        e.render(out.l.data(), out.r.data(), int(s), int(std::min(n, s + size_t(block))));
    return out;
}

SynthEngine::Params single(Wave w)
{
    SynthEngine::Params p;
    p.a.wave = w; p.a.unison = 1; p.a.level = 1.0f;
    p.b.on = false; p.subOn = false; p.noiseOn = false; p.drift = 0.0f; p.satDrive = 0.0f;
    return p;
}
} // namespace

TEST_CASE("Oscillators: PolyBLEP/BLAMP reduce aliasing versus naive waveforms")
{
    const double sr = 48000.0;
    for (Wave w : { Wave::Saw, Wave::Square, Wave::Pulse, Wave::Triangle })
        for (double f : { 1318.5, 3520.0 })
        {
            const double naive = aliasDb(spectrum(oscillator(w, f, sr, 1 << 15, true), sr), f);
            const double blep  = aliasDb(spectrum(oscillator(w, f, sr, 1 << 15, false), sr), f);
            INFO("wave " << int(w) << " f " << f << ": naive " << naive << " dB, band-limited " << blep << " dB");
            REQUIRE(blep < naive - 9.0);
        }
}

TEST_CASE("Oscillators: waveforms bounded, pulse width without DC shift")
{
    for (Wave w : { Wave::Sine, Wave::Triangle, Wave::Saw, Wave::Square, Wave::Pulse })
    {
        auto y = oscillator(w, 220.0, 48000.0, 48000, false, 0.2f);
        double mean = 0.0; float peak = 0.0f;
        for (float v : y) { mean += v; peak = std::max(peak, std::fabs(v)); }
        mean /= double(y.size());
        INFO("wave " << int(w));
        REQUIRE(peak < 1.7f);            // pulse DC correction can reach 1.6 at 20 % width
        REQUIRE(std::fabs(mean) < 0.01);
    }
}

TEST_CASE("Engine: tuning - note, octave, semitone and fine")
{
    const double sr = 48000.0;
    struct Case { int note, oct, semi; float fine; double hz; };
    for (auto c : { Case { 69, 0, 0, 0.0f, 440.0 }, Case { 57, 1, 0, 0.0f, 440.0 }, Case { 69, 0, 7, 0.0f, 659.255 },
                    Case { 69, -2, 0, 0.0f, 110.0 }, Case { 69, 0, 0, 50.0f, 452.893 } })
    {
        SynthEngine e; e.prepare(sr, 256);
        auto p = single(Wave::Sine); p.a.octave = c.oct; p.a.semi = c.semi; p.a.fine = c.fine;
        e.setParams(p);
        auto out = play(e, c.note, sr, 1.0);
        const double f = peakHz(spectrum(out.l, sr, 4800));
        INFO("expected " << c.hz << " got " << f);
        REQUIRE(std::fabs(1200.0 * std::log2(f / c.hz)) < 2.0); // within 2 cents
    }
}

TEST_CASE("Engine: sub oscillator one / two octaves below Osc A")
{
    const double sr = 48000.0;
    for (int oct : { -1, -2 })
    {
        SynthEngine e; e.prepare(sr, 256);
        auto p = single(Wave::Sine); p.a.on = false; p.subOn = true; p.subLevel = 1.0f; p.subOctave = oct;
        e.setParams(p);
        auto out = play(e, 69, sr, 1.0);
        REQUIRE(peakHz(spectrum(out.l, sr, 4800)) == Approx(440.0 * std::pow(2.0, oct)).epsilon(0.002));
    }
}

TEST_CASE("Engine: unison detune widens the spectrum and spread makes stereo")
{
    const double sr = 48000.0;
    SynthEngine mono; mono.prepare(sr, 256);
    auto p = single(Wave::Saw); p.a.unison = 1;
    mono.setParams(p);
    auto m = play(mono, 57, sr, 1.0);
    for (size_t i = 0; i < m.l.size(); ++i) REQUIRE(m.l[i] == m.r[i]); // centred, no spread

    SynthEngine uni; uni.prepare(sr, 256);
    p.a.unison = 7; p.a.detune = 25.0f; p.a.spread = 1.0f;
    uni.setParams(p);
    auto u = play(uni, 57, sr, 1.0);
    double diff = 0.0, sum = 0.0;
    for (size_t i = 4800; i < u.l.size(); ++i) { diff += std::pow(u.l[i] - u.r[i], 2); sum += std::pow(u.l[i] + u.r[i], 2); }
    REQUIRE(diff / sum > 0.05);             // real stereo content
    // energy around the fundamental is spread over a band of +-25 cents
    const auto s = spectrum(u.l, sr, 4800);
    const double f0 = 220.0, lo = f0 * std::exp2(-30.0 / 1200.0), hi = f0 * std::exp2(30.0 / 1200.0);
    int strongBins = 0;
    for (size_t k = size_t(lo / s.binHz); k <= size_t(hi / s.binHz); ++k) if (s.power[k] > 1e-3 * s.power[size_t(f0 / s.binHz)]) ++strongBins;
    REQUIRE(strongBins >= 3);
}

TEST_CASE("Engine: FM and PM change the spectrum, depth 0 leaves it untouched")
{
    const double sr = 48000.0;
    auto render = [&](float fm, float pm)
    {
        SynthEngine e; e.prepare(sr, 256);
        auto p = single(Wave::Sine);
        p.b.wave = Wave::Sine; p.b.octave = 1; p.b.on = false; // B only as modulator
        p.fm = fm; p.pm = pm;
        e.setParams(p);
        return play(e, 57, sr, 1.0).l;
    };
    auto clean = render(0.0f, 0.0f), fm = render(0.5f, 0.0f), pm = render(0.0f, 0.6f);
    auto sideband = [&](const std::vector<float>& x)
    {
        const auto s = spectrum(x, sr, 4800);
        return 10.0 * std::log10(s.power[size_t(660.0 / s.binHz + 0.5)] / s.power[size_t(220.0 / s.binHz + 0.5)] + 1e-30);
    };
    REQUIRE(sideband(clean) < -60.0);        // pure sine: no sideband at f + fmod (220 + 440)
    REQUIRE(sideband(fm) > -30.0);
    REQUIRE(sideband(pm) > -30.0);
    for (float v : fm) REQUIRE(std::isfinite(v));
}

TEST_CASE("Noise: white is flat, pink falls about 3 dB per octave")
{
    const double sr = 48000.0;
    Noise n(99);
    std::vector<float> w(1 << 17), pk(1 << 17);
    for (auto& v : w) v = n.white();
    for (auto& v : pk) v = n.pink();
    auto bandDb = [&](const std::vector<float>& x, double f)
    {
        double e = 0; int cnt = 0;
        for (size_t off = 0; off + (1 << 15) <= x.size(); off += (1 << 14))
        {
            const auto s = spectrum(x, sr, off);
            for (size_t k = size_t(f / std::sqrt(2.0) / s.binHz); k < size_t(f * std::sqrt(2.0) / s.binHz); ++k) { e += s.power[k]; ++cnt; }
        }
        return 10.0 * std::log10(e / cnt); // mean power density in the octave
    };
    const double whiteSlope = (bandDb(w, 4000.0) - bandDb(w, 250.0)) / 4.0;
    const double pinkSlope  = (bandDb(pk, 4000.0) - bandDb(pk, 250.0)) / 4.0;
    INFO("white " << whiteSlope << " dB/oct, pink " << pinkSlope << " dB/oct");
    REQUIRE(std::fabs(whiteSlope) < 0.5);
    REQUIRE(pinkSlope == Approx(-3.0).margin(0.6));
}

TEST_CASE("Drift: zero is exact pitch, full drift stays within a musical range and is deterministic")
{
    const double sr = 48000.0;
    auto pitchCents = [&](float drift)
    {
        SynthEngine e; e.prepare(sr, 256);
        auto p = single(Wave::Sine); p.drift = drift;
        e.setParams(p);
        auto out = play(e, 69, sr, 1.5);
        return 1200.0 * std::log2(peakHz(spectrum(out.l, sr, 9600)) / 440.0);
    };
    REQUIRE(std::fabs(pitchCents(0.0f)) < 1.0);
    const double d1 = pitchCents(1.0f), d2 = pitchCents(1.0f);
    REQUIRE(d1 == d2);                 // deterministic per voice seed
    REQUIRE(std::fabs(d1) < 30.0);     // analog wobble, never out of tune
}

TEST_CASE("Saturation: ADAA modes are bounded and transparent at zero drive")
{
    Saturator s;
    for (auto m : { Saturator::Mode::Soft, Saturator::Mode::Asym, Saturator::Mode::Fold })
    {
        s.reset();
        for (int i = 0; i < 48000; ++i)
        {
            const float x = 8.0f * float(std::sin(2.0 * kPi * 100.0 * i / 48000.0));
            const float y = s.process(x, m);
            REQUIRE(std::isfinite(y));
            // analytic limits: soft/fold +-1, asym in [-1 - tanh(b), 1 - tanh(b)] (b = 0.35)
            REQUIRE(std::fabs(y) <= (m == Saturator::Mode::Asym ? 1.35f : 1.02f));
        }
    }
    // engine: drive 0 = no saturation applied
    const double sr = 48000.0;
    SynthEngine a; a.prepare(sr, 256); auto p = single(Wave::Saw); a.setParams(p);
    SynthEngine b; b.prepare(sr, 256); p.satDrive = 0.0f; p.satMix = 1.0f; p.satMode = 2; b.setParams(p);
    auto x = play(a, 60, sr, 0.2), y = play(b, 60, sr, 0.2);
    for (size_t i = 0; i < x.l.size(); ++i) REQUIRE(x.l[i] == y.l[i]);
    // and with drive the harmonic content changes
    SynthEngine c; c.prepare(sr, 256); p.satDrive = 18.0f; p.satMode = 0; c.setParams(p);
    auto z = play(c, 60, sr, 0.2);
    double d = 0; for (size_t i = 0; i < z.l.size(); ++i) d += std::fabs(z.l[i] - x.l[i]);
    REQUIRE(d > 1.0);
}

TEST_CASE("Engine: polyphony, voice stealing, click-free attack and full release")
{
    const double sr = 48000.0;
    SynthEngine e; e.prepare(sr, 256);
    auto p = single(Wave::Saw); p.a.unison = 3;
    e.setParams(p);
    std::vector<float> l(4800, 0.0f), r(4800, 0.0f);
    for (int n = 48; n < 60; ++n) e.noteOn(n, 0.9f);     // 12 notes -> 8 voices
    e.render(l.data(), r.data(), 0, 4800);
    REQUIRE(e.activeVoices() == SynthEngine::kMaxVoices);

    SynthEngine one; one.prepare(sr, 256); one.setParams(single(Wave::Sine));
    std::vector<float> a(64, 0.0f), b(64, 0.0f);
    one.noteOn(69, 1.0f);
    one.render(a.data(), b.data(), 0, 64);
    for (size_t i = 1; i < a.size(); ++i) REQUIRE(std::fabs(a[i] - a[i - 1]) < 0.02f); // 3 ms attack ramp

    e.allNotesOff();
    std::vector<float> tl(size_t(sr * 1.5), 0.0f), tr(tl.size(), 0.0f);
    e.render(tl.data(), tr.data(), 0, int(tl.size()));
    REQUIRE(e.activeVoices() == 0);
    REQUIRE(tl.back() == 0.0f);
}

TEST_CASE("Engine: stable and finite at all sample rates, block sizes and extreme settings")
{
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (int block : { 1, 33, 256, 2048 })
        {
            SynthEngine e; e.prepare(sr, block);
            SynthEngine::Params p;
            p.a.unison = 7; p.a.detune = 100.0f; p.a.wave = Wave::Pulse; p.a.pw = 0.05f; p.a.octave = 3;
            p.b.unison = 7; p.b.wave = Wave::Square; p.b.octave = 3;
            p.fm = 1.0f; p.pm = 1.0f; p.drift = 1.0f; p.noiseOn = true; p.noiseType = 0;
            p.satDrive = 36.0f; p.satMode = 2;
            e.setParams(p);
            for (int n : { 24, 60, 96, 108 }) e.noteOn(n, 1.0f);
            const size_t n = size_t(sr * 0.25);
            std::vector<float> l(n, 0.0f), r(n, 0.0f);
            for (size_t s = 0; s < n; s += size_t(block)) e.render(l.data(), r.data(), int(s), int(std::min(n, s + size_t(block))));
            float peak = 0.0f;
            for (size_t i = 0; i < n; ++i) { REQUIRE(std::isfinite(l[i])); peak = std::max(peak, std::fabs(l[i])); }
            REQUIRE(peak < 4.0f);
        }
}
