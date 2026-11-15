#include "ambient/dsp/SpectralDucker.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace ambient::dsp
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
inline float toDb(float lin) noexcept { return 20.0f * std::log10(lin + 1.0e-9f); }
inline float fromDb(float db) noexcept { return std::pow(10.0f, db / 20.0f); }

template <typename T, typename Less>
void insertionSort(T* a, int n, Less less) noexcept // tiny fixed arrays, no allocation
{
    for (int i = 1; i < n; ++i)
        for (int j = i; j > 0 && less(a[j], a[j - 1]); --j) std::swap(a[j], a[j - 1]);
}
}

// ---------------------------------------------------------------------------
float Bell::magnitudeDb(double sampleRate, float f0, float q, float reductionDb, float f) noexcept
{
    // Bilinear-transformed analog prototype: H(s) = 1 - c * (k s) / (s^2 + k s + 1), s = j w
    const double c = Bell::cutForDb(reductionDb), k = 1.0 / q;
    const double w = std::tan(kPi * std::fmin(f, sampleRate * 0.49) / sampleRate)
                   / std::tan(kPi * std::fmin(f0, sampleRate * 0.49) / sampleRate);
    const std::complex<double> s(0.0, w);
    const auto h = 1.0 - c * (k * s) / (s * s + k * s + 1.0);
    return float(20.0 * std::log10(std::abs(h) + 1e-12));
}

// ---------------------------------------------------------------------------
SpectralDucker::SpectralDucker()
{
    const float defaults[] = { 50.0f, 90.0f, 150.0f, 250.0f };
    for (int b = 0; b < kNumBands; ++b)
    {
        params.bands[size_t(b)].freq = defaults[b];
        params.bands[size_t(b)].enabled = b < 2;
        bands[size_t(b)].freq = bands[size_t(b)].autoTarget = defaults[b];
    }
}

mod::SourceInfo SpectralDucker::getModOutputInfo(int index) const noexcept
{
    static const mod::SourceInfo info[] = {
        { mod::SourceId::SidechainEnvelope,      "sc.env",       "Sidechain envelope",   false },
        { mod::SourceId::SidechainTransient,     "sc.transient", "Kick transient",       false },
        { mod::SourceId::SidechainLowEnergy,     "sc.low",       "Sidechain low energy", false },
        { mod::SourceId::SidechainMidEnergy,     "sc.mid",       "Sidechain mid energy", false },
        { mod::SourceId::SidechainHighEnergy,    "sc.high",      "Sidechain high energy",false },
        { mod::SourceId::SidechainCentroid,      "sc.centroid",  "Spectral centroid",    false },
        { mod::SourceId::SidechainGainReduction, "sc.gr",        "Gain reduction",       false },
    };
    return info[std::clamp(index, 0, 6)];
}

void SpectralDucker::prepare(double sampleRate, int maxBlock)
{
    (void) maxBlock; // processing is sample/sub-block based; no per-block buffers needed
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;

    // FFT for Auto detection: ~170 ms window => ~6 Hz bins at 48 kHz (kick/bass fundamentals).
    fftSize = 4096;
    while (fftSize < int(sr * 0.17)) fftSize <<= 1;
    hop = fftSize / 4;
    fft.prepare(fftSize);
    fifo.assign(size_t(fftSize), 0.0f);
    window.resize(size_t(fftSize));
    double wsum = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        window[size_t(i)] = float(0.5 - 0.5 * std::cos(2.0 * kPi * i / (fftSize - 1)));
        wsum += window[size_t(i)];
    }
    const float norm = float(2.0 / wsum); // full-scale sine -> magnitude 1
    for (auto& w : window) w *= norm;
    specHold.assign(size_t(fftSize / 2 + 1), 0.0f);
    fftBuf.assign(size_t(fftSize), {});
    specRelease = float(std::exp(-double(hop) / (sr * 0.35)));

    detAtt = EnvelopeFollower::coeff(sr, 1.0f);
    transientLp.setGK(Svf::prewarp(sr, 180.0), 1.0f / 0.707f);
    fastEnv.setTimes(sr, 0.5f, 8.0f);
    slowEnv.setTimes(sr, 25.0f, 250.0f);
    holdoffSamples = int(sr * 0.07);
    transientDecay = EnvelopeFollower::coeff(sr, 150.0f);
    modeCoeff = 1.0f - EnvelopeFollower::coeff(sr, 10.0f);
    autoGlide = 1.0f - float(std::exp(-double(kSubBlock) / (sr * 0.12)));

    reset();
}

