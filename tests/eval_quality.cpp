// High Quality mode evaluation (hidden test: run with  ambient_tests "[.quality]" -s ).
// Measures aliasing (energy in 20 Hz..20 kHz not at true harmonics) of every nonlinear /
// band-limited stage at 48 kHz, and the same object run at 4x with a high-quality
// polyphase-equivalent up/down filter (511-tap Kaiser sinc, ~-100 dB stopband):
// the difference is exactly what a 4x oversampled High Quality mode could buy.
#include <catch2/catch_test_macros.hpp>
#include <ambient/dsp/Oscillator.h>
#include <ambient/dsp/Filter.h>
#include <ambient/dsp/Delay.h>
#include <ambient/dsp/Fft.h>
#include <complex>
#include <cstdio>
#include <functional>
#include <vector>
#include <cstdlib>

using namespace ambient::dsp;

namespace
{
constexpr double kSr = 48000.0, kPi = 3.14159265358979323846;
constexpr int kN = 1 << 16;

double besselI0(double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; }
std::vector<float> kaiserSinc(int taps, double cutoff /*cycles/sample*/, double beta)
{
    std::vector<float> h(static_cast<size_t>(taps));
    const int m = taps - 1;
    for (int i = 0; i < taps; ++i)
    {
        const double n = i - m / 2.0;
        const double sinc = n == 0 ? 2 * cutoff : std::sin(2 * kPi * cutoff * n) / (kPi * n);
        const double r = 2.0 * i / m - 1.0;
        h[size_t(i)] = float(sinc * besselI0(beta * std::sqrt(1 - r * r)) / besselI0(beta));
    }
    return h;
}
std::vector<float> fir(const std::vector<float>& x, const std::vector<float>& h)
{
    std::vector<float> y(x.size(), 0.0f);
    for (size_t n = 0; n < x.size(); ++n)
    {
        double acc = 0;
        const size_t kmax = std::min(h.size(), n + 1);
        for (size_t k = 0; k < kmax; ++k) acc += double(h[k]) * x[n - k];
        y[n] = float(acc);
    }
    return y;
}

/** Aliasing of y (fundamental f0): energy outside +-6 bins of k*f0 within 20..20k Hz, relative to harmonic energy. */
double aliasDb(const std::vector<float>& y, double f0, double sr = kSr)
{
    Fft fft; fft.prepare(kN);
    std::vector<std::complex<float>> X(kN);
    const size_t off = y.size() - kN;
    for (int i = 0; i < kN; ++i)
    {
        const double a = 2 * kPi * i / (kN - 1);
        const double w = 0.35875 - 0.48829 * std::cos(a) + 0.14128 * std::cos(2 * a) - 0.01168 * std::cos(3 * a);
        X[size_t(i)] = { float(y[off + size_t(i)] * w), 0.0f };
    }
    fft.perform(X.data());
    const double binHz = sr / kN;
    double harm = 0, alias = 0;
    for (int b = 1; b < kN / 2; ++b)
    {
        const double f = b * binHz, e = std::norm(X[size_t(b)]);
        const double k = std::round(f / f0);
        const bool isHarm = k >= 1 && std::abs(f - k * f0) <= 6 * binHz;
        if (isHarm) harm += e;
        else if (f >= 20 && f <= 20000) alias += e;
    }
    return 10 * std::log10((alias + 1e-30) / (harm + 1e-30));
}

/** Runs a per-sample process at 48 kHz, and at 4x with proper up/down filtering. */
struct Pair { double base, os4; };
int gOs = 4;
Pair measure(double f0, std::function<std::vector<float>(double sr, int n)> gen, std::function<float(float)> proc48, std::function<float(float)> proc192, bool genIsSource = false)
{
    const int OS = gOs;
    const int n = kN + 4096;
    // base rate
    auto x = gen(kSr, n);
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = genIsSource ? x[i] : proc48(x[i]);
    // 4x
    const auto h = kaiserSinc(511, 0.5 / OS * 0.92, 10.0);
    std::vector<float> x4;
    if (genIsSource) x4 = gen(kSr * OS, n * OS);
    else
    {
        std::vector<float> z(size_t(n) * size_t(OS), 0.0f);
        for (int i = 0; i < n; ++i) z[size_t(i) * size_t(OS)] = float(OS) * x[size_t(i)];
        x4 = fir(z, h);
    }
    std::vector<float> y4(x4.size());
    for (size_t i = 0; i < x4.size(); ++i) y4[i] = genIsSource ? x4[i] : proc192(x4[i]);
    const auto d = fir(y4, h);
    std::vector<float> yd(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) yd[size_t(i)] = d[size_t(i) * size_t(OS)];
    return { aliasDb(y, f0), aliasDb(yd, f0) };
}
std::function<std::vector<float>(double, int)> sine(double f, float amp)
{
    return [f, amp](double sr, int n) { std::vector<float> v(static_cast<size_t>(n)); for (int i = 0; i < n; ++i) v[size_t(i)] = amp * float(std::sin(2 * kPi * f * i / sr)); return v; };
}
void row(const char* what, Pair p)
{
    std::printf("  %-52s %7.1f dB   %7.1f dB   gain %5.1f dB\n", what, p.base, p.os4, p.base - p.os4);
}
} // namespace

