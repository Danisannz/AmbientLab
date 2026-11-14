#pragma once
// UI-thread hub: pulls audio from the processor's capture ring and runs the
// analysers for whichever views are visible. Never touched by the audio thread.

#include "SpectrumAnalyser.h"
#include <ambient/StereoCaptureRing.h>
#include <ambient/VisualAnalysis.h>
#include <vector>

namespace ambient::viz
{
enum class AnalyzerMode { Scope = 0, Stereo, Spectrum };
enum class ScopeChannels { Left = 0, Right, Overlay, Mono };

/** Spectrum signal selection. Level convention (all modes): dBFS of the analysed
    signal, calibrated so a full-scale sine reads 0 dB.
      Left / Right : that channel alone.
      Stereo       : L and R analysed separately (two calibrated curves) - a signal
                     present in only one channel is NOT reduced.
      Mid          : (L + R) / 2  - identical L/R at full scale reads 0 dB;
                     a signal only in one channel reads -6 dB by definition.
      Side         : (L - R) / 2  - polarity-inverted L/R at full scale reads 0 dB. */
enum class SpectrumChannel { Left = 0, Right, Stereo, Mid, Side };

class VisualizerHub
{
public:
    explicit VisualizerHub(const StereoCaptureRing& ring);

    // ---- configuration (UI thread) -------------------------------------
    void setSampleRate(double sr);
    void setMode(AnalyzerMode m) noexcept { mode = m; }
    AnalyzerMode getMode() const noexcept { return mode; }
    void setFrozen(bool f) noexcept { frozen = f; }
    bool isFrozen() const noexcept { return frozen; }

    /** Scope window length in milliseconds (1 .. 500). */
    void setScopeTimeMs(float ms) noexcept { scopeTimeMs = juce::jlimit(1.0f, 500.0f, ms); }
    float getScopeTimeMs() const noexcept { return scopeTimeMs; }
    void setTrigger(TriggerMode m, float level) noexcept { triggerMode = m; triggerLevel = level; }
    TriggerMode getTriggerMode() const noexcept { return triggerMode; }
    float getTriggerLevel() const noexcept { return triggerLevel; }
    /** Which signal the scope trigger watches. */
    enum class TriggerSource { Mono = 0, Left, Right };
    void setTriggerSource(TriggerSource s) noexcept { triggerSource = s; }

    /** Number of most-recent stereo frames shown by the vectorscope. */
    void setVectorscopeFrames(int n) noexcept { vectorFrames = juce::jlimit(256, 8192, n); }

    SpectrumAnalyser& spectrum() noexcept { return spec; }          // primary (L in Stereo mode)
    SpectrumAnalyser& spectrumRight() noexcept { return specR; }    // R curve (Stereo mode only)

    void setSpectrumChannel(SpectrumChannel c) noexcept { if (c != specChannel) { specChannel = c; specR.resetPeaks(); spec.resetPeaks(); } }
    SpectrumChannel getSpectrumChannel() const noexcept { return specChannel; }
    // Settings applied to both analysers (UI thread; setFftOrder allocates).
    void setFftOrder(int order) { spec.setOrder(order); specR.setOrder(order); }
    void setAveraging(float a) noexcept { spec.setAveraging(a); specR.setAveraging(a); }
    void setPeakDecay(float dbPerSec) noexcept { spec.setPeakDecay(dbPerSec); specR.setPeakDecay(dbPerSec); }
    void resetPeaks() noexcept { spec.resetPeaks(); specR.resetPeaks(); }

    // ---- per-frame update (UI timer) ------------------------------------
    /** Returns true if new data was analysed. */
    bool update(float dtSeconds);

    // ---- results ----------------------------------------------------------
    const StereoStats& getStereoStats() const noexcept { return stats; }

    /** Scope window (trigger-aligned) - getScopeLength() samples per channel. */
    const float* getScopeLeft()  const noexcept { return scopeL.data(); }
    const float* getScopeRight() const noexcept { return scopeR.data(); }
    int getScopeLength() const noexcept { return scopeLen; }

    /** Frames received since the previous update (max setVectorscopeFrames) for the vectorscope. */
    const float* getVectorLeft()  const noexcept { return capL.data() + (capLen - vecLen); }
    const float* getVectorRight() const noexcept { return capR.data() + (capLen - vecLen); }
    int getVectorLength() const noexcept { return vecLen; }

    double getSampleRate() const noexcept { return sampleRate; }

private:
    void ensureCapacity(int frames);

    const StereoCaptureRing& ring;
    double sampleRate = 48000.0;
    AnalyzerMode mode = AnalyzerMode::Spectrum;
    bool frozen = false;

    float scopeTimeMs = 20.0f;
    TriggerMode triggerMode = TriggerMode::Rising;
    float triggerLevel = 0.0f;
    TriggerSource triggerSource = TriggerSource::Mono;
    int vectorFrames = 8192;

    SpectrumAnalyser spec, specR;
    SpectrumChannel specChannel = SpectrumChannel::Mid;
    StereoStats stats;

    std::vector<float> capL, capR, mono, scopeL, scopeR;
    int capLen = 0, scopeLen = 0, vecLen = 0;
    uint64_t lastWritePos = 0;
};
} // namespace ambient::viz
