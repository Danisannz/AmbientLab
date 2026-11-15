#pragma once
// Reverb engine (Stage 5): seven algorithms with their own topologies.
//
//   Hall       8-line FDN (Householder), medium-long lines, smooth ER, light modulation
//   Room       8-line FDN, short lines, dense early reflections, fast build-up
//   Cathedral  8-line FDN, very long lines, sparse wide ER, heavy diffusion, slow build
//   Plate      Dattorro figure-eight tank (modulated allpasses), no ER, bright and dense
//   Spring     two dispersive loops (allpass chains -> chirp), band-limited, "drip"
//   Shimmer    Hall FDN with a +12 st pitch shifter feeding the loop
//   Infinite   lossless FDN with a soft limiter in the loop: never decays
//
// Shared: pre-delay, input diffusion, decay (RT60), damping (in-loop low-pass),
// early reflections, modulation, regenerative feedback (wet -> input), freeze, mix.
// FDN loop gains are derived from RT60 per line, so decay times are accurate.
// Real-time safe: prepare() allocates; process() never allocates.

#include "Oscillator.h"
#include "SideHighPass.h"
#include <array>
#include <cassert>
#include <vector>

namespace ambient::dsp
{
/** Fractional delay line with cubic interpolation. */
class DelayLine
{
public:
    void prepare(int maxSamples) { buf.assign(size_t(maxSamples) + 4, 0.0f); size = int(buf.size()); w = 0; }
    void reset() { std::fill(buf.begin(), buf.end(), 0.0f); w = 0; }
    inline void push(float x) noexcept { buf[size_t(w)] = x; if (++w >= size) w = 0; }
    /** Sample written `d` samples ago (1 <= d < size; asserted in debug, clamped always). */
    inline float read(int d) const noexcept
    {
        assert(d >= 1 && d < size);
        d = d < 1 ? 1 : (d >= size ? size - 1 : d);
        int i = w - d; if (i < 0) i += size;
        return buf[size_t(i)];
    }
    inline float readFrac(float d) const noexcept
    {
        const int i = int(d); const float t = d - float(i);
        const float y0 = read(i - 1 < 1 ? 1 : i - 1), y1 = read(i < 1 ? 1 : i), y2 = read(i + 1), y3 = read(i + 2);
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }
    int capacity() const noexcept { return size - 4; }
private:
    std::vector<float> buf;
    int size = 0, w = 0;
};

/** Schroeder allpass on a DelayLine (optionally modulated). */
struct Allpass
{
    DelayLine d;
    void prepare(int maxSamples) { d.prepare(maxSamples); }
    inline float process(float x, float delay, float g) noexcept
    {
        const float v = d.readFrac(delay);
        const float u = x + g * v;
        d.push(u);
        return v - g * u;
    }
};

class Reverb
{
public:
    enum class Algo { Hall = 0, Room, Cathedral, Plate, Spring, Shimmer, Infinite };

    struct Params
    {
        bool  on = false;
        Algo  algo = Algo::Hall;
        float decay = 3.0f;        // RT60 seconds 0.2 .. 30
        float predelayMs = 20.0f;  // 0 .. 500
        float size = 1.0f;         // 0.5 .. 2 (line length scale)
        float diffusion = 0.7f;    // 0..1
        float damping = 0.4f;      // 0..1 (in-loop HF loss)
        float early = 0.5f;        // early reflections level 0..1
        float modulation = 0.3f;   // 0..1
        float feedback = 0.0f;     // regenerative wet -> input 0..0.9
        float shimmer = 0.5f;      // Shimmer algorithm: pitched feedback 0..1
        float mix = 0.3f;          // 0..1
        bool  freeze = false;
        float width = 0.7f;        // 0 (mono wet) .. 1 (maximum: decorrelated, never systematically anti-phase)
    };

    void prepare(double sampleRate, int maxBlock);
    void reset();
    void setParams(const Params& p) noexcept { params = p; }
    void process(float* L, float* R, int n) noexcept;
    static constexpr int getLatencySamples() noexcept { return 0; }

private:
    static constexpr int kLines = 8;
    void configure() noexcept;                       // derive per-algorithm constants
    void fdn(float inL, float inR, float& outL, float& outR) noexcept;
    void plate(float inL, float inR, float& outL, float& outR) noexcept;
    void spring(float inL, float inR, float& outL, float& outR) noexcept;
    float pitchShift(float x) noexcept;              // +12 st, dual-tap crossfade

    Params params;
    Algo configured = Algo::Hall;
    float configuredSize = -1.0f;
    double sr = 48000.0;

    // shared
    DelayLine pre;
    float predelaySmooth = 0.0f;
    std::array<Allpass, 4> diffL, diffR;
    std::array<float, 4> diffLen {};
    DelayLine erL, erR;
    std::array<float, 12> erTimeL {}, erTimeR {}, erGain {};

    // FDN
    std::array<DelayLine, kLines> lines;
    std::array<float, kLines> lineLen {}, lineGain {}, damp {}, lpState {}, lfoPhase {}, lfoRate {};
    float modDepth = 0.0f;
    bool limiterInLoop = false;

    // plate (Dattorro)
    Allpass pIn[4];
    Allpass tankAp1[2], tankAp2[2];
    DelayLine tankD1[2], tankD2[2];
    float tankLp[2] = { 0, 0 }, tankFb[2] = { 0, 0 }, bwState = 0.0f, plateLfo = 0.0f;
    float pScale = 1.0f;

    // spring
    struct Spring
    {
        std::array<float, 16> apState {};
        DelayLine loop;
        float len = 0.0f, lp = 0.0f, hp = 0.0f, hpIn = 0.0f, fb = 0.0f;
    } springs[2];

    // shimmer pitch shifter
    DelayLine shiftBuf;
    float shiftPhase = 0.0f, shimmerState = 0.0f;

    // regeneration, smoothing
    SideHighPass sideHp; float widthSmooth = 0.7f; // wet stereo stage (side crossover, width)
    float regenL = 0.0f, regenR = 0.0f, hpRegenL = 0.0f, hpRegenR = 0.0f;
    float wMix = 0.0f, wOn = 0.0f, wIn = 1.0f, smooth = 0.0f;
};
} // namespace ambient::dsp
