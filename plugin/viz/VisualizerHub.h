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

    /** Number of most-recent stereo frames shown by the vectorscope. */
    void setVectorscopeFrames(int n) noexcept { vectorFrames = juce::jlimit(256, 8192, n); }

    SpectrumAnalyser& spectrum() noexcept { return spec; }

    // ---- per-frame update (UI timer) ------------------------------------
    /** Returns true if new data was analysed. */
    bool update(float dtSeconds);

    // ---- results ----------------------------------------------------------
    const StereoStats& getStereoStats() const noexcept { return stats; }

    /** Scope window (trigger-aligned) - getScopeLength() samples per channel. */
    const float* getScopeLeft()  const noexcept { return scopeL.data(); }
    const float* getScopeRight() const noexcept { return scopeR.data(); }
    int getScopeLength() const noexcept { return scopeLen; }

    /** Latest frames for the vectorscope. */
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
    int vectorFrames = 2048;

    SpectrumAnalyser spec;
    StereoStats stats;

    std::vector<float> capL, capR, mono, scopeL, scopeR;
    int capLen = 0, scopeLen = 0, vecLen = 0;
    uint64_t lastWritePos = 0;
};
} // namespace ambient::viz
