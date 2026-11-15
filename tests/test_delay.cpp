// Delay engine (Stage 6) tests.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <ambient/dsp/Delay.h>
#include "StereoMetrics.h"
#include <cmath>
#include <vector>

using namespace ambient::dsp;
using M = Delay::Mode;

namespace
{
constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;
Delay::Params base(M mode, float timeMs = 250.0f, float fb = 0.5f)
{
    Delay::Params p; p.on = true; p.mode = mode; p.timeMs = timeMs; p.feedback = fb; p.mix = 0.5f;
    p.modDepth = 0.0f; p.drive = 0.0f; p.hpHz = 20.0f; p.lpHz = 20000.0f; p.width = 1.0f;
    return p;
}
struct St { std::vector<float> l, r; };
St run(Delay& d, St s, int block = 256)
{
    for (size_t i = 0; i < s.l.size(); i += size_t(block)) d.process(s.l.data() + i, s.r.data() + i, int(std::min<size_t>(size_t(block), s.l.size() - i)));
    return s;
}
St impulse(double seconds) { St s { std::vector<float>(size_t(kSr * seconds)), std::vector<float>(size_t(kSr * seconds)) }; s.l[0] = s.r[0] = 1.0f; return s; }
size_t argmaxAfter(const std::vector<float>& x, size_t from, size_t to)
{
    size_t k = from; for (size_t i = from; i < std::min(to, x.size()); ++i) if (std::fabs(x[i]) > std::fabs(x[k])) k = i; return k;
}
double energy(const std::vector<float>& x, size_t a, size_t b) { double e = 0; for (size_t i = a; i < std::min(b, x.size()); ++i) e += double(x[i]) * x[i]; return e; }
} // namespace

TEST_CASE("Delay: off / mix 0 are bit-exact; zero latency")
{
    for (bool on : { false, true })
    {
        Delay d; d.prepare(kSr, 256);
        auto p = base(M::Digital); p.on = on; p.mix = 0.0f; d.setParams(p); d.reset();
        St s { std::vector<float>(48000), std::vector<float>(48000) };
        for (size_t i = 0; i < s.l.size(); ++i) { s.l[i] = 0.3f * float(std::sin(i * 0.01)); s.r[i] = 0.2f * float(std::cos(i * 0.013)); }
        const auto out = run(d, s);
        for (size_t i = 0; i < s.l.size(); ++i) { REQUIRE(out.l[i] == s.l[i]); REQUIRE(out.r[i] == s.r[i]); }
    }
    REQUIRE(Delay::getLatencySamples() == 0);
}

TEST_CASE("Delay: digital echo lands at the set time and repeats decay by the feedback")
{
    for (float t : { 10.0f, 250.0f, 1000.0f })
    {
        Delay d; d.prepare(kSr, 256); d.setParams(base(M::Digital, t, 0.5f)); d.reset();
        const auto s = run(d, impulse(t * 0.001 * 3.5 + 0.1));
        const size_t T = size_t(std::lround(t * 0.001 * kSr));
        const size_t e1 = argmaxAfter(s.l, 5, T + T / 2), e2 = argmaxAfter(s.l, T + T / 2, 2 * T + T / 2);
        INFO("time " << t << " ms: first echo at " << e1 << " expected " << T);
        REQUIRE(std::abs(long(e1) - long(T)) <= 2);
        REQUIRE(std::abs(long(e2) - long(2 * T)) <= 3);
        const double r = std::sqrt(energy(s.l, e2 - 40, e2 + 40) / energy(s.l, e1 - 40, e1 + 40));
        INFO("repeat ratio " << r);
        REQUIRE(r == Catch::Approx(0.51).margin(0.05));
    }
}