void SpectralDucker::reset()
{
    for (auto& b : bands)
    {
        b.refL.reset(); b.refR.reset(); b.intL.reset(); b.intR.reset();
        b.bellL.reset(); b.bellR.reset();
        b.refEnv = b.intEnv = 0.0f;
        b.reductionDb = 0.0f;
        b.appliedDb = -1.0f; b.detFreq = 0.0f;
        b.active = false;
    }
    transientLp.reset();
    fastEnv.reset(); slowEnv.reset();
    holdoff = 0; transientPulse = 0.0f;
    std::fill(fifo.begin(), fifo.end(), 0.0f);
    std::fill(specHold.begin(), specHold.end(), 0.0f);
    fftFill = 0; samplesSinceFft = 0;
    broadbandDb = 0.0f; broadbandPrevGain = 1.0f;
    wEnable = params.enabled ? 1.0f : 0.0f;
    wListen = params.listen ? 1.0f : 0.0f;
    wDelta = params.delta ? 1.0f : 0.0f;
    wMix = params.mix;
    modOut.fill(0.0f);
}

// ---------------------------------------------------------------------------
void SpectralDucker::updateBandFilters(Band& b, const BandParams& p, bool force) noexcept
{
    const float q = std::clamp(p.q, 0.2f, 12.0f);
    // Detectors: band-pass at the band frequency (never narrower than Q 0.8 so
    // gliding kick pitch stays inside the detector).
    const float dq = std::max(q, 0.8f);
    if (force || std::fabs(b.freq - b.detFreq) > b.detFreq * 0.002f || dq != b.detQ)
    {
        const float g = Svf::prewarp(sr, b.freq), k = 1.0f / dq;
        b.refL.setGK(g, k); b.refR.setGK(g, k);
        b.intL.setGK(g, k); b.intR.setGK(g, k);
        b.detFreq = b.freq; b.detQ = dq;
        // Level-detector release never shorter than ~2.5 periods: no ripple at low frequencies.
        b.detRel = EnvelopeFollower::coeff(sr, std::max(20.0f, 2500.0f / b.freq));
    }
    // Bell coefficients depend only on frequency and Q (the cut is applied per sample).
    if (force || std::fabs(b.freq - b.appliedFreq) > b.appliedFreq * 0.002f || q != b.appliedQ)
    {
        b.bellL.set(sr, b.freq, q);
        b.bellR.set(sr, b.freq, q);
        b.appliedFreq = b.freq; b.appliedQ = q;
    }
}

void SpectralDucker::computeTargets(int n, bool referencePresent) noexcept
{
    const float s = std::clamp(params.sensitivity, 0.0f, 1.0f);
    const float slope = 0.3f + 1.7f * s;
    const float W = 12.0f + 24.0f * s;
    float maxRed = 0.0f;

    for (int i = 0; i < kNumBands; ++i)
    {
        auto& b = bands[size_t(i)];
        const auto& p = params.bands[size_t(i)];

        // frequency: Auto follows detected peaks, Manual follows the parameter; both glide.
        const float target = params.automatic ? b.autoTarget : std::clamp(p.freq, 20.0f, 20000.0f);
        b.freq = std::exp(std::log(b.freq) + autoGlide * (std::log(target) - std::log(b.freq)));

        float t = 0.0f;
        if (p.enabled && params.enabled && referencePresent)
        {
            const float refDb = toDb(b.refEnv), intDb = toDb(b.intEnv);
            const float over = refDb - (p.threshold + params.thresholdOffset);
            if (over > 0.0f && intDb > -80.0f)
            {
                const float masking = std::clamp((intDb - (refDb - W)) / W, 0.0f, 1.0f);
                t = std::min(p.maxReduction, over * slope) * masking * std::clamp(params.depth, 0.0f, 1.0f);
            }
        }
        // Attack never faster than half a period of the band frequency: a low band
        // cannot be gated inside one cycle (that is what makes ducking click).
        const float minAttackMs = 500.0f / b.freq;
        const float ms = t > b.reductionDb ? std::max(p.attackMs, minAttackMs) : std::max(p.releaseMs, minAttackMs);
        const float a = float(std::exp(-double(n) / (sr * std::max(0.1, double(ms)) * 0.001)));
        b.reductionDb = t + a * (b.reductionDb - t);
        if (b.reductionDb < 1.0e-4f) b.reductionDb = 0.0f;
        const bool nowActive = p.enabled || b.reductionDb > 0.0f;
        if (nowActive && ! b.active) // (re)activated: start from clean filter states
        {
            b.refL.reset(); b.refR.reset(); b.intL.reset(); b.intR.reset();
            b.bellL.reset(); b.bellR.reset();
            b.refEnv = b.intEnv = 0.0f;
        }
        b.active = nowActive;
        maxRed = std::max(maxRed, b.reductionDb);
        updateBandFilters(b, p, false);
    }
    broadbandDb = params.fullBand ? maxRed : 0.0f;
}

