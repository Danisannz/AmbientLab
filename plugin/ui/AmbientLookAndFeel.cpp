#include "AmbientLookAndFeel.h"

namespace ambient::ui
{
using namespace theme;

AmbientLookAndFeel::AmbientLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, bg);
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, purpleDeep);
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

void AmbientLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float a0, float a1, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).reduced(2.0f);
    const float r = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = a0 + pos * (a1 - a0);
    const bool hot = s.isMouseOverOrDragging() && s.isEnabled();

    // ---- value track -------------------------------------------------------
    const float trackR = r * 0.90f, trackW = juce::jmax(2.0f, r * 0.10f);
    juce::Path track;
    track.addCentredArc(c.x, c.y, trackR, trackR, 0.0f, a0, a1, true);
    g.setColour(bgDeep);
    g.strokePath(track, juce::PathStrokeType(trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = s.getProperties()[props::bipolar];
    const float from = bipolar ? (a0 + a1) * 0.5f : a0;
    if (std::abs(angle - from) > 0.001f)
    {
        juce::Path val;
        val.addCentredArc(c.x, c.y, trackR, trackR, 0.0f, juce::jmin(from, angle), juce::jmax(from, angle), true);
        const auto col = s.isEnabled() ? purple : textFaint;
        g.setColour(col.withAlpha(hot ? 0.30f : 0.18f)); // soft glow
        g.strokePath(val, juce::PathStrokeType(trackW * 2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(col);
        g.strokePath(val, juce::PathStrokeType(trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ---- modulation ring (lime) - shown only when a source is assigned ----
    const float mod = float(double(s.getProperties()[props::modDepth]));
    if (std::abs(mod) > 0.001f)
    {
        const float modR = r * 0.99f;
        const float end = juce::jlimit(a0, a1, angle + mod * (a1 - a0));
        juce::Path m;
        m.addCentredArc(c.x, c.y, modR, modR, 0.0f, juce::jmin(angle, end), juce::jmax(angle, end), true);
        g.setColour(lime);
        g.strokePath(m, juce::PathStrokeType(juce::jmax(1.5f, r * 0.05f)));
    }

    // ---- body: brushed metal cap -------------------------------------------
    const float bodyR = r * 0.70f;
    const auto body = juce::Rectangle<float>(bodyR * 2, bodyR * 2).withCentre(c);

    g.setColour(juce::Colours::black.withAlpha(0.55f)); // drop shadow
    g.fillEllipse(body.translated(0.0f, r * 0.06f).expanded(r * 0.03f));

    juce::ColourGradient metal(juce::Colour(0xff4a4e57), body.getX(), body.getY(),
                               juce::Colour(0xff17181c), body.getRight(), body.getBottom(), false);
    metal.addColour(0.45, juce::Colour(0xff2b2e34));
    g.setGradientFill(metal);
    g.fillEllipse(body);

    const auto cap = body.reduced(bodyR * 0.16f);
    juce::ColourGradient capG(juce::Colour(0xff33363d), cap.getCentreX(), cap.getY(),
                              juce::Colour(0xff1f2125), cap.getCentreX(), cap.getBottom(), false);
    g.setGradientFill(capG);
    g.fillEllipse(cap);

    g.setColour(edgeHi.withAlpha(hot ? 0.9f : 0.6f));
    g.drawEllipse(body.reduced(0.5f), 1.0f);

    // ---- pointer -----------------------------------------------------------
    const auto dir = juce::Point<float>(std::sin(angle), -std::cos(angle));
    const auto p0 = c + dir * (bodyR * 0.30f);
    const auto p1 = c + dir * (bodyR * 0.90f);
    g.setColour(s.isEnabled() ? text : textFaint);
    g.drawLine({ p0, p1 }, juce::jmax(1.5f, r * 0.07f));
}

void AmbientLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool hi, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const float h = juce::jmin(r.getHeight(), 18.0f);
    auto sw = r.removeFromLeft(h * 1.9f).withSizeKeepingCentre(h * 1.9f, h);
    const bool on = b.getToggleState();

    g.setColour(on ? purpleDeep : bgDeep);
    g.fillRoundedRectangle(sw, h * 0.5f);
    g.setColour(on ? purple : (hi ? edgeHi : edge));
    g.drawRoundedRectangle(sw.reduced(0.5f), h * 0.5f, 1.0f);

    const auto knob = juce::Rectangle<float>(h - 4, h - 4)
                          .withCentre({ on ? sw.getRight() - h * 0.5f : sw.getX() + h * 0.5f, sw.getCentreY() });
    g.setColour(on ? juce::Colours::white : textDim);
    g.fillEllipse(knob);

    g.setColour(on ? text : textDim);
    g.setFont(labelFont(10.5f));
    g.drawText(b.getButtonText().toUpperCase(), r.withTrimmedLeft(8.0f), juce::Justification::centredLeft);
}

void AmbientLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                              bool hi, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = b.getToggleState();
    g.setColour(on ? purpleDeep.withAlpha(0.85f) : (down ? panelHdr.brighter(0.1f) : (hi ? panelHdr : panel)));
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(on ? purple : (hi ? edgeHi : edge));
    g.drawRoundedRectangle(r, 3.0f, 1.0f);
}

juce::Font AmbientLookAndFeel::getTextButtonFont(juce::TextButton&, int h)
{
    return labelFont(juce::jlimit(9.0f, 12.0f, h * 0.42f));
}

void AmbientLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h)
{
    g.fillAll(panel);
    g.setColour(edge);
    g.drawRect(0, 0, w, h);
}

juce::Font AmbientLookAndFeel::getPopupMenuFont() { return font(13.0f); }

juce::Label* AmbientLookAndFeel::createSliderTextBox(juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox(s);
    l->setColour(juce::Label::textColourId, text);
    l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setFont(font(11.0f));
    return l;
}
} // namespace ambient::ui
