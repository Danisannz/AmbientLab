#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <ambient/StereoCaptureRing.h>
#include <ambient/VisualAnalysis.h>
#include <atomic>
#include <cmath>
#include <chrono>
#include <thread>
#include <vector>

using namespace ambient;
using namespace ambient::viz;
using Catch::Approx;

TEST_CASE("Capture ring: latest frames are returned in order")
{
    StereoCaptureRing ring(1024);
    REQUIRE(ring.getCapacity() == 1024);
    std::vector<float> l(300), r(300);
    for (int block = 0; block < 10; ++block)
    {
        for (int i = 0; i < 300; ++i) { l[size_t(i)] = float(block * 300 + i); r[size_t(i)] = -l[size_t(i)]; }
        ring.write(l.data(), r.data(), 300);
    }
    std::vector<float> ol(256), orr(256);
    REQUIRE(ring.readLatest(256, ol.data(), orr.data()));
    for (int i = 0; i < 256; ++i)
    {
        REQUIRE(ol[size_t(i)] == float(3000 - 256 + i));
        REQUIRE(orr[size_t(i)] == -ol[size_t(i)]);
    }
}

TEST_CASE("Capture ring: refuses stale or not-yet-written ranges")
{
    StereoCaptureRing ring(256);
    std::vector<float> d(100, 1.0f), o(100);
    REQUIRE_FALSE(ring.readLatest(64, o.data(), o.data())); // nothing written
    for (int i = 0; i < 10; ++i) ring.write(d.data(), d.data(), 100);
    REQUIRE_FALSE(ring.read(0, 64, o.data(), o.data()));        // overwritten long ago
    REQUIRE_FALSE(ring.read(990, 64, o.data(), o.data()));      // partly in the future
    REQUIRE_FALSE(ring.readLatest(200, o.data(), o.data()));    // > capacity/2
}

TEST_CASE("Capture ring: concurrent writer never corrupts validated reads")
{
    StereoCaptureRing ring(4096);
    std::atomic<bool> stop { false };
    std::thread writer([&] {
        std::vector<float> l(64), r(64);
        float counter = 0.0f;
        while (! stop.load())
        {
            for (int i = 0; i < 64; ++i) { l[size_t(i)] = counter; r[size_t(i)] = counter; counter += 1.0f; if (counter > 1.0e6f) counter = 0.0f; }
            ring.write(l.data(), r.data(), 64);
            std::this_thread::sleep_for(std::chrono::microseconds(50)); // ~audio-rate writer
        }
    });
    std::vector<float> ol(1024), orr(1024);
    int good = 0;
    for (int k = 0; k < 2000; ++k)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(100)); // UI-like pacing
        if (! ring.readLatest(1024, ol.data(), orr.data())) continue;
        ++good;
        for (size_t i = 1; i < ol.size(); ++i)
        {
            const float d = ol[i] - ol[i - 1];
            if (! (d == 1.0f || ol[i] == 0.0f)) { FAIL("non-contiguous frames in a validated read"); }
        }
    }
    stop = true;
    writer.join();
    REQUIRE(good > 0);
}

TEST_CASE("Stereo stats: mono, inverted, single channel, uncorrelated")
{
    const size_t n = 4800;
    std::vector<float> l(n), r(n);
    for (size_t i = 0; i < n; ++i) l[i] = std::sin(float(i) * 0.05f);

    r = l;
    auto s = computeStereoStats(l.data(), r.data(), n);
    REQUIRE(s.correlation == Approx(1.0f).margin(1e-4));
    REQUIRE(s.width == Approx(0.0f).margin(1e-4));
    REQUIRE(s.balance == Approx(0.0f).margin(1e-4));

    for (size_t i = 0; i < n; ++i) r[i] = -l[i];
    s = computeStereoStats(l.data(), r.data(), n);
    REQUIRE(s.correlation == Approx(-1.0f).margin(1e-4));
    REQUIRE(s.width == Approx(1.0f).margin(1e-4));

    std::fill(r.begin(), r.end(), 0.0f);
    s = computeStereoStats(l.data(), r.data(), n);
    REQUIRE(s.balance == Approx(-1.0f).margin(1e-4));

    // Quadrature (90 degrees): correlation ~0, width 0.5
    for (size_t i = 0; i < n; ++i) r[i] = std::cos(float(i) * 0.05f);
    s = computeStereoStats(l.data(), r.data(), n);
    REQUIRE(std::abs(s.correlation) < 0.02f);
    REQUIRE(s.width == Approx(0.5f).margin(0.02));

    std::vector<float> z(n, 0.0f);
    s = computeStereoStats(z.data(), z.data(), n); // silence -> no NaN
    REQUIRE(std::isfinite(s.correlation));
    REQUIRE(std::isfinite(s.width));
}

TEST_CASE("Scope trigger: rising edge aligns to the zero crossing")
{
    const size_t n = 4000, win = 1000;
    std::vector<float> d(n);
    const float w = 2.0f * 3.14159265f / 100.0f; // period 100 samples
    for (size_t i = 0; i < n; ++i) d[i] = std::sin(w * float(i) + 0.3f);

    const size_t start = findTrigger(d.data(), n, win, TriggerMode::Rising);
    REQUIRE(start + win <= n);
    REQUIRE(d[start - 1] < 0.0f);
    REQUIRE(d[start] >= 0.0f);

    const size_t f = findTrigger(d.data(), n, win, TriggerMode::Falling);
    REQUIRE(d[f - 1] > 0.0f);
    REQUIRE(d[f] <= 0.0f);

    REQUIRE(findTrigger(d.data(), n, win, TriggerMode::Free) == n - win);

    std::vector<float> dc(n, 0.5f); // no crossing -> latest
    REQUIRE(findTrigger(dc.data(), n, win, TriggerMode::Rising) == n - win);
}

TEST_CASE("Min/max decimation keeps extremes")
{
    std::vector<float> d(1000, 0.0f);
    d[123] = 0.9f; d[777] = -0.8f;
    std::vector<float> lo(10), hi(10);
    minMaxDecimate(d.data(), d.size(), 10, lo.data(), hi.data());
    REQUIRE(hi[1] == 0.9f);
    REQUIRE(lo[7] == -0.8f);
    REQUIRE(hi[0] == 0.0f);
}

TEST_CASE("Log frequency mapping round-trips")
{
    for (float f : { 20.0f, 100.0f, 1000.0f, 20000.0f })
        REQUIRE(logFreq(logPos(f, 20.0f, 20000.0f), 20.0f, 20000.0f) == Approx(f).epsilon(1e-4));
}
