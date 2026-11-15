// Step sequencer + scales (Stage 8) tests.
#include <catch2/catch_test_macros.hpp>
#include <ambient/dsp/Sequencer.h>
#include <ambient/dsp/Scales.h>
#include <ambient/dsp/Delay.h>
#include <cmath>
#include <cstdlib>
#include <string>
#include <map>
#include <set>
#include <vector>

using namespace ambient::dsp;
using Seq = Sequencer;

namespace
{
struct AbsEvent { long sample; int note; float vel; bool on; };

Seq::Step on(int note = 0) { Seq::Step s; s.active = true; s.note = note; s.gate = 0.5f; return s; }

void fill(Seq& q, int len, Seq::Step st = on())
{
    for (int i = 0; i < Seq::kMaxSteps; ++i) { auto s = st; s.active = i < len; q.setStep(i, s); }
}

/** Runs the sequencer for `total` samples in blocks; host sync with ppq derived from the sample position. */
std::vector<AbsEvent> run(Seq& q, double sr, int block, long total, double bpm = 120.0, bool hostSync = true, long startSample = 0,
                          std::vector<int>* stepsAtOn = nullptr)
{
    std::vector<AbsEvent> out;
    Seq::Transport t; t.playing = true; t.hostSync = hostSync; t.bpm = bpm;
    const double spb = sr * 60.0 / bpm;
    for (long pos = 0; pos < total; pos += block)
    {
        const int n = int(std::min<long>(block, total - pos));
        t.ppqAtBlockStart = double(pos + startSample) / spb;
        q.process(n, t);
        for (int i = 0; i < q.numEvents(); ++i)
        {
            const auto& e = q.event(i);
            out.push_back({ pos + e.sampleOffset, e.note, e.velocity, e.noteOn });
            if (e.noteOn && stepsAtOn) stepsAtOn->push_back(q.currentStep());
        }
    }
    return out;
}

std::vector<long> onsets(const std::vector<AbsEvent>& ev)
{
    std::vector<long> r; for (auto& e : ev) if (e.on) r.push_back(e.sample); return r;
}

/** Every note-on is followed by a matching note-off, and nothing is left sounding. */
bool balanced(const std::vector<AbsEvent>& ev)
{
    std::map<int, int> c;
    for (auto& e : ev) { c[e.note] += e.on ? 1 : -1; if (c[e.note] < 0) return false; }
    for (auto& [k, v] : c) if (v != 0) return false;
    return true;
}

void stop(Seq& q, std::vector<AbsEvent>& ev, long at)
{
    Seq::Transport t; t.playing = false;
    q.process(64, t);
    for (int i = 0; i < q.numEvents(); ++i) ev.push_back({ at + q.event(i).sampleOffset, q.event(i).note, 0.0f, q.event(i).noteOn });
}
} // namespace

TEST_CASE("Scales: 16 scales, names, every quantized note is in the scale")
{
    REQUIRE(scales::kNumScales == 16);
    REQUIRE(std::string(scales::name(0)) == "Major");
    REQUIRE(std::string(scales::name(13)) == "Hirajoshi");
    REQUIRE(std::string(scales::name(15)) == "Chromatic");
    for (int s = 0; s < 16; ++s)
        for (int root = 0; root < 12; ++root)
        {
            REQUIRE(scales::contains(60 + root, s, root));          // root always in scale
            for (int n = 20; n < 110; ++n)
            {
                const int q = scales::quantize(n, s, root);
                REQUIRE(scales::contains(q, s, root));
                REQUIRE(std::abs(q - n) <= 3);
                if (scales::contains(n, s, root)) REQUIRE(q == n);
            }
        }
    REQUIRE(scales::quantize(61, 0, 0) == 60);   // C# in C major: tie -> down
    REQUIRE(scales::quantize(63, 9, 0) == 62);   // D# in C major pentatonic: tie D/E -> D
    REQUIRE(scales::quantize(66, 0, 0) == 65);   // F# in C major: tie F/G -> F
}

