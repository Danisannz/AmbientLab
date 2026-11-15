#include "Mascot.h"
#include <cmath>

namespace ambient::ui
{
using namespace theme;

Mascot::Mascot()
{
    setInterceptsMouseClicks(false, false);
}

void Mascot::setAnimated(bool a)
{
    animated = a;
    if (! a) { state = {}; blink = flash = hatKick = 0.0f; }
    repaint();
}

void Mascot::tick(float dt, uint32_t count, float strength, bool active, float reductionDb)
{
    if (first) { lastCount = count; first = false; }

    if (animated)
    {
        // Real detector events only: one reaction per detected transient (coalesced per frame).
        if (count != lastCount)
        {
            lastCount = count;
            ++reactions;
            blink = 1.0f;
            flash = juce::jmax(flash, juce::jlimit(0.3f, 1.0f, strength));
            if (strength > 0.7f) hatKick = juce::jmax(hatKick, (strength - 0.7f) / 0.3f);
        }
        time += dt;
        blink = juce::jmax(0.0f, blink - dt / 0.16f);           // 160 ms blink
        flash *= std::exp(-dt / 0.18f);
        hatKick *= std::exp(-dt / 0.22f);

        // eye: closes in the first 35 % of the blink, reopens over the rest
        const float b = 1.0f - blink;
        state.eyeOpen = b < 0.35f ? 1.0f - b / 0.35f : (b - 0.35f) / 0.65f;
        if (blink <= 0.0f) state.eyeOpen = 1.0f;
        state.eyeOpen = juce::jlimit(0.08f, 1.0f, state.eyeOpen);

        const float breathe = std::sin(juce::MathConstants<float>::twoPi * 0.22f * time);
        state.bob = 0.012f * breathe;
        state.hatTilt = 0.035f * std::sin(juce::MathConstants<float>::twoPi * 0.17f * time) - 0.18f * hatKick;
        state.orb = juce::jlimit(0.0f, 1.0f, 0.32f + 0.1f * breathe + 0.68f * flash);
        auraTarget = active ? juce::jlimit(0.25f, 1.0f, 0.25f + reductionDb / 10.0f) : 0.0f;
        state.aura += (auraTarget - state.aura) * (1.0f - std::exp(-dt / 0.15f));
    }
    else
    {
        lastCount = count;
        state.aura = active ? 0.25f : 0.0f; // static: show "active" only
    }

    // Repaint only on a visible change (negligible cost when idle).
    auto diff = [](float a, float b) { return std::abs(a - b) > 0.004f; };
    if (diff(state.eyeOpen, shown.eyeOpen) || diff(state.orb, shown.orb) || diff(state.aura, shown.aura)
        || diff(state.hatTilt, shown.hatTilt) || diff(state.bob, shown.bob))
    {
        shown = state;
        repaint();
    }
}

void Mascot::paint(juce::Graphics& g)
{
    draw(g, getLocalBounds().toFloat(), state);
}

void Mascot::draw(juce::Graphics& g, juce::Rectangle<float> area, const State& st)
{
    // Unit design space: 100 x 120. Map preserving aspect.
    const float scale = juce::jmin(area.getWidth() / 100.0f, area.getHeight() / 120.0f);
    const float ox = area.getCentreX() - 50.0f * scale;
    const float oy = area.getCentreY() - 60.0f * scale + st.bob * 120.0f * scale;
    auto P = [&](float x, float y) { return juce::Point<float>(ox + x * scale, oy + y * scale); };
    auto R = [&](float x, float y, float w, float h) { return juce::Rectangle<float>(ox + x * scale, oy + y * scale, w * scale, h * scale); };
    const float s = scale;

    const auto charcoal = juce::Colour(0xff23232b), charcoalDark = juce::Colour(0xff111116);

    // ---- aura (lime) ---------------------------------------------------------
    if (st.aura > 0.01f && ! st.mono)
    {
        const auto c = P(50.0f, 72.0f);
        juce::ColourGradient aura(lime.withAlpha(0.32f * st.aura), c.x, c.y, lime.withAlpha(0.0f), c.x + 52.0f * s, c.y, true);
        g.setGradientFill(aura);
        g.fillEllipse(juce::Rectangle<float>(104.0f * s, 104.0f * s).withCentre(c));
    }

    // ---- beard (gnome) ----------------------------------------------------------
    {
        juce::Path beard;
        beard.startNewSubPath(P(30.0f, 84.0f));
        beard.quadraticTo(P(36.0f, 108.0f), P(50.0f, 117.0f));
        beard.quadraticTo(P(64.0f, 108.0f), P(70.0f, 84.0f));
        beard.closeSubPath();
        g.setColour(st.mono ? textFaint.withAlpha(0.5f) : charcoalDark);
        g.fillPath(beard);
        if (! st.mono)
        {
            g.setColour(uv.withAlpha(0.55f));
            for (int i = 0; i < 4; ++i) // psychedelic stripes
            {
                juce::Path wave;
                const float y = 92.0f + float(i) * 6.0f, w = 17.0f - float(i) * 3.5f;
                wave.startNewSubPath(P(50.0f - w, y));
                wave.quadraticTo(P(50.0f, y + 4.0f), P(50.0f + w, y));
                g.strokePath(wave, juce::PathStrokeType(1.1f * s));
            }
        }
    }

    // ---- ears (alien, pointed) -------------------------------------------------
    for (int side : { -1, 1 })
    {
        juce::Path ear;
        ear.startNewSubPath(P(50.0f + side * 27.0f, 62.0f));
        ear.lineTo(P(50.0f + side * 47.0f, 52.0f));
        ear.lineTo(P(50.0f + side * 29.0f, 76.0f));
        ear.closeSubPath();
        g.setColour(st.mono ? textFaint : charcoal);
        g.fillPath(ear);
        if (! st.mono) { g.setColour(uv.withAlpha(0.6f)); g.strokePath(ear, juce::PathStrokeType(1.0f * s)); }
    }

    // ---- head -----------------------------------------------------------------
    const auto head = R(20.0f, 46.0f, 60.0f, 48.0f);
    if (st.mono) g.setColour(textFaint);
    else
    {
        juce::ColourGradient hg(juce::Colour(0xff34343f), head.getX(), head.getY(), charcoalDark, head.getRight(), head.getBottom(), false);
        g.setGradientFill(hg);
    }
    juce::Path headPath;
    headPath.addRoundedRectangle(head, 24.0f * s);
    g.fillPath(headPath);
    if (! st.mono)
    {
        g.setColour(uvBright.withAlpha(0.45f)); // rim light
        g.strokePath(headPath, juce::PathStrokeType(1.0f * s));
    }

    // ---- eyes (large almond, tilted) ---------------------------------------------
    for (int side : { -1, 1 })
    {
        const auto c = P(50.0f + side * 12.5f, 69.0f);
        const float w = 19.0f * s, h = 12.0f * s * st.eyeOpen;
        juce::Path eye;
        eye.addEllipse(-w * 0.5f, -h * 0.5f, w, h);
        eye.applyTransform(juce::AffineTransform::rotation(side * 0.42f).translated(c));
        g.setColour(juce::Colours::black);
        g.fillPath(eye);
        if (st.eyeOpen > 0.3f)
        {
            const auto glint = c + juce::Point<float>(side * 2.5f * s, -1.5f * s);
            const float gr = 2.4f * s;
            g.setColour((st.mono ? textDim : lime).withAlpha(0.55f + 0.45f * st.orb));
            g.fillEllipse(juce::Rectangle<float>(gr * 2, gr * 2).withCentre(glint));
        }
    }

    // ---- mouth: minimal smile ---------------------------------------------------------
    {
        juce::Path smile;
        smile.startNewSubPath(P(43.5f, 82.0f));
        smile.quadraticTo(P(50.0f, 87.0f), P(56.5f, 82.0f));
        g.setColour(st.mono ? textDim : lime);
        g.strokePath(smile, juce::PathStrokeType(1.6f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ---- hat (gnome cone, sways and bounces) ------------------------------------------
    {
        const auto pivot = P(50.0f, 50.0f);
        const auto rot = juce::AffineTransform::rotation(st.hatTilt, pivot.x, pivot.y);
        juce::Path hat;
        hat.startNewSubPath(P(17.0f, 52.0f));
        hat.quadraticTo(P(40.0f, 22.0f), P(70.0f, 4.0f));     // curled tip
        hat.quadraticTo(P(66.0f, 26.0f), P(83.0f, 52.0f));
        hat.quadraticTo(P(50.0f, 58.0f), P(17.0f, 52.0f));
        hat.closeSubPath();
        hat.applyTransform(rot);
        if (st.mono) g.setColour(textFaint);
        else
        {
            const auto a = P(30.0f, 50.0f).transformedBy(rot), b = P(70.0f, 6.0f).transformedBy(rot);
            juce::ColourGradient hg(uvDeep, a.x, a.y, uvBright, b.x, b.y, false);
            hg.addColour(0.55, uv);
            g.setGradientFill(hg);
        }
        g.fillPath(hat);

        // magenta band
        juce::Path band;
        band.startNewSubPath(P(19.0f, 47.0f));
        band.quadraticTo(P(50.0f, 53.0f), P(81.0f, 47.0f));
        band.applyTransform(rot);
        g.setColour(st.mono ? textDim : magenta);
        g.strokePath(band, juce::PathStrokeType(3.2f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // antenna orb at the tip
        const auto tip = P(71.0f, 4.0f).transformedBy(rot);
        if (! st.mono && st.orb > 0.05f)
        {
            juce::ColourGradient glow(lime.withAlpha(0.6f * st.orb), tip.x, tip.y, lime.withAlpha(0.0f), tip.x + 14.0f * s, tip.y, true);
            g.setGradientFill(glow);
            g.fillEllipse(juce::Rectangle<float>(28.0f * s, 28.0f * s).withCentre(tip));
        }
        g.setColour(st.mono ? textDim : lime.interpolatedWith(juce::Colours::white, 0.4f * st.orb));
        g.fillEllipse(juce::Rectangle<float>(7.0f * s, 7.0f * s).withCentre(tip));
    }
}
} // namespace ambient::ui
