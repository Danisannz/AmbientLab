#include "MascotArt.h"
#include <cmath>

namespace ambient::ui
{
using namespace theme;

void MascotArt::draw(juce::Graphics& g, juce::Rectangle<float> area, const MascotPose& p)
{
    const float s = juce::jmin(area.getWidth() / 100.0f, area.getHeight() / 160.0f);
    const float ox = area.getCentreX() - 50.0f * s;
    const float oy = area.getCentreY() - 80.0f * s + p.bob * 160.0f * s;
    const float px = 100.0f * s;           // rendered width in pixels -> level of detail
    const bool fine = px >= 70.0f, mid = px >= 34.0f;
    auto P = [&](float x, float y) { return juce::Point<float>(ox + x * s, oy + y * s); };
    auto stroke = [&](float w) { return juce::PathStrokeType(juce::jmax(0.8f, w * s), juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };

    const juce::Colour skin  { 0xff262833 }, skinDark { 0xff121319 }, robeA { 0xff2a1450 }, robeB { 0xff120a22 };
    const juce::Colour limeLit = lime.withAlpha(0.35f + 0.65f * p.glow);

    // ---- aura (gain reduction) -------------------------------------------------------
    if (p.aura > 0.01f)
    {
        const auto c = P(50.0f, 92.0f);
        juce::ColourGradient a(lime.withAlpha(0.22f * p.aura), c.x, c.y, lime.withAlpha(0.0f), c.x + 70.0f * s, c.y, true);
        g.setGradientFill(a);
        g.fillEllipse(juce::Rectangle<float>(140.0f * s, 150.0f * s).withCentre(c));
    }

    // ---- robe / body -----------------------------------------------------------------
    {
        juce::Path robe;
        robe.startNewSubPath(P(30.0f, 108.0f));
        robe.cubicTo(P(20.0f, 120.0f), P(16.0f, 140.0f), P(14.0f, 158.0f));
        robe.lineTo(P(86.0f, 158.0f));
        robe.cubicTo(P(84.0f, 140.0f), P(80.0f, 120.0f), P(70.0f, 108.0f));
        robe.closeSubPath();
        juce::ColourGradient rg(robeA, ox, oy + 108.0f * s, robeB, ox, oy + 158.0f * s, false);
        g.setGradientFill(rg);
        g.fillPath(robe);
        g.setColour(uv.withAlpha(0.45f));
        g.strokePath(robe, stroke(0.8f));
        if (mid) // glowing trim along the hem
        {
            juce::Path hem;
            hem.startNewSubPath(P(15.0f, 152.0f));
            hem.quadraticTo(P(50.0f, 156.0f), P(85.0f, 152.0f));
            g.setColour(magenta.withAlpha(0.8f));
            g.strokePath(hem, stroke(1.6f));
            if (fine)
                for (int i = 0; i < 9; ++i)
                {
                    const float x = 20.0f + float(i) * 7.5f;
                    g.setColour(limeLit);
                    g.fillEllipse(juce::Rectangle<float>(1.6f * s, 1.6f * s).withCentre(P(x, 148.5f + std::abs(float(i) - 4.0f) * 0.2f)));
                }
        }
    }

    // ---- high pointed collar (silhouette) -------------------------------------------------------
    for (int side : { -1, 1 })
    {
        juce::Path collar;
        collar.startNewSubPath(P(50.0f + side * 8.0f, 112.0f));
        collar.quadraticTo(P(50.0f + side * 30.0f, 106.0f), P(50.0f + side * 36.0f, 88.0f)); // point rises behind the ears
        collar.quadraticTo(P(50.0f + side * 30.0f, 108.0f), P(50.0f + side * 24.0f, 118.0f));
        collar.closeSubPath();
        g.setColour(juce::Colour(0xff1b0d36));
        g.fillPath(collar);
        g.setColour(uv.withAlpha(0.7f));
        g.strokePath(collar, stroke(0.8f));
    }

    // ---- patch cable held in the right hand (synth-lab detail) -----------------------------
    if (fine)
    {
        juce::Path cable;
        cable.startNewSubPath(P(77.0f, 128.0f));
        cable.cubicTo(P(92.0f, 132.0f), P(96.0f, 146.0f), P(90.0f, 156.0f));
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.strokePath(cable, stroke(2.6f));
        g.setColour(magenta.withAlpha(0.9f));
        g.strokePath(cable, stroke(1.6f));
        // jack plug pointing up from the hand
        auto plug = juce::Rectangle<float>(3.0f * s, 6.0f * s).withCentre(P(77.5f, 120.5f));
        g.setColour(metal);
        g.fillRoundedRectangle(plug, 1.0f * s);
        g.setColour(limeLit);
        g.fillEllipse(juce::Rectangle<float>(2.2f * s, 2.2f * s).withCentre(P(77.5f, 116.6f)));
    }

    // ---- sleeves + hands (long thin three-fingered) -------------------------------------------
    auto arm = [&](float shoulderX, float handX, float handY, float dir, bool holding)
    {
        juce::Path sleeve; // tapered tube from the shoulder to the wrist
        sleeve.startNewSubPath(P(shoulderX - dir * 2.0f, 112.0f));
        sleeve.quadraticTo(P(shoulderX + dir * 6.0f, 120.0f), P(handX + dir * 3.0f, handY + 3.0f));
        sleeve.lineTo(P(handX - dir * 4.0f, handY + 6.0f));
        sleeve.quadraticTo(P(shoulderX - dir * 3.0f, 126.0f), P(shoulderX - dir * 9.0f, 116.0f));
        sleeve.closeSubPath();
        g.setColour(robeA.brighter(0.12f));
        g.fillPath(sleeve);
        g.setColour(uv.withAlpha(0.45f));
        g.strokePath(sleeve, stroke(0.7f));
        juce::Path cuff;
        cuff.startNewSubPath(P(handX + dir * 3.0f, handY + 3.0f));
        cuff.lineTo(P(handX - dir * 4.0f, handY + 6.0f));
        g.setColour(magenta.withAlpha(0.85f));
        g.strokePath(cuff, stroke(1.4f));

        g.setColour(skin);
        g.fillEllipse(juce::Rectangle<float>(5.5f * s, 5.0f * s).withCentre(P(handX, handY + 1.0f)));
        if (! mid) return;
        for (int f = 0; f < 3; ++f)
        {
            const float a = (holding ? -1.75f : -1.15f) + float(f) * 0.38f;
            juce::Path finger;
            const auto base = P(handX + dir * 1.0f, handY);
            const float len = (holding ? 6.5f : 8.0f) - std::abs(float(f) - 1.0f) * 1.2f;
            const auto tipPt = base + juce::Point<float>(dir * std::cos(a) * len, std::sin(a) * len) * s;
            finger.startNewSubPath(base);
            finger.quadraticTo(base + juce::Point<float>(dir * std::cos(a) * len * 0.7f, std::sin(a) * len * 0.3f) * s, tipPt);
            g.setColour(skin.brighter(0.2f));
            g.strokePath(finger, stroke(1.3f));
            if (fine)
            {
                g.setColour(uvBright.withAlpha(0.8f));
                g.fillEllipse(juce::Rectangle<float>(1.5f * s, 1.5f * s).withCentre(tipPt));
            }
        }
    };
    arm(30.0f, 25.0f, 130.0f, -1.0f, false);
    arm(70.0f, 76.0f, 127.0f, 1.0f, true);

    // ---- beard (wispy forest spirit) ------------------------------------------------------
    {
        juce::Path beard;
        beard.startNewSubPath(P(27.0f, 92.0f));
        beard.cubicTo(P(26.0f, 112.0f), P(36.0f, 130.0f), P(44.0f, 146.0f));   // left edge
        beard.quadraticTo(P(46.0f, 150.0f), P(49.0f, 153.0f));                   // curled tip
        beard.cubicTo(P(54.0f, 140.0f), P(64.0f, 124.0f), P(73.0f, 92.0f));   // right edge
        beard.quadraticTo(P(50.0f, 104.0f), P(27.0f, 92.0f));
        beard.closeSubPath();
        juce::ColourGradient bg(juce::Colour(0xff4a4160), ox, oy + 95.0f * s, juce::Colour(0xff2a1f45), ox, oy + 152.0f * s, false);
        g.setGradientFill(bg);
        g.fillPath(beard);
        if (mid)
        {
            const float strandsX[] = { 36.0f, 43.0f, 50.0f, 57.0f, 64.0f };
            for (int i = 0; i < 5; ++i)
            {
                juce::Path st;
                const float x0 = strandsX[i];
                st.startNewSubPath(P(x0, 104.0f));
                st.cubicTo(P(x0 + 4.0f * std::sin(float(i)), 118.0f), P(x0 - 3.0f + i, 130.0f), P(46.0f + float(i) * 0.6f, 142.0f + std::abs(float(i) - 2.0f) * -2.5f));
                g.setColour((i == 2 ? limeLit : juce::Colour(0xff8a7bb0).withAlpha(0.7f)));
                g.strokePath(st, stroke(i == 2 ? 0.9f : 0.8f));
            }
        }
    }

    // ---- ears (swept back, perk with reduction) ----------------------------------------------
    for (int side : { -1, 1 })
    {
        const float lift = p.ears * 5.0f;
        juce::Path ear;
        ear.startNewSubPath(P(50.0f + side * 24.0f, 70.0f));
        ear.quadraticTo(P(50.0f + side * 40.0f, 62.0f - lift), P(50.0f + side * 49.0f, 52.0f - lift)); // tip
        ear.quadraticTo(P(50.0f + side * 38.0f, 74.0f), P(50.0f + side * 26.0f, 84.0f));
        ear.closeSubPath();
        g.setColour(skin);
        g.fillPath(ear);
        if (mid)
        {
            juce::Path inner;
            inner.startNewSubPath(P(50.0f + side * 27.0f, 72.0f));
            inner.quadraticTo(P(50.0f + side * 39.0f, 64.0f - lift), P(50.0f + side * 45.0f, 57.0f - lift));
            inner.quadraticTo(P(50.0f + side * 36.0f, 72.0f), P(50.0f + side * 28.0f, 79.0f));
            inner.closeSubPath();
            g.setColour(uvDeep.withAlpha(0.9f));
            g.fillPath(inner);
        }
        g.setColour(uv.withAlpha(0.5f));
        g.strokePath(ear, stroke(0.7f));
    }

    // ---- head: wide alien cranium narrowing to a pointed chin ---------------------------------
    juce::Path head;
    head.startNewSubPath(P(50.0f, 56.0f));
    head.cubicTo(P(70.0f, 56.0f), P(80.0f, 64.0f), P(78.0f, 76.0f));
    head.cubicTo(P(76.0f, 90.0f), P(62.0f, 104.0f), P(50.0f, 110.0f));
    head.cubicTo(P(38.0f, 104.0f), P(24.0f, 90.0f), P(22.0f, 76.0f));
    head.cubicTo(P(20.0f, 64.0f), P(30.0f, 56.0f), P(50.0f, 56.0f));
    head.closeSubPath();
    {
        juce::ColourGradient hg(juce::Colour(0xff363946), ox + 30.0f * s, oy + 60.0f * s, skinDark, ox + 70.0f * s, oy + 108.0f * s, false);
        g.setGradientFill(hg);
        g.fillPath(head);
        g.setColour(uvBright.withAlpha(0.35f)); // cool rim light
        g.strokePath(head, stroke(0.8f));
    }

    // ---- cheek spots (bioluminescent) + third-eye mark --------------------------------------
    if (fine)
        for (int side : { -1, 1 })
            for (int i = 0; i < 3; ++i)
            {
                g.setColour(lime.withAlpha((0.18f + 0.55f * p.glow) * (1.0f - float(i) * 0.2f)));
                g.fillEllipse(juce::Rectangle<float>((1.6f - float(i) * 0.3f) * s, (1.6f - float(i) * 0.3f) * s)
                                  .withCentre(P(50.0f + side * (24.5f + float(i) * 2.2f), 88.0f + float(i) * 1.8f)));
            }
    if (mid)
    {
        juce::Path third;
        const auto c = P(50.0f, 67.0f);
        third.addEllipse(-3.0f * s, -1.6f * s, 6.0f * s, 3.2f * s);
        third.applyTransform(juce::AffineTransform::translation(c));
        g.setColour(limeLit);
        g.strokePath(third, stroke(0.8f));
        g.fillEllipse(juce::Rectangle<float>(1.6f * s, 1.6f * s).withCentre(c));
    }

    // ---- eyes: large almond, slit pupils, heavy upper lids -------------------------------------
    const float surprise = juce::jlimit(0.0f, 1.0f, p.surprise);
    for (int side : { -1, 1 })
    {
        const auto c = P(50.0f + side * 13.0f, 80.0f - surprise * 1.0f);
        const float w = 19.0f * s * (1.0f + 0.10f * surprise);
        const float hMax = 12.5f * s * (1.0f + 0.30f * surprise);
        const float h = juce::jmax(0.6f * s, hMax * p.eyeOpen);
        const auto rot = juce::AffineTransform::rotation(side * 0.30f).translated(c);

        juce::Path eye;
        eye.addEllipse(-w * 0.5f, -h * 0.5f, w, h);
        eye.applyTransform(rot);
        g.setColour(juce::Colour(0xff050806));
        g.fillPath(eye);

        if (p.eyeOpen > 0.15f)
        {
            juce::Graphics::ScopedSaveState ss(g);
            g.reduceClipRegion(eye);
            // iris + slit pupil (dilates on kicks, shrinks with surprise)
            const float ir = 5.2f * s * (1.0f - 0.15f * surprise);
            const auto ic = c + juce::Point<float>(side * 1.0f * s, 0.6f * s);
            juce::ColourGradient ig(lime, ic.x, ic.y, limeDeep, ic.x + ir, ic.y, true);
            g.setGradientFill(ig);
            g.fillEllipse(juce::Rectangle<float>(ir * 2, ir * 2).withCentre(ic));
            const float slitW = (1.1f + 2.6f * p.pupil) * s * (1.0f - 0.4f * surprise);
            g.setColour(juce::Colours::black);
            g.fillEllipse(juce::Rectangle<float>(slitW, ir * 1.7f).withCentre(ic));
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.fillEllipse(juce::Rectangle<float>(1.6f * s, 1.6f * s).withCentre(ic + juce::Point<float>(-side * 1.8f * s, -2.2f * s)));

            // heavy upper lid: the face colour covers the top of the eye
            const float lid = juce::jlimit(0.0f, 0.75f, p.lids * (1.0f - surprise));
            if (lid > 0.01f)
            {
                juce::Path cover;
                cover.addRectangle(-w, -h, w * 2.0f, h * lid);
                cover.applyTransform(rot);
                g.setColour(juce::Colour(0xff2d303b));
                g.fillPath(cover);
            }
        }
        // lid line / brow
        juce::Path brow;
        const float by = -h * 0.5f + h * juce::jlimit(0.0f, 0.75f, p.lids * (1.0f - surprise)) - surprise * 4.5f * s;
        brow.startNewSubPath(-w * 0.55f, by + 1.5f * s);
        brow.quadraticTo(0.0f, by - 1.5f * s - surprise * 1.5f * s, w * 0.55f, by + 1.5f * s);
        brow.applyTransform(rot);
        g.setColour(juce::Colours::black.withAlpha(0.85f));
        g.strokePath(brow, stroke(1.3f));
    }

    // ---- nose slits + mouth -----------------------------------------------------------------------
    if (mid)
    {
        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.fillEllipse(juce::Rectangle<float>(1.2f * s, 2.0f * s).withCentre(P(48.2f, 90.0f)));
        g.fillEllipse(juce::Rectangle<float>(1.2f * s, 2.0f * s).withCentre(P(51.8f, 90.0f)));
    }
    if (surprise > 0.35f)
    {
        g.setColour(juce::Colours::black);
        g.fillEllipse(juce::Rectangle<float>(5.0f * s, 6.5f * s * surprise).withCentre(P(50.0f, 98.0f)));
        g.setColour(lime.withAlpha(0.8f));
        g.drawEllipse(juce::Rectangle<float>(5.0f * s, 6.5f * s * surprise).withCentre(P(50.0f, 98.0f)), juce::jmax(0.7f, 0.8f * s));
    }
    else
    {
        juce::Path smirk; // asymmetric knowing smile
        smirk.startNewSubPath(P(43.5f, 96.5f));
        smirk.quadraticTo(P(50.0f, 99.5f), P(57.5f, 94.5f));
        g.setColour(lime.withAlpha(0.85f));
        g.strokePath(smirk, stroke(1.3f));
    }

    // ---- hat: crinkled cone, brim, band, embroidery, antenna tip ---------------------------------------
    {
        const auto pivot = P(50.0f, 60.0f);
        const auto rot = juce::AffineTransform::rotation(p.hatTilt, pivot.x, pivot.y);

        juce::Path cone;
        cone.startNewSubPath(P(24.0f, 58.0f));
        cone.cubicTo(P(28.0f, 40.0f), P(36.0f, 30.0f), P(46.0f, 24.0f));
        cone.lineTo(P(44.0f, 19.0f));                                  // crinkle
        cone.cubicTo(P(52.0f, 14.0f), P(62.0f, 8.0f), P(80.0f, 6.0f)); // drooping tip to the right
        cone.cubicTo(P(66.0f, 14.0f), P(60.0f, 20.0f), P(58.0f, 26.0f));
        cone.lineTo(P(62.0f, 30.0f));                                  // second crinkle
        cone.cubicTo(P(68.0f, 40.0f), P(74.0f, 48.0f), P(76.0f, 58.0f));
        cone.closeSubPath();
        cone.applyTransform(rot);
        {
            const auto a = P(30.0f, 58.0f).transformedBy(rot), b = P(78.0f, 8.0f).transformedBy(rot);
            juce::ColourGradient hg(uvDeep, a.x, a.y, uvBright, b.x, b.y, false);
            hg.addColour(0.5, uv);
            g.setGradientFill(hg);
            g.fillPath(cone);
        }
        if (mid) // fold shading
        {
            juce::Path fold;
            fold.startNewSubPath(P(44.0f, 19.0f)); fold.quadraticTo(P(50.0f, 22.0f), P(58.0f, 26.0f));
            fold.startNewSubPath(P(34.0f, 36.0f)); fold.quadraticTo(P(48.0f, 40.0f), P(65.0f, 36.0f));
            fold.applyTransform(rot);
            g.setColour(uvDeep.withAlpha(0.8f));
            g.strokePath(fold, stroke(1.2f));
        }
        if (fine) // sparse embroidery: tiny four-point stars and dots
        {
            const juce::Point<float> stars[] = { { 38.0f, 46.0f }, { 56.0f, 40.0f }, { 66.0f, 50.0f }, { 50.0f, 30.0f }, { 64.0f, 14.0f } };
            for (int i = 0; i < 5; ++i)
            {
                const auto c = P(stars[i].x, stars[i].y).transformedBy(rot);
                const float r = (i % 2 == 0 ? 2.0f : 1.4f) * s;
                juce::Path st;
                st.startNewSubPath(c.x, c.y - r); st.lineTo(c.x + r * 0.3f, c.y - r * 0.3f); st.lineTo(c.x + r, c.y);
                st.lineTo(c.x + r * 0.3f, c.y + r * 0.3f); st.lineTo(c.x, c.y + r); st.lineTo(c.x - r * 0.3f, c.y + r * 0.3f);
                st.lineTo(c.x - r, c.y); st.lineTo(c.x - r * 0.3f, c.y - r * 0.3f); st.closeSubPath();
                g.setColour(uvBright.withAlpha(0.55f));
                g.fillPath(st);
            }
        }

        // brim
        juce::Path brim;
        brim.addEllipse(P(12.0f, 53.0f).x, P(12.0f, 53.0f).y, 76.0f * s, 11.0f * s);
        brim.applyTransform(rot);
        g.setColour(juce::Colour(0xff241046));
        g.fillPath(brim);
        g.setColour(uv.withAlpha(0.8f));
        g.strokePath(brim, stroke(0.8f));

        // magenta band with a lime stitch
        juce::Path band;
        band.startNewSubPath(P(25.0f, 53.0f));
        band.quadraticTo(P(50.0f, 58.0f), P(75.0f, 53.0f));
        band.applyTransform(rot);
        g.setColour(magenta);
        g.strokePath(band, stroke(3.2f));
        if (mid)
        {
            const float dash[] = { 1.5f * s, 2.0f * s };
            juce::Path dashed;
            juce::PathStrokeType(0.7f * s).createDashedStroke(dashed, band, dash, 2);
            g.setColour(limeLit);
            g.fillPath(dashed);
        }

        // antenna filament + orb at the tip
        const auto tip = P(80.0f, 6.0f).transformedBy(rot);
        const auto orbC = tip + juce::Point<float>(3.0f * s, -1.5f * s);
        if (p.orb > 0.05f)
        {
            juce::ColourGradient glow(lime.withAlpha(0.55f * p.orb), orbC.x, orbC.y, lime.withAlpha(0.0f), orbC.x + 13.0f * s, orbC.y, true);
            g.setGradientFill(glow);
            g.fillEllipse(juce::Rectangle<float>(26.0f * s, 26.0f * s).withCentre(orbC));
        }
        g.setColour(lime.interpolatedWith(juce::Colours::white, 0.45f * p.orb));
        g.fillEllipse(juce::Rectangle<float>(6.5f * s, 6.5f * s).withCentre(orbC));
    }
}
} // namespace ambient::ui
