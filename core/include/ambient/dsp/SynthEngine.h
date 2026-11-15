#pragma once
// Polyphonic oscillator engine (Stage 2).
//
// Per voice:  Osc A (unison 1..7) + Osc B (unison 1..7) + Sub + Noise
//             B -> A through-zero linear FM and phase modulation
//             analog drift (Ornstein-Uhlenbeck pitch walk + per-unison component offsets)
//             mix -> saturation / waveshaping (ADAA) -> multimode filter (drive) -> amp ADSR
//             mod ADSR -> filter cutoff (+-6 oct) and pitch (+-24 st); keytracking, velocity
// Real-time safe: no allocation after prepare(); all state is fixed-size.

#include "Envelope.h"
#include "Filter.h"
#include "Oscillator.h"
#include <array>

namespace ambient::dsp
{
class SynthEngine
{
public:
    static constexpr int kMaxVoices = 8;
    static constexpr int kMaxUnison = 7;

    struct OscParams
    {
        bool  on = true;
        Wave  wave = Wave::Saw;
        int   octave = 0;        // -3..+3
        int   semi = 0;          // -12..+12
        float fine = 0.0f;       // cents -100..+100
        float pw = 0.5f;         // pulse width
        float phase = 0.0f;      // start phase 0..1 (on note-on); < 0 = free running
        float level = 0.7f;      // 0..1
        int   unison = 1;        // 1..7
        float detune = 15.0f;    // cents spread (outer voices)
        float spread = 0.5f;     // stereo 0..1
    };

    struct Params
    {
        OscParams a, b;
        float fm = 0.0f;         // B -> A linear FM depth 0..1
        float pm = 0.0f;         // B -> A phase modulation depth 0..1
        bool  subOn = true;
        int   subWave = 0;       // 0 sine, 1 square, 2 triangle
        int   subOctave = -1;    // -1 or -2 relative to Osc A
        float subLevel = 0.3f;
        bool  noiseOn = false;
        int   noiseType = 1;     // 0 white, 1 pink
        float noiseLevel = 0.1f;
        float drift = 0.25f;     // 0..1 analog instability
        int   satMode = 0;       // Saturator::Mode
        float satDrive = 0.0f;   // dB 0..24
        float satMix = 1.0f;     // 0..1
        float velocitySens = 0.6f;

        // Stage 3: filter + envelopes
        bool  filterOn = true;
        int   filterMode = 0;      // MultiFilter::Mode
        float cutoff = 8000.0f;    // Hz
        float resonance = 0.2f;    // 0..1
        float filterDrive = 0.0f;  // dB 0..24
        float filterEnv = 0.0f;    // -1..1 -> +-6 octaves from the mod envelope
        float keytrack = 0.5f;     // 0..1 (1 = cutoff follows pitch 1:1, ref. C4)
        float velToCutoff = 0.0f;  // 0..1 -> up to +2 octaves at full velocity
        Adsr::Params ampEnv { 5.0f, 300.0f, 0.8f, 400.0f };
        Adsr::Params modEnv { 10.0f, 600.0f, 0.0f, 600.0f };
        float modEnvPitch = 0.0f;  // semitones -24..+24
    };

    void prepare(double sampleRate, int maxBlock);
    /** Live change of the internal processing rate (High Quality oversampling): no allocation,
        voices keep sounding; every rate-dependent coefficient is recomputed per control block. */
    void setInternalRate(double sampleRate) noexcept;
    double getInternalRate() const noexcept { return sr; }
    void reset();
    void setParams(const Params& p) noexcept { params = p; }
    const Params& getParams() const noexcept { return params; }

    void noteOn(int note, float velocity) noexcept;
    void noteOff(int note) noexcept;
    void allNotesOff() noexcept;

    /** ADDS stereo output into L/R over [start, end). */
    void render(float* L, float* R, int start, int end) noexcept;

    int activeVoices() const noexcept;

private:
    struct OscState
    {
        std::array<BlepOsc, kMaxUnison> osc;
        std::array<float, kMaxUnison> tolerance {}; // static per-voice offsets (cents) - "component tolerance"
    };

    struct Voice
    {
        int note = -1;
        float velocity = 0.0f;
        bool held = false;           // key down (vs. releasing)
        Adsr amp, mod;
        MultiFilter filterL, filterR;
        unsigned age = 0;
        OscState a, b;
        BlepOsc sub;
        Noise noise { 1u };
        Drift drift { 1u };
        float driftCents = 0.0f;
        Saturator satL, satR;
        bool active() const noexcept { return amp.isActive(); }
    };

    void startVoice(Voice&, int note, float velocity) noexcept;

    Params params;
    std::array<Voice, kMaxVoices> voices;
    double sr = 48000.0;
    unsigned ageCounter = 0;
    static constexpr int kControlBlock = 32;
    static constexpr int kFilterBlock = 8; // filter coefficient update interval
};
} // namespace ambient::dsp
