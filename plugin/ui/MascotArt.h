#pragma once
// "Glim" v2 - evolved AmbientLab mascot (PROPOSAL - not yet wired into the editor).
// A small psychedelic extraterrestrial forest spirit that lives inside the synth lab:
// crinkled ultraviolet wizard hat with a lime antenna tip, swept-back alien ears,
// dark face with heavy-lidded slit-pupil eyes, a third-eye mark, bioluminescent
// cheek spots, a long wispy beard, a robe with a glowing trim, and long thin hands
// holding a patch cable.
//
// Pure vector drawing, transparent background, fixed 100 x 160 design space mapped
// with preserved aspect ratio. Level of detail drops automatically at small sizes.

#include "Theme.h"

namespace ambient::ui
{
struct MascotPose
{
    float eyeOpen  = 1.0f;  // 0 closed .. 1 open (blinks)
    float lids     = 0.35f; // upper-lid droop 0 alert .. 1 sleepy (relaxed idle = high)
    float pupil    = 0.0f;  // 0 slit .. 1 dilated pulse (kick)
    float surprise = 0.0f;  // 0 .. 1 (strong kick): wide eyes, raised brows, "o" mouth
    float glow     = 0.0f;  // lime details lit (sidechain active)
    float aura     = 0.0f;  // gain-reduction aura
    float orb      = 0.4f;  // hat-tip glow
    float hatTilt  = 0.0f;  // radians
    float bob      = 0.0f;  // fraction of height
    float ears     = 0.0f;  // 0 relaxed .. 1 perked (high reduction)
};

struct MascotArt
{
    /** Draws the character into `area`, aspect preserved, transparent background. */
    static void draw(juce::Graphics&, juce::Rectangle<float> area, const MascotPose&);
    static constexpr float kAspect = 100.0f / 160.0f; // width / height
};
} // namespace ambient::ui
