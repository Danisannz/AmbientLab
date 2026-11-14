#include "Components.h"

namespace ambient::ui
{
using namespace theme;

// ============================================================================
// RotaryKnob
// ============================================================================
RotaryKnob::RotaryKnob(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
                       const juce::String& displayName, bool bipolar)
    : name(displayName.toUpperCase())
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters(juce::degreesToRadians(-135.0f), juce::degreesToRadians(135.0f), true);
    slider.setMouseDragSensitivity(220);
    slider.getProperties().set(props::bipolar, bipolar);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible(slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, paramID, slider);
    if (auto* p = apvts.getParameter(paramID))
    {
        slider.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
        slider.setTooltip(p->getName(64));
    }
}

void RotaryKnob::FineSlider::mouseDown(const juce::MouseEvent& e)
{
    setMouseDragSensitivity(e.mods.isShiftDown() ? 1200 : 220);
    Slider::mouseDown(e);
}

void RotaryKnob::FineSlider::mouseDrag(const juce::MouseEvent& e)
{
    setMouseDragSensitivity(e.mods.isShiftDown() ? 1200 : 220);
    Slider::mouseDrag(e);
}

void RotaryKnob::resized()
{
    auto r = getLocalBounds();
    const int labelH = compact ? 13 : 15;
    r.removeFromTop(labelH);
    r.removeFromBottom(labelH);
    slider.setBounds(r.withSizeKeepingCentre(juce::jmin(r.getWidth(), r.getHeight()),
                                             juce::jmin(r.getWidth(), r.getHeight())));
}

void RotaryKnob::paint(juce::Graphics& g)
{
    auto r = getLocalBounds();
    const int labelH = compact ? 13 : 15;
    const float fs = compact ? 9.5f : 10.5f;
    const bool hot = slider.isMouseOverOrDragging();

    g.setFont(labelFont(fs));
    g.setColour(hot ? text : textDim);
    g.drawFittedText(name, r.removeFromTop(labelH), juce::Justification::centred, 1, 0.8f);

    g.setFont(font(fs + 0.5f));
    g.setColour(hot ? purple.brighter(0.3f) : text.withAlpha(0.85f));
    g.drawFittedText(slider.getTextFromValue(slider.getValue()), r.removeFromBottom(labelH),
                     juce::Justification::centred, 1, 0.8f);
}

// ============================================================================
// ModuleCard
// ============================================================================
ModuleCard::ModuleCard(const juce::String& t) : title(t.toUpperCase()) {}

juce::Rectangle<int> ModuleCard::getContentBounds() const
{
    return getLocalBounds().withTrimmedTop(int(cardHeader * scale)).reduced(int(8 * scale), int(6 * scale));
}

void ModuleCard::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    const float rad = cornerRadius * scale;

    // Body
    juce::ColourGradient body(panelTop, 0.0f, r.getY(), panel, 0.0f, r.getBottom(), false);
    g.setGradientFill(body);
    g.fillRoundedRectangle(r, rad);

    // Header strip
    const float hh = cardHeader * scale;
    auto hdr = r.withHeight(hh);
    {
        juce::Graphics::ScopedSaveState s(g);
        juce::Path cardShape; cardShape.addRoundedRectangle(r, rad);
        g.reduceClipRegion(cardShape);
        g.setColour(panelHdr);
        g.fillRect(hdr);
        g.setColour(bgDeep.withAlpha(0.8f));
        g.drawHorizontalLine(int(hdr.getBottom()), r.getX(), r.getRight());
    }

    // Metallic border: brighter on top edge
    g.setColour(edge);
    g.drawRoundedRectangle(r, rad, 1.0f);
    g.setColour(edgeHi.withAlpha(0.35f));
    g.drawHorizontalLine(int(r.getY()) + 1, r.getX() + rad, r.getRight() - rad);

    // LED + title
    const float led = 6.0f * scale;
    const auto ledR = juce::Rectangle<float>(led, led).withCentre({ hdr.getX() + 12.0f * scale, hdr.getCentreY() });
    const bool lit = active && ! isPlaceholder();
    if (lit) { g.setColour(purple.withAlpha(0.35f)); g.fillEllipse(ledR.expanded(2.5f * scale)); }
    g.setColour(lit ? purple : textFaint.withAlpha(0.5f));
    g.fillEllipse(ledR);

    g.setColour(isPlaceholder() ? textDim : text);
    g.setFont(labelFont(11.0f * scale));
    g.drawText(title, hdr.withTrimmedLeft(22.0f * scale), juce::Justification::centredLeft);

    // Placeholder body: no controls, just an honest note.
    if (isPlaceholder())
    {
        auto c = getContentBounds().toFloat().reduced(6.0f * scale);
        juce::Path dash;
        dash.addRoundedRectangle(c, 4.0f * scale);
        juce::Path dashed;
        const float d[] = { 3.0f * scale, 4.0f * scale };
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, dash, d, 2);
        g.setColour(edge);
        g.fillPath(dashed);

        g.setColour(textFaint);
        g.setFont(labelFont(10.0f * scale));
        g.drawFittedText(placeholder.toUpperCase(), c.toNearestInt().reduced(int(8 * scale)),
                         juce::Justification::centred, 3);
    }
}

