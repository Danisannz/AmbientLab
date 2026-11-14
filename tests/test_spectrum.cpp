#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "viz/SpectrumAnalyser.h"
#include "viz/VisualizerHub.h"
#include <cmath>
#include <vector>

using namespace ambient;
using namespace ambient::viz;

static std::vector<float> sine(double freq, double sr, int n, float amp = 1.0f)
{
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) v[size_t(i)] = amp * float(std::sin(2.0 * juce::MathConstants<double>::pi * freq * i / sr));
    return v;
}

TEST_CASE("Spectrum: 1 kHz full-scale sine peaks at 1 kHz and reads ~0 dBFS")
{
    for (int order : { 10, 12, 14 })
    {
        SpectrumAnalyser s;
        s.setSampleRate(48000.0);
        s.setOrder(order);
        s.setAveraging(0.0f);
        auto x = sine(1000.0, 48000.0, s.getSize());
        s.process(x.data(), 1.0f / 60.0f);

        const float f = s.findPeakFrequency();
        const float binHz = 48000.0f / float(s.getSize());
        REQUIRE(std::abs(f - 1000.0f) < binHz * 0.5f);

        const int bin = int(std::lround(1000.0f / binHz));
        float peak = -200.0f;
        for (int b = bin - 2; b <= bin + 2; ++b) peak = std::max(peak, s.getAverageDb()[size_t(b)]);
        REQUIRE(peak == Catch::Approx(0.0f).margin(1.0f)); // Blackman-Harris scalloping < 1 dB
    }
}

TEST_CASE("Spectrum: -20 dBFS sine reads -20 dB; far bins are low (dynamic range)")
{
    SpectrumAnalyser s;
    s.setSampleRate(48000.0);
    s.setOrder(13);
    s.setAveraging(0.0f);
    const double binHz = 48000.0 / s.getSize();
    auto x = sine(binHz * 200.0, 48000.0, s.getSize(), 0.1f); // bin-centred
    s.process(x.data(), 0.016f);
    REQUIRE(s.getAverageDb()[200] == Catch::Approx(-20.0f).margin(0.2f));
    REQUIRE(s.getAverageDb()[1000] < -100.0f);
}

TEST_CASE("Spectrum: peak hold decays, averaging converges")
{
    SpectrumAnalyser s;
    s.setSampleRate(48000.0);
    s.setOrder(11);
    s.setAveraging(0.8f);
    s.setPeakDecay(20.0f);
    const int bin = 64;
    auto loud = sine(48000.0 / s.getSize() * bin, 48000.0, s.getSize());
    std::vector<float> silent(size_t(s.getSize()), 0.0f);

    s.process(loud.data(), 0.1f);
    const float held = s.getPeakDb()[bin];
    REQUIRE(held > -1.0f);

    s.process(silent.data(), 0.5f);
    // Peak never falls below the current average, and never faster than 20 dB/s.
    REQUIRE(s.getAverageDb()[bin] < held);   // averaging moving towards silence
    REQUIRE(s.getAverageDb()[bin] > -30.0f); // ...but not instantly
    REQUIRE(s.getPeakDb()[bin] >= s.getAverageDb()[bin]);
    REQUIRE(s.getPeakDb()[bin] >= held - 10.0f - 1e-3f);

    // With averaging off the average collapses and the peak decays at exactly the set rate.
    SpectrumAnalyser p;
    p.setSampleRate(48000.0); p.setOrder(11); p.setAveraging(0.0f); p.setPeakDecay(20.0f);
    p.process(loud.data(), 0.1f);
    const float h2 = p.getPeakDb()[bin];
    p.process(silent.data(), 0.5f);
    REQUIRE(p.getPeakDb()[bin] == Catch::Approx(h2 - 10.0f).margin(0.01f));

    for (int i = 0; i < 200; ++i) s.process(loud.data(), 0.016f);
    REQUIRE(s.getAverageDb()[bin] == Catch::Approx(0.0f).margin(1.0f));
}

TEST_CASE("Spectrum: log curve rendering with smoothing is finite and peaks near the tone")
{
    SpectrumAnalyser s;
    s.setSampleRate(48000.0);
    s.setOrder(12);
    s.setAveraging(0.0f);
    auto x = sine(1000.0, 48000.0, s.getSize());
    s.process(x.data(), 0.016f);

    std::vector<float> curve(512);
    for (float sm : { 0.0f, 1.0f / 6.0f, 1.0f / 3.0f })
    {
        s.renderLogCurve(512, 20.0f, 20000.0f, sm, false, curve.data());
        int best = 0;
        for (int i = 0; i < 512; ++i) { REQUIRE(std::isfinite(curve[size_t(i)])); if (curve[size_t(i)] > curve[size_t(best)]) best = i; }
        const float fBest = logFreq(float(best) / 511.0f, 20.0f, 20000.0f);
        // Band-averaging smears a pure tone across the smoothing width; the peak must
        // stay inside that band (or within 1/10 octave when unsmoothed).
        REQUIRE(std::abs(std::log2(fBest / 1000.0f)) <= std::max(0.1f, sm * 0.5f));
    }
}

