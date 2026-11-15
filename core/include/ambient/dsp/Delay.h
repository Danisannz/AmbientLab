#pragma once
// Delay engine (Stage 6): eight characters sharing one stereo buffer pair.
//
//   Tape        smoothed time (varispeed glide), wow + flutter, tape saturation, head-bump-ish LP
//   BBD         clock-darkened loop (LP falls with delay time), compander-like soft clip, chorus mod
//   Digital     clean, sample-accurate, optional modulation
//   Ping-pong   mono-summed input into L, feedback crosses L <-> R
//   Multi-tap   4 taps (1/4, 2/4, 3/4, 1 x time) alternately panned, feedback from the last tap
//   Reverse     time-reversed segments (two overlapping windowed heads), reversed repeats
//   Pitch       pitch shifter in the loop: every repeat moves by Pitch semitones
//   Granular    the delay line is read as grains (scatter, pitch jitter, random pan)
//
// Shared: time (ms, or host-synced by the caller), feedback (soft-limited loop),
// HP/LP in the loop, modulation depth/rate, drive (saturation), stereo width, mix.
// Real-time safe: prepare() allocates; process() never allocates. Zero latency.
#include "Oscillator.h"
#include "Reverb.h"   // DelayLine
#include <algorithm>
#include <array>

namespace ambient::dsp
{
class Delay
{
public:
    enum class Mode { Tape = 0, BBD, Digital, PingPong, MultiTap, Reverse, Pitch, Granular };
    static constexpr int kNumModes = 8;
    static constexpr float kMaxTimeMs = 4000.0f;

    struct Params
    {
        bool  on = false;
        Mode  mode = Mode::Tape;
        float timeMs = 375.0f;    // 1 .. 4000 (the caller converts synced divisions to ms)
        float feedback = 0.45f;   // 0 .. 1 (loop is soft-limited; 1 = near-infinite)
        float hpHz = 80.0f;       // loop high-pass 20 .. 2000
        float lpHz = 9000.0f;     // loop low-pass 500 .. 20000
        float modDepth = 0.2f;    // 0..1
        float modRate = 0.6f;     // Hz 0.05 .. 10
        float drive = 0.2f;       // 0..1 saturation
        float width = 0.7f;       // 0 (mono) .. 1
        float pitch = 12.0f;      // Pitch mode: semitones per repeat -24 .. 24
        float mix = 0.3f;         // 0..1
    };

    /** Musical divisions for host sync (beats per division). */
    static constexpr int kNumDivisions = 15;
    static float divisionBeats(int index) noexcept;
    static const char* divisionName(int index) noexcept;
    static float syncedMs(int division, double bpm) noexcept { return float(60000.0 / std::max(20.0, bpm) * divisionBeats(division)); }

    void prepare(double sampleRate, int maxBlock);
    void reset();
    void setParams(const Params& p) noexcept { params = p; }
    void process(float* L, float* R, int n) noexcept;
    static constexpr int getLatencySamples() noexcept { return 0; }

private:
    struct OnePole { float z = 0.0f; inline float lp(float x, float c) noexcept { z += c * (x - z); return z; } };
    struct Shifter
    {
        DelayLine buf; float phase = 0.0f;
        void prepare(int n) { buf.prepare(n); }
        void reset() { buf.reset(); phase = 0.0f; }
        float process(float x, float ratio, float window) noexcept;
    };
    struct Grain { bool active = false; float pos = 0.0f, rate = 1.0f, age = 0.0f, len = 1.0f, gl = 1.0f, gr = 1.0f; };

    float sat(float x, float drive) const noexcept;
    float readHead(int ch, float delaySamples) const noexcept { return lines[size_t(ch)].readFrac(std::clamp(delaySamples, 2.0f, maxDelay)); }

    Params params;
    double sr = 48000.0;
    float maxDelay = 1.0f;
    std::array<DelayLine, 2> lines;
    std::array<OnePole, 2> hpS, lpS, lpS2, dcS;
    std::array<Shifter, 2> shifters;
    std::array<Grain, 16> grains;
    Rng rng { 0x5eedu };
    float timeSmooth = 0.0f;          // samples (tape/BBD/reverse/granular glide)
    float dA = 1.0f, dB = 1.0f, xf = 0.0f; bool fading = false; // digital modes: two-head crossfade on time change
    float readX(int ch, float frac, float mod) const noexcept { return (1.0f - xf) * readHead(ch, dA * frac + mod) + xf * readHead(ch, dB * frac + mod); }
    float lfo = 0.0f, wow = 0.0f, flutter = 0.0f, flutterN = 0.0f;
    float revPhase = 0.0f;
    float grainClock = 0.0f;
    float wOn = 0.0f, wMix = 0.0f, smooth = 0.001f;
    std::array<float, 2> fbState {};  // last loop outputs (for cross / regen)
};
} // namespace ambient::dsp
