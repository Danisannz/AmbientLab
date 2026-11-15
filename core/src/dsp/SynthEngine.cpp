#include "ambient/dsp/SynthEngine.h"
#include <algorithm>
#include <cmath>

namespace ambient::dsp
{
namespace
{
inline double noteToHz(double note) noexcept { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }
inline float centsToRatio(float c) noexcept { return std::exp2(c / 1200.0f); }

/** Unison position -1..+1 for voice i of n (centre voice at 0 for odd n). */
inline float unisonPos(int i, int n) noexcept { return n <= 1 ? 0.0f : -1.0f + 2.0f * float(i) / float(n - 1); }
}

void SynthEngine::prepare(double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    attackStep = float(1.0 / (0.003 * sr));                 // temporary gate: 3 ms linear attack
    releaseCoeff = float(std::exp(-1.0 / (0.12 * sr)));     // 120 ms exponential release
    uint32_t seed = 0xA51u;
    for (auto& v : voices)
    {
        v.noise = Noise(seed++ * 2654435761u);
        v.drift = Drift(seed++ * 2246822519u);
        v.drift.setTimeConstant(1.8f);
        Rng r(seed++ * 3266489917u);
        for (auto& t : v.a.tolerance) t = r.bi();
        for (auto& t : v.b.tolerance) t = r.bi();
    }
    reset();
}

void SynthEngine::reset()
{
    for (auto& v : voices)
    {
        v.note = -1; v.gain = v.target = 0.0f;
        v.satL.reset(); v.satR.reset(); v.noise.reset(); v.drift.reset();
    }
}

void SynthEngine::startVoice(Voice& v, int note, float velocity) noexcept
{
    const bool wasSilent = ! v.active();
    v.note = note;
    v.velocity = std::clamp(velocity, 0.0f, 1.0f);
    v.target = 1.0f;
    v.age = ++ageCounter;
    if (wasSilent) // a stolen, still-sounding voice keeps its phases: no click
    {
        Rng r(uint32_t(note * 7919 + int(ageCounter)));
        for (int i = 0; i < kMaxUnison; ++i)
        {
            // Unison voices beyond the first start at spread phases (avoid a comb-filter attack).
            const double ra = params.a.phase < 0.0f ? r.uni() : params.a.phase + (i == 0 ? 0.0 : r.uni());
            const double rb = params.b.phase < 0.0f ? r.uni() : params.b.phase + (i == 0 ? 0.0 : r.uni());
            v.a.osc[size_t(i)].reset(ra);
            v.b.osc[size_t(i)].reset(rb);
        }
        v.sub.reset(params.a.phase < 0.0f ? 0.25 : params.a.phase);
        v.satL.reset(); v.satR.reset();
    }
}

void SynthEngine::noteOn(int note, float velocity) noexcept
{
    Voice* chosen = nullptr;
    for (auto& v : voices) if (v.note == note) { chosen = &v; break; }                  // retrigger same note
    if (! chosen) for (auto& v : voices) if (! v.active()) { chosen = &v; break; }       // free voice
    if (! chosen) // steal: prefer released voices, then the oldest
    {
        chosen = &voices[0];
        for (auto& v : voices)
        {
            const bool vRel = v.target <= 0.0f, cRel = chosen->target <= 0.0f;
            if ((vRel && ! cRel) || (vRel == cRel && v.age < chosen->age)) chosen = &v;
        }
    }
    startVoice(*chosen, note, velocity);
}

void SynthEngine::noteOff(int note) noexcept
{
    for (auto& v : voices) if (v.note == note && v.target > 0.0f) { v.target = 0.0f; }
}

void SynthEngine::allNotesOff() noexcept
{
    for (auto& v : voices) v.target = 0.0f;
}

int SynthEngine::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& v : voices) if (v.active()) ++n;
    return n;
}

