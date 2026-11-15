#include "Mascot.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace ambient::ui
{
Mascot::Mascot()
{
    setInterceptsMouseClicks(false, false);
    setOpaque(false);
}

void Mascot::setAnimated(bool a)
{
    animated = a;
    if (! a) { kick = surprise = hop = nod = 0.0f; pose = {}; }
    repaint();
}

void Mascot::tick(float dt, const Inputs& in)
{
    if (first) { lastCount = in.transientCount; lastStep = in.seqStep; first = false; }
    auto follow = [dt](float& v, float target, float tau) { v += (target - v) * (1.0f - std::exp(-dt / tau)); };
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    // ---- events (real detector data only) ----
    if (in.transientCount != lastCount)
    {
        lastCount = in.transientCount;
        ++reactions;
        const float s = juce::jlimit(0.25f, 1.0f, in.transientStrength);
        kick = juce::jmax(kick, s);
        hop = juce::jmax(hop, s);
        if (in.transientStrength > 0.7f) surprise = juce::jmax(surprise, (in.transientStrength - 0.7f) / 0.3f);
    }
    if (in.seqStep != lastStep)
    {
        if (in.seqStep >= 0) nod = 1.0f;
        lastStep = in.seqStep;
    }

    if (! animated)
    {
        pose = {};
        pose.glow = in.sidechainActive ? 0.6f : 0.15f;   // static: show state only
        pose.aura = in.sidechainActive ? 0.2f : 0.0f;
    }
    else
    {
        time += dt;
        // decays
        kick *= std::exp(-dt / 0.20f);
        hop *= std::exp(-dt / 0.16f);
        surprise *= std::exp(-dt / 0.35f);
        nod *= std::exp(-dt / 0.12f);

        // smoothed continuous telemetry
        const float lvl = juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(in.outputLevel, -100.0f) + 50.0f) / 44.0f);
        follow(level, lvl, lvl > level ? 0.15f : 0.8f);
        follow(aura, in.sidechainActive ? juce::jlimit(0.0f, 1.0f, in.reductionDb / 12.0f) : 0.0f, 0.15f);
        follow(ears, in.sidechainActive ? juce::jlimit(0.0f, 1.0f, (in.reductionDb - 4.0f) / 8.0f) : 0.0f, 0.2f);
        follow(glow, (in.sidechainActive ? 0.55f : 0.18f) + 0.25f * level, 0.3f);
        follow(sway, in.lfo, 0.12f);

        // breathing / float: faster and wider with signal
        const float rate = 0.20f + 0.12f * level;
        const float breathe = std::sin(twoPi * rate * time);
        pose.bob = (0.008f + 0.010f * level) * breathe - 0.035f * hop + 0.012f * nod;

        // deterministic blink every 5.2 s (160 ms), suppressed during a surprise
        const float bphase = std::fmod(time, 5.2f);
        float eye = 1.0f;
        if (bphase < 0.16f && surprise < 0.2f)
        {
            const float b = bphase / 0.16f;
            eye = b < 0.35f ? 1.0f - b / 0.35f : (b - 0.35f) / 0.65f;
        }
        pose.eyeOpen = juce::jlimit(0.05f, 1.0f, eye);
        pose.lids = juce::jlimit(0.0f, 1.0f, 0.62f - 0.35f * level - 0.4f * kick - 0.6f * surprise);
        pose.pupil = juce::jlimit(0.0f, 1.0f, kick);
        pose.surprise = juce::jlimit(0.0f, 1.0f, surprise);
        pose.glow = juce::jlimit(0.0f, 1.0f, glow + 0.5f * kick);
        pose.aura = juce::jlimit(0.0f, 1.0f, aura + 0.25f * kick);
        pose.orb = juce::jlimit(0.0f, 1.0f, 0.3f + 0.1f * breathe + 0.2f * level + 0.7f * kick);
        pose.ears = juce::jlimit(0.0f, 1.0f, ears + 0.3f * surprise);
        pose.hatTilt = 0.03f * std::sin(twoPi * 0.17f * time) + 0.06f * sway - 0.16f * surprise - 0.05f * hop;
    }

    // repaint only on a visible change
    auto diff = [](float a, float b) { return std::abs(a - b) > 0.004f; };
    if (diff(pose.eyeOpen, shown.eyeOpen) || diff(pose.lids, shown.lids) || diff(pose.pupil, shown.pupil)
        || diff(pose.surprise, shown.surprise) || diff(pose.glow, shown.glow) || diff(pose.aura, shown.aura)
        || diff(pose.orb, shown.orb) || diff(pose.hatTilt, shown.hatTilt) || diff(pose.bob, shown.bob) || diff(pose.ears, shown.ears))
    {
        shown = pose;
        repaint();
    }
}

void Mascot::paint(juce::Graphics& g)
{
    MascotArt::draw(g, getLocalBounds().toFloat(), pose);
}
} // namespace ambient::ui
