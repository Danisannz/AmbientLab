#include "ambient/dsp/Reverb.h"
#include <algorithm>
#include <cmath>

namespace ambient::dsp
{
namespace
{
constexpr double kPlateRate = 29761.0; // Dattorro's reference sample rate
constexpr float kTwoPi = 6.2831853f;

struct AlgoSpec
{
    std::array<float, 8> linesMs;
    float erStartMs, erEndMs, erLevel;
    std::array<float, 4> diffMs;
    float modSamples;
};

const AlgoSpec& spec(Reverb::Algo a)
{
    static const AlgoSpec hall { { 29.7f, 37.1f, 41.1f, 43.7f, 53.9f, 59.5f, 67.3f, 79.1f }, 7.0f, 85.0f, 0.8f, { 4.7f, 3.6f, 12.7f, 9.3f }, 6.0f };
    static const AlgoSpec room { { 7.1f, 9.7f, 11.3f, 13.9f, 17.1f, 19.3f, 23.9f, 27.7f }, 2.5f, 32.0f, 1.0f, { 2.1f, 1.7f, 5.3f, 4.1f }, 2.0f };
    static const AlgoSpec cath { { 61.3f, 73.9f, 89.1f, 97.3f, 113.5f, 131.7f, 149.3f, 171.1f }, 18.0f, 150.0f, 0.6f, { 8.9f, 7.1f, 19.7f, 15.3f }, 10.0f };
    static const AlgoSpec inf  { { 41.3f, 53.9f, 67.1f, 79.3f, 97.7f, 109.3f, 127.1f, 149.9f }, 10.0f, 60.0f, 0.3f, { 6.1f, 4.9f, 15.1f, 11.3f }, 8.0f };
    switch (a)
    {
        case Reverb::Algo::Room: return room;
        case Reverb::Algo::Cathedral: return cath;
        case Reverb::Algo::Infinite: return inf;
        default: return hall;
    }
}
} // namespace

void Reverb::prepare(double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    pre.prepare(int(sr * 0.52) + 8);
    for (auto& a : diffL) a.prepare(int(sr * 0.06) + 8);
    for (auto& a : diffR) a.prepare(int(sr * 0.07) + 8);
    erL.prepare(int(sr * 0.32) + 8);
    erR.prepare(int(sr * 0.32) + 8);
    for (auto& l : lines) l.prepare(int(sr * 0.40) + 64);
    // Plate lines: Dattorro lengths (at 29761 Hz) x rate x max size 2, + modulation excursion + margin.
    const auto plateLen = [this](double n) { return int(std::ceil((n + 20.0) * sr / kPlateRate * 2.0)) + 16; };
    for (auto& a : pIn) a.prepare(plateLen(379.0));
    for (int h = 0; h < 2; ++h)
    {
        tankAp1[h].prepare(plateLen(908.0)); tankAp2[h].prepare(plateLen(2656.0));
        tankD1[h].prepare(plateLen(4453.0)); tankD2[h].prepare(plateLen(3720.0));
    }
    for (auto& s : springs) s.loop.prepare(int(sr * 0.13) + 8);
    shiftBuf.prepare(int(sr * 0.12) + 8);
    smooth = 1.0f - float(std::exp(-1.0 / (0.03 * sr)));
    configuredSize = -1.0f;
    reset();
}

void Reverb::reset()
{
    pre.reset();
    for (auto& a : diffL) a.d.reset();
    for (auto& a : diffR) a.d.reset();
    erL.reset(); erR.reset();
    for (auto& l : lines) l.reset();
    lpState.fill(0.0f);
    for (auto& a : pIn) a.d.reset();
    for (int h = 0; h < 2; ++h) { tankAp1[h].d.reset(); tankAp2[h].d.reset(); tankD1[h].reset(); tankD2[h].reset(); tankLp[h] = tankFb[h] = 0.0f; }
    bwState = 0.0f;
    for (auto& s : springs) { s.apState.fill(0.0f); s.loop.reset(); s.lp = s.hp = s.hpIn = s.fb = 0.0f; }
    shiftBuf.reset(); shiftPhase = 0.0f; shimmerState = 0.0f;
    regenL = regenR = hpRegenL = hpRegenR = 0.0f;
    wOn = params.on ? 1.0f : 0.0f; wMix = params.mix; wIn = params.freeze ? 0.0f : 1.0f;
    predelaySmooth = params.predelayMs;
}

void Reverb::configure() noexcept
{
    configured = params.algo;
    configuredSize = params.size;
    const float size = std::clamp(params.size, 0.5f, 2.0f);
    const auto& sp = spec(params.algo == Algo::Shimmer ? Algo::Hall : params.algo);

    for (int i = 0; i < kLines; ++i)
    {
        lineLen[size_t(i)] = std::min(float(lines[0].capacity()) - 40.0f, sp.linesMs[size_t(i)] * size * 0.001f * float(sr));
        lfoRate[size_t(i)] = 0.13f + 0.11f * float(i);                 // 0.13 .. 0.9 Hz, all different
        lfoPhase[size_t(i)] = 0.37f * float(i);
    }
    for (int k = 0; k < 4; ++k) diffLen[size_t(k)] = sp.diffMs[size_t(k)] * size * 0.001f * float(sr);
    modDepth = sp.modSamples * float(sr / 48000.0);
    limiterInLoop = params.algo == Algo::Infinite;

    // Early reflections: deterministic, irregular spacing, decaying gains, L/R different.
    Rng r(1234u + uint32_t(params.algo) * 77u);
    for (int t = 0; t < 12; ++t)
    {
        const float x = (float(t) + 0.5f * r.uni()) / 12.0f;
        const float ms = sp.erStartMs + (sp.erEndMs - sp.erStartMs) * x * x * size;
        erTimeL[size_t(t)] = std::min(float(erL.capacity()) - 4.0f, ms * 0.001f * float(sr));
        erTimeR[size_t(t)] = std::min(float(erR.capacity()) - 4.0f, ms * (1.07f + 0.1f * r.uni()) * 0.001f * float(sr));
        erGain[size_t(t)] = sp.erLevel * (1.0f - 0.7f * x) * ((t % 3 == 1) ? -1.0f : 1.0f);
    }
    pScale = float(sr / kPlateRate) * size;
    springs[0].len = std::min(float(springs[0].loop.capacity()) - 8.0f, 0.033f * size * float(sr));
    springs[1].len = std::min(float(springs[1].loop.capacity()) - 8.0f, 0.041f * size * float(sr));

    // Algorithm change: start the new topology from silence (tail of the old one is cut).
    for (auto& l : lines) l.reset();
    lpState.fill(0.0f);
}

void Reverb::fdn(float inL, float inR, float& outL, float& outR) noexcept
{
    std::array<float, kLines> o {};
    const float d = params.freeze ? 0.0f : std::clamp(params.damping, 0.0f, 1.0f) * 0.75f;
    const float depth = modDepth * std::clamp(params.modulation, 0.0f, 1.0f);
    float sum = 0.0f;
    for (int i = 0; i < kLines; ++i)
    {
        auto& ph = lfoPhase[size_t(i)];
        ph += lfoRate[size_t(i)] / float(sr);
        if (ph >= 1.0f) ph -= 1.0f;
        const float len = lineLen[size_t(i)] + depth * (1.0f + std::sin(kTwoPi * ph));
        float y = lines[size_t(i)].readFrac(len);
        lpState[size_t(i)] = y + d * (lpState[size_t(i)] - y);   // in-loop damping
        o[size_t(i)] = lpState[size_t(i)];
        sum += o[size_t(i)];
    }
    const float h = sum * (2.0f / float(kLines)); // Householder reflection
    for (int i = 0; i < kLines; ++i)
    {
        float fb = (o[size_t(i)] - h) * lineGain[size_t(i)];
        if (limiterInLoop) fb = std::tanh(fb);
        else if (std::fabs(fb) > 2.0f) fb = std::copysign(2.0f + std::tanh(std::fabs(fb) - 2.0f), fb); // safety knee, linear below 2
        lines[size_t(i)].push(fb + ((i & 1) ? inR : inL));
    }
    outL = 0.5f * (o[0] - o[2] + o[4] - o[6]);
    outR = 0.5f * (o[1] - o[3] + o[5] - o[7]);
}

void Reverb::plate(float inL, float inR, float& outL, float& outR) noexcept
{
    const float k = pScale;
    const float diff = std::clamp(params.diffusion, 0.0f, 1.0f);
    // input band-limit + 4 input diffusers
    const float bw = 0.9995f - 0.4f * std::clamp(params.damping, 0.0f, 1.0f) * 0.5f;
    bwState = bw * 0.5f * (inL + inR) + (1.0f - bw) * bwState;
    float x = bwState;
    const float inLen[4] = { 142.0f, 107.0f, 379.0f, 277.0f };
    const float inG[4] = { 0.75f, 0.75f, 0.625f, 0.625f };
    for (int i = 0; i < 4; ++i) x = pIn[i].process(x, inLen[i] * k, inG[i] * diff);

    const float loopSec = float((672.0 + 4453.0 + 1800.0 + 3720.0) / kPlateRate) * std::clamp(params.size, 0.5f, 2.0f);
    const float rt = params.freeze ? 1.0e9f : std::max(0.2f, params.decay);
    const float decay = std::min(params.freeze ? 1.0f : 0.9999f, std::pow(10.0f, -3.0f * loopSec / rt));
    const float damp = params.freeze ? 0.0f : 0.05f + 0.6f * std::clamp(params.damping, 0.0f, 1.0f);

    plateLfo += 1.0f / float(sr);
    if (plateLfo > 1.0f) plateLfo -= 1.0f;
    const float exc = 16.0f * k * std::clamp(params.modulation, 0.0f, 1.0f);
    const float ap1Len[2] = { 672.0f, 908.0f }, d1Len[2] = { 4453.0f, 4217.0f }, ap2Len[2] = { 1800.0f, 2656.0f }, d2Len[2] = { 3720.0f, 3163.0f };
    const float prevFb[2] = { tankFb[1], tankFb[0] }; // figure-eight cross-coupling
    for (int h = 0; h < 2; ++h)
    {
        const float m = exc * std::sin(kTwoPi * (plateLfo + 0.25f * float(h)));
        float t = x + prevFb[h];
        t = tankAp1[h].process(t, ap1Len[h] * k + m, -0.7f * diff);
        tankD1[h].push(t);
        float y = tankD1[h].read(int(d1Len[h] * k));
        tankLp[h] = y + damp * (tankLp[h] - y);
        y = tankLp[h] * decay;
        y = tankAp2[h].process(y, ap2Len[h] * k, 0.5f * diff);
        tankD2[h].push(y);
        const float fb = tankD2[h].read(int(d2Len[h] * k)) * decay;
        tankFb[h] = std::fabs(fb) > 2.0f ? std::copysign(2.0f + std::tanh(std::fabs(fb) - 2.0f), fb) : fb; // safety knee
    }
    auto tap = [&](DelayLine& d, float n) { return d.read(std::max(1, int(n * k))); };
    outL = 0.6f * (tap(tankD1[1], 266) + tap(tankD1[1], 2974) - tap(tankAp2[1].d, 1913) + tap(tankD2[1], 1996)
                   - tap(tankD1[0], 1990) - tap(tankAp2[0].d, 187) - tap(tankD2[0], 1066));
    outR = 0.6f * (tap(tankD1[0], 353) + tap(tankD1[0], 3627) - tap(tankAp2[0].d, 1228) + tap(tankD2[0], 2673)
                   - tap(tankD1[1], 2111) - tap(tankAp2[1].d, 335) - tap(tankD2[1], 121));
}

void Reverb::spring(float inL, float inR, float& outL, float& outR) noexcept
{
    const float rt = params.freeze ? 1.0e9f : std::max(0.2f, params.decay);
    const float in[2] = { inL, inR };
    float out[2] = { 0.0f, 0.0f };
    const float aDisp[2] = { 0.62f, 0.68f };
    const float lpC = 1.0f - float(std::exp(-2.0 * 3.14159265 * 4500.0 / sr));
    const float hpC = float(std::exp(-2.0 * 3.14159265 * 90.0 / sr));
    for (int s = 0; s < 2; ++s)
    {
        auto& sp = springs[s];
        const float g = std::min(params.freeze ? 1.0f : 0.985f, std::pow(10.0f, -3.0f * (sp.len / float(sr)) / rt));
        const float y = sp.loop.readFrac(sp.len);
        // dispersion: 16 first-order allpasses -> low frequencies delayed more = chirp
        float x = in[s] * 0.5f + g * y;
        for (auto& st : sp.apState)
        {
            const float v = aDisp[s] * x + st;
            st = x - aDisp[s] * v;
            x = v;
        }
        sp.lp += lpC * (x - sp.lp);                      // ~4.5 kHz low-pass
        sp.hp = hpC * (sp.hp + sp.lp - sp.hpIn);         // ~90 Hz high-pass
        sp.hpIn = sp.lp;
        sp.loop.push(std::tanh(sp.hp));
        out[s] = y;
    }
    outL = 0.7f * out[0];
    outR = 0.7f * out[1];
}

float Reverb::pitchShift(float x) noexcept
{
    // +12 st: two read taps whose delay shrinks by one sample per sample, crossfaded (sin^2).
    shiftBuf.push(x);
    const float W = 0.05f * float(sr);
    shiftPhase += 1.0f / W;
    if (shiftPhase >= 1.0f) shiftPhase -= 1.0f;
    float y = 0.0f;
    for (int t = 0; t < 2; ++t)
    {
        float ph = shiftPhase + 0.5f * float(t);
        if (ph >= 1.0f) ph -= 1.0f;
        const float d = 2.0f + (1.0f - ph) * W;
        const float w = std::sin(3.14159265f * ph);
        y += shiftBuf.readFrac(d) * w * w;
    }
    return y;
}

void Reverb::process(float* L, float* R, int n) noexcept
{
    const auto& p = params;
    const float tOn = p.on ? 1.0f : 0.0f;
    if (! p.on && wOn < 1.0e-4f) { wOn = 0.0f; return; }
    if (configuredSize < 0.0f || p.algo != configured || std::fabs(p.size - configuredSize) > 1.0e-3f) configure();

    // Per-line loop gains from RT60 (accurate decay); freeze / infinite = lossless (+ limiter).
    const float rt = p.algo == Algo::Infinite ? 600.0f : std::clamp(p.decay, 0.2f, 30.0f);
    for (int i = 0; i < kLines; ++i)
        lineGain[size_t(i)] = p.freeze ? 1.0f : std::pow(10.0f, -3.0f * (lineLen[size_t(i)] / float(sr)) / rt);

    const float tIn = p.freeze ? 0.0f : 1.0f, tMix = std::clamp(p.mix, 0.0f, 1.0f);
    const float fbAmt = std::clamp(p.feedback, 0.0f, 0.9f);
    const float shim = std::clamp(p.shimmer, 0.0f, 1.0f) * 0.6f;
    const float diff = std::clamp(p.diffusion, 0.0f, 1.0f) * 0.75f;
    const float early = std::clamp(p.early, 0.0f, 1.0f);
    const float hpC = float(std::exp(-2.0 * 3.14159265 * 80.0 / sr));

    for (int i = 0; i < n; ++i)
    {
        const float dryL = L[i], dryR = R[i];
        wIn += smooth * (tIn - wIn);
        wOn += smooth * (tOn - wOn);
        wMix += smooth * (tMix - wMix);
        predelaySmooth += smooth * (std::clamp(p.predelayMs, 0.0f, 500.0f) - predelaySmooth);

        // regenerative feedback: DC-blocked, soft-limited wet signal back into the input
        hpRegenL = hpC * hpRegenL + (1.0f - hpC) * regenL;   // slow average (DC estimate)
        hpRegenR = hpC * hpRegenR + (1.0f - hpC) * regenR;
        const float fl = std::tanh((regenL - hpRegenL) * fbAmt), fr = std::tanh((regenR - hpRegenR) * fbAmt);
        const float inMono = 0.5f * (dryL + dryR) * wIn + 0.5f * (fl + fr);
        pre.push(inMono);
        const float x = pre.readFrac(std::max(1.0f, predelaySmooth * 0.001f * float(sr)));

        // early reflections from the pre-delayed signal
        erL.push(x); erR.push(x);
        float el = 0.0f, er = 0.0f;
        for (int t = 0; t < 12; ++t) { el += erL.read(int(erTimeL[size_t(t)])) * erGain[size_t(t)]; er += erR.read(int(erTimeR[size_t(t)])) * erGain[size_t(t)]; }

        float wl = 0.0f, wr = 0.0f;
        switch (p.algo)
        {
            case Algo::Plate: plate(x, x, wl, wr); break;
            case Algo::Spring: spring(x + 0.3f * (dryL - dryR) * wIn, x - 0.3f * (dryL - dryR) * wIn, wl, wr); break;
            default:
            {
                // input diffusion (decorrelated L/R feeds)
                float a = x, b = x;
                for (int k = 0; k < 4; ++k)
                {
                    a = diffL[size_t(k)].process(a, diffLen[size_t(k)], diff);
                    b = diffR[size_t(k)].process(b, diffLen[size_t(k)] * 1.13f, diff);
                }
                if (p.algo == Algo::Shimmer)
                {
                    const float sh = pitchShift(shimmerState) * shim;
                    a += sh; b += sh;
                }
                fdn(a, b, wl, wr);
                wl *= 0.7f; wr *= 0.7f;
                if (p.algo == Algo::Shimmer) shimmerState = std::tanh(0.5f * (wl + wr));
                break;
            }
        }
        if (p.algo != Algo::Plate) { wl += el * early * 0.3f; wr += er * early * 0.3f; }

        regenL = wl; regenR = wr;
        const float m = wOn * wMix;
        L[i] = dryL + m * (wl - dryL);
        R[i] = dryR + m * (wr - dryR);
    }
}
} // namespace ambient::dsp