TEST_CASE("Delay: host sync divisions")
{
    REQUIRE(Delay::syncedMs(9, 120.0) == Catch::Approx(500.0f));        // 1/4 at 120 BPM
    REQUIRE(Delay::syncedMs(6, 120.0) == Catch::Approx(250.0f));        // 1/8
    REQUIRE(Delay::syncedMs(7, 120.0) == Catch::Approx(375.0f));        // 1/8 dotted
    REQUIRE(Delay::syncedMs(5, 120.0) == Catch::Approx(1000.0f / 6.0f)); // 1/8 triplet
    REQUIRE(std::string(Delay::divisionName(9)) == "1/4");
}

TEST_CASE("Delay: ping-pong alternates sides")
{
    Delay d; d.prepare(kSr, 256); d.setParams(base(M::PingPong, 200.0f, 0.7f)); d.reset();
    const auto s = run(d, impulse(1.0));
    const size_t T = size_t(0.2 * kSr);
    auto side = [&](size_t k) { const size_t a = k * T - 50, b = k * T + 50; return energy(s.l, a, b) > energy(s.r, a, b) ? 'L' : 'R'; };
    REQUIRE(side(1) == 'L'); REQUIRE(side(2) == 'R'); REQUIRE(side(3) == 'L');
}

TEST_CASE("Delay: multi-tap produces four taps per period")
{
    Delay d; d.prepare(kSr, 256); d.setParams(base(M::MultiTap, 400.0f, 0.0f)); d.reset();
    const auto s = run(d, impulse(0.5));
    int taps = 0;
    for (float f : { 0.25f, 0.5f, 0.75f, 1.0f })
    {
        const size_t k = size_t(f * 0.4 * kSr);
        if (energy(s.l, k - 20, k + 20) + energy(s.r, k - 20, k + 20) > 0.01) ++taps;
    }
    REQUIRE(taps == 4);
}

TEST_CASE("Delay: reverse mode plays segments backwards")
{
    // input: rising chirp; reversed output must contain a falling chirp (instantaneous frequency decreases)
    Delay d; d.prepare(kSr, 256);
    auto p = base(M::Reverse, 500.0f, 0.0f); p.mix = 1.0f; d.setParams(p); d.reset();
    St s { std::vector<float>(size_t(kSr * 3)), std::vector<float>(size_t(kSr * 3)) };
    for (size_t i = 0; i < s.l.size(); ++i)
    {
        const double t = std::fmod(double(i) / kSr, 0.5);           // 500 ms rising sweeps 200 -> 2000 Hz
        s.l[i] = s.r[i] = float(0.5 * std::sin(2 * kPi * (200.0 * t + 0.5 * 3600.0 * t * t)));
    }
    const auto in = s;
    const auto o = run(d, s);
    // zero-crossing rate trend inside output windows: count falling vs rising trend
    auto zcr = [&](const std::vector<float>& x, size_t a, size_t b) { int z = 0; for (size_t i = a + 1; i < b; ++i) if ((x[i - 1] < 0) != (x[i] < 0)) ++z; return z; };
    auto trend = [&](const std::vector<float>& x)
    {
        int down = 0, up = 0;
        for (size_t a = size_t(kSr); a + 2400 < x.size(); a += 1200) { const int z1 = zcr(x, a, a + 1200), z2 = zcr(x, a + 1200, a + 2400); if (z2 < z1) ++down; else if (z2 > z1) ++up; }
        return double(down) / double(std::max(1, down + up));
    };
    INFO("input falling fraction " << trend(in.l) << ", output " << trend(o.l));
    REQUIRE(trend(in.l) < 0.3);
    REQUIRE(trend(o.l) > 0.6);
}

