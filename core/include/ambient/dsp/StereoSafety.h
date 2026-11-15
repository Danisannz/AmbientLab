#pragma once
// Global output stereo safety (zero latency).
//
//   L/R -> M/S -> bass mono (side high-passed, 24 dB/oct) -> width (scale side) -> safety: feed-forward
//          side gain that keeps the short-term correlation above a floor
//       -> L/R  (optional mono-check monitor: L = R = M)
//
// Correlation in M/S terms: corr = (M^2 - S^2) / (M^2 + S^2). Keeping corr >= f means
// S^2 <= k M^2 with k = (1 - f) / (1 + f); the safety gain is sqrt(k M^2 / S^2) when
// that is below 1, otherwise 1 - normal material is never narrowed.
#include <atomic>
#include "SideHighPass.h"

namespace ambient::dsp
{
class StereoSafety
{
public:
    struct Params
    {
        float width = 1.0f;          // 0 .. 1.5 (above 1 = widening; safety still applies)
        bool  bassMono = true;
        float bassMonoHz = 120.0f;   // 40 .. 400
        bool  safety = true;
        float floor = 0.0f;          // minimum short-term correlation, -0.5 .. 0.5
        bool  monoCheck = false;     // monitoring: output (L+R)/2 on both sides
    };

    struct Telemetry
    {
        std::atomic<float> corrIn { 1.0f };      // correlation before width/safety (smoothed ~300 ms)
        std::atomic<float> corrOut { 1.0f };     // after
        std::atomic<float> midDb { -120.0f }, sideDb { -120.0f };   // output M and S levels (RMS dBFS)
        std::atomic<float> correctionDb { 0.0f };                     // side attenuation by the safety, >= 0
        std::atomic<bool>  antiPhase { false };  // input correlation below -0.3
    };

    void prepare(double sampleRate);
    void reset();
    void setParams(const Params& p) noexcept { params = p; }
    void process(float* L, float* R, int n) noexcept;
    const Telemetry& telemetry() const noexcept { return tel; }
    static constexpr int getLatencySamples() noexcept { return 0; }

private:
    Params params;
    Telemetry tel;
    double sr = 48000.0;
    SideHighPass sideHp;
    // smoothed controls
    float wSmooth = 1.0f, bmSmooth = 1.0f, monoSmooth = 0.0f, gSafety = 1.0f;
    // detectors (EMA energies)
    float eM = 0.0f, eS = 0.0f;                 // safety detector (~100 ms)
    float tLL = 0, tRR = 0, tLR = 0, oLL = 0, oRR = 0, oLR = 0, oM = 0, oS = 0; // telemetry (~300 ms)
    float aDet = 0.0f, aTel = 0.0f, aCtl = 0.0f, gAtt = 0.0f, gRel = 0.0f;
};
} // namespace ambient::dsp
