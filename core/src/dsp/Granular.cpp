#include "ambient/dsp/Granular.h"
#include <algorithm>
#include <cmath>

namespace ambient::dsp
{
void Granular::prepare(double sampleRate, int, double bufferSeconds)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    bufLen = std::max(1024, int(sr * bufferSeconds));
    bufL.assign(size_t(bufLen) + 4, 0.0f);
    bufR.assign(size_t(bufLen) + 4, 0.0f);
    mixCoef = 1.0f - float(std::exp(-1.0 / (0.02 * sr))); // 20 ms mix / on smoothing

    constexpr double pi = 3.14159265358979323846;
    for (int i = 0; i <= kWindowSize; ++i)
    {
        const double x = double(i) / kWindowSize; // 0..1
        windows[0][size_t(i)] = float(0.5 - 0.5 * std::cos(2.0 * pi * x));                       // Hann
        windows[1][size_t(i)] = float(std::exp(-0.5 * std::pow((x - 0.5) / 0.15, 2.0)));         // Gauss
        const double a = 0.25;                                                                     // Tukey 25 %
        windows[2][size_t(i)] = float(x < a ? 0.5 - 0.5 * std::cos(pi * x / a)
                                    : (x > 1.0 - a ? 0.5 - 0.5 * std::cos(pi * (1.0 - x) / a) : 1.0));
        windows[3][size_t(i)] = float(1.0 - std::fabs(2.0 * x - 1.0));                           // Triangle
        windows[4][size_t(i)] = float(std::min(1.0, x / 0.02) * std::exp(-4.0 * x) * (1.0 - std::pow(x, 8.0))); // Decay (percussive)
    }
    reset();
}

void Granular::reset()
{
    std::fill(bufL.begin(), bufL.end(), 0.0f);
    std::fill(bufR.begin(), bufR.end(), 0.0f);
    writePos = 0; written = 0;
    for (auto& g : grains) g.active = false;
    nextGrainIn = 0.0; fbL = fbR = 0.0f;
    wMix = params.on ? params.mix : 0.0f;
    wOn = params.on ? 1.0f : 0.0f;
    grainsStarted = 0;
}

int Granular::getActiveGrains() const noexcept
{
    int n = 0;
    for (const auto& g : grains) if (g.active) ++n;
    return n;
}

float Granular::windowAt(int w, float phase) const noexcept
{
    const float x = std::clamp(phase, 0.0f, 1.0f) * float(kWindowSize);
    const int i = std::min(kWindowSize - 1, int(x));
    const float f = x - float(i);
    const auto& t = windows[size_t(w)];
    return t[size_t(i)] + f * (t[size_t(i) + 1] - t[size_t(i)]);
}

