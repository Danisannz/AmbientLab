// Granular engine (Stage 4) tests.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <ambient/dsp/Granular.h>
#include <cmath>
#include <vector>

using namespace ambient::dsp;
using Catch::Approx;

namespace
{
constexpr double kPi = 3.14159265358979323846;

struct St { std::vector<float> l, r; };
St sine(double f, double sr, size_t n, float a = 0.5f)
{
    St s { std::vector<float>(n), std::vector<float>(n) };
    for (size_t i = 0; i < n; ++i) s.l[i] = s.r[i] = a * float(std::sin(2.0 * kPi * f * double(i) / sr));
    return s;
}
void run(Granular& g, St& s, int block = 256)
{
    for (size_t i = 0; i < s.l.size(); i += size_t(block))
        g.process(s.l.data() + i, s.r.data() + i, int(std::min<size_t>(size_t(block), s.l.size() - i)));
}
double goertzelDb(const std::vector<float>& x, size_t from, size_t to, double f, double sr)
{
    double re = 0, im = 0;
    for (size_t i = from; i < to; ++i) { re += x[i] * std::cos(2 * kPi * f * double(i) / sr); im += x[i] * std::sin(2 * kPi * f * double(i) / sr); }
    return 10.0 * std::log10(re * re + im * im + 1e-30);
}
Granular::Params wetOnly()
{
    Granular::Params p;
    p.on = true; p.mix = 1.0f; p.sizeMs = 80.0f; p.density = 30.0f; p.positionMs = 200.0f;
    p.scatter = 0.0f; p.spread = 0.0f; p.feedback = 0.0f;
    return p;
}
} // namespace

TEST_CASE("Granular: off = bit-exact dry path, zero latency")
{
    Granular g; g.prepare(48000.0, 256);
    auto s = sine(330.0, 48000.0, 48000);
    const auto ref = s;
    run(g, s);
    for (size_t i = 0; i < s.l.size(); ++i) REQUIRE(s.l[i] == ref.l[i]);
    REQUIRE(Granular::getLatencySamples() == 0);
}

TEST_CASE("Granular: pitch shifting moves the dominant frequency")
{
    const double sr = 48000.0;
    for (float semis : { 12.0f, -12.0f, 7.0f })
    {
        Granular g; g.prepare(sr, 256);
        auto p = wetOnly(); p.pitch = semis;
        g.setParams(p);
        auto s = sine(440.0, sr, size_t(sr * 3));
        run(g, s);
        const double target = 440.0 * std::exp2(semis / 12.0);
        const size_t a = size_t(sr * 1.5), b = size_t(sr * 3);
        INFO("pitch " << semis);
        REQUIRE(goertzelDb(s.l, a, b, target, sr) > goertzelDb(s.l, a, b, 440.0, sr) + 20.0);
    }
}

TEST_CASE("Granular: freeze holds the buffer - output continues after the input stops")
{
    const double sr = 48000.0;
    Granular g; g.prepare(sr, 256);
    auto p = wetOnly();
    g.setParams(p);
    auto s = sine(220.0, sr, size_t(sr * 2));
    run(g, s);
    p.freeze = true;
    g.setParams(p);
    St silence { std::vector<float>(size_t(sr * 3), 0.0f), std::vector<float>(size_t(sr * 3), 0.0f) };
    run(g, silence);
    REQUIRE(goertzelDb(silence.l, size_t(sr * 2), size_t(sr * 3), 220.0, sr) > 40.0); // still sounding 3 s later

    // without freeze, silence flushes through once the position has passed
    Granular h; h.prepare(sr, 256); h.setParams(wetOnly());
    auto s2 = sine(220.0, sr, size_t(sr * 2)); run(h, s2);
    St sil2 { std::vector<float>(size_t(sr * 3), 0.0f), std::vector<float>(size_t(sr * 3), 0.0f) };
    run(h, sil2);
    float tail = 0.0f;
    for (size_t i = size_t(sr * 1); i < sil2.l.size(); ++i) tail = std::max(tail, std::fabs(sil2.l[i]));
    REQUIRE(tail < 1e-6f);
}