TEST_CASE("Delay: pitch mode shifts each repeat")
{
    Delay d; d.prepare(kSr, 256);
    auto p = base(M::Pitch, 300.0f, 0.0f); p.pitch = 12.0f; p.mix = 1.0f; d.setParams(p); d.reset();
    St s { std::vector<float>(size_t(kSr * 2)), std::vector<float>(size_t(kSr * 2)) };
    for (size_t i = 0; i < s.l.size(); ++i) s.l[i] = s.r[i] = 0.4f * float(std::sin(2 * kPi * 330.0 * double(i) / kSr));
    const auto o = run(d, s);
    auto g = [&](double f) { double re = 0, im = 0; for (size_t i = size_t(kSr); i < o.l.size(); ++i) { re += o.l[i] * std::cos(2 * kPi * f * double(i) / kSr); im += o.l[i] * std::sin(2 * kPi * f * double(i) / kSr); } return re * re + im * im; };
    REQUIRE(10.0 * std::log10(g(660.0) / g(330.0)) > 15.0);
}

TEST_CASE("Delay: tape modulation detunes, filters darken, granular is active")
{
    // tape wow: a steady 1 kHz tone through tape with mod gets measurable FM sidebands vs no mod
    auto spreadAround = [](M mode, float depth)
    {
        Delay d; d.prepare(kSr, 256); auto p = base(mode, 300.0f, 0.0f); p.modDepth = depth; p.mix = 1.0f; d.setParams(p); d.reset();
        St s { std::vector<float>(size_t(kSr * 3)), std::vector<float>(size_t(kSr * 3)) };
        for (size_t i = 0; i < s.l.size(); ++i) s.l[i] = s.r[i] = 0.4f * float(std::sin(2 * kPi * 1000.0 * double(i) / kSr));
        const auto o = run(d, s);
        auto g = [&](double f) { double re = 0, im = 0; for (size_t i = size_t(kSr); i < o.l.size(); ++i) { re += o.l[i] * std::cos(2 * kPi * f * double(i) / kSr); im += o.l[i] * std::sin(2 * kPi * f * double(i) / kSr); } return re * re + im * im; };
        return g(1000.0) / (g(990.0) + g(1010.0) + g(1003.0) + g(997.0) + 1e-12);
    };
    REQUIRE(spreadAround(M::Tape, 1.0f) < 0.3 * spreadAround(M::Tape, 0.0f));

    // BBD: long delay times darken the loop more than the digital mode
    auto hf = [](M mode)
    {
        Delay d; d.prepare(kSr, 256); auto p = base(mode, 1500.0f, 0.0f); p.mix = 1.0f; d.setParams(p); d.reset();
        auto s = impulse(2.0);
        const auto o = run(d, s);
        double h = 0, a = 0; for (size_t i = size_t(1.45 * kSr); i < size_t(1.6 * kSr); ++i) { h += std::pow(double(o.l[i] - o.l[i - 1]), 2); a += double(o.l[i]) * o.l[i]; }
        return a > 0 ? h / a : 1.0;
    };
    REQUIRE(hf(M::BBD) < 0.1 * hf(M::Digital));

    Delay d; d.prepare(kSr, 256); auto p = base(M::Granular, 400.0f, 0.3f); p.mix = 1.0f; p.modDepth = 0.5f; d.setParams(p); d.reset();
    St s { std::vector<float>(size_t(kSr * 3)), std::vector<float>(size_t(kSr * 3)) };
    for (size_t i = 0; i < size_t(kSr); ++i) s.l[i] = s.r[i] = 0.4f * float(std::sin(2 * kPi * 220.0 * double(i) / kSr));
    const auto o = run(d, s);
    REQUIRE(energy(o.l, size_t(1.2 * kSr), size_t(1.6 * kSr)) > 1.0);   // grains keep reading the past
    const auto m = stereo_test::measure(o.l, o.r, kSr, size_t(0.5 * kSr));
    REQUIRE(m.corr < 0.98);                                              // random pan = stereo
}

