// LFO + modulation matrix core (Stage 7).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <ambient/dsp/Lfo.h>
#include <cmath>
#include <vector>

using namespace ambient::dsp;
using W = Lfo::Wave;
using Catch::Approx;

namespace
{
std::vector<float> run(Lfo& l, double seconds, double sr = 48000.0, int block = 32, Lfo::Transport t = {})
{
    std::vector<float> v;
    const int n = int(seconds * sr / block);
    for (int i = 0; i < n; ++i)
    {
        v.push_back(l.advance(block, t));
        if (t.playing) t.ppq += double(block) / sr * t.bpm / 60.0;
    }
    return v;
}
}

TEST_CASE("LFO: every waveform stays in range, bipolar and unipolar")
{
    for (int w = 0; w < Lfo::kNumWaves; ++w)
        for (bool uni : { false, true })
        {
            Lfo l; Lfo::Params p; p.wave = W(w); p.rateHz = 7.0f; p.unipolar = uni; p.shape = 0.8f;
            l.setParams(p); l.prepare(48000.0);
            const auto v = run(l, 3.0);
            float lo = 1e9f, hi = -1e9f;
            for (float x : v) { REQUIRE(std::isfinite(x)); lo = std::min(lo, x); hi = std::max(hi, x); }
            INFO(Lfo::waveName(w) << (uni ? " uni" : " bi") << " range " << lo << " .. " << hi);
            REQUIRE(lo >= (uni ? -1e-4f : -1.0001f));
            REQUIRE(hi <= 1.0001f);
            REQUIRE(hi - lo > 0.3f);       // it actually moves
        }
}

TEST_CASE("LFO: free rate is accurate at different sample rates / blocks")
{
    for (double sr : { 44100.0, 96000.0 })
        for (int block : { 1, 32, 64 })
        {
            Lfo l; Lfo::Params p; p.wave = W::SawUp; p.rateHz = 2.0f; l.setParams(p); l.prepare(sr);
            const auto v = run(l, 5.1, sr, block);
            int wraps = 0; for (size_t i = 1; i < v.size(); ++i) if (v[i] < v[i - 1] - 1.0f) ++wraps;
            REQUIRE(wraps == 10);
        }
}

TEST_CASE("LFO: host sync locks the phase to the song position")
{
    Lfo l; Lfo::Params p; p.wave = W::SawUp; p.sync = true; p.division = 9; // 1/4 note
    l.setParams(p); l.prepare(48000.0);
    Lfo::Transport t; t.playing = true; t.bpm = 120.0;
    t.ppq = 3.25;   // a quarter into beat 4
    const float v = l.advance(32, t);
    REQUIRE(v == Approx(2.0f * 0.25f - 1.0f).margin(0.02));
    t.ppq = 10.75;
    REQUIRE(l.advance(32, t) == Approx(0.5f).margin(0.02));
    // phase offset
    p.phase = 0.5f; l.setParams(p); t.ppq = 0.0;
    REQUIRE(l.advance(32, t) == Approx(0.0f).margin(0.02));
}

TEST_CASE("LFO: synced rate without transport follows the tempo")
{
    Lfo l; Lfo::Params p; p.wave = W::SawUp; p.sync = true; p.division = 6;  // 1/8 = 4 Hz at 120
    l.setParams(p); l.prepare(48000.0);
    Lfo::Transport t; t.playing = false; t.bpm = 120.0;
    const auto v = run(l, 2.0, 48000.0, 32, t);
    int wraps = 0; for (size_t i = 1; i < v.size(); ++i) if (v[i] < v[i - 1] - 1.0f) ++wraps;
    REQUIRE(wraps == 8);
}