// ============================================================================
// TabBar
// ============================================================================
TabBar::TabBar(juce::StringArray n) : names(std::move(n)) {}

void TabBar::setSelected(int i, bool notify)
{
    i = juce::jlimit(0, names.size() - 1, i);
    if (i == selected) return;
    selected = i;
    repaint();
    if (notify && onChange) onChange(selected);
}

int TabBar::indexAt(juce::Point<int> p) const
{
    if (names.isEmpty()) return -1;
    const int w = juce::jmin(150, getWidth() / names.size());
    const int i = p.x / juce::jmax(1, w);
    return i < names.size() ? i : -1;
}

void TabBar::mouseDown(const juce::MouseEvent& e)  { if (auto i = indexAt(e.getPosition()); i >= 0) setSelected(i); }
void TabBar::mouseMove(const juce::MouseEvent& e)  { const int h = indexAt(e.getPosition()); if (h != hovered) { hovered = h; repaint(); } }
void TabBar::mouseExit(const juce::MouseEvent&)    { hovered = -1; repaint(); }

void TabBar::paint(juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour(bg);
    g.fillRect(r);
    g.setColour(edge);
    g.drawHorizontalLine(r.getBottom() - 1, 0.0f, float(r.getRight()));

    const int w = juce::jmin(150, r.getWidth() / juce::jmax(1, names.size()));
    g.setFont(labelFont(juce::jlimit(10.0f, 14.0f, r.getHeight() * 0.36f)));
    for (int i = 0; i < names.size(); ++i)
    {
        auto cell = juce::Rectangle<int>(i * w, 0, w, r.getHeight());
        const bool sel = i == selected;
        if (sel)
        {
            juce::ColourGradient glow(purple.withAlpha(0.16f), 0.0f, float(cell.getBottom()),
                                      purple.withAlpha(0.0f), 0.0f, float(cell.getY()), false);
            g.setGradientFill(glow);
            g.fillRect(cell);
            g.setColour(purple);
            g.fillRect(cell.removeFromBottom(2).reduced(10, 0));
        }
        g.setColour(sel ? text : (i == hovered ? text.withAlpha(0.8f) : textDim));
        g.drawText(names[i], juce::Rectangle<int>(i * w, 0, w, r.getHeight()), juce::Justification::centred);
    }
}

// ============================================================================
// SegmentedControl
// ============================================================================
SegmentedControl::SegmentedControl(juce::StringArray n) : names(std::move(n)) {}

void SegmentedControl::setSelected(int i, bool notify)
{
    i = juce::jlimit(0, names.size() - 1, i);
    if (i == selected) return;
    selected = i;
    repaint();
    if (notify && onChange) onChange(selected);
}

void SegmentedControl::mouseDown(const juce::MouseEvent& e)
{
    if (names.isEmpty()) return;
    setSelected(e.x * names.size() / juce::jmax(1, getWidth()));
}

