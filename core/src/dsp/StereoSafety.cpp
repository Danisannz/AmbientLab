#include <ambient/dsp/StereoSafety.h>
#include <algorithm>
#include <cmath>

namespace ambient::dsp
{
namespace { float coef(double sr, double tauSec) { return float(1.0 - std::exp(-1.0 / (tauSec * sr))); } }

void StereoSafety::prepare(double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    aDet = coef(sr, 0.1); aTel = coef(sr, 0.3); aCtl = coef(sr, 0.02);
    gAtt = coef(sr, 0.03); gRel = coef(sr, 0.4);
    reset();
}

void StereoSafety::reset()
{
    sideHp.reset();
    wSmooth = std::clamp(params.width, 0.0f, 1.5f);
    bmSmooth = params.bassMono ? 1.0f : 0.0f;
    monoSmooth = params.monoCheck ? 1.0f : 0.0f;
    gSafety = 1.0f;
    eM = eS = tLL = tRR = tLR = oLL = oRR = oLR = oM = oS = 0.0f;
}

void StereoSafety::process(float* L, float* R, int n) noexcept
{
    const auto& p = params;
    const float tW = std::clamp(p.width, 0.0f, 1.5f), tBm = p.bassMono ? 1.0f : 0.0f, tMono = p.monoCheck ? 1.0f : 0.0f;
    const float fl = std::clamp(p.floor, -0.5f, 0.5f);
    const float k = (1.0f - fl) / (1.0f + fl);
    sideHp.setCutoff(std::clamp(double(p.bassMonoHz), 20.0, 1000.0), sr);
    float gMin = 1.0f;

    for (int i = 0; i < n; ++i)
    {
        const float l = L[i], r = R[i];
        // input correlation telemetry
        tLL += aTel * (l * l - tLL); tRR += aTel * (r * r - tRR); tLR += aTel * (l * r - tLR);

        wSmooth += aCtl * (tW - wSmooth);
        bmSmooth += aCtl * (tBm - bmSmooth);
        monoSmooth += aCtl * (tMono - monoSmooth);

        const float m = 0.5f * (l + r), s = 0.5f * (l - r);
        // bass mono: side high-passed (4th order); off = side untouched (exact)
        const float sh = sideHp.process(s);
        float side = (s + bmSmooth * (sh - s)) * wSmooth;

        // safety: feed-forward, measured on the post-width signal
        eM += aDet * (m * m - eM);
        eS += aDet * (side * side - eS);
        float target = 1.0f;
        if (p.safety && eS > 1.0e-10f && eS > k * eM) target = std::sqrt(std::max(0.0f, k * eM / eS));
        gSafety += (target < gSafety ? gAtt : gRel) * (target - gSafety);
        if (! p.safety) gSafety += gRel * (1.0f - gSafety);
        gMin = std::min(gMin, gSafety);
        side *= gSafety;

        float lo = m + side, ro = m - side;
        lo += monoSmooth * (m - lo); ro += monoSmooth * (m - ro);
        L[i] = lo; R[i] = ro;

        oLL += aTel * (lo * lo - oLL); oRR += aTel * (ro * ro - oRR); oLR += aTel * (lo * ro - oLR);
        const float mo = 0.5f * (lo + ro), so = 0.5f * (lo - ro);
        oM += aTel * (mo * mo - oM); oS += aTel * (so * so - oS);
    }
    sideHp.flushDenormals();
    for (auto& st : sideHp.s) for (auto& v : st) if (! std::isfinite(v)) sideHp.reset();

    auto corr = [](float lr, float ll, float rr) { return (ll > 1.0e-12f && rr > 1.0e-12f) ? std::clamp(lr / std::sqrt(ll * rr), -1.0f, 1.0f) : 1.0f; };
    auto db = [](float e) { return 10.0f * std::log10(std::max(e, 1.0e-12f)); };
    const float cIn = corr(tLR, tLL, tRR);
    tel.corrIn.store(cIn, std::memory_order_relaxed);
    tel.corrOut.store(corr(oLR, oLL, oRR), std::memory_order_relaxed);
    tel.midDb.store(db(oM), std::memory_order_relaxed);
    tel.sideDb.store(db(oS), std::memory_order_relaxed);
    tel.correctionDb.store(-20.0f * std::log10(std::max(gMin, 1.0e-4f)), std::memory_order_relaxed);
    tel.antiPhase.store(cIn < -0.3f && (tLL + tRR) > 1.0e-8f, std::memory_order_relaxed);
}
} // namespace ambient::dsp