TEST_CASE("Delay: changing the time never clicks (digital and tape)")
{
    for (M mode : { M::Digital, M::Tape, M::PingPong, M::MultiTap })
    {
        Delay d; d.prepare(kSr, 64); auto p = base(mode, 100.0f, 0.4f); p.mix = 1.0f; d.setParams(p); d.reset();
        St s { std::vector<float>(size_t(kSr * 3)), std::vector<float>(size_t(kSr * 3)) };
        for (size_t i = 0; i < s.l.size(); ++i) s.l[i] = s.r[i] = 0.3f * float(std::sin(2 * kPi * 200.0 * double(i) / kSr));
        float maxStep = 0.0f;
        for (size_t i = 0; i < s.l.size(); i += 64)
        {
            if (i == size_t(kSr) + 64 * 10) { p.timeMs = 900.0f; d.setParams(p); }
            if (i == size_t(2 * kSr)) { p.timeMs = 37.0f; d.setParams(p); }
            d.process(s.l.data() + i, s.r.data() + i, 64);
        }
        for (size_t i = size_t(0.5 * kSr); i < s.l.size(); ++i) maxStep = std::max(maxStep, std::fabs(s.l[i] - s.l[i - 1]));
        INFO("mode " << int(mode) << " max sample step " << maxStep);
        REQUIRE(maxStep < 0.12f);   // a 200 Hz sine at this level steps <= ~0.04; a click would be >> 0.1
    }
}

TEST_CASE("Delay: every mode is stable at max feedback, drive and modulation; mono and stereo input")
{
    for (double rate : { 44100.0, 96000.0, 192000.0 })
        for (int m = 0; m < Delay::kNumModes; ++m)
            for (int bs : { 1, 512 })
            {
                Delay d; d.prepare(rate, bs);
                auto p = base(M(m), 60.0f, 1.0f); p.drive = 1.0f; p.modDepth = 1.0f; p.modRate = 10.0f; p.mix = 1.0f; p.lpHz = 20000.0f; p.hpHz = 20.0f;
                d.setParams(p); d.reset();
                const size_t n = size_t(rate * 2.0);
                St s { std::vector<float>(n), std::vector<float>(n) };
                uint32_t seed = 3;
                for (size_t i = 0; i < n; ++i) { seed = seed * 1664525u + 1013904223u; s.l[i] = 0.9f * (float(int(seed >> 8) - (1 << 23)) / float(1 << 23)); s.r[i] = (m & 1) ? s.l[i] : -s.l[i] * 0.5f; }
                const auto o = run(d, s, bs);
                float peak = 0.0f; for (size_t i = 0; i < n; ++i) { REQUIRE(std::isfinite(o.l[i])); REQUIRE(std::isfinite(o.r[i])); peak = std::max({ peak, std::fabs(o.l[i]), std::fabs(o.r[i]) }); }
                INFO("rate " << rate << " mode " << m << " block " << bs << " peak " << peak);
                REQUIRE(peak < 6.0f);
            }
}

TEST_CASE("Delay: stereo is never anti-phase for a mono source")
{
    for (int m = 0; m < Delay::kNumModes; ++m)
    {
        Delay d; d.prepare(kSr, 256); auto p = base(M(m), 330.0f, 0.5f); p.width = 0.7f; p.modDepth = 0.3f; p.mix = 1.0f; d.setParams(p); d.reset();
        St s { std::vector<float>(size_t(kSr * 4)), std::vector<float>(size_t(kSr * 4)) };
        for (size_t i = 0; i < size_t(kSr * 2); ++i) { double x = 0; for (double f : { 110.0, 164.8, 220.5 }) x += 2.0 * std::fmod(f * double(i) / kSr, 1.0) - 1.0; s.l[i] = s.r[i] = float(0.15 * x); }
        const auto o = run(d, s);
        const auto mm = stereo_test::measure(o.l, o.r, kSr, size_t(0.5 * kSr));
        INFO("mode " << m << " corr " << mm.corr << " neg windows " << mm.negFraction);
        REQUIRE(mm.corr > 0.2);
        REQUIRE(mm.monoLossDb > -2.5);
    }
}
