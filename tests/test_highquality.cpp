// High Quality mode: decimator response, synth aliasing Standard vs HQ, glitch-free switching.
#include <catch2/catch_test_macros.hpp>
#include <ambient/dsp/HalfbandIir.h>
#include <ambient/dsp/SynthEngine.h>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace ambient::dsp;

namespace
{
constexpr double kPi = 3.14159265358979323846;
/** Gain (dB) of the 4x decimator for a sine at f (Hz, at 192 kHz); measured at the output frequency (folded). */
double decimGainDb(double f)
{
    Decimator4x d;
    const int n = 48000;
    std::vector<float> in(static_cast<size_t>(n) * 4), out(static_cast<size_t>(n));
    for (size_t i = 0; i < in.size(); ++i) in[i] = float(std::sin(2 * kPi * f * double(i) / 192000.0));
    d.process(in.data(), out.data(), n);
    double fo = std::fmod(f, 48000.0); if (fo > 24000.0) fo = 48000.0 - fo;   // folded frequency
    double re = 0, im = 0;
    for (int i = n / 2; i < n; ++i) { re += out[size_t(i)] * std::cos(2 * kPi * fo * i / 48000.0); im += out[size_t(i)] * std::sin(2 * kPi * fo * i / 48000.0); }
    const double amp = 2.0 * std::sqrt(re * re + im * im) / (n / 2);
    return 20.0 * std::log10(amp + 1e-12);
}
}

TEST_CASE("HQ decimator: flat passband to 20 kHz, > 90 dB rejection of everything that would alias into 0-20 kHz")
{
    double ripple = 0;
    for (double f = 20.0; f <= 20000.0; f *= 1.25) ripple = std::max(ripple, std::abs(decimGainDb(f)));
    INFO("max passband deviation " << ripple << " dB");
    REQUIRE(ripple < 0.15);
    double worst = -300;
    // frequencies at 192 kHz whose aliases land in 0..20 kHz at 48 kHz
    for (double f : { 28500.0, 30000.0, 40000.0, 47000.0, 50000.0, 60000.0, 67500.0, 76500.0, 80000.0, 90000.0, 95000.0 })
        worst = std::max(worst, decimGainDb(f));
    INFO("worst stopband gain " << worst << " dB");
    REQUIRE(worst < -90.0);
}

namespace
{
/** Alias energy of the synth output (one held note): bins off the harmonic series in 20..20k. */
double synthAliasDb(bool hq, int note, float driveDb, int satMode)
{
    const double sr = 48000.0;
    SynthEngine s;
    s.prepare(hq ? sr * 4 : sr, 512);
    SynthEngine::Params p;
    p.a.wave = Wave::Saw; p.a.unison = 1; p.a.detune = 0; p.a.level = 0.6f;
    p.b.on = false; p.subOn = false; p.noiseOn = false; p.drift = 0.0f;
    p.satMode = satMode; p.satDrive = driveDb; p.satMix = 1.0f;
    p.filterOn = false;
    s.setParams(p);
    s.noteOn(note, 1.0f);
    const int n = 1 << 15, os = hq ? 4 : 1;
    std::vector<float> L(static_cast<size_t>(n + 8192) * size_t(os)), R(L.size());
    for (size_t i = 0; i < L.size(); i += 64) s.render(L.data(), R.data(), int(i), int(std::min(L.size(), i + 64)));
    std::vector<float> y(static_cast<size_t>(n + 8192));
    if (hq) { Decimator4x d; d.process(L.data(), y.data(), int(y.size())); } else y = L;
    // Goertzel-free: DFT on a Blackman-Harris window over the last n samples via direct bins near harmonics is slow;
    // use a naive spectrum on 8192 points (enough resolution for one note)
    const int m = 8192; const size_t off = y.size() - size_t(m);
    const double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
    double harm = 0, alias = 0;
    for (int b = 1; b < m / 2; ++b)
    {
        double re = 0, im = 0;
        const double w0 = 2 * kPi * b / m;
        for (int i = 0; i < m; ++i)
        {
            const double a = 2 * kPi * i / (m - 1);
            const double win = 0.35875 - 0.48829 * std::cos(a) + 0.14128 * std::cos(2 * a) - 0.01168 * std::cos(3 * a);
            re += y[off + size_t(i)] * win * std::cos(w0 * i); im -= y[off + size_t(i)] * win * std::sin(w0 * i);
        }
        const double f = b * sr / m, e = re * re + im * im;
        const double k = std::round(f / f0);
        if (k >= 1 && std::abs(f - k * f0) <= 6 * sr / m) harm += e;
        else if (f >= 20 && f <= 20000) alias += e;
    }
    return 10 * std::log10((alias + 1e-30) / (harm + 1e-30));
}
}

TEST_CASE("HQ mode: the synth's aliasing drops substantially (high note, hot saturation)", "[hq][.slow]")
{
    for (auto [note, drive, mode] : { std::tuple<int, float, int> { 91, 0.0f, 0 }, { 79, 30.0f, 0 }, { 72, 30.0f, 2 } })
    {
        const double st = synthAliasDb(false, note, drive, mode), hq = synthAliasDb(true, note, drive, mode);
        std::printf("  synth note %d, drive %.0f dB, mode %d: standard %.1f dB, HQ %.1f dB\n", note, drive, mode, st, hq);
        REQUIRE(hq < st - 12.0);
    }
}

TEST_CASE("SynthEngine: internal rate can change live without resetting voices")
{
    SynthEngine s; s.prepare(48000.0, 512);
    SynthEngine::Params p; s.setParams(p);
    s.noteOn(60, 0.8f);
    std::vector<float> L(4096), R(4096);
    s.render(L.data(), R.data(), 0, 4096);
    REQUIRE(s.activeVoices() == 1);
    s.setInternalRate(192000.0);
    std::vector<float> L4(4096 * 4), R4(4096 * 4);
    s.render(L4.data(), R4.data(), 0, 4096 * 4);
    REQUIRE(s.activeVoices() == 1);   // still sounding: no reset
    float pk = 0; for (float v : L4) { REQUIRE(std::isfinite(v)); pk = std::max(pk, std::fabs(v)); }
    REQUIRE(pk > 0.05f);
}