void SpectralDucker::runAutoDetection() noexcept
{
    // Oldest-to-newest copy of the circular FIFO, windowed.
    for (int i = 0; i < fftSize; ++i)
    {
        const int idx = (fftFill + i) % fftSize;
        fftBuf[size_t(i)] = { fifo[size_t(idx)] * window[size_t(i)], 0.0f };
    }
    fft.perform(fftBuf.data());

    const int nb = fftSize / 2 + 1;
    const double binHz = sr / fftSize;
    double eLow = 0, eMid = 0, eHigh = 0, eTot = 0, cent = 0;
    for (int k = 1; k < nb; ++k)
    {
        const float mag = std::abs(fftBuf[size_t(k)]);
        float& h = specHold[size_t(k)];
        h = std::max(mag, h * specRelease); // peak hold keeps kick peaks between hits
        const double f = k * binHz, p = double(mag) * mag;
        if (f < 150.0) eLow += p; else if (f < 2000.0) eMid += p; else eHigh += p;
        eTot += p; cent += p * std::log(std::max(20.0, f));
    }
    auto norm = [](double e) { return std::clamp(float((10.0 * std::log10(e + 1e-12) + 60.0) / 60.0), 0.0f, 1.0f); };
    modOut[2] = norm(eLow); modOut[3] = norm(eMid); modOut[4] = norm(eHigh);
    modOut[5] = eTot > 1e-10 ? std::clamp(float((cent / eTot - std::log(20.0)) / std::log(1000.0)), 0.0f, 1.0f) : 0.0f;

    if (! params.automatic) return;

    // Peak picking inside the search range.
    const int kLo = std::max(2, int(params.rangeLow / binHz));
    const int kHi = std::min(nb - 2, int(params.rangeHigh / binHz) + 1);
    if (kHi <= kLo) return;
    float maxInRange = 0.0f;
    for (int k = kLo; k <= kHi; ++k) maxInRange = std::max(maxInRange, specHold[size_t(k)]);
    const float floorLin = std::max(1.0e-3f, maxInRange * 0.0316f); // -60 dBFS and -30 dB re. strongest

    struct Peak { float freq, mag; };
    std::array<Peak, 16> peaks {};
    int numPeaks = 0;
    for (int k = kLo; k <= kHi; ++k)
    {
        const float m = specHold[size_t(k)];
        if (m < floorLin || m <= specHold[size_t(k - 1)] || m < specHold[size_t(k + 1)]) continue;
        const float y0 = std::log(specHold[size_t(k - 1)] + 1e-9f), y1 = std::log(m + 1e-9f), y2 = std::log(specHold[size_t(k + 1)] + 1e-9f);
        const float den = y0 - 2.0f * y1 + y2;
        const float d = std::fabs(den) > 1e-9f ? std::clamp(0.5f * (y0 - y2) / den, -0.5f, 0.5f) : 0.0f;
        const Peak pk { float((k + d) * binHz), m };
        if (numPeaks < int(peaks.size())) peaks[size_t(numPeaks++)] = pk;
        else
        {
            auto weakest = std::min_element(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) { return a.mag < b.mag; });
            if (weakest->mag < pk.mag) *weakest = pk;
        }
    }
    insertionSort(peaks.data(), numPeaks, [](const Peak& a, const Peak& b) { return a.mag > b.mag; });

    // Enabled bands, ordered by their current frequency.
    std::array<int, kNumBands> en {};
    int numEn = 0;
    for (int b = 0; b < kNumBands; ++b) if (params.bands[size_t(b)].enabled) en[size_t(numEn++)] = b;
    if (numEn == 0) return;
    insertionSort(en.data(), numEn, [this](int a, int b) { return bands[size_t(a)].freq < bands[size_t(b)].freq; });

    std::array<float, kNumBands> chosen {};
    int numChosen = 0;
    for (int i = 0; i < numPeaks && numChosen < numEn; ++i)
    {
        bool farEnough = true; // at least half an octave apart
        for (int c = 0; c < numChosen; ++c)
            if (std::fabs(std::log2(peaks[size_t(i)].freq / chosen[size_t(c)])) < 0.5f) { farEnough = false; break; }
        if (farEnough) chosen[size_t(numChosen++)] = peaks[size_t(i)].freq;
    }
    insertionSort(chosen.data(), numChosen, [](float a, float b) { return a < b; });
    // Fewer peaks than bands: keep the bands closest to the detected peaks moving,
    // leave the others where they are.
    if (numChosen == numEn)
        for (int i = 0; i < numEn; ++i) bands[size_t(en[size_t(i)])].autoTarget = chosen[size_t(i)];
    else
        for (int c = 0; c < numChosen; ++c)
        {
            int best = en[0];
            for (int i = 0; i < numEn; ++i)
                if (std::fabs(std::log(bands[size_t(en[size_t(i)])].freq / chosen[size_t(c)]))
                    < std::fabs(std::log(bands[size_t(best)].freq / chosen[size_t(c)]))) best = en[size_t(i)];
            bands[size_t(best)].autoTarget = chosen[size_t(c)];
        }
}