TEST_CASE("Sequencer: host-sync step timing is sample-accurate across blocks and rates")
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 1, 64, 512 })
        {
            Seq q; q.prepare(sr);
            Seq::Params p; p.length = 16; p.division = 3; q.setParams(p);   // 1/16
            fill(q, 16);
            const double stepSamples = 0.25 * sr * 60.0 / 120.0;
            const auto ev = run(q, sr, block, long(stepSamples * 20.5));
            const auto on = onsets(ev);
            INFO("sr " << sr << " block " << block);
            REQUIRE(on.size() == 21);
            for (size_t k = 0; k < on.size(); ++k) REQUIRE(std::fabs(double(on[k]) - double(k) * stepSamples) <= 1.0);
            // gate 0.5: note-offs half a step later
            for (auto& e : ev) if (! e.on) { const double ph = std::fmod(double(e.sample), stepSamples); REQUIRE(std::fabs(ph - 0.5 * stepSamples) <= 1.5); }
        }
}

TEST_CASE("Sequencer: starting mid-song aligns to the host grid")
{
    const double sr = 48000.0;
    Seq q; q.prepare(sr);
    Seq::Params p; p.division = 6; q.setParams(p); fill(q, 16);              // 1/8 = 12000 samples at 120
    const auto ev = run(q, sr, 512, 50000, 120.0, true, 100000);             // starts at sample 100000 (not on a step)
    const auto on = onsets(ev);
    REQUIRE(! on.empty());
    REQUIRE(on[0] + 100000 == 108000);                                       // next 1/8 boundary
}

TEST_CASE("Sequencer: internal clock timing")
{
    const double sr = 48000.0;
    Seq q; q.prepare(sr);
    Seq::Params p; p.length = 4; p.division = 9; q.setParams(p); fill(q, 4); // 1/4 at 100 BPM = 28800 samples
    const auto on = onsets(run(q, sr, 333, 28800 * 6 + 10, 100.0, false));
    REQUIRE(on.size() == 7);
    for (size_t k = 0; k < on.size(); ++k) REQUIRE(std::labs(on[k] - long(k) * 28800) <= 1);
}

TEST_CASE("Sequencer: swing delays every second step")
{
    const double sr = 48000.0;
    Seq q; q.prepare(sr);
    Seq::Params p; p.division = 3; p.swing = 0.5f; q.setParams(p); fill(q, 16);
    const double step = 6000.0;
    const auto on = onsets(run(q, sr, 128, long(step * 8)));
    REQUIRE(on.size() == 8);
    for (size_t k = 0; k < on.size(); ++k)
    {
        const double expect = double(k) * step + ((k & 1) ? 0.5 * 0.5 * step : 0.0);
        REQUIRE(std::fabs(double(on[k]) - expect) <= 1.0);
    }
}