TEST_CASE("Hub: pulls from ring, honours freeze, triggered scope window")
{
    StereoCaptureRing ring(1 << 16);
    VisualizerHub hub(ring);
    hub.setSampleRate(48000.0);

    REQUIRE_FALSE(hub.update(0.016f)); // no data yet

    auto x = sine(480.0, 48000.0, 48000); // period = 100 samples
    for (int i = 0; i < 48000; i += 512) ring.write(x.data() + i, x.data() + i, std::min(512, 48000 - i));

    hub.setMode(AnalyzerMode::Scope);
    hub.setScopeTimeMs(10.0f);
    REQUIRE(hub.update(0.016f));
    REQUIRE(hub.getScopeLength() == 480);
    const float* sl = hub.getScopeLeft();
    REQUIRE(std::abs(sl[0]) < 0.07f); // starts at a rising zero crossing
    REQUIRE(sl[3] > sl[0]);
    REQUIRE(hub.getStereoStats().correlation == Catch::Approx(1.0f).margin(1e-3));

    REQUIRE_FALSE(hub.update(0.016f)); // nothing new written

    hub.setMode(AnalyzerMode::Spectrum);
    hub.setFrozen(true);
    ring.write(x.data(), x.data(), 512);
    REQUIRE_FALSE(hub.update(0.016f));
    hub.setFrozen(false);
    REQUIRE(hub.update(0.016f));
    REQUIRE(std::abs(hub.spectrum().findPeakFrequency() - 480.0f) < 10.0f);
}

TEST_CASE("Hub: scope trigger source follows the displayed channel")
{
    StereoCaptureRing ring(1 << 16);
    VisualizerHub hub(ring);
    hub.setSampleRate(48000.0);
    hub.setMode(AnalyzerMode::Scope);
    hub.setScopeTimeMs(10.0f);
    hub.setTrigger(TriggerMode::Rising, 0.0f);

    // L = sine, R = inverted sine: a rising edge on L is a falling edge on R.
    auto l = sine(480.0, 48000.0, 24000);
    std::vector<float> r(l.size());
    for (size_t i = 0; i < l.size(); ++i) r[i] = -l[i];
    ring.write(l.data(), r.data(), 24000);

    hub.setTriggerSource(VisualizerHub::TriggerSource::Left);
    REQUIRE(hub.update(0.016f));
    REQUIRE(std::abs(hub.getScopeLeft()[0]) < 0.07f);
    REQUIRE(hub.getScopeLeft()[3] > hub.getScopeLeft()[0]);

    hub.setTriggerSource(VisualizerHub::TriggerSource::Right);
    ring.write(l.data(), r.data(), 512); // new data so the hub analyses again
    REQUIRE(hub.update(0.016f));
    REQUIRE(std::abs(hub.getScopeRight()[0]) < 0.07f);
    REQUIRE(hub.getScopeRight()[3] > hub.getScopeRight()[0]); // rising on R
}

TEST_CASE("Hub: spectrum channel selection keeps calibration (no silent level loss)")
{
    const double sr = 48000.0;
    StereoCaptureRing ring(1 << 16);
    VisualizerHub hub(ring);
    hub.setSampleRate(sr);
    hub.setMode(AnalyzerMode::Spectrum);
    hub.setFftOrder(12);
    hub.setAveraging(0.0f);
    const int n = hub.spectrum().getSize();
    const double binHz = sr / n;
    const int bin = 100;
    auto tone = sine(binHz * bin, sr, 8192);
    std::vector<float> zero(tone.size(), 0.0f), inv(tone.size());
    for (size_t i = 0; i < tone.size(); ++i) inv[i] = -tone[i];

    auto levelAt = [&](SpectrumAnalyser& a) { return a.getAverageDb()[size_t(bin)]; };
    auto run = [&](const std::vector<float>& l, const std::vector<float>& r, SpectrumChannel ch)
    {
        hub.setSpectrumChannel(ch);
        ring.write(l.data(), r.data(), int(l.size()));
        REQUIRE(hub.update(0.016f));
    };

    // Full-scale tone in LEFT only
    run(tone, zero, SpectrumChannel::Left);   REQUIRE(levelAt(hub.spectrum()) == Catch::Approx(0.0f).margin(0.2f));
    run(tone, zero, SpectrumChannel::Right);  REQUIRE(levelAt(hub.spectrum()) < -100.0f);
    run(tone, zero, SpectrumChannel::Stereo); REQUIRE(levelAt(hub.spectrum()) == Catch::Approx(0.0f).margin(0.2f));
                                              REQUIRE(levelAt(hub.spectrumRight()) < -100.0f);
    run(tone, zero, SpectrumChannel::Mid);    REQUIRE(levelAt(hub.spectrum()) == Catch::Approx(-6.02f).margin(0.2f)); // by definition

    // Identical L/R (mono) and polarity-inverted
    run(tone, tone, SpectrumChannel::Mid);    REQUIRE(levelAt(hub.spectrum()) == Catch::Approx(0.0f).margin(0.2f));
    run(tone, tone, SpectrumChannel::Side);   REQUIRE(levelAt(hub.spectrum()) < -100.0f);
    run(tone, inv,  SpectrumChannel::Side);   REQUIRE(levelAt(hub.spectrum()) == Catch::Approx(0.0f).margin(0.2f));
    run(tone, inv,  SpectrumChannel::Mid);    REQUIRE(levelAt(hub.spectrum()) < -100.0f);
}
