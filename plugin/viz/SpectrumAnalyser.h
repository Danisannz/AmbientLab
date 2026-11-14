#pragma once
// FFT spectrum analysis for the UI thread (juce_dsp only, no GUI dependency).

#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace ambient::viz
{
class SpectrumAnalyser
{
public:
    SpectrumAnalyser();

    /** order 10..14 -> FFT size 1024..16384. Allocates: call from the UI thread only. */
    void setOrder(int order);
    int  getOrder() const noexcept { return order; }
    int  getSize()  const noexcept { return size; }

    void setSampleRate(double sr) noexcept { sampleRate = sr; }
    double getSampleRate() const noexcept { return sampleRate; }

    /** 0 = no averaging, 0.95 = heavy averaging (per update, power domain). */
    void setAveraging(float a) noexcept { averaging = juce::jlimit(0.0f, 0.99f, a); }
    /** Peak-hold fall rate in dB per second. */
    void setPeakDecay(float dbPerSec) noexcept { peakDecayDbPerSec = dbPerSec; }
    void resetPeaks() noexcept;

    /** Analyses exactly getSize() samples (mono). dtSeconds = time since previous call. */
    void process(const float* samples, float dtSeconds);

    /** Number of usable bins (size/2 + 1). */
    int getNumBins() const noexcept { return size / 2 + 1; }
    float binFrequency(int bin) const noexcept { return float(bin * sampleRate / size); }

    /** dBFS, calibrated so a full-scale sine reads 0 dB. */
    const std::vector<float>& getAverageDb() const noexcept { return avgDb; }
    const std::vector<float>& getPeakDb() const noexcept { return peakDb; }

    /** Samples the averaged (or peak) spectrum at `numPoints` log-spaced frequencies with
        fractional-octave smoothing (smoothingOct = 0 -> none, 1/3 -> third-octave). */
    void renderLogCurve(int numPoints, float fMin, float fMax, float smoothingOct,
                        bool usePeak, float* outDb) const noexcept;

    /** Frequency of the strongest bin (parabolic interpolation). */
    float findPeakFrequency(float fMin = 20.0f) const noexcept;

    static constexpr float kFloorDb = -140.0f;

private:
    int order = 0, size = 0;
    double sampleRate = 48000.0;
    float averaging = 0.7f, peakDecayDbPerSec = 12.0f;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, work, avgPower, avgDb, peakDb;
    float windowGain = 1.0f;
    mutable std::vector<double> cumPower; // scratch for renderLogCurve (UI thread)
    bool hasData = false;
};
} // namespace ambient::viz
