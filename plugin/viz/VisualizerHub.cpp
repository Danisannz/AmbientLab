#include "VisualizerHub.h"
#include <algorithm>

namespace ambient::viz
{
VisualizerHub::VisualizerHub(const StereoCaptureRing& r) : ring(r)
{
    setSampleRate(48000.0);
}

void VisualizerHub::setSampleRate(double sr)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    spec.setSampleRate(sampleRate);
    specR.setSampleRate(sampleRate);
}

void VisualizerHub::ensureCapacity(int frames)
{
    if (int(capL.size()) < frames)
    {
        capL.assign(size_t(frames), 0.0f);
        capR.assign(size_t(frames), 0.0f);
        mono.assign(size_t(frames), 0.0f);
        scopeL.assign(size_t(frames), 0.0f);
        scopeR.assign(size_t(frames), 0.0f);
    }
}

bool VisualizerHub::update(float dtSeconds)
{
    if (frozen) return false;

    const uint64_t w = ring.getWritePosition();
    if (w == lastWritePos) return false; // transport stopped / no audio flowing
    const uint64_t newFrames = lastWritePos == 0 ? uint64_t(vectorFrames) : w - lastWritePos;
    lastWritePos = w;

    // How much history each view needs. The scope gets 2x its window so the
    // trigger search has a full window of candidates.
    const int scopeWindow = juce::jmax(16, int(scopeTimeMs * 0.001 * sampleRate));
    const int needScope = scopeWindow * 2;
    const int needSpec = spec.getSize();
    const int maxRead = ring.getCapacity() / 2;
    const int need = juce::jmin(maxRead, std::max({ needScope, needSpec, vectorFrames, 1024 }));

    ensureCapacity(need);
    if (! ring.readLatest(need, capL.data(), capR.data())) return false;
    capLen = need;

    // Stereo statistics over the last ~50 ms are always computed (cheap, feeds meters).
    const int statN = juce::jmin(need, juce::jmax(256, int(0.05 * sampleRate)));
    stats = computeStereoStats(capL.data() + (need - statN), capR.data() + (need - statN), size_t(statN));

    // Vectorscope draws only frames it has not drawn before (persistence does the rest).
    vecLen = int(juce::jmin<uint64_t>(newFrames, uint64_t(juce::jmin(vectorFrames, need))));

    if (mode == AnalyzerMode::Scope)
    {
        const int n = juce::jmin(needScope, need);
        const int win = juce::jmin(scopeWindow, n);
        const float* srcL = capL.data() + (need - n);
        const float* srcR = capR.data() + (need - n);
        const float* trig = mono.data();
        if (triggerSource == TriggerSource::Left) trig = srcL;
        else if (triggerSource == TriggerSource::Right) trig = srcR;
        else for (int i = 0; i < n; ++i) mono[size_t(i)] = 0.5f * (srcL[i] + srcR[i]);
        const size_t start = findTrigger(trig, size_t(n), size_t(win), triggerMode, triggerLevel);
        std::copy(srcL + start, srcL + start + win, scopeL.begin());
        std::copy(srcR + start, srcR + start + win, scopeR.begin());
        scopeLen = win;
    }
    else if (mode == AnalyzerMode::Spectrum)
    {
        const int n = spec.getSize();
        const float* srcL = capL.data() + (need - n);
        const float* srcR = capR.data() + (need - n);
        switch (specChannel)
        {
            case SpectrumChannel::Left:  spec.process(srcL, dtSeconds); break;
            case SpectrumChannel::Right: spec.process(srcR, dtSeconds); break;
            case SpectrumChannel::Stereo:
                spec.process(srcL, dtSeconds);
                specR.process(srcR, dtSeconds);
                break;
            case SpectrumChannel::Mid:
                for (int i = 0; i < n; ++i) mono[size_t(i)] = 0.5f * (srcL[i] + srcR[i]);
                spec.process(mono.data(), dtSeconds);
                break;
            case SpectrumChannel::Side:
                for (int i = 0; i < n; ++i) mono[size_t(i)] = 0.5f * (srcL[i] - srcR[i]);
                spec.process(mono.data(), dtSeconds);
                break;
        }
    }
    return true;
}
} // namespace ambient::viz
