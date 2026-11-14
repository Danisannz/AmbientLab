#include "AmbientLookAndFeel.h"

namespace ambient::ui
{
using namespace theme;

AmbientLookAndFeel::AmbientLookAndFeel()
{
    setDefaultSansSerifTypeface(Fonts::get().plexRegular);

    setColour(juce::ResizableWindow::backgroundColourId, bg);
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, uvDeep);
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour(juce::PopupMenu::headerTextColourId, textDim);
    setColour(juce::TextButton::textColourOffId, textDim);
    setColour(juce::TextButton::textColourOnId, text);
    setColour(juce::Label::textColourId, text);
    setColour(juce::BubbleComponent::backgroundColourId, panelHdr);
    setColour(juce::BubbleComponent::outlineColourId, edge);
    setColour(juce::TooltipWindow::backgroundColourId, panelHdr);
    setColour(juce::TooltipWindow::textColourId, text);
    setColour(juce::TooltipWindow::outlineColourId, edge);
}

// ============================================================================
// Rotary knob: scale dots, value track, knurled metal skirt, brushed cap, pointer.
// ============================================================================
void AmbientLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float a0, float a1, juce::Slider& s)
{
    const bool hq = quality == GraphicsQuality::High;
    const auto bounds = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).reduced(1.0f);
    const float r = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = a0 + pos * (a1 - a0);
    const bool hot = s.isMouseOverOrDragging() && s.isEnabled();
    const bool enabled = s.isEnabled();

    // ---- scale dots ----------------------------------------------------------
    for (int i = 0; i <= 10; ++i)
    {
        const float a = a0 + (a1 - a0) * float(i) / 10.0f;
        const auto p = c + juce::Point<float>(std::sin(a), -std::cos(a)) * (r * 0.965f);
        const float d = (i == 0 || i == 5 || i == 10) ? r * 0.055f : r * 0.035f;
        g.setColour(textFaint.withAlpha(i == 5 ? 0.9f : 0.6f));
        g.fillEllipse(juce::Rectangle<float>(d, d).withCentre(p));
    }

    // ---- value track -----------------------------------------------------------
    const float trackR = r * 0.82f, trackW = juce::jmax(2.0f, r * 0.085f);
    const juce::PathStrokeType stroke(trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path track;
    track.addCentredArc(c.x, c.y, trackR, trackR, 0.0f, a0, a1, true);
    g.setColour(juce::Colours::black.withAlpha(0.7f));
    g.strokePath(track, stroke);

    const bool bipolar = s.getProperties()[props::bipolar];
    const float from = bipolar ? (a0 + a1) * 0.5f : a0;
    if (std::abs(angle - from) > 0.001f)
    {
        juce::Path val;
        val.addCentredArc(c.x, c.y, trackR, trackR, 0.0f, juce::jmin(from, angle), juce::jmax(from, angle), true);
        const auto col = enabled ? (hot ? uvBright : uv) : textFaint;
        if (hq && enabled)
        {
            g.setColour(col.withAlpha(hot ? 0.22f : 0.12f));
            g.strokePath(val, juce::PathStrokeType(trackW * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour(col);
        g.strokePath(val, stroke);
    }

    // ---- modulation ring (lime) - only drawn when a modulation source exists -----
    const float mod = float(double(s.getProperties()[props::modDepth]));
    if (std::abs(mod) > 0.001f)
    {
        const float modR = r * 0.965f;
        const float end = juce::jlimit(a0, a1, angle + mod * (a1 - a0));
        juce::Path m;
        m.addCentredArc(c.x, c.y, modR, modR, 0.0f, juce::jmin(angle, end), juce::jmax(angle, end), true);
        g.setColour(lime);
        g.strokePath(m, juce::PathStrokeType(juce::jmax(1.5f, r * 0.05f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ---- skirt (knurled) --------------------------------------------------------
    const float skirtR = r * 0.66f;
    const auto skirt = juce::Rectangle<float>(skirtR * 2, skirtR * 2).withCentre(c);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillEllipse(skirt.translated(0.0f, r * 0.05f).expanded(r * 0.02f));

    juce::ColourGradient skirtG(juce::Colour(0xff3a3a44), skirt.getX(), skirt.getY(),
                                juce::Colour(0xff0f0f13), skirt.getRight(), skirt.getBottom(), false);
    g.setGradientFill(skirtG);
    g.fillEllipse(skirt);

    if (hq)
    {
        const int grooves = 40;
        for (int i = 0; i < grooves; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * float(i) / float(grooves);
            const juce::Point<float> d { std::sin(a), -std::cos(a) };
            // light from top-left: grooves on that side read brighter
            const float lit = 0.5f + 0.5f * (-d.x * 0.7f - d.y * 0.7f);
            g.setColour(juce::Colour(0xff6a6a78).withAlpha(0.10f + 0.25f * lit));
            g.drawLine({ c + d * (skirtR * 0.86f), c + d * (skirtR * 0.99f) }, juce::jmax(0.7f, r * 0.018f));
        }
    }

    // ---- cap (brushed metal) ---------------------------------------------------------
    const auto cap = skirt.reduced(skirtR * 0.17f);
    juce::ColourGradient capG(juce::Colour(0xff5c5b69), cap.getX(), cap.getY(),
                              juce::Colour(0xff1b1b22), cap.getRight(), cap.getBottom(), false);
    capG.addColour(0.5, juce::Colour(0xff2e2e38));
    g.setGradientFill(capG);
    g.fillEllipse(cap);
    if (hq)
    {
        juce::ColourGradient sheen(juce::Colours::white.withAlpha(0.10f), cap.getCentreX(), cap.getY(),
                                   juce::Colours::white.withAlpha(0.0f), cap.getCentreX(), cap.getCentreY(), false);
        g.setGradientFill(sheen);
        g.fillEllipse(cap.reduced(cap.getWidth() * 0.06f));
    }
    g.setColour(edgeHi.withAlpha(hot ? 0.9f : 0.55f));
    g.drawEllipse(skirt.reduced(0.5f), 1.0f);

    // ---- pointer --------------------------------------------------------------------------
    const auto dir = juce::Point<float>(std::sin(angle), -std::cos(angle));
    const auto p0 = c + dir * (skirtR * 0.22f);
    const auto p1 = c + dir * (skirtR * 0.92f);
    juce::Path ptr;
    ptr.startNewSubPath(p0);
    ptr.lineTo(p1);
    g.setColour(! enabled ? textFaint : (hot ? juce::Colours::white : text));
    g.strokePath(ptr, juce::PathStrokeType(juce::jmax(1.6f, r * 0.07f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ============================================================================
void AmbientLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool hi, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const float h = juce::jmin(r.getHeight(), 18.0f);
    auto sw = r.removeFromLeft(h * 1.9f).withSizeKeepingCentre(h * 1.9f, h);
    const bool on = b.getToggleState();

    g.setColour(on ? uvDeep : bgDeep);
    g.fillRoundedRectangle(sw, h * 0.5f);
    g.setColour(on ? uv : (hi ? edgeHi : edge));
    g.drawRoundedRectangle(sw.reduced(0.5f), h * 0.5f, 1.0f);

    const auto knob = juce::Rectangle<float>(h - 5, h - 5)
                          .withCentre({ on ? sw.getRight() - h * 0.5f : sw.getX() + h * 0.5f, sw.getCentreY() });
    g.setColour(on ? juce::Colours::white : textDim);
    g.fillEllipse(knob);

    g.setColour(on ? text : textDim);
    g.setFont(body(12.5f, Weight::Medium));
    g.drawText(b.getButtonText(), r.withTrimmedLeft(9.0f), juce::Justification::centredLeft);
}

void AmbientLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                              bool hi, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = b.getToggleState();
    g.setColour(on ? uvDeep.withAlpha(0.9f) : (down ? panelHdr.brighter(0.1f) : (hi ? panelHdr : bgRaised)));
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(on ? uv : (hi ? edgeHi : edge));
    g.drawRoundedRectangle(r, 3.0f, 1.0f);
}

void AmbientLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool hi, bool)
{
    g.setFont(getTextButtonFont(b, b.getHeight()));
    g.setColour(b.getToggleState() ? juce::Colours::white : (hi ? text : textDim));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(4, 0), juce::Justification::centred, 1, 0.9f);
}

juce::Font AmbientLookAndFeel::getTextButtonFont(juce::TextButton&, int h)
{
    return body(juce::jlimit(11.0f, 13.0f, h * 0.46f), Weight::Medium, 0.03f);
}

void AmbientLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h)
{
    g.fillAll(panel);
    g.setColour(uv.withAlpha(0.5f));
    g.fillRect(0, 0, w, 1);
    g.setColour(edge);
    g.drawRect(0, 0, w, h);
}

juce::Font AmbientLookAndFeel::getPopupMenuFont() { return body(14.0f); }

void AmbientLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
{
    g.setFont(heading(12.0f, false, 0.06f));
    g.setColour(uvBright);
    g.drawFittedText(name.toUpperCase(), area.reduced(12, 0), juce::Justification::bottomLeft, 1);
}

juce::Rectangle<int> AmbientLookAndFeel::getTooltipBounds(const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    const auto f = body(13.0f);
    const int w = juce::roundToInt(textWidth(f, tip)) + 20, h = 26;
    return juce::Rectangle<int>(pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 24,
                                pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h)
        .constrainedWithin(parent);
}

void AmbientLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& tip, int w, int h)
{
    g.fillAll(panelHdr);
    g.setColour(edge);
    g.drawRect(0, 0, w, h);
    g.setColour(text);
    g.setFont(body(13.0f));
    g.drawText(tip, 10, 0, w - 20, h, juce::Justification::centredLeft);
}

juce::Label* AmbientLookAndFeel::createSliderTextBox(juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox(s);
    l->setColour(juce::Label::textColourId, text);
    l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setFont(body(12.0f));
    return l;
}
} // namespace ambient::ui