void SynthEngine::render(float* L, float* R, int start, int end) noexcept
{
    const auto& p = params;
    const int ua = std::clamp(p.a.unison, 1, kMaxUnison), ub = std::clamp(p.b.unison, 1, kMaxUnison);
    const float normA = 1.0f / std::sqrt(float(ua)), normB = 1.0f / std::sqrt(float(ub));
    const auto satMode = Saturator::Mode(std::clamp(p.satMode, 0, 2));
    const float drive = std::pow(10.0f, std::clamp(p.satDrive, 0.0f, 36.0f) / 20.0f);
    const bool satOn = p.satDrive > 0.01f && p.satMix > 0.001f;
    const float driftAmt = std::clamp(p.drift, 0.0f, 1.0f);
    const Wave subWave = p.subWave == 1 ? Wave::Square : (p.subWave == 2 ? Wave::Triangle : Wave::Sine);

    // Stereo pan gains per unison slot (constant power).
    std::array<float, kMaxUnison> panLA {}, panRA {}, panLB {}, panRB {};
    for (int i = 0; i < kMaxUnison; ++i)
    {
        const float pa = unisonPos(i, ua) * p.a.spread, pb = unisonPos(i, ub) * p.b.spread;
        panLA[size_t(i)] = std::cos((pa + 1.0f) * 0.7853982f); panRA[size_t(i)] = std::sin((pa + 1.0f) * 0.7853982f);
        panLB[size_t(i)] = std::cos((pb + 1.0f) * 0.7853982f); panRB[size_t(i)] = std::sin((pb + 1.0f) * 0.7853982f);
    }
    constexpr float kPanNorm = 1.41421356f; // centre pan = unity

    for (auto& v : voices)
    {
        if (! v.active()) continue;
        const float vel = 1.0f - p.velocitySens + p.velocitySens * v.velocity;

        for (int s0 = start; s0 < end; s0 += kControlBlock)
        {
            const int s1 = std::min(end, s0 + kControlBlock);
            // ---- control rate: pitch incl. drift --------------------------------------
            const float driftNow = v.drift.step(float(s1 - s0) / float(sr));
            const float driftCents = driftAmt * 9.0f * driftNow; // up to ~ +-9 cents (1 sigma) at full drift
            const double baseA = double(v.note) + 12.0 * std::clamp(p.a.octave, -3, 3) + std::clamp(p.a.semi, -12, 12) + p.a.fine / 100.0;
            const double baseB = double(v.note) + 12.0 * std::clamp(p.b.octave, -3, 3) + std::clamp(p.b.semi, -12, 12) + p.b.fine / 100.0;
            std::array<double, kMaxUnison> incA {}, incB {};
            for (int i = 0; i < ua; ++i)
            {
                const float c = unisonPos(i, ua) * p.a.detune + driftCents + driftAmt * 4.0f * v.a.tolerance[size_t(i)];
                incA[size_t(i)] = noteToHz(baseA) * centsToRatio(c) / sr;
            }
            for (int i = 0; i < ub; ++i)
            {
                const float c = unisonPos(i, ub) * p.b.detune + driftCents * 0.8f + driftAmt * 4.0f * v.b.tolerance[size_t(i)];
                incB[size_t(i)] = noteToHz(baseB) * centsToRatio(c) / sr;
            }
            const double incSub = noteToHz(baseA + 12.0 * std::clamp(p.subOctave, -2, -1)) * centsToRatio(driftCents) / sr;

            for (int s = s0; s < s1; ++s)
            {
                // ---- temporary amp gate --------------------------------------------------
                if (v.target > 0.0f) v.gain = std::min(1.0f, v.gain + attackStep);
                else { v.gain *= releaseCoeff; if (v.gain < 1.0e-4f) { v.gain = 0.0f; v.note = -1; break; } }

                float l = 0.0f, r = 0.0f;

                // ---- Osc B (also the FM / PM modulator) ----------------------------------
                float bMod = 0.0f;
                if (p.b.on || p.fm > 0.0f || p.pm > 0.0f)
                    for (int i = 0; i < ub; ++i)
                    {
                        const float y = v.b.osc[size_t(i)].next(incB[size_t(i)], p.b.wave, p.b.pw);
                        if (i == 0) bMod = y;
                        if (p.b.on)
                        {
                            const float g = y * p.b.level * normB * kPanNorm;
                            l += g * panLB[size_t(i)]; r += g * panRB[size_t(i)];
                        }
                    }

                // ---- Osc A with B -> A FM (through-zero, index up to ~4) and PM ----------
                if (p.a.on)
                {
                    const double fmFactor = 1.0 + 4.0 * double(p.fm) * double(bMod);
                    const double pmCycles = 0.5 * double(p.pm) * double(bMod);
                    for (int i = 0; i < ua; ++i)
                    {
                        const float y = v.a.osc[size_t(i)].next(incA[size_t(i)] * fmFactor, p.a.wave, p.a.pw, pmCycles);
                        const float g = y * p.a.level * normA * kPanNorm;
                        l += g * panLA[size_t(i)]; r += g * panRA[size_t(i)];
                    }
                }

                // ---- Sub + noise (centre) ------------------------------------------------
                float c = 0.0f;
                if (p.subOn) c += v.sub.next(incSub, subWave, 0.5f) * p.subLevel;
                else v.sub.next(incSub, subWave, 0.5f); // keep phase coherent
                if (p.noiseOn) c += (p.noiseType == 0 ? v.noise.white() : v.noise.pink()) * p.noiseLevel;
                l += c; r += c;

                // ---- saturation / waveshaping (ADAA) -------------------------------------
                if (satOn)
                {
                    const float wl = v.satL.process(l * drive, satMode), wr = v.satR.process(r * drive, satMode);
                    l += p.satMix * (wl - l);
                    r += p.satMix * (wr - r);
                }

                const float amp = v.gain * vel * 0.35f; // headroom for 8 voices
                L[s] += l * amp;
                R[s] += r * amp;
            }
            if (! v.active()) break;
        }
    }
}
} // namespace ambient::dsp
