#include <ambient/dsp/Delay.h>
#include <algorithm>
#include <cmath>

namespace ambient::dsp
{
namespace
{
constexpr float kPi = 3.14159265358979f;
constexpr float kDivBeats[Delay::kNumDivisions] = { 0.0625f, 0.125f, 1.0f / 6.0f, 0.25f, 0.375f, 1.0f / 3.0f, 0.5f, 0.75f,
                                                    2.0f / 3.0f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 8.0f };
const char* const kDivNames[Delay::kNumDivisions] = { "1/64", "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
                                                      "1/4T", "1/4", "1/4D", "1/2", "1/2D", "1/1", "2/1" };
} // namespace

float Delay::divisionBeats(int i) noexcept { return kDivBeats[std::clamp(i, 0, kNumDivisions - 1)]; }
const char* Delay::divisionName(int i) noexcept { return kDivNames[std::clamp(i, 0, kNumDivisions - 1)]; }

void Delay::prepare(double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    // 4 s + modulation + reverse (2 x segment) + granular scatter headroom
    const int n = int(sr * (2.0 * kMaxTimeMs / 1000.0 + 0.2)) + 16;
    for (auto& l : lines) l.prepare(n);
    maxDelay = float(lines[0].capacity() - 4);
    for (auto& s : shifters) s.prepare(int(sr * 0.1) + 16);
    smooth = 1.0f - float(std::exp(-1.0 / (0.03 * sr)));
    reset();
}

void Delay::reset()
{
    for (auto& l : lines) l.reset();
    for (auto* a : { &hpS, &lpS, &lpS2, &dcS }) for (auto& f : *a) f.z = 0.0f;
    for (auto& s : shifters) s.reset();
    for (auto& g : grains) g.active = false;
    timeSmooth = std::clamp(params.timeMs, 1.0f, kMaxTimeMs) * 0.001f * float(sr);
    dA = dB = timeSmooth; xf = 0.0f; fading = false;
    lfo = wow = flutter = flutterN = revPhase = grainClock = 0.0f;
    fbState = {};
    wOn = params.on ? 1.0f : 0.0f;
    wMix = params.mix;
}

float Delay::sat(float x, float drive) const noexcept
{
    const float k = 1.0f + 6.0f * drive;
    switch (params.mode)
    {
        case Mode::Tape: { const float y = std::tanh(k * x + 0.1f * drive) - std::tanh(0.1f * drive); return y / k * (1.0f + 0.5f * drive); }
        case Mode::BBD:  { const float y = k * x; return (y / (1.0f + std::fabs(y))) / k * (1.0f + drive); }   // softer knee
        default:         return std::tanh(k * x) / k;
    }
}

float Delay::Shifter::process(float x, float ratio, float window) noexcept
{
    buf.push(x);
    phase += (1.0f - ratio) / window;
    phase -= std::floor(phase);
    float y = 0.0f;
    for (int t = 0; t < 2; ++t)
    {
        float ph = phase + 0.5f * float(t);
        if (ph >= 1.0f) ph -= 1.0f;
        const float w = std::sin(kPi * ph);
        y += buf.readFrac(2.0f + ph * window) * w * w;
    }
    return y;
}

void Delay::process(float* L, float* R, int n) noexcept
{
    const auto& p = params;
    const float tOn = p.on ? 1.0f : 0.0f;
    if (! p.on && wOn < 1.0e-4f) { wOn = 0.0f; return; }

    const float fsr = float(sr);
    const float target = std::clamp(p.timeMs, 1.0f, kMaxTimeMs) * 0.001f * fsr;
    // time glide: tape/BBD glide slowly (varispeed), others quickly (no zipper, short glide)
    const bool analog = p.mode == Mode::Tape || p.mode == Mode::BBD;
    const float glide = 1.0f - float(std::exp(-1.0 / ((analog ? 0.25 : 0.05) * sr)));
    const float fb = std::clamp(p.feedback, 0.0f, 1.0f) * 1.02f;   // just past unity at max: limiter holds it
    const float hpC = 1.0f - float(std::exp(-2.0 * kPi * std::clamp(p.hpHz, 20.0f, 2000.0f) / sr));
    float lpHz = std::clamp(p.lpHz, 500.0f, 20000.0f);
    if (p.mode == Mode::BBD) lpHz = std::min(lpHz, std::clamp(2.4e6f / std::max(10.0f, p.timeMs) / 2.0f, 1800.0f, 12000.0f)); // clock-darkening
    if (p.mode == Mode::Tape) lpHz = std::min(lpHz, 11000.0f);
    // LP fully open (>= 19.5 kHz) on a non-analog mode = bypassed: a clean digital loop
    const float lpC = (! analog && lpHz >= 19500.0f) ? 1.0f : 1.0f - float(std::exp(-2.0 * kPi * std::min(lpHz, 0.45f * fsr) / sr));
    const float drive = std::clamp(p.drive, 0.0f, 1.0f);
    const float depth = std::clamp(p.modDepth, 0.0f, 1.0f);
    const float rate = std::clamp(p.modRate, 0.05f, 10.0f);
    const float width = std::clamp(p.width, 0.0f, 1.0f);
    const float tMix = std::clamp(p.mix, 0.0f, 1.0f);
    const float ratio = std::pow(2.0f, std::clamp(p.pitch, -24.0f, 24.0f) / 12.0f);
    const float shiftWin = 0.06f * fsr;
    const float flutterC = 1.0f - float(std::exp(-2.0 * kPi * 8.0 / sr));
    const float dcC = 1.0f - float(std::exp(-2.0 * kPi * 8.0 / sr));
    const float xfStep = 1.0f / (0.04f * fsr);   // 40 ms head crossfade
    const bool crossfadeModes = ! analog && p.mode != Mode::Reverse && p.mode != Mode::Granular;

    for (int i = 0; i < n; ++i)
    {
        const float dryL = L[i], dryR = R[i];
        wOn += smooth * (tOn - wOn);
        wMix += smooth * (tMix - wMix);
        timeSmooth += glide * (target - timeSmooth);
        if (crossfadeModes)
        {
            const float cur = xf < 0.5f ? dA : dB;
            if (! fading && std::fabs(target - cur) > 0.5f) { if (xf < 0.5f) dB = target; else dA = target; fading = true; }
            if (fading)
            {
                const float goal = (std::fabs(target - dB) < std::fabs(target - dA)) ? 1.0f : 0.0f;
                xf += (goal > xf ? xfStep : -xfStep);
                if (xf >= 1.0f) { xf = 1.0f; fading = false; } else if (xf <= 0.0f) { xf = 0.0f; fading = false; }
            }
        }
        else { dA = dB = timeSmooth; }

        // ---- modulation (samples of delay offset) ----
        lfo += rate / fsr; if (lfo >= 1.0f) lfo -= 1.0f;
        float modL = 0.0f, modR = 0.0f;
        switch (p.mode)
        {
            case Mode::Tape:
            {
                wow += 0.45f / fsr; if (wow >= 1.0f) wow -= 1.0f;
                flutterN += flutterC * (rng.bi() - flutterN);
                flutter += 6.3f / fsr; if (flutter >= 1.0f) flutter -= 1.0f;
                const float m = depth * (0.0025f * fsr * std::sin(2 * kPi * wow) + 0.0003f * fsr * std::sin(2 * kPi * flutter)
                                         + 0.0006f * fsr * flutterN) + depth * 0.002f * fsr * std::sin(2 * kPi * lfo);
                modL = modR = m;   // one tape transport: same wow on both sides
                break;
            }
            case Mode::BBD:
                modL = depth * 0.004f * fsr * (1.0f + std::sin(2 * kPi * lfo));
                modR = depth * 0.004f * fsr * (1.0f + std::sin(2 * kPi * (lfo + 0.25f)));
                break;
            default:
                modL = depth * 0.003f * fsr * (1.0f + std::sin(2 * kPi * lfo));
                modR = depth * 0.003f * fsr * (1.0f + std::sin(2 * kPi * (lfo + 0.5f)));
                break;
        }

        // ---- read ----
        const float d = timeSmooth;
        float yl = 0.0f, yr = 0.0f;      // wet output
        float fl = 0.0f, fr = 0.0f;      // loop signal (before filters)
        switch (p.mode)
        {
            case Mode::MultiTap:
            {
                static constexpr float frac[4] = { 0.25f, 0.5f, 0.75f, 1.0f }, gain[4] = { 0.45f, 0.65f, 0.8f, 1.0f };
                for (int t = 0; t < 4; ++t)
                {
                    const float a = readX(0, frac[t], modL), b = readX(1, frac[t], modR);
                    const float pan = (t & 1) ? 1.0f : -1.0f;      // alternate sides
                    yl += gain[t] * (a * (0.5f - 0.5f * pan * width) + b * (0.5f - 0.5f * pan * width)) ;
                    yr += gain[t] * (a * (0.5f + 0.5f * pan * width) + b * (0.5f + 0.5f * pan * width));
                }
                yl *= 0.5f; yr *= 0.5f;
                fl = readX(0, 1.0f, modL); fr = readX(1, 1.0f, modR);
                break;
            }
            case Mode::Reverse:
            {
                // segment length = time; head delay sweeps 0 -> 2*seg at 2 samples/sample => backwards at speed 1
                revPhase += 1.0f / std::max(64.0f, d);
                if (revPhase >= 1.0f) revPhase -= 1.0f;
                for (int h = 0; h < 2; ++h)
                {
                    float ph = revPhase + 0.5f * float(h);
                    if (ph >= 1.0f) ph -= 1.0f;
                    const float w = std::sin(kPi * ph);
                    const float dd = 2.0f + 2.0f * ph * d;
                    yl += readHead(0, dd) * w * w;
                    yr += readHead(1, dd) * w * w;
                }
                fl = yl; fr = yr;
                break;
            }
            case Mode::Granular:
            {
                // spawn grains: ~25 /s, 40..120 ms, start within the last `time` (+-scatter by mod depth)
                grainClock += 25.0f / fsr;
                if (grainClock >= 1.0f)
                {
                    grainClock -= 1.0f;
                    for (auto& g : grains)
                        if (! g.active)
                        {
                            g.active = true; g.age = 0.0f;
                            g.len = (0.04f + 0.08f * rng.uni()) * fsr;
                            g.rate = std::pow(2.0f, (rng.bi() * 0.15f * depth));     // gentle pitch jitter
                            g.pos = std::clamp(d * (1.0f + 0.5f * depth * rng.bi()), g.len * 2.0f + 4.0f, maxDelay - 4.0f);
                            const float pan = width * rng.bi();
                            g.gl = std::cos((pan + 1.0f) * 0.25f * kPi) * 1.41421356f;
                            g.gr = std::sin((pan + 1.0f) * 0.25f * kPi) * 1.41421356f;
                            break;
                        }
                }
                for (auto& g : grains)
                {
                    if (! g.active) continue;
                    const float w = std::sin(kPi * g.age / g.len);
                    const float x = 0.5f * (readHead(0, g.pos) + readHead(1, g.pos)) * w * w;
                    yl += x * g.gl; yr += x * g.gr;
                    g.pos += 1.0f - g.rate;      // delay grows when rate < 1 (slower), shrinks when > 1
                    g.age += 1.0f;
                    if (g.age >= g.len) g.active = false;
                }
                yl *= 0.5f; yr *= 0.5f;
                fl = yl; fr = yr;
                break;
            }
            default:
                if (analog) { fl = readHead(0, d + modL); fr = readHead(1, d + modR); }
                else { fl = readX(0, 1.0f, modL); fr = readX(1, 1.0f, modR); }
                yl = fl; yr = fr;
                break;
        }
        if (p.mode == Mode::Pitch)
        {
            fl = shifters[0].process(fl, ratio, shiftWin);
            fr = shifters[1].process(fr, ratio, shiftWin);
            yl = fl; yr = fr;
        }

        // ---- loop: HP -> LP (2 poles for BBD/tape) -> saturation -> feedback ----
        float lo[2] = { fl, fr };
        for (int c = 0; c < 2; ++c)
        {
            float x = lo[c];
            x -= hpS[size_t(c)].lp(x, hpC);
            x = lpS[size_t(c)].lp(x, lpC);
            if (analog) x = lpS2[size_t(c)].lp(x, lpC);
            x = sat(x, drive);
            x -= dcS[size_t(c)].lp(x, dcC);
            lo[c] = x;     // filtered / saturated echo
        }
        if (p.mode != Mode::MultiTap) { yl = lo[0]; yr = lo[1]; }   // every echo carries the tone / character
        for (int c = 0; c < 2; ++c)
        {
            lo[c] *= fb;
            if (std::fabs(lo[c]) > 1.5f) lo[c] = std::copysign(1.5f + std::tanh(std::fabs(lo[c]) - 1.5f), lo[c]); // safety
        }

        // ---- write ----
        if (p.mode == Mode::PingPong)
        {
            const float in = 0.5f * (dryL + dryR);
            lines[0].push(in + lo[1]);      // cross: R -> L, L -> R
            lines[1].push(lo[0]);
            // width: blend the two sides toward the centre
            const float m = 0.5f * (yl + yr), s = 0.5f * (yl - yr) * width;
            yl = m + s; yr = m - s;
        }
        else
        {
            const float inM = 0.5f * (dryL + dryR), inS = 0.5f * (dryL - dryR);
            lines[0].push(inM + inS + lo[0]);
            lines[1].push(inM - inS + lo[1]);
            if (p.mode != Mode::MultiTap && p.mode != Mode::Granular)
            {
                const float m = 0.5f * (yl + yr), s = 0.5f * (yl - yr) * width;
                yl = m + s; yr = m - s;
            }
        }

        // mix law: dry stays at full level up to 50 %, wet reaches full level at 50 %
        const float dg = 1.0f + wOn * (std::min(1.0f, 2.0f * (1.0f - wMix)) - 1.0f);
        const float wg = wOn * std::min(1.0f, 2.0f * wMix);
        L[i] = dg * dryL + wg * yl;
        R[i] = dg * dryR + wg * yr;
    }
    for (auto* a : { &hpS, &lpS, &lpS2, &dcS }) for (auto& f : *a) if (std::fabs(f.z) < 1e-20f) f.z = 0.0f;
}
} // namespace ambient::dsp
