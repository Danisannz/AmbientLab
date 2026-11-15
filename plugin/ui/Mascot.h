#pragma once
// "Glim" v2 - the AmbientLab mascot (drawing: MascotArt).
//
// Every reaction is driven by real DSP telemetry polled on the UI thread (never random,
// never on the audio thread):
//   idle / no signal      -> slow breathing + float, deterministic blink every ~5 s, relaxed lids
//   audio level           -> breathing / float intensity and alertness follow the output level
//   detected kick         -> pupils pulse, hat tip flashes, lime details brighten, small hop
//                            (all scaled by the detector's transient strength)
//   strong kick (> 0.7)   -> surprise: wide eyes, 'o' mouth, hat jolt
//   sidechain active      -> lime details lit; gain reduction drives the aura and perks the ears
//   routed LFO activity   -> subtle hat sway following the LFO value
//   sequencer steps       -> small nod on each step (rhythm)
// The audio thread only publishes atomics (counters, levels); the UI smooths everything.

#include "Theme.h"
#include "MascotArt.h"

namespace ambient::ui
{
class Mascot : public juce::Component
{
public:
    /** Telemetry snapshot read by the UI timer. */
    struct Inputs
    {
        uint32_t transientCount = 0;   // monotonic counter from the sidechain detector
        float transientStrength = 0;   // 0..1 strength of the last detected kick
        bool  sidechainActive = false; // sidechain enabled and a reference signal present
        float reductionDb = 0;         // current maximum band reduction (dB)
        float outputLevel = 0;         // output peak (linear)
        float lfo = 0;                 // value of the strongest *routed* LFO, -1..1 (0 = none)
        int   seqStep = -1;            // current sequencer step (-1 = stopped)
    };

    Mascot();

    void tick(float dt, const Inputs&);
    /** Legacy entry point (sidechain-only telemetry). */
    void tick(float dt, uint32_t transientCount, float lastStrength, bool sidechainActive, float reductionDb)
    {
        Inputs in; in.transientCount = transientCount; in.transientStrength = lastStrength;
        in.sidechainActive = sidechainActive; in.reductionDb = reductionDb;
        tick(dt, in);
    }
    void setAnimated(bool a);
    bool isAnimated() const noexcept { return animated; }

    void paint(juce::Graphics&) override;

    /** Number of kick reactions triggered so far (tests / diagnostics). */
    uint32_t getReactionCount() const noexcept { return reactions; }
    const MascotPose& getPose() const noexcept { return pose; }

private:
    MascotPose pose, shown;
    bool animated = true, first = true;
    float time = 0, level = 0, kick = 0, surprise = 0, hop = 0, nod = 0, aura = 0, glow = 0, ears = 0, sway = 0;
    uint32_t lastCount = 0, reactions = 0;
    int lastStep = -1;
};
} // namespace ambient::ui