TEST_CASE("High Quality evaluation: aliasing at 48 kHz vs 4x oversampled", "[.quality]")
{
    if (const char* e = std::getenv("AMBIENT_OS")) gOs = std::atoi(e);
    std::printf("\n(oversampling factor for the right column: %dx)", gOs);
    std::printf("\nAliasing (energy 20 Hz-20 kHz off the harmonic series, relative to the signal)\n");
    std::printf("  %-52s %10s   %10s\n", "stage", "48 kHz", "4x OS");

    // Oscillators (PolyBLEP), generated natively at each rate
    for (Wave w : { Wave::Saw, Wave::Square })
        for (double f : { 440.0, 1318.5, 3135.96 })
        {
            auto gen = [w, f](double sr, int n) { BlepOsc o; std::vector<float> v(static_cast<size_t>(n)); for (int i = 0; i < n; ++i) v[size_t(i)] = 0.5f * o.next(f / sr, w, 0.5f); return v; };
            char nm[96]; std::snprintf(nm, sizeof nm, "oscillator %s %.0f Hz (PolyBLEP)", w == Wave::Saw ? "saw" : "square", f);
            row(nm, measure(f, gen, nullptr, nullptr, true));
        }

    // Saturator (first-order ADAA), drive on a sine
    for (auto [mode, name] : { std::pair<Saturator::Mode, const char*> { Saturator::Mode::Soft, "soft" }, { Saturator::Mode::Asym, "asym" }, { Saturator::Mode::Fold, "fold" } })
        for (double driveDb : { 12.0, 36.0 })
            for (double f : { 987.77, 2793.83 })
            {
                const float g = float(std::pow(10.0, driveDb / 20.0));
                Saturator a, b;
                char nm[96]; std::snprintf(nm, sizeof nm, "saturator %s, drive %2.0f dB, %.0f Hz (ADAA)", name, driveDb, f);
                row(nm, measure(f, sine(f, 0.5f), [&](float x) { return a.process(x * g, mode) / g; }, [&](float x) { return b.process(x * g, mode) / g; }));
            }

    // Filter drive (saturator inside the filter input), LP24 8 kHz, resonance 0.7
    {
        MultiFilter a, b;
        a.set(kSr, 8000.0f, 0.7f, MultiFilter::Mode::LP24); a.setDrive(16.0f);
        b.set(kSr * gOs, 8000.0f, 0.7f, MultiFilter::Mode::LP24); b.setDrive(16.0f);
        row("filter LP24 8 kHz reso 0.7, drive 24 dB, 2.5 kHz", measure(2500.0, sine(2500.0, 0.5f), [&](float x) { return a.process(x); }, [&](float x) { return b.process(x); }));
    }

    // Delay loop saturation (tanh, no ADAA), digital, drive 100 %
    {
        auto delayProc = [](double sr) { auto d = std::make_shared<Delay>(); d->prepare(sr, 1);
            Delay::Params p; p.on = true; p.mode = Delay::Mode::Digital; p.timeMs = 50; p.feedback = 0; p.drive = 1.0f; p.mix = 1.0f; p.lpHz = 20000; p.hpHz = 20; p.modDepth = 0;
            d->setParams(p); d->reset(); return d; };
        auto a = delayProc(kSr), b = delayProc(kSr * gOs);
        row("delay digital, drive 100 %, 3 kHz (loop saturation)", measure(3000.0, sine(3000.0, 0.8f),
            [&](float x) { float l = x, r = x; a->process(&l, &r, 1); return l; }, [&](float x) { float l = x, r = x; b->process(&l, &r, 1); return l; }));
    }
    std::printf("  (reference: < -80 dB inaudible, -60..-80 dB inaudible in music, > -50 dB audible on exposed tones)\n");
    SUCCEED();
}
