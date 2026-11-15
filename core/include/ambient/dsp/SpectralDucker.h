#pragma once
// Frequency Sidechain - intelligent spectral ducking (zero latency).
//
// The INTERNAL signal (AmbientLab's own audio) is attenuated only in frequency
// regions where the external REFERENCE (kick / bass on the sidechain bus)
// genuinely conflicts with it.
//
//   reference ─► per-band band-pass ─► level detector ─┐
//   internal  ─► per-band band-pass ─► level detector ─┼─► conflict ─► gain smoothing ─► dynamic cut y = x - c·BP(x)
//                                                     │
//   reference ─► low-pass ─► fast/slow envelopes ─► transient detector (events, mascot, mod source)
//   reference ─► FFT (hop, Hann, peak-hold) ─► peak finder ─► AUTO band frequencies (glide)
//
// Conflict rule per band (dB):
//   over    = refLevel - (bandThreshold + thresholdOffset)            (kick above threshold?)
//   masking = clamp((intLevel - (refLevel - W)) / W, 0, 1)            (is the internal signal
//             W = 12..36 dB from Sensitivity                           loud enough there to matter?)
//   target  = min(maxReduction, over * slope) * masking * depth        slope = 0.3..2 from Sensitivity
// An overlap where the internal signal is far below the reference is NOT treated
// as a conflict, and nothing happens when the reference is below threshold.
// Gain changes are never faster than half a period of the band frequency
// (minimum effective attack/release), which prevents clicks on low bands.
//
// The FFT never touches the audio path: it only decides band frequencies in
// Auto mode. Audio path = IIR bells only => 0 samples latency.
// Real-time safe: prepare() allocates; process() never allocates or locks.

#include "Fft.h"
#include "Svf.h"
#include "../ModulationSource.h"
#include <array>
#include <atomic>
#include <complex>
#include <vector>

namespace ambient::dsp
{
class SpectralDucker : public mod::ModulationSource
{
public:
    static constexpr int kNumBands = 4;

    struct BandParams
    {
        bool  enabled     = true;
        float freq        = 60.0f;   // Hz (manual frequency; Auto mode overrides)
        float q           = 1.2f;
        float threshold   = -30.0f;  // dBFS of the reference in this band
        float maxReduction = 6.0f;   // dB
        float attackMs    = 5.0f;
        float releaseMs   = 120.0f;
    };

    struct Params
    {
        bool  enabled     = true;
        float sensitivity = 0.5f;    // 0..1
        float thresholdOffset = 0.0f;// dB, added to every band threshold
        float depth       = 0.7f;    // 0..1, scales all reductions
        float mix         = 1.0f;    // 0..1 processed vs dry
        bool  automatic   = true;
        bool  fullBand    = false;   // conventional ducking: broadband gain = max band reduction
        float rangeLow    = 30.0f;   // Auto search range (Hz)
        float rangeHigh   = 250.0f;
        bool  listen      = false;   // output = reference
        bool  delta       = false;   // output = dry - processed (what is removed)
        std::array<BandParams, kNumBands> bands {};
    };

    /** Lock-free telemetry for the UI (written by the audio thread once per block). */
    struct Telemetry
    {
        std::array<std::atomic<float>, kNumBands> bandFreq {};
        std::array<std::atomic<float>, kNumBands> bandReductionDb {}; // >= 0
        std::array<std::atomic<float>, kNumBands> bandRefDb {};
        std::atomic<float> maxReductionDb { 0.0f };
        std::atomic<float> referencePeak { 0.0f };    // linear, post input gain
        std::atomic<bool>  referencePresent { false };
        std::atomic<uint32_t> transientCount { 0 };   // increments per detected transient
        std::atomic<float> lastTransientStrength { 0.0f }; // 0..1
    };

    SpectralDucker();

    /** Allocates. maxBlock = largest block process() will see. */
    void prepare(double sampleRate, int maxBlock);
    void reset();

    void setParams(const Params& p) noexcept { params = p; }
    const Params& getParams() const noexcept { return params; }

    /** In-place processing of the internal signal. refR may equal refL (mono reference),
        refL may be nullptr (no sidechain connected). */
    void process(float* intL, float* intR, const float* refL, const float* refR, int numSamples) noexcept;

    Telemetry& telemetry() noexcept { return tele; }
    const Telemetry& telemetry() const noexcept { return tele; }

    /** Current (smoothed) per-band state, audio thread. */
    float getBandFrequency(int b) const noexcept { return bands[size_t(b)].freq; }
    float getBandReductionDb(int b) const noexcept { return bands[size_t(b)].reductionDb; }
    float getBroadbandReductionDb() const noexcept { return broadbandDb; }

    static constexpr int getLatencySamples() noexcept { return 0; }

    // ModulationSource
    int getNumModOutputs() const noexcept override { return 7; }
    mod::SourceInfo getModOutputInfo(int index) const noexcept override;
    float getModOutput(int index) const noexcept override { return (index >= 0 && index < 7) ? modOut[size_t(index)] : 0.0f; }

private:
    struct Band
    {
        Svf refL, refR, intL, intR;
        Bell bellL, bellR;
        float refEnv = 0.0f, intEnv = 0.0f;
        float freq = 60.0f, autoTarget = 60.0f;
        float reductionDb = 0.0f;
        float appliedDb = -1.0f, appliedFreq = 0.0f, appliedQ = 0.0f, detFreq = 0.0f, detQ = 0.0f; // last coefficients
        bool active = false;
        float detRel = 0.0f; // detector release coefficient (depends on band frequency)
    };

    void updateBandFilters(Band&, const BandParams&, bool force) noexcept;
    void runAutoDetection() noexcept;
    void computeTargets(int subBlock, bool referencePresent) noexcept;

    Params params;
    Telemetry tele;
    double sr = 48000.0;
    std::array<Band, kNumBands> bands;

    // detection
    float detAtt = 0.0f;       // level detector attack coefficient (per sample)
    std::array<float, kNumBands> detRel {};

    // transient detector
    Svf transientLp;
    EnvelopeFollower fastEnv, slowEnv;
    int holdoff = 0, holdoffSamples = 0;
    float transientPulse = 0.0f, transientDecay = 0.0f;

    // FFT auto detection
    Fft fft;
    int fftSize = 0, hop = 0, fftFill = 0, samplesSinceFft = 0;
    std::vector<float> fifo, window, specHold;
    std::vector<std::complex<float>> fftBuf;
    float specRelease = 0.0f;

    // full-band mode + output modes (smoothed weights avoid clicks on switching)
    float broadbandDb = 0.0f, broadbandPrevGain = 1.0f;
    float wEnable = 1.0f, wListen = 0.0f, wDelta = 0.0f, wMix = 1.0f, modeCoeff = 0.0f;
    float autoGlide = 0.0f;

    std::array<float, 7> modOut {};
    float refEnvelope = 0.0f;

    static constexpr int kSubBlock = 16; // control-rate update interval (samples)
};
} // namespace ambient::dsp