// ---------------------------------------------------------------------------
void SpectralDucker::process(float* L, float* R, const float* refL, const float* refR, int numSamples) noexcept
{
    if (refR == nullptr) refR = refL;

    // Reference presence (block peak) - nothing reacts to a silent / disconnected sidechain.
    float refPeak = 0.0f;
    if (refL != nullptr)
        for (int i = 0; i < numSamples; ++i) refPeak = std::max({ refPeak, std::fabs(refL[i]), std::fabs(refR[i]) });
    const bool present = refPeak > 1.0e-4f; // -80 dBFS

    const float tEnable = params.enabled ? 1.0f : 0.0f, tListen = params.listen ? 1.0f : 0.0f;
    const float tDelta = params.delta ? 1.0f : 0.0f, tMix = std::clamp(params.mix, 0.0f, 1.0f);

    for (int start = 0; start < numSamples; start += kSubBlock)
    {
        const int n = std::min(kSubBlock, numSamples - start);
        std::array<float, kNumBands> cutStart {}, cutStep {};
        for (int b = 0; b < kNumBands; ++b) cutStart[size_t(b)] = Bell::cutForDb(bands[size_t(b)].reductionDb);
        computeTargets(n, present);
        for (int b = 0; b < kNumBands; ++b)
            cutStep[size_t(b)] = (Bell::cutForDb(bands[size_t(b)].reductionDb) - cutStart[size_t(b)]) / float(n);
        const float bbTarget = fromDb(-broadbandDb);
        const float bbStep = (bbTarget - broadbandPrevGain) / float(n);

        for (int j = 0; j < n; ++j)
        {
            const int i = start + j;
            const float rl = refL ? refL[i] : 0.0f, rr = refL ? refR[i] : 0.0f;
            const float dryL = L[i], dryR = R[i];

            // ---- transient detector (reference low band) ----
            const float m = 0.5f * (rl + rr);
            const float lp = transientLp.lowpass(m);
            const float fe = fastEnv.process(lp), se = slowEnv.process(lp);
            transientPulse *= transientDecay;
            if (holdoff > 0) --holdoff;
            else if (fe > 3.16e-3f && fe > 2.0f * se) // > -50 dBFS and > 6 dB above the slow envelope
            {
                const float ratioPart = std::clamp((toDb(fe) - toDb(se) - 6.0f) / 18.0f, 0.0f, 1.0f);
                const float levelPart = std::clamp((toDb(fe) + 50.0f) / 44.0f, 0.0f, 1.0f);
                const float strength = std::clamp(0.35f + 0.4f * ratioPart + 0.25f * levelPart, 0.0f, 1.0f);
                tele.lastTransientStrength.store(strength, std::memory_order_relaxed);
                tele.transientCount.fetch_add(1, std::memory_order_relaxed);
                transientPulse = std::max(transientPulse, strength);
                holdoff = holdoffSamples;
            }

            // ---- FFT fifo ----
            fifo[size_t(fftFill)] = m;
            fftFill = (fftFill + 1) % fftSize;

            // ---- per-band detection + dynamic bells ----
            float wl = dryL, wr = dryR;
            for (int b = 0; b < kNumBands; ++b)
            {
                auto& bd = bands[size_t(b)];
                if (! bd.active) continue;
                const float detRelC = bd.detRel;
                const float rb = std::max(std::fabs(bd.refL.bandpass(rl)), std::fabs(bd.refR.bandpass(rr)));
                bd.refEnv = rb + (rb > bd.refEnv ? detAtt : detRelC) * (bd.refEnv - rb);
                const float ib = std::max(std::fabs(bd.intL.bandpass(dryL)), std::fabs(bd.intR.bandpass(dryR)));
                bd.intEnv = ib + (ib > bd.intEnv ? detAtt : detRelC) * (bd.intEnv - ib);
                if (! params.fullBand)
                {
                    const float c = cutStart[size_t(b)] + cutStep[size_t(b)] * float(j + 1); // per-sample ramp
                    wl = bd.bellL.process(wl, c);
                    wr = bd.bellR.process(wr, c);
                }
            }
            if (params.fullBand)
            {
                broadbandPrevGain += bbStep;
                wl *= broadbandPrevGain;
                wr *= broadbandPrevGain;
            }

            // ---- output modes (smoothed weights: no clicks when switching) ----
            wEnable += modeCoeff * (tEnable - wEnable);
            wListen += modeCoeff * (tListen - wListen);
            wDelta  += modeCoeff * (tDelta - wDelta);
            wMix    += modeCoeff * (tMix - wMix);
            const float pl = dryL + wEnable * wMix * (wl - dryL);
            const float pr = dryR + wEnable * wMix * (wr - dryR);
            const float ol = pl + wDelta * ((dryL - pl) - pl);
            const float orr = pr + wDelta * ((dryR - pr) - pr);
            L[i] = ol + wListen * (rl - ol);
            R[i] = orr + wListen * (rr - orr);
        }
        broadbandPrevGain = bbTarget;

        samplesSinceFft += n;
        if (samplesSinceFft >= hop) { samplesSinceFft = 0; runAutoDetection(); }
    }

    // ---- telemetry + modulation outputs ----
    float maxRed = 0.0f;
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& bd = bands[size_t(b)];
        tele.bandFreq[size_t(b)].store(bd.freq, std::memory_order_relaxed);
        tele.bandReductionDb[size_t(b)].store(params.fullBand ? broadbandDb : bd.reductionDb, std::memory_order_relaxed);
        tele.bandRefDb[size_t(b)].store(toDb(bd.refEnv), std::memory_order_relaxed);
        maxRed = std::max(maxRed, bd.reductionDb);
    }
    tele.maxReductionDb.store(maxRed, std::memory_order_relaxed);
    tele.referencePeak.store(refPeak, std::memory_order_relaxed);
    tele.referencePresent.store(present, std::memory_order_relaxed);

    modOut[0] = std::clamp((toDb(slowEnv.get()) + 60.0f) / 60.0f, 0.0f, 1.0f);
    modOut[1] = transientPulse;
    modOut[6] = std::clamp(maxRed / 24.0f, 0.0f, 1.0f);
}
} // namespace ambient::dsp
