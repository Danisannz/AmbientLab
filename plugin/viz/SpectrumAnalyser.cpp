#include "SpectrumAnalyser.h"
#include <cmath>

namespace ambient::viz
{
SpectrumAnalyser::SpectrumAnalyser() { setOrder(12); }

void SpectrumAnalyser::setOrder(int newOrder)
{
    newOrder = juce::jlimit(10, 14, newOrder);
    if (newOrder == order && fft) return;
    order = newOrder;
    size = 1 << order;
    fft = std::make_unique<juce::dsp::FFT>(order);

    // 4-term Blackman-Harris: -92 dB sidelobes, good for wide dynamic range display.
    window.resize(size_t(size));
    double sum = 0.0;
    for (int i = 0; i < size; ++i)
    {
        const double x = juce::MathConstants<double>::twoPi * i / (size - 1);
        const double w = 0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2 * x) - 0.01168 * std::cos(3 * x);
        window[size_t(i)] = float(w);
        sum += w;
    }
    // Amplitude calibration: a sine of amplitude A gives |X| = A * sum(w) / 2.
    windowGain = float(2.0 / sum);

    work.assign(size_t(size) * 2, 0.0f);
    avgPower.assign(size_t(getNumBins()), 0.0f);
    avgDb.assign(size_t(getNumBins()), kFloorDb);
    peakDb.assign(size_t(getNumBins()), kFloorDb);
    hasData = false;
}

void SpectrumAnalyser::resetPeaks() noexcept
{
    std::fill(peakDb.begin(), peakDb.end(), kFloorDb);
}

void SpectrumAnalyser::process(const float* samples, float dtSeconds)
{
    for (int i = 0; i < size; ++i) work[size_t(i)] = samples[i] * window[size_t(i)];
    std::fill(work.begin() + size, work.end(), 0.0f);
    fft->performFrequencyOnlyForwardTransform(work.data(), true);

    const float a = hasData ? averaging : 0.0f;
    const float decay = peakDecayDbPerSec * juce::jmax(0.0f, dtSeconds);
    const int bins = getNumBins();
    for (int b = 0; b < bins; ++b)
    {
        const float mag = work[size_t(b)] * windowGain;
        const float p = mag * mag;
        float& ap = avgPower[size_t(b)];
        ap = a * ap + (1.0f - a) * p;
        const float db = ap > 1.0e-14f ? 10.0f * std::log10(ap) : kFloorDb;
        avgDb[size_t(b)] = juce::jmax(kFloorDb, db);
        peakDb[size_t(b)] = juce::jmax(avgDb[size_t(b)], peakDb[size_t(b)] - decay);
    }
    hasData = true;
}

void SpectrumAnalyser::renderLogCurve(int numPoints, float fMin, float fMax, float smoothingOct,
                                      bool usePeak, float* outDb) const noexcept
{
    const auto& src = usePeak ? peakDb : avgDb;
    const float binHz = float(sampleRate / size);
    const int bins = getNumBins();
    const float half = smoothingOct * 0.5f;

    // Prefix sums of linear power: any band average is O(1).
    // cum[k] = sum of power of bins [0, k). Fractional edge bins are weighted by overlap.
    if (half > 0.0f)
    {
        cumPower.resize(size_t(bins) + 1);
        cumPower[0] = 0.0;
        for (int k = 0; k < bins; ++k)
            cumPower[size_t(k) + 1] = cumPower[size_t(k)] + std::pow(10.0, double(src[size_t(k)]) / 10.0);
    }
    // Integral of power over bin coordinate [0, x], bin k covering [k-0.5, k+0.5).
    auto integral = [&](double x)
    {
        x = juce::jlimit(-0.5, double(bins) - 0.5, x);
        const double u = x + 0.5;               // shift so bin k covers [k, k+1)
        const int k = juce::jmin(bins - 1, int(u));
        return cumPower[size_t(k)] + (u - double(k)) * (cumPower[size_t(k) + 1] - cumPower[size_t(k)]);
    };

    for (int i = 0; i < numPoints; ++i)
    {
        const float x = numPoints > 1 ? float(i) / float(numPoints - 1) : 0.0f;
        const float f = fMin * std::pow(fMax / fMin, x);

        float lo = f, hi = f;
        if (half > 0.0f) { lo = f * std::pow(2.0f, -half); hi = f * std::pow(2.0f, half); }
        const float bLo = lo / binHz, bHi = hi / binHz;

        if (bHi - bLo < 1.0f)
        {
            // Narrower than a bin: interpolate linearly in dB between neighbours.
            const float pos = juce::jlimit(0.0f, float(bins - 1), f / binHz);
            const int i0 = int(pos);
            const int i1 = juce::jmin(bins - 1, i0 + 1);
            const float t = pos - float(i0);
            outDb[i] = src[size_t(i0)] + t * (src[size_t(i1)] - src[size_t(i0)]);
        }
        else
        {
            const double width = double(bHi - bLo);
            const double p = (integral(bHi) - integral(bLo)) / width;
            outDb[i] = p > 1.0e-14 ? float(10.0 * std::log10(p)) : kFloorDb;
        }
    }
}

float SpectrumAnalyser::findPeakFrequency(float fMin) const noexcept
{
    const int bins = getNumBins();
    const int start = juce::jmax(1, int(fMin / (sampleRate / size)));
    int best = start;
    for (int b = start; b < bins - 1; ++b)
        if (avgDb[size_t(b)] > avgDb[size_t(best)]) best = b;
    if (best <= 0 || best >= bins - 1) return binFrequency(best);
    const float y0 = avgDb[size_t(best - 1)], y1 = avgDb[size_t(best)], y2 = avgDb[size_t(best + 1)];
    const float den = y0 - 2.0f * y1 + y2;
    const float delta = std::abs(den) > 1.0e-9f ? 0.5f * (y0 - y2) / den : 0.0f;
    return float((best + juce::jlimit(-0.5f, 0.5f, delta)) * sampleRate / size);
}
} // namespace ambient::viz