float Granular::readBuffer(const std::vector<float>& b, double pos) const noexcept
{
    // 4-point cubic Hermite, wrapped.
    double p = std::fmod(pos, double(bufLen));
    if (p < 0.0) p += bufLen;
    const int i1 = int(p);
    const float t = float(p - i1);
    const int i0 = (i1 - 1 + bufLen) % bufLen, i2 = (i1 + 1) % bufLen, i3 = (i1 + 2) % bufLen;
    const float y0 = b[size_t(i0)], y1 = b[size_t(i1)], y2 = b[size_t(i2)], y3 = b[size_t(i3)];
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

void Granular::startGrain() noexcept
{
    Grain* g = nullptr;
    for (auto& c : grains) if (! c.active) { g = &c; break; }
    if (g == nullptr) return; // pool full: skip (bounded CPU)

    const auto& p = params;
    const int len = std::max(16, int(std::clamp(p.sizeMs, 5.0f, 1000.0f) * 0.001 * sr));
    const float semis = std::clamp(p.pitch, -24.0f, 24.0f) + std::clamp(p.pitchRandom, 0.0f, 12.0f) * rng.bi();
    const double rate = std::exp2(double(semis) / 12.0);
    const bool rev = rng.uni() < std::clamp(p.reverse, 0.0f, 1.0f);

    const double history = double(std::min<uint64_t>(written, uint64_t(bufLen))) - 8.0;
    if (history < double(len) * rate + 8.0) return; // not enough recorded audio yet

    // Distance back from the write head where the grain starts reading.
    double back = std::clamp(double(p.positionMs), 0.0, history * 1000.0 / sr) * 0.001 * sr
                + double(std::clamp(p.scatter, 0.0f, 1.0f)) * rng.uni() * 2.0 * sr;
    // Forward grains consume (rate - 1) * len frames relative to the moving write head:
    // start far enough back so they never overtake it (unless frozen, where the head stops).
    const double ahead = p.freeze ? 0.0 : std::max(0.0, (rate - 1.0) * len);
    // Reverse grains read backwards from their start, so they also need len*rate of older history.
    const double lo = ahead + (rev ? double(len) * rate : 4.0);
    const double hi = rev ? history - double(len) * rate : history;
    if (hi < lo) return;
    back = std::clamp(back, lo, hi);

    g->active = true;
    g->pos = double(writePos) - back;
    g->inc = rev ? -rate : rate;
    g->length = len; g->age = 0;
    g->window = int(p.window);
    const float pan = std::clamp(p.spread, 0.0f, 1.0f) * rng.bi();               // -1..1
    g->gainL = std::cos((pan + 1.0f) * 0.7853982f) * 1.41421356f;
    g->gainR = std::sin((pan + 1.0f) * 0.7853982f) * 1.41421356f;
    ++grainsStarted;
}

void Granular::process(float* L, float* R, int n) noexcept
{
    const auto& p = params;
    const float tOn = p.on ? 1.0f : 0.0f, tMix = std::clamp(p.mix, 0.0f, 1.0f);
    if (! p.on && wOn < 1.0e-4f) { wOn = 0.0f; return; } // fully off: dry path untouched, no buffer work

    const double interval = sr / double(std::clamp(p.density, 0.5f, 200.0f));
    const float fb = std::clamp(p.feedback, 0.0f, 0.95f);
    // Overlap-dependent normalisation keeps the wet level roughly constant.
    const float overlap = std::max(1.0f, std::clamp(p.density, 0.5f, 200.0f) * std::clamp(p.sizeMs, 5.0f, 1000.0f) * 0.001f);
    const float norm = 1.0f / std::sqrt(overlap);

    for (int i = 0; i < n; ++i)
    {
        const float dryL = L[i], dryR = R[i];

        // ---- record (input + soft-limited feedback) -------------------------------
        if (! p.freeze)
        {
            bufL[size_t(writePos)] = dryL + std::tanh(fbL * fb);
            bufR[size_t(writePos)] = dryR + std::tanh(fbR * fb);
            writePos = (writePos + 1) % bufLen;
            ++written;
        }

        // ---- schedule ---------------------------------------------------------------
        nextGrainIn -= 1.0;
        if (nextGrainIn <= 0.0 && p.on)
        {
            startGrain();
            nextGrainIn += interval * (0.75 + 0.5 * double(rng.uni())); // +-25 % jitter: no metallic comb
            if (nextGrainIn < 1.0) nextGrainIn = 1.0;
        }

        // ---- render grains -------------------------------------------------------------
        float wl = 0.0f, wr = 0.0f;
        for (auto& g : grains)
        {
            if (! g.active) continue;
            const float w = windowAt(g.window, float(g.age) / float(g.length));
            const float sl = readBuffer(bufL, g.pos), sr_ = readBuffer(bufR, g.pos);
            wl += sl * w * g.gainL;
            wr += sr_ * w * g.gainR;
            g.pos += g.inc;
            if (++g.age >= g.length) g.active = false;
        }
        wl *= norm; wr *= norm;
        fbL = wl; fbR = wr;

        wOn += mixCoef * (tOn - wOn);
        wMix += mixCoef * (tMix - wMix);
        const float m = wOn * wMix;
        L[i] = dryL + m * (wl - dryL);
        R[i] = dryR + m * (wr - dryR);
    }
}
} // namespace ambient::dsp
