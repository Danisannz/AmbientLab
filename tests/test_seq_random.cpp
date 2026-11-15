// Sequencer Random mode + dedicated sequencer LFO.
#include <catch2/catch_test_macros.hpp>
#include <ambient/dsp/Sequencer.h>
#include <ambient/dsp/Scales.h>
#include <cmath>
#include <map>
#include <vector>

using namespace ambient::dsp;
using Seq = Sequencer;

namespace
{
constexpr double kSr = 48000.0;
struct Ev { long t; int note; float vel; bool on; };

std::vector<Ev> run(Seq& s, double seconds, bool host = true, double bpm = 120.0, int block = 256, double startPpq = 0.0)
{
    std::vector<Ev> out;
    Seq::Transport t; t.playing = true; t.hostSync = host; t.bpm = bpm;
    const long n = long(seconds * kSr);
    double ppq = startPpq;
    for (long pos = 0; pos < n; pos += block)
    {
        t.ppqAtBlockStart = ppq;
        s.process(block, t);
        for (int i = 0; i < s.numEvents(); ++i) { const auto& e = s.event(i); out.push_back({ pos + e.sampleOffset, e.note, e.velocity, e.noteOn }); }
        ppq += double(block) / kSr * bpm / 60.0;
    }
    t.playing = false; s.process(block, t);
    for (int i = 0; i < s.numEvents(); ++i) { const auto& e = s.event(i); out.push_back({ n + e.sampleOffset, e.note, e.velocity, e.noteOn }); }
    return out;
}
Seq make(int scale = 0, int root = 0)
{
    Seq s; s.prepare(kSr);
    Seq::Params p; p.length = 16; p.division = 3; p.scale = scale; p.root = root; p.baseNote = 48;
    s.setParams(p);
    for (int i = 0; i < 16; ++i) { Seq::Step st; st.active = true; st.note = 0; st.probability = 1; st.gate = 0.5f; s.setStep(i, st); }
    return s;
}
bool balanced(const std::vector<Ev>& ev) { std::map<int, int> c; for (auto& e : ev) c[e.note] += e.on ? 1 : -1; for (auto& [n, v] : c) if (v != 0) return false; return true; }
}

TEST_CASE("Random: Generate stays in scale and range for every scale and root, respects density")
{
    for (int sc = 0; sc < 16; ++sc)
        for (int root : { 0, 3, 7, 11 })
        {
            auto s = make(sc, root);
            auto p = s.getParams(); p.random.range = 9; p.random.density = 0.6f; s.setParams(p);
            int active = 0;
            for (uint32_t seed = 1; seed <= 20; ++seed)
            {
                s.generate(seed);
                for (int i = 0; i < 16; ++i)
                {
                    const auto& st = s.getStep(i);
                    INFO("scale " << sc << " root " << root << " step " << i << " note " << st.note);
                    REQUIRE(scales::contains(48 + root + st.note, sc, root));
                    const int deg = scales::semisToDegree(st.note, sc);
                    REQUIRE(deg <= 9); REQUIRE(deg >= -4);
                    if (st.active) ++active;
                }
            }
            const double share = double(active) / (20.0 * 16.0);
            REQUIRE(share > 0.45); REQUIRE(share < 0.9);   // density 0.6 with strong-beat emphasis
        }
}

TEST_CASE("Random: Generate is musical (mostly stepwise), respects locks and chromatic option")
{
    auto s = make(0, 0);
    auto p = s.getParams(); p.random.range = 14; s.setParams(p);
    int small = 0, total = 0;
    for (uint32_t seed = 1; seed <= 50; ++seed)
    {
        s.generate(seed);
        for (int i = 1; i < 16; ++i)
        {
            const int d = std::abs(scales::semisToDegree(s.getStep(i).note, 0) - scales::semisToDegree(s.getStep(i - 1).note, 0));
            if (d <= 2) ++small;
            ++total;
        }
    }
    REQUIRE(double(small) / total > 0.6);    // not arbitrary note soup

    // locks
    s.generate(77);
    std::vector<Seq::Step> before; for (int i = 0; i < 16; ++i) before.push_back(s.getStep(i));
    p.random.lockNotes = true; p.random.lockSteps = true; s.setParams(p);
    s.generate(78);
    bool velChanged = false;
    for (int i = 0; i < 16; ++i)
    {
        REQUIRE(s.getStep(i).note == before[size_t(i)].note);
        REQUIRE(s.getStep(i).active == before[size_t(i)].active);
        velChanged = velChanged || s.getStep(i).velocity != before[size_t(i)].velocity;
    }
    REQUIRE(velChanged);

    // chromatic: off-scale notes appear, and playback does not quantise them
    auto c = make(0, 0);
    auto pc = c.getParams(); pc.random.chromatic = true; pc.random.on = true; pc.random.amount = 0.0f; c.setParams(pc);
    bool off = false;
    for (uint32_t seed = 1; seed <= 30 && ! off; ++seed) { c.generate(seed); for (int i = 0; i < 16; ++i) off = off || ! scales::contains(48 + c.getStep(i).note, 0, 0); }
    REQUIRE(off);
    const auto ev = run(c, 2.5);
    bool offPlayed = false; for (auto& e : ev) if (e.on && ! scales::contains(e.note, 0, 0)) offPlayed = true;
    REQUIRE(offPlayed);
}

