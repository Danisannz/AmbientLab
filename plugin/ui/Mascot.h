#pragma once
// "Glim" - the AmbientLab mascot: an alien gnome (original artwork, vector-drawn).
//
// Reactions are driven ONLY by real detector data polled on the UI thread:
//   no signal            -> idle (slow deterministic breathing / hat sway)
//   detected transient   -> blink + antenna flash (strength scales the flash)
//   strong transient     -> additionally a short hat bounce
//   sidechain active     -> subtle lime aura
//   gain reduction       -> aura intensity follows the reduction amount
// The audio thread only increments an atomic counter; it never draws or animates.

#include "Theme.h"

namespace ambient::ui
{
class Mascot : public juce::Component
{
public:
    struct State
    {
        float eyeOpen = 1.0f;     // 0 closed .. 1 open
        float orb = 0.35f;        // antenna glow 0..1
        float aura = 0.0f;        // lime aura 0..1
        float hatTilt = 0.0f;     // radians
        float bob = 0.0f;         // vertical offset (fraction of height)
        bool  mono = false;       // monochrome small-icon rendering
    };

    Mascot();

    /** Poll from the UI timer. transientCount: monotonic counter from the detector. */
    void tick(float dt, uint32_t transientCount, float lastStrength, bool sidechainActive, float reductionDb);
    void setAnimated(bool a);
    bool isAnimated() const noexcept { return animated; }

    void paint(juce::Graphics&) override;

    /** Draws the character into `area` (aspect preserved). Usable for small static icons. */
    static void draw(juce::Graphics&, juce::Rectangle<float> area, const State&);

    /** Number of reactions triggered so far (for tests / diagnostics). */
    uint32_t getReactionCount() const noexcept { return reactions; }

private:
    State state;
    bool animated = true;
    float time = 0.0f, blink = 0.0f, flash = 0.0f, hatKick = 0.0f, auraTarget = 0.0f;
    uint32_t lastCount = 0, reactions = 0;
    bool first = true;
    State shown;
};
} // namespace ambient::ui
