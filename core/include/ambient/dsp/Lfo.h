#pragma once
// LFO (Stage 7): 14 waveforms, free or host-synced (phase-locked to the host ppq while
// playing), phase offset, shape, output smoothing, unipolar, note retrigger, 8-point custom curve.
// Control-rate: advance() is called once per control sub-block (32 samples).
#include <array>
#include <cstdint>

namespace ambient::dsp
{
class Lfo
{
public:
    enum class Wave { Sine = 0, Triangle, SawUp, SawDown, Square, Pulse, Exponential, Logarithmic,
                      SmoothRandom, SteppedRandom, SampleHold, SlewedSampleHold, Chaotic, Custom };
    static constexpr int kNumWaves = 14;
    static const char* waveName(int w) noexcept;

    struct Params
    {
        Wave  wave = Wave::Sine;
        float rateHz = 1.0f;        // 0.01 .. 40 (free)
        bool  sync = false;
        int   division = 9;         // Delay::divisionBeats index (1/4)
        float phase = 0.0f;         // 0..1 start / offset
        float shape = 0.5f;         // pulse width, curve amount, step count, slew time, chaos
        float smooth = 0.0f;        // 0..1 output slew
        bool  unipolar = false;
        bool  retrigger = false;
        std::array<float, 8> custom { 0.0f, 0.7f, 1.0f, 0.4f, -0.2f, -0.8f, -1.0f, -0.4f };
    };

    struct Transport { bool playing = false; double ppq = 0.0; double bpm = 120.0; };

    void prepare(double sampleRate) noexcept { sr = sampleRate > 0 ? sampleRate : 48000.0; reset(); }
    void reset() noexcept;
    void setParams(const Params& p) noexcept { params = p; }
    void retrigger() noexcept;

    /** Advance by n samples; returns the output (bipolar -1..1 or unipolar 0..1). */
    float advance(int n, const Transport& t) noexcept;
    float value() const noexcept { return out; }
    float currentPhase() const noexcept { return phase; }

    /** Deterministic waveform shape (non-random waves) for UI drawing, bipolar. */
    static float shapeAt(const Params& p, float phase) noexcept;

private:
    float randomTarget() noexcept;
    Params params;
    double sr = 48000.0;
    double phase = 0.0;
    float out = 0.0f, raw = 0.0f, slewSH = 0.0f;
    float rPrev = 0.0f, rNext = 0.0f, rHold = 0.0f, chaosX = 0.4f, chaosPrev = 0.0f, chaosNext = 0.0f;
    int chaosSub = 0;
    uint32_t seed = 0x9e3779b9u;
    bool first = true;
};

/** Modulation matrix core: slots map a source value to a normalised offset on a destination. */
class ModMatrix
{
public:
    static constexpr int kSlots = 32;
    struct Slot { int source = 0; int destination = 0; float amount = 0.0f; };   // 0 = none
    /** offsets[d] += amount * sources[s] for every active slot; offsets sized numDest + 1. */
    static void accumulate(const std::array<Slot, kSlots>& slots, const float* sources, int numSources,
                           float* offsets, int numDest) noexcept
    {
        for (int d = 0; d <= numDest; ++d) offsets[d] = 0.0f;
        for (const auto& s : slots)
            if (s.source > 0 && s.source < numSources && s.destination > 0 && s.destination <= numDest && s.amount != 0.0f)
                offsets[s.destination] += s.amount * sources[s.source];
    }
};
} // namespace ambient::dsp