TEST_CASE("Granular: reverse grains read backwards")
{
    const double sr = 48000.0;
    Granular g; g.prepare(sr, 256);
    auto p = wetOnly(); p.reverse = 1.0f; p.window = Granular::Window::Tukey; p.density = 2.0f; p.sizeMs = 300.0f;
    g.setParams(p);
    // input: rising ramp (slope +), so forward reading rises and reverse reading falls
    St s { std::vector<float>(size_t(sr * 4)), std::vector<float>(size_t(sr * 4)) };
    for (size_t i = 0; i < s.l.size(); ++i) s.l[i] = s.r[i] = float(std::fmod(double(i) / sr, 2.0)) * 0.4f - 0.4f;
    run(g, s);
    int down = 0, up = 0;
    for (size_t i = size_t(sr * 2.2); i + 1 < s.l.size(); ++i)
    {
        const float d = s.l[i + 1] - s.l[i];
        if (d < -1e-6f) ++down; else if (d > 1e-6f) ++up;
    }
    INFO("down " << down << " up " << up);
    REQUIRE(down > up * 2);
}

TEST_CASE("Granular: density controls the grain rate; pool is bounded")
{
    const double sr = 48000.0;
    for (float d : { 5.0f, 40.0f })
    {
        Granular g; g.prepare(sr, 256);
        auto p = wetOnly(); p.density = d; p.sizeMs = 50.0f;
        g.setParams(p);
        auto s = sine(200.0, sr, size_t(sr * 4)); run(g, s);
        // first ~250 ms have too little history; count over the rest
        const double rate = double(g.getGrainsStarted()) / 3.7;
        INFO("density " << d << " -> " << rate << " grains/s");
        REQUIRE(rate == Approx(d).epsilon(0.15));
    }
    Granular g; g.prepare(sr, 256);
    auto p = wetOnly(); p.density = 200.0f; p.sizeMs = 1000.0f; // would need 200 overlapping grains
    g.setParams(p);
    auto s = sine(200.0, sr, size_t(sr * 3)); run(g, s);
    REQUIRE(g.getActiveGrains() <= Granular::kMaxGrains);
}

TEST_CASE("Granular: stereo spread decorrelates, zero spread stays mono")
{
    const double sr = 48000.0;
    auto corr = [&](float spread)
    {
        Granular g; g.prepare(sr, 256);
        auto p = wetOnly(); p.spread = spread; p.scatter = 0.5f;
        g.setParams(p);
        auto s = sine(300.0, sr, size_t(sr * 3)); run(g, s);
        double lr = 0, ll = 0, rr = 0;
        for (size_t i = size_t(sr); i < s.l.size(); ++i) { lr += s.l[i] * s.r[i]; ll += s.l[i] * s.l[i]; rr += s.r[i] * s.r[i]; }
        return lr / std::sqrt(ll * rr);
    };
    REQUIRE(corr(0.0f) > 0.999);
    REQUIRE(corr(1.0f) < 0.97);
}

TEST_CASE("Granular: maximum feedback stays bounded; deterministic; stable at all rates/blocks")
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int block : { 1, 64, 1024 })
        {
            Granular g; g.prepare(sr, block);
            auto p = wetOnly(); p.feedback = 0.95f; p.density = 80.0f; p.sizeMs = 200.0f; p.pitch = 12.0f; p.pitchRandom = 12.0f; p.reverse = 0.5f;
            p.scatter = 1.0f; p.spread = 1.0f;
            g.setParams(p);
            auto s = sine(150.0, sr, size_t(sr * 2), 0.9f);
            run(g, s, block);
            float peak = 0.0f;
            for (float v : s.l) { REQUIRE(std::isfinite(v)); peak = std::max(peak, std::fabs(v)); }
            REQUIRE(peak < 6.0f);
        }
    Granular a; a.prepare(48000.0, 256); a.setParams(wetOnly());
    Granular b; b.prepare(48000.0, 256); b.setParams(wetOnly());
    auto x = sine(200.0, 48000.0, 48000), y = x;
    run(a, x); run(b, y);
    for (size_t i = 0; i < x.l.size(); ++i) REQUIRE(x.l[i] == y.l[i]);
}

TEST_CASE("Granular: mix crossfades between dry and wet without steps")
{
    const double sr = 48000.0;
    Granular g; g.prepare(sr, 256);
    auto p = wetOnly(); p.on = false;
    g.setParams(p);
    auto s = sine(100.0, sr, size_t(sr * 2), 0.5f);
    for (size_t i = 0; i < s.l.size(); i += 256)
    {
        if (i == size_t(sr)) { p.on = true; g.setParams(p); } // switch on mid-stream
        g.process(s.l.data() + i, s.r.data() + i, int(std::min<size_t>(256, s.l.size() - i)));
    }
    float maxStep = 0.0f;
    for (size_t i = size_t(sr) - 10; i < size_t(sr) + 2000; ++i) maxStep = std::max(maxStep, std::fabs(s.l[i] - s.l[i - 1]));
    REQUIRE(maxStep < 0.05f);
}