TEST_CASE("Sequencer: probability, ratchets and skip")
{
    const double sr = 48000.0;
    {
        Seq q; q.prepare(sr); Seq::Params p; p.division = 0; q.setParams(p);   // 1/64: many steps fast
        auto st = on(); st.probability = 0.5f; fill(q, 16, st);
        const long slots = 2000; const double step = 0.0625 * sr * 0.5;
        const auto on = onsets(run(q, sr, 512, long(step * slots)));
        INFO("played " << on.size() << " of " << slots);
        REQUIRE(on.size() > size_t(0.4 * slots));
        REQUIRE(on.size() < size_t(0.6 * slots));
    }
    {
        Seq q; q.prepare(sr); Seq::Params p; p.length = 4; p.division = 6; q.setParams(p);
        fill(q, 4);
        for (int i = 0; i < 4; ++i) { auto s = on(); s.ratchet = i + 1; q.setStep(i, s); }
        const auto ev = run(q, sr, 64, 4 * 12000);
        const auto on = onsets(ev);
        REQUIRE(on.size() == 1 + 2 + 3 + 4);
        int inStep[4] = {}; for (long s : on) ++inStep[s / 12000];
        for (int i = 0; i < 4; ++i) REQUIRE(inStep[i] == i + 1);
        // 4-ratchet notes evenly spaced at 3000 samples
        for (int k = 0; k < 4; ++k) REQUIRE(std::labs(on[size_t(6 + k)] - (36000 + 3000L * k)) <= 1);
        REQUIRE(balanced(ev));
    }
    {
        Seq q; q.prepare(sr); Seq::Params p; p.length = 4; p.division = 6; q.setParams(p);
        for (int i = 0; i < 4; ++i) q.setStep(i, on(i * 2));                 // notes C D E F#->quantized
        auto s1 = on(2); s1.skip = true; q.setStep(1, s1);
        std::vector<int> stepsSeen;
        const auto on = onsets(run(q, sr, 64, 6 * 12000, 120.0, true, 0, &stepsSeen));
        REQUIRE(on.size() == 6);                                             // skipped step consumes no time
        for (size_t k = 0; k < on.size(); ++k) REQUIRE(std::labs(on[k] - long(k) * 12000) <= 1);
        const std::vector<int> expect { 0, 2, 3, 0, 2, 3 };
        REQUIRE(stepsSeen == expect);
    }
}

TEST_CASE("Sequencer: directions")
{
    const double sr = 48000.0;
    auto order = [&](Seq::Direction d, int slots, int len = 4)
    {
        Seq q; q.prepare(sr); Seq::Params p; p.length = len; p.division = 6; p.direction = d; q.setParams(p);
        fill(q, len);
        std::vector<int> seen;
        run(q, sr, 256, long(12000) * slots, 120.0, true, 0, &seen);
        return seen;
    };
    REQUIRE(order(Seq::Direction::Forward, 6) == std::vector<int> { 0, 1, 2, 3, 0, 1 });
    REQUIRE(order(Seq::Direction::Reverse, 6) == std::vector<int> { 3, 2, 1, 0, 3, 2 });
    REQUIRE(order(Seq::Direction::PingPong, 10) == std::vector<int> { 0, 1, 2, 3, 2, 1, 0, 1, 2, 3 });
    const auto rnd = order(Seq::Direction::Random, 200, 8);
    std::set<int> visited(rnd.begin(), rnd.end());
    REQUIRE(visited.size() == 8);
    REQUIRE(rnd != order(Seq::Direction::Forward, 200, 8));
}

TEST_CASE("Sequencer: notes follow scale and root for all scales")
{
    const double sr = 48000.0;
    for (int sc = 0; sc < 16; ++sc)
        for (int root : { 0, 3, 7, 11 })
        {
            Seq q; q.prepare(sr); Seq::Params p; p.length = 16; p.division = 1; p.scale = sc; p.root = root; q.setParams(p);
            for (int i = 0; i < 16; ++i) q.setStep(i, on(-15 + 2 * i));
            const auto ev = run(q, sr, 512, 16 * 3000);
            for (auto& e : ev) if (e.on) REQUIRE(scales::contains(e.note, sc, root));
        }
    // step note 0 lands on base + root
    Seq q; q.prepare(sr); Seq::Params p; p.root = 2; p.scale = 0; p.division = 6; q.setParams(p); fill(q, 1, on(0));
    const auto ev = run(q, sr, 64, 1000);
    REQUIRE(ev.front().note == 48 + 2);
}