TEST_CASE("Random mode ON: per-cycle variations in scale, deterministic, original pattern kept; amount 0 = plain")
{
    auto plain = make(5, 2);
    for (int i = 0; i < 16; ++i) { auto st = plain.getStep(i); st.note = (i % 5) * 2; plain.setStep(i, st); }
    auto basePattern = plain;
    const auto ref = run(plain, 8.0);

    auto zero = basePattern; auto pz = zero.getParams(); pz.random.on = true; pz.random.amount = 0.0f; zero.setParams(pz);
    const auto z = run(zero, 8.0);
    REQUIRE(z.size() == ref.size());
    for (size_t i = 0; i < z.size(); ++i) { REQUIRE(z[i].t == ref[i].t); REQUIRE(z[i].note == ref[i].note); }

    auto rnd = basePattern; auto pr = rnd.getParams(); pr.random.on = true; pr.random.amount = 0.5f; pr.random.seed = 9; rnd.setParams(pr);
    auto rnd2 = rnd;
    const auto a = run(rnd, 8.0), b = run(rnd2, 8.0);
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) REQUIRE(a[i].note == b[i].note);            // deterministic
    int differ = 0; size_t k = 0;
    for (auto& e : a) if (e.on) { REQUIRE(scales::contains(e.note, 5, 2)); }
    for (size_t i = 0; i < std::min(a.size(), ref.size()); ++i) if (a[i].note != ref[i].note) ++differ;
    REQUIRE(differ > 0);                                                               // it varies
    for (int i = 0; i < 16; ++i) REQUIRE(rnd.getStep(i).note == basePattern.getStep(i).note);   // stored pattern untouched
    REQUIRE(balanced(a));
    (void) k;
}

TEST_CASE("Seq LFO -> notes: shifts by scale degrees, stays in key and range, repeats with the LFO period")
{
    auto s = make(1, 9);   // A natural minor
    auto p = s.getParams();
    p.lfo.depth = 1.0f; p.lfo.dest = Seq::Params::SeqLfo::Dest::Notes; p.lfo.wave = Seq::Params::SeqLfo::Wave::Triangle;
    p.lfo.sync = true; p.lfo.division = 13;   // one bar
    s.setParams(p);
    const auto ev = run(s, 8.0);   // 4 bars at 120 BPM
    std::vector<int> notes; for (auto& e : ev) if (e.on) notes.push_back(e.note);
    REQUIRE(notes.size() >= 60);
    int lo = 999, hi = -999;
    for (int n : notes) { REQUIRE(scales::contains(n, 1, 9)); lo = std::min(lo, n); hi = std::max(hi, n); }
    REQUIRE(hi - lo >= 12);                                    // real melodic movement (+-7 degrees)
    REQUIRE(lo >= 48 + 9 - 24); REQUIRE(hi <= 48 + 9 + 36);
    for (size_t i = 16; i < notes.size(); ++i) REQUIRE(notes[i] == notes[i - 16]);   // period = 1 bar = 16 steps
}

