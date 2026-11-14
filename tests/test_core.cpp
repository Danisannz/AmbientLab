#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <ambient/ParamIDs.h>
#include <ambient/Safety.h>
#include <ambient/Smoother.h>
#include <ambient/SpscFifo.h>
#include <ambient/TestToneSynth.h>
#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace ambient;

TEST_CASE("Parameter IDs are unique and non-empty")
{
    std::set<std::string> seen;
    for (auto id : { ids::masterGain, ids::inputGain, ids::sourceMix, ids::bypassSynth })
        REQUIRE(seen.insert(std::string(id)).second);
    for (auto id : ids::macros)
        REQUIRE(seen.insert(std::string(id)).second);
    REQUIRE(seen.size() == 12);
}

TEST_CASE("Smoother converges without overshoot")
{
    Smoother s;
    s.prepare(48000.0, 20.0f);
    s.reset(0.0f);
    s.setTarget(1.0f);
    float prev = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        const float v = s.next();
        REQUIRE(v >= prev);
        REQUIRE(v <= 1.0f);
        prev = v;
    }
    REQUIRE(s.getCurrent() == Catch::Approx(1.0f));
    REQUIRE_FALSE(s.isSmoothing());
}

TEST_CASE("SPSC fifo preserves order and respects capacity")
{
    SpscFifo<int, 8> f;
    for (int i = 0; i < 8; ++i) REQUIRE(f.push(i));
    REQUIRE_FALSE(f.push(99));
    for (int i = 0; i < 8; ++i) REQUIRE(*f.pop() == i);
    REQUIRE_FALSE(f.pop().has_value());
}

TEST_CASE("Safety: sanitize removes NaN/Inf, softClip bounded")
{
    float buf[4] = { 0.5f, NAN, INFINITY, -0.25f };
    REQUIRE(sanitize(buf, 4));
    REQUIRE(buf[1] == 0.0f);
    REQUIRE(buf[2] == 0.0f);
    REQUIRE(buf[0] == 0.5f);

    REQUIRE(softClip(0.5f) == 0.5f);
    for (float x : { 2.0f, 10.0f, 1000.0f, -50.0f })
        REQUIRE(std::abs(softClip(x)) <= 1.0f);
}

TEST_CASE("DC blocker removes offset")
{
    DcBlocker d;
    d.prepare(48000.0);
    float y = 0.0f;
    for (int i = 0; i < 48000; ++i) y = d.process(1.0f);
    REQUIRE(std::abs(y) < 1.0e-3f);
}

TEST_CASE("Test synth: 8-voice cap, click-free attack, release to silence")
{
    TestToneSynth s;
    const double sr = 48000.0;
    s.prepare(sr);

    for (int n = 60; n < 70; ++n) s.noteOn(n, 1.0f); // 10 notes -> steal
    std::vector<float> buf(4800, 0.0f);
    s.render(buf.data(), 0, int(buf.size()));
    REQUIRE(s.activeVoices() == ids::kMaxVoices);

    // First sample must be near zero (ramped attack, no click)
    TestToneSynth t; t.prepare(sr); t.noteOn(69, 1.0f);
    std::vector<float> a(64, 0.0f);
    t.render(a.data(), 0, 64);
    REQUIRE(std::abs(a[0]) < 1.0e-3f);
    // Max sample-to-sample jump bounded
    for (size_t i = 1; i < a.size(); ++i) REQUIRE(std::abs(a[i] - a[i - 1]) < 0.02f);

    s.allNotesOff();
    std::vector<float> tail(4800, 0.0f);
    s.render(tail.data(), 0, int(tail.size()));
    REQUIRE(s.activeVoices() == 0);
    REQUIRE(tail.back() == 0.0f);
}

TEST_CASE("Test synth output finite at all supported sample rates")
{
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        TestToneSynth s; s.prepare(sr);
        s.noteOn(36, 1.0f); s.noteOn(96, 1.0f);
        std::vector<float> b(2048, 0.0f);
        s.render(b.data(), 0, 2048);
        for (float v : b) REQUIRE(std::isfinite(v));
    }
}