TEST_CASE("Sequencer: scale/root modulation only applies at step boundaries (or pattern start)")
{
    const double sr = 48000.0;
    for (bool patternOnly : { false, true })
    {
        Seq q; q.prepare(sr);
        Seq::Params p; p.length = 4; p.division = 6; p.scale = 15; p.changeOnPatternStartOnly = patternOnly; q.setParams(p);
        fill(q, 4, on(0));
        Seq::Transport t; t.playing = true; t.bpm = 120.0;
        std::vector<std::pair<long, int>> notes;
        const double spb = 24000.0;
        for (long pos = 0; pos < 12000L * 10; pos += 100)
        {
            if (pos == 12000L + 5000) { p.rootMod = 5.0f; q.setParams(p); }   // mid-step change (step 1)
            t.ppqAtBlockStart = double(pos) / spb;
            q.process(100, t);
            for (int i = 0; i < q.numEvents(); ++i) if (q.event(i).noteOn) notes.push_back({ pos + q.event(i).sampleOffset, q.event(i).note });
            if (pos == 12000L + 5000) REQUIRE(q.currentRoot() == 0);          // not applied mid-step
        }
        REQUIRE(notes.size() == 10);
        REQUIRE(notes[0].second == 48); REQUIRE(notes[1].second == 48);       // steps 0,1 unchanged
        if (! patternOnly) REQUIRE(notes[2].second == 53);                   // next boundary
        else { REQUIRE(notes[2].second == 48); REQUIRE(notes[3].second == 48); REQUIRE(notes[4].second == 53); }
    }
    // scale mod in index units, clamped
    Seq q; q.prepare(sr); Seq::Params p; p.scale = 14; p.scaleMod = 5.4f; p.division = 6; q.setParams(p); fill(q, 1);
    run(q, sr, 64, 2000);
    REQUIRE(q.currentScale() == 15);
}

TEST_CASE("Sequencer: no hanging notes on stop, reset, length change, tempo change and loop jumps")
{
    const double sr = 48000.0;
    {   // stop while a note is held (gate 1, long notes)
        Seq q; q.prepare(sr); Seq::Params p; p.division = 13; q.setParams(p);
        auto st = on(); st.gate = 1.0f; fill(q, 4, st);
        auto ev = run(q, sr, 512, 30000);
        REQUIRE(! balanced(ev));                       // note currently held
        stop(q, ev, 30000);
        REQUIRE(balanced(ev));
        REQUIRE(q.currentStep() == -1);
    }
    {   // reset while playing
        Seq q; q.prepare(sr); Seq::Params p; p.division = 13; q.setParams(p);
        auto st = on(); st.gate = 1.0f; fill(q, 4, st);
        auto ev = run(q, sr, 512, 30000);
        q.reset();
        auto ev2 = run(q, sr, 512, 512, 120.0, true, 30000);
        ev.insert(ev.end(), ev2.begin(), ev2.end());
        stop(q, ev, 40000);
        REQUIRE(balanced(ev));
    }
    {   // length / direction changes mid-run, ratchets, random
        Seq q; q.prepare(sr); Seq::Params p; p.length = 16; p.division = 3; q.setParams(p);
        q.loadPresetPattern(6);
        std::vector<AbsEvent> ev;
        Seq::Transport t; t.playing = true; t.bpm = 120.0;
        for (long pos = 0; pos < 48000L * 8; pos += 256)
        {
            if (pos % 48000 == 0) { p.length = 1 + int((pos / 48000) * 5 % 32); p.direction = Seq::Direction(int(pos / 48000) % 4); q.setParams(p); }
            t.ppqAtBlockStart = double(pos) / 24000.0;
            q.process(256, t);
            for (int i = 0; i < q.numEvents(); ++i) ev.push_back({ pos + q.event(i).sampleOffset, q.event(i).note, 0.0f, q.event(i).noteOn });
        }
        stop(q, ev, 48000L * 8);
        REQUIRE(balanced(ev));
        for (size_t i = 1; i < ev.size(); ++i) REQUIRE(ev[i].sample >= ev[i - 1].sample);
    }
    {   // tempo change: steps keep the grid of the new tempo; loop jump backwards re-derives
        Seq q; q.prepare(sr); Seq::Params p; p.division = 6; q.setParams(p);
        auto st = on(); st.gate = 0.9f; fill(q, 8, st);
        std::vector<AbsEvent> ev; std::vector<int> stepAtOn;
        Seq::Transport t; t.playing = true;
        double ppq = 0.0;
        long pos = 0;
        auto block = [&](double bpm)
        {
            t.bpm = bpm; t.ppqAtBlockStart = ppq;
            q.process(480, t);
            for (int i = 0; i < q.numEvents(); ++i)
            {
                ev.push_back({ pos + q.event(i).sampleOffset, q.event(i).note, 0.0f, q.event(i).noteOn });
                if (q.event(i).noteOn) stepAtOn.push_back(q.currentStep());
            }
            ppq += 480.0 * bpm / (60.0 * sr);
            pos += 480;
        };
        for (int b = 0; b < 100; ++b) block(120.0);     // 1 s = 2 beats = 4 eighths
        for (int b = 0; b < 100; ++b) block(60.0);      // 1 s = 1 beat  = 2 eighths
        const auto on = onsets(ev);
        REQUIRE(on.size() == 6);
        REQUIRE(std::labs(on[4] - 48000) <= 1);         // beat 2.0 at the tempo change
        REQUIRE(std::labs(on[5] - (48000 + 24000)) <= 1); // 1/8 at 60 BPM = 24000 samples
        ppq = 0.6;                                      // loop back into the middle of 1/8 #1
        block(60.0);
        const auto on2 = onsets(ev);
        REQUIRE(on2.size() == 6);                       // jumping into the middle of a step: wait for the next boundary
        for (int b = 0; b < 60; ++b) block(60.0);
        REQUIRE(stepAtOn.size() > 6);
        REQUIRE(stepAtOn[6] == 2);                      // re-derived from ppq: slot 2 after the jump
        stop(q, ev, pos);
        REQUIRE(balanced(ev));
    }
}