void SegmentedControl::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(bgDeep);
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(r, 3.0f, 1.0f);

    const float w = r.getWidth() / float(juce::jmax(1, names.size()));
    g.setFont(labelFont(juce::jlimit(9.0f, 11.5f, r.getHeight() * 0.48f)));
    for (int i = 0; i < names.size(); ++i)
    {
        auto cell = juce::Rectangle<float>(r.getX() + i * w, r.getY(), w, r.getHeight());
        if (i == selected)
        {
            g.setColour(purpleDeep);
            g.fillRoundedRectangle(cell.reduced(2.0f), 2.5f);
            g.setColour(purple);
            g.drawRoundedRectangle(cell.reduced(2.0f), 2.5f, 1.0f);
        }
        g.setColour(i == selected ? juce::Colours::white : textDim);
        g.drawText(names[i], cell, juce::Justification::centred);
    }
}

// ============================================================================
// StatusLed
// ============================================================================
StatusLed::StatusLed(const juce::String& l) : label(l.toUpperCase()) {}

void StatusLed::tick(float dt)
{
    level = juce::jmax(0.0f, level - dt * 4.0f);
    if (std::abs(level - shown) > 0.02f) { shown = level; repaint(); }
}

void StatusLed::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float d = juce::jmin(8.0f, r.getHeight() * 0.5f);
    auto led = juce::Rectangle<float>(d, d).withCentre({ r.getX() + d, r.getCentreY() });
    if (level > 0.01f)
    {
        g.setColour(lime.withAlpha(0.35f * level));
        g.fillEllipse(led.expanded(3.0f));
    }
    g.setColour(bgDeep);
    g.fillEllipse(led);
    g.setColour(lime.withAlpha(0.15f + 0.85f * level));
    g.fillEllipse(led.reduced(1.0f));
    g.setColour(textDim);
    g.setFont(labelFont(10.0f));
    g.drawText(label, r.withTrimmedLeft(d * 2.2f), juce::Justification::centredLeft);
}

// ============================================================================
// LevelMeter
// ============================================================================
float LevelMeter::toY(float db, float top, float height)
{
    const float t = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 66.0f);
    return top + height * (1.0f - t);
}

void LevelMeter::setLevels(float linL, float linR, float dt)
{
    const float in[2] = { linL, linR };
    bool changed = false;
    for (int i = 0; i < 2; ++i)
    {
        const float db = juce::Decibels::gainToDecibels(in[i], -100.0f);
        auto& c = ch[i];
        const float prev = c.db;
        c.db = db > c.db ? db : juce::jmax(db, c.db - dt * 26.0f); // ~26 dB/s fall
        if (c.db >= c.hold) { c.hold = c.db; c.holdTime = 1.5f; }
        else if ((c.holdTime -= dt) <= 0.0f) c.hold = juce::jmax(c.db, c.hold - dt * 15.0f);
        changed |= std::abs(prev - c.db) > 0.05f || c.holdTime > 0.0f;
    }
    if (changed) repaint();
}

void LevelMeter::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(bgDeep);
    g.fillRoundedRectangle(r, 2.0f);
    const float pad = 2.0f, gapW = 2.0f;
    const float barW = (r.getWidth() - pad * 2 - gapW) * 0.5f;
    const float top = r.getY() + pad, h = r.getHeight() - pad * 2;

    juce::ColourGradient grad(limeDeep, 0.0f, top + h, clip, 0.0f, top, false);
    grad.addColour(1.0 - (6.0 / 66.0) - 0.06, lime);
    grad.addColour(1.0 - (6.0 / 66.0), warn);

    for (int i = 0; i < 2; ++i)
    {
        const float x = r.getX() + pad + i * (barW + gapW);
        const float y = toY(ch[i].db, top, h);
        g.setGradientFill(grad);
        g.fillRect(juce::Rectangle<float>(x, y, barW, top + h - y));
        if (ch[i].hold > -60.0f)
        {
            g.setColour(ch[i].hold > 0.0f ? clip : text);
            g.fillRect(juce::Rectangle<float>(x, toY(ch[i].hold, top, h), barW, 1.5f));
        }
    }
    // 0 dB tick
    g.setColour(textFaint);
    g.drawHorizontalLine(int(toY(0.0f, top, h)), r.getX(), r.getRight());
}
} // namespace ambient::ui