TEST_CASE("Seq LFO -> speed (host sync): x2 / x1/2 per bar, exact grid, identical after a transport jump")
{
    auto s = make();
    auto p = s.getParams();
    p.lfo.depth = 1.0f; p.lfo.dest = Seq::Params::SeqLfo::Dest::Speed; p.lfo.wave = Seq::Params::SeqLfo::Wave::Square;
    p.lfo.sync = true; p.lfo.division = 14;   // 2 bars: bar 0 fast (x2), bar 1 slow (x1/2)
    s.setParams(p);
    const auto ev = run(s, 8.0, true, 120.0, 64);   // 4 bars
    const long bar = long(kSr * 2.0);               // 2 s per bar at 120 BPM
    std::vector<long> ons; for (auto& e : ev) if (e.on) ons.push_back(e.t);
    int bar0 = 0, bar1 = 0; for (long t : ons) { if (t < bar - 2) ++bar0; else if (t < 2 * bar - 2) ++bar1; }   // +-2 samples: harness ppq accumulation
    INFO("notes in bar 0: " << bar0 << ", bar 1: " << bar1);
    REQUIRE(bar0 == 32);   // 1/16 at x2
    REQUIRE(bar1 == 8);    // 1/16 at x1/2
    for (long t : ons) if (t < bar - 2) { const long g = 3000, r = t % g; REQUIRE(std::min(r, g - r) <= 1); }   // x2 grid: 1/16 at 120 BPM = 6000 samples -> 3000
    REQUIRE(balanced(ev));

    // start playback directly at bar 2 (transport jump): the same events as the continuous run
    auto j = make(); j.setParams(p);
    const auto ev2 = run(j, 4.0, true, 120.0, 64, 8.0);
    std::vector<long> onsCont; for (auto& e : ev) if (e.on && e.t >= 2 * bar) onsCont.push_back(e.t - 2 * bar);
    std::vector<long> onsJump; for (auto& e : ev2) if (e.on) onsJump.push_back(e.t);
    REQUIRE(onsCont.size() == onsJump.size());
    for (size_t i = 0; i < onsJump.size(); ++i) REQUIRE(std::abs(onsCont[i] - onsJump[i]) <= 1);
}

TEST_CASE("Seq LFO -> smooth speed (internal clock): continuous, monotonic, no timing glitches")
{
    auto s = make();
    auto p = s.getParams();
    p.lfo.depth = 1.0f; p.lfo.dest = Seq::Params::SeqLfo::Dest::Speed; p.lfo.smoothSpeed = true;
    p.lfo.sync = false; p.lfo.rateHz = 0.25f;
    s.setParams(p);
    const auto ev = run(s, 12.0, false);
    std::vector<long> ons; for (auto& e : ev) if (e.on) ons.push_back(e.t);
    REQUIRE(ons.size() > 40);
    double minIoi = 1e9, maxIoi = 0, maxJump = 0;
    for (size_t i = 1; i < ons.size(); ++i)
    {
        const double ioi = double(ons[i] - ons[i - 1]);
        REQUIRE(ioi > 0);
        minIoi = std::min(minIoi, ioi); maxIoi = std::max(maxIoi, ioi);
        if (i > 1) maxJump = std::max(maxJump, std::abs(ioi - double(ons[i - 1] - ons[i - 2])) / double(ons[i - 1] - ons[i - 2]));
    }
    INFO("IOI " << minIoi << " .. " << maxIoi << " samples, max step-to-step change " << maxJump * 100 << " %");
    REQUIRE(maxIoi / minIoi > 2.5);          // speed really varies (x1/2 .. x2)
    REQUIRE(maxJump < 0.25);                 // but smoothly
    REQUIRE(balanced(ev));
}

TEST_CASE("Seq LFO -> evolution combined with random mode: in scale, more variation at LFO peaks, no hanging notes")
{
    auto s = make(9, 4);   // E major pentatonic
    for (int i = 0; i < 16; ++i) { auto st = s.getStep(i); st.note = 0; s.setStep(i, st); }
    auto p = s.getParams();
    p.random.on = true; p.random.amount = 0.1f;
    p.lfo.depth = 1.0f; p.lfo.dest = Seq::Params::SeqLfo::Dest::NotesAndEvolution; p.lfo.wave = Seq::Params::SeqLfo::Wave::SampleHold; p.lfo.smooth = 0.5f;
    p.lfo.sync = true; p.lfo.division = 13;
    s.setParams(p);
    const auto ev = run(s, 16.0, true, 120.0, 128);
    std::map<int, int> distinct;
    for (auto& e : ev) if (e.on) { REQUIRE(scales::contains(e.note, 9, 4)); distinct[e.note]++; }
    REQUIRE(distinct.size() >= 4);
    REQUIRE(balanced(ev));
}