TEST_CASE("Sequencer: deterministic with a seed; randomise; presets play")
{
    const double sr = 48000.0;
    auto render = [&](uint32_t seed)
    {
        Seq q; q.prepare(sr); Seq::Params p; p.division = 3; p.direction = Seq::Direction::Random; p.seed = seed; q.setParams(p);
        q.randomise(seed, 1.0f);
        std::vector<int> notes; for (auto& e : run(q, sr, 512, 48000 * 4)) if (e.on) notes.push_back(e.note * 1000 + int(e.sample % 1000));
        return notes;
    };
    REQUIRE(render(7) == render(7));
    REQUIRE(render(7) != render(8));

    Seq q; q.prepare(sr);
    q.randomise(3, 0.0f);                              // amount 0 changes nothing
    REQUIRE(q.getStep(5).note == 0);
    for (int i = 0; i < Seq::kNumPresets; ++i)
    {
        Seq s; s.prepare(sr);
        const auto info = s.loadPresetPattern(i);
        REQUIRE(std::string(info.name) == Seq::presetName(i));
        Seq::Params p; p.length = info.length; p.division = info.division; p.swing = info.swing; p.direction = info.direction;
        if (info.scale >= 0) p.scale = info.scale;
        s.setParams(p);
        auto ev = run(s, sr, 512, 48000 * 8);
        INFO("preset " << info.name);
        REQUIRE(onsets(ev).size() >= 4);
        stop(s, ev, 48000 * 8);
        REQUIRE(balanced(ev));
    }
    // lock lanes
    Seq l; l.prepare(sr); Seq::Params p; p.division = 6; l.setParams(p); fill(l, 2);
    auto st = on(); st.locks[1] = 0.25f; l.setStep(1, st);
    run(l, sr, 64, 12000 + 100);
    REQUIRE(l.currentStep() == 1);
    REQUIRE(l.lockValue(1) == 0.25f);
    REQUIRE(std::isnan(l.lockValue(0)));
    REQUIRE(Seq::getLatencySamples() == 0);
}
