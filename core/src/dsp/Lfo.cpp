#include <ambient/dsp/Lfo.h>
#include <ambient/dsp/Delay.h>
#include <algorithm>
#include <cmath>

namespace ambient::dsp
{
namespace
{
constexpr float kTwoPi = 6.28318530718f;
const char* const kNames[Lfo::kNumWaves] = { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Pulse", "Exponential",
    "Logarithmic", "Smooth Random", "Stepped Random", "Sample & Hold", "Slewed S&H", "Chaotic", "Custom" };
}

const char* Lfo::waveName(int w) noexcept { return kNames[std::clamp(w, 0, kNumWaves - 1)]; }

void Lfo::reset() noexcept
{
    phase = std::fmod(double(std::clamp(params.phase, 0.0f, 1.0f)), 1.0);
    out = raw = slewSH = 0.0f;
    seed = 0x9e3779b9u;
    rPrev = randomTarget(); rNext = randomTarget(); rHold = rPrev;
    chaosX = 0.4f; chaosPrev = chaosNext = 0.0f; chaosSub = 0;
    first = true;
}

void Lfo::retrigger() noexcept
{
    if (! params.retrigger) return;
    phase = std::clamp(params.phase, 0.0f, 1.0f);
    rPrev = rNext; rNext = randomTarget(); rHold = randomTarget();
}

float Lfo::randomTarget() noexcept
{
    seed = seed * 1664525u + 1013904223u;
    return float(int32_t(seed >> 8) - (1 << 23)) / float(1 << 23);
}

float Lfo::shapeAt(const Params& p, float ph) noexcept
{
    ph -= std::floor(ph);
    const float s = std::clamp(p.shape, 0.0f, 1.0f);
    switch (p.wave)
    {
        case Wave::Sine:     return std::sin(kTwoPi * ph);
        case Wave::Triangle: return ph < 0.25f ? 4.0f * ph : (ph < 0.75f ? 2.0f - 4.0f * ph : 4.0f * ph - 4.0f);
        case Wave::SawUp:    return 2.0f * ph - 1.0f;
        case Wave::SawDown:  return 1.0f - 2.0f * ph;
        case Wave::Square:   return ph < 0.5f ? 1.0f : -1.0f;
        case Wave::Pulse:    return ph < std::clamp(s, 0.02f, 0.98f) ? 1.0f : -1.0f;
        case Wave::Exponential: { const float k = 1.0f + 7.0f * s; return 2.0f * (std::exp(k * ph) - 1.0f) / (std::exp(k) - 1.0f) - 1.0f; }
        case Wave::Logarithmic: { const float k = 1.0f + 7.0f * s; return 2.0f * (1.0f - std::exp(-k * ph)) / (1.0f - std::exp(-k)) - 1.0f; }
        case Wave::Custom:
        {
            const float x = ph * 8.0f;
            const int i = std::min(7, int(x));
            float f = x - float(i);
            f = f + s * ((0.5f - 0.5f * std::cos(3.14159265f * f)) - f);      // shape 0 = linear .. 1 = smooth
            return std::clamp(p.custom[size_t(i)] + f * (p.custom[size_t((i + 1) & 7)] - p.custom[size_t(i)]), -1.0f, 1.0f);
        }
        default: return 0.0f;   // random waves are stateful
    }
}

float Lfo::advance(int n, const Transport& t) noexcept
{
    const auto& p = params;
    const double dt = double(n) / sr;
    double newPhase;
    double cycles;   // cycles per second (for slew scaling)
    if (p.sync)
    {
        const double beats = std::max(1.0e-3, double(Delay::divisionBeats(p.division)));
        cycles = std::max(20.0, t.bpm) / 60.0 / beats;
        if (t.playing) newPhase = std::fmod(t.ppq / beats + double(p.phase) + 64.0, 1.0);   // phase-locked to the host
        else newPhase = phase + cycles * dt;
    }
    else
    {
        cycles = std::clamp(double(p.rateHz), 0.01, 40.0);
        newPhase = phase + cycles * dt;
    }
    const bool wrapped = newPhase >= 1.0 || newPhase < phase;
    newPhase -= std::floor(newPhase);
    phase = newPhase;
    const float ph = float(phase);
    const float s = std::clamp(p.shape, 0.0f, 1.0f);

    if (wrapped || first)
    {
        rPrev = rNext; rNext = randomTarget(); rHold = randomTarget();
        first = false;
    }
    float v;
    switch (p.wave)
    {
        case Wave::SmoothRandom: v = rPrev + (rNext - rPrev) * (0.5f - 0.5f * std::cos(3.14159265f * ph)); break;
        case Wave::SteppedRandom:
        {
            const float steps = 2.0f + std::round(s * 14.0f);   // 2..16 levels
            v = std::round((rHold * 0.5f + 0.5f) * (steps - 1.0f)) / (steps - 1.0f) * 2.0f - 1.0f;
            break;
        }
        case Wave::SampleHold: v = rHold; break;
        case Wave::SlewedSampleHold:
        {
            const double tau = (0.02 + 0.98 * s) / cycles * 0.5;              // up to half a cycle
            const float c = float(1.0 - std::exp(-dt / std::max(1.0e-4, tau)));
            slewSH += c * (rHold - slewSH);
            v = slewSH;
            break;
        }
        case Wave::Chaotic:
        {
            // logistic map, 16 iterations per cycle, cosine-interpolated (bounded, aperiodic)
            const int sub = int(ph * 16.0f);
            if (sub != chaosSub || wrapped)
            {
                chaosSub = sub;
                const float r = 3.6f + 0.4f * s;
                chaosX = std::clamp(r * chaosX * (1.0f - chaosX), 1.0e-4f, 0.9999f);
                chaosPrev = chaosNext; chaosNext = chaosX * 2.0f - 1.0f;
            }
            const float f = ph * 16.0f - float(sub);
            v = chaosPrev + (chaosNext - chaosPrev) * (0.5f - 0.5f * std::cos(3.14159265f * f));
            break;
        }
        default: v = shapeAt(p, ph); break;
    }
    raw = v;
    // output slew (0 = none, 1 = ~200 ms)
    const float sm = std::clamp(p.smooth, 0.0f, 1.0f);
    if (sm <= 0.0f) out = v;
    else out += float(1.0 - std::exp(-dt / (0.2 * double(sm) * double(sm) + 1.0e-4))) * (v - out);
    return p.unipolar ? 0.5f * out + 0.5f : out;
}
} // namespace ambient::dsp
