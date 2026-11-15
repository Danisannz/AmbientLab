#pragma once
// Real-time granular processor (Stage 4).
//
//   input (stereo) ─► [+ feedback] ─► circular buffer (10 s, fixed at prepare) ◄─ freeze stops writing
//                                         │
//            scheduler (density, jitter) ─► grain pool (64): position back from the write head
//                                         + scatter, size, pitch (+random), reverse probability,
//                                         window shape, random stereo pan ─► sum ─► wet
//   output = dry * (1 - mix) + wet * mix          (dry path untouched: zero latency)
//
// Grains never read ahead of the write head (forward pitch-up grains are started far
// enough back). Feedback is soft-limited (tanh) and capped at 0.95: always stable.
// Real-time safe: prepare() allocates; process() never allocates or locks.

#include "Oscillator.h"
#include <array>
#include <atomic>
#include <vector>

namespace ambient::dsp
{
class Granular
{
public:
    static constexpr int kMaxGrains = 64;
    static constexpr int kWindowSize = 1024;
    enum class Window { Hann = 0, Gauss, Tukey, Triangle, Decay };

    struct Params
    {
        bool  on = false;
        float sizeMs = 120.0f;       // 5 .. 1000
        float density = 12.0f;       // grains per second 0.5 .. 200
        float positionMs = 250.0f;   // distance behind the write head 0 .. buffer length
        float scatter = 0.2f;        // position randomisation 0..1 (of 2 s)
        float pitch = 0.0f;          // semitones -24..+24
        float pitchRandom = 0.0f;    // semitones 0..12 (+-)
        float reverse = 0.0f;        // probability 0..1
        Window window = Window::Hann;
        float spread = 0.5f;         // random stereo pan 0..1
        float feedback = 0.0f;       // 0..0.95
        float mix = 0.5f;            // 0..1
        bool  freeze = false;
    };

    void prepare(double sampleRate, int maxBlock, double bufferSeconds = 10.0);
    void reset();
    void setParams(const Params& p) noexcept { params = p; }
    const Params& getParams() const noexcept { return params; }

    /** In-place stereo processing. */
    void process(float* L, float* R, int numSamples) noexcept;

    int getActiveGrains() const noexcept;
    uint64_t getGrainsStarted() const noexcept { return grainsStarted; }
    double getBufferSeconds() const noexcept { return double(bufLen) / sr; }
    static constexpr int getLatencySamples() noexcept { return 0; }

private:
    struct Grain
    {
        bool active = false;
        double pos = 0.0;       // read position (frames, absolute modulo buffer)
        double inc = 1.0;       // frames per output sample (sign = direction)
        int length = 0, age = 0;
        float gainL = 0.0f, gainR = 0.0f;
        int window = 0;
    };

    void startGrain() noexcept;
    float readBuffer(const std::vector<float>& b, double pos) const noexcept;
    float windowAt(int w, float phase) const noexcept;

    Params params;
    double sr = 48000.0;
    std::vector<float> bufL, bufR;
    int bufLen = 0, writePos = 0;
    uint64_t written = 0;               // frames ever written (valid history = min(written, bufLen))
    std::array<Grain, kMaxGrains> grains {};
    std::array<std::array<float, kWindowSize + 1>, 5> windows {};
    double nextGrainIn = 0.0;           // samples until the next grain
    float fbL = 0.0f, fbR = 0.0f;       // last wet sample for feedback
    float wMix = 0.0f, mixCoef = 0.0f, wOn = 0.0f;
    uint64_t grainsStarted = 0;
    Rng rng { 0x6A11u };
};
} // namespace ambient::dsp