TEST_CASE("LFO: shapes, retrigger, smoothing, custom curve")
{
    Lfo::Params p;
    p.wave = W::Sine;       REQUIRE(Lfo::shapeAt(p, 0.25f) == Approx(1.0f));
    p.wave = W::Triangle;   REQUIRE(Lfo::shapeAt(p, 0.5f) == Approx(0.0f).margin(1e-6)); REQUIRE(Lfo::shapeAt(p, 0.75f) == Approx(-1.0f));
    p.wave = W::Pulse; p.shape = 0.2f; REQUIRE(Lfo::shapeAt(p, 0.1f) == 1.0f); REQUIRE(Lfo::shapeAt(p, 0.3f) == -1.0f);
    p.wave = W::Exponential; p.shape = 1.0f; REQUIRE(Lfo::shapeAt(p, 0.5f) < -0.8f);   // slow start
    p.wave = W::Logarithmic; REQUIRE(Lfo::shapeAt(p, 0.5f) > 0.8f);                    // fast start
    p.wave = W::Custom; p.shape = 0.0f; p.custom = { -1, 1, -1, 1, -1, 1, -1, 1 };
    REQUIRE(Lfo::shapeAt(p, 0.0f) == Approx(-1.0f)); REQUIRE(Lfo::shapeAt(p, 0.125f) == Approx(1.0f)); REQUIRE(Lfo::shapeAt(p, 0.0625f) == Approx(0.0f).margin(1e-5));

    Lfo l; Lfo::Params q; q.wave = W::SawUp; q.rateHz = 1.0f; q.retrigger = true; l.setParams(q); l.prepare(48000.0);
    run(l, 0.6); l.retrigger();
    REQUIRE(l.advance(1, {}) == Approx(-1.0f).margin(0.01));

    // smoothing slows a square
    auto maxStep = [](float smooth) { Lfo a; Lfo::Params s; s.wave = W::Square; s.rateHz = 2.0f; s.smooth = smooth; a.setParams(s); a.prepare(48000.0);
        const auto v = run(a, 2.0); float m = 0; for (size_t i = 1; i < v.size(); ++i) m = std::max(m, std::fabs(v[i] - v[i - 1])); return m; };
    REQUIRE(maxStep(0.0f) > 1.9f);
    REQUIRE(maxStep(1.0f) < 0.05f);
}

TEST_CASE("LFO: random waves are deterministic after reset; S&H holds per cycle; chaos is aperiodic")
{
    auto render = [](W w) { Lfo l; Lfo::Params p; p.wave = w; p.rateHz = 5.0f; l.setParams(p); l.prepare(48000.0); return run(l, 2.0); };
    REQUIRE(render(W::SampleHold) == render(W::SampleHold));
    const auto sh = render(W::SampleHold);
    int changes = 0; for (size_t i = 1; i < sh.size(); ++i) if (sh[i] != sh[i - 1]) ++changes;
    REQUIRE(changes >= 9); REQUIRE(changes <= 11);   // ~10 cycles in 2 s
    const auto ch = render(W::Chaotic);
    // compare consecutive cycles: not periodic
    const size_t cyc = ch.size() / 10; double diff = 0; for (size_t i = 0; i < cyc; ++i) diff += std::fabs(ch[i + cyc] - ch[i + 2 * cyc]);
    REQUIRE(diff / double(cyc) > 0.05);
}

TEST_CASE("ModMatrix: sums slots per destination, ignores empty slots")
{
    std::array<ModMatrix::Slot, ModMatrix::kSlots> slots {};
    slots[0] = { 1, 3, 0.5f };
    slots[1] = { 2, 3, -0.25f };
    slots[2] = { 1, 5, 1.0f };
    slots[3] = { 0, 4, 1.0f };    // no source
    slots[4] = { 2, 0, 1.0f };    // no destination
    const float src[3] = { 0.0f, 1.0f, 0.4f };
    float off[8];
    ModMatrix::accumulate(slots, src, 3, off, 7);
    REQUIRE(off[3] == Approx(0.5f - 0.1f));
    REQUIRE(off[5] == Approx(1.0f));
    REQUIRE(off[4] == 0.0f);
    REQUIRE(off[0] == 0.0f);
}
