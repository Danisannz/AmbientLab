#pragma once
// ADSR envelope (analog-style RC segments), allocation-free.
//   Attack: exponential charge towards 1.3 (overshoot target), reaches 1.0 at exactly
//           the attack time, so it sounds linear-ish but has an analog shoulder.
//   Decay / release: exponential, time = time to fall by 60 dB of the remaining distance.
//   Retrigger starts from the CURRENT level (legato-safe, no click).
//   Minimum attack 0.5 ms and release 1 ms keep every transition click-free.

#include <cmath>

namespace ambient::dsp
{
class Adsr
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    struct Params { float attackMs = 5.0f, decayMs = 300.0f, sustain = 0.7f, releaseMs = 400.0f; };

    void setSampleRate(double sr) noexcept { fs = sr > 0.0 ? sr : 48000.0; }

    void setParams(const Params& p) noexcept
    {
        constexpr double overshoot = 1.3;
        const double a = std::fmax(0.5, double(p.attackMs)) * 0.001 * fs;
        attackCoef = float(std::exp(-std::log(overshoot / (overshoot - 1.0)) / a));
        attackTarget = float(overshoot);
        decayCoef = float(std::exp(-6.907755 / (std::fmax(1.0, double(p.decayMs)) * 0.001 * fs)));
        releaseCoef = float(std::exp(-6.907755 / (std::fmax(1.0, double(p.releaseMs)) * 0.001 * fs)));
        sustain = std::fmin(1.0f, std::fmax(0.0f, p.sustain));
    }

    void noteOn() noexcept { stage = Stage::Attack; }
    void noteOff() noexcept { if (stage != Stage::Idle) stage = Stage::Release; }
    void reset() noexcept { stage = Stage::Idle; level = 0.0f; }

    inline float next() noexcept
    {
        switch (stage)
        {
            case Stage::Idle: return 0.0f;
            case Stage::Attack:
                level = attackTarget + attackCoef * (level - attackTarget);
                if (level >= 1.0f) { level = 1.0f; stage = Stage::Decay; }
                break;
            case Stage::Decay:
                level = sustain + decayCoef * (level - sustain);
                if (level - sustain < 1.0e-4f) { level = sustain; stage = Stage::Sustain; }
                break;
            case Stage::Sustain:
                level = sustain;
                break;
            case Stage::Release:
                level *= releaseCoef;
                if (level < 1.0e-5f) { level = 0.0f; stage = Stage::Idle; }
                break;
        }
        return level;
    }

    Stage getStage() const noexcept { return stage; }
    float getLevel() const noexcept { return level; }
    bool isActive() const noexcept { return stage != Stage::Idle; }

private:
    double fs = 48000.0;
    Stage stage = Stage::Idle;
    float level = 0.0f, sustain = 0.7f;
    float attackCoef = 0.0f, attackTarget = 1.3f, decayCoef = 0.0f, releaseCoef = 0.0f;
};
} // namespace ambient::dsp
