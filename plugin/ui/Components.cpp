#include "Components.h"

namespace ambient::ui
{
using namespace theme;

// ============================================================================
// RotaryKnob
// ============================================================================
RotaryKnob::RotaryKnob(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
                       const juce::String& displayName, bool bipolar)
    : name(displayName)
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
        slider.setTooltip(p->getName(64) + "  -  Shift-drag: fine  -  Double-click: reset");
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
    const int labelH = compact ? 16 : 18;
    r.removeFromTop(labelH);
    r.removeFromBottom(labelH);
    const int d = juce::jmin(r.getWidth(), r.getHeight());
    slider.setBounds(r.withSizeKeepingCentre(d, d));
}

void RotaryKnob::paint(juce::Graphics& g)
{
    auto r = getLocalBounds();
    const int labelH = compact ? 16 : 18;
    const bool hot = slider.isMouseOverOrDragging();

    g.setFont(body(compact ? 11.5f : 12.5f, Weight::Medium));
    g.setColour(hot ? text : textDim);
    g.drawFittedText(name, r.removeFromTop(labelH), juce::Justification::centred, 1, 0.85f);

    // Value readout in a subtle inset so it reads as an instrument display.
    auto vr = r.removeFromBottom(labelH);
    const auto val = slider.getTextFromValue(slider.getValue());
    const auto f = body(compact ? 11.5f : 12.0f, hot ? Weight::Medium : Weight::Regular);
    const float tw = juce::jmin(float(vr.getWidth()), textWidth(f, val) + 12.0f);
    auto pill = vr.toFloat().withSizeKeepingCentre(tw, float(labelH - 2));
    if (hot)
    {
        g.setColour(uvDeep.withAlpha(0.55f));
        g.fillRoundedRectangle(pill, 3.0f);
    }
    g.setFont(f);
    g.setColour(hot ? juce::Colours::white : text.withAlpha(0.85f));
    g.drawFittedText(val, vr, juce::Justification::centred, 1, 0.85f);
}

// ============================================================================
// Glyphs
// ============================================================================
void drawGlyph(juce::Graphics& g, Glyph kind, juce::Rectangle<float> r, juce::Colour c, float sw)
{
    if (kind == Glyph::None) return;
    r = r.withSizeKeepingCentre(juce::jmin(r.getWidth(), r.getHeight() * 1.6f), r.getHeight());
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    const float cy = r.getCentreY();
    juce::Path p;
    const juce::PathStrokeType st(sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    auto sine = [&](float x0, float x1, float amp, float cycles, float yc)
    {
        juce::Path s;
        for (int i = 0; i <= 64; ++i)
        {
            const float t = float(i) / 64.0f;
            const juce::Point<float> pt { x0 + (x1 - x0) * t, yc - amp * std::sin(t * cycles * juce::MathConstants<float>::twoPi) };
            if (i == 0) s.startNewSubPath(pt); else s.lineTo(pt);
        }
        return s;
    };

    g.setColour(c);
    switch (kind)
    {
        case Glyph::Oscillator:
        {
            g.strokePath(sine(x, x + w * 0.48f, h * 0.35f, 1.0f, cy), st);
            p.startNewSubPath(x + w * 0.52f, cy + h * 0.35f);
            p.lineTo(x + w, cy - h * 0.35f);
            p.lineTo(x + w, cy + h * 0.35f);
            g.strokePath(p, st);
            break;
        }
        case Glyph::Filter:
            p.startNewSubPath(x, cy - h * 0.15f);
            p.lineTo(x + w * 0.45f, cy - h * 0.15f);
            p.quadraticTo(x + w * 0.62f, cy - h * 0.55f, x + w * 0.68f, cy - h * 0.05f);
            p.quadraticTo(x + w * 0.80f, cy + h * 0.45f, x + w, cy + h * 0.45f);
            g.strokePath(p, st);
            break;
        case Glyph::Envelope:
            p.startNewSubPath(x, cy + h * 0.4f);
            p.lineTo(x + w * 0.18f, cy - h * 0.4f);
            p.lineTo(x + w * 0.38f, cy - h * 0.05f);
            p.lineTo(x + w * 0.72f, cy - h * 0.05f);
            p.lineTo(x + w, cy + h * 0.4f);
            g.strokePath(p, st);
            break;
        case Glyph::Input:
            g.drawEllipse(juce::Rectangle<float>(h * 0.7f, h * 0.7f).withCentre({ x + h * 0.4f, cy }), sw);
            p.startNewSubPath(x + h * 0.75f, cy);
            p.lineTo(x + w, cy);
            p.startNewSubPath(x + w - h * 0.25f, cy - h * 0.25f);
            p.lineTo(x + w, cy);
            p.lineTo(x + w - h * 0.25f, cy + h * 0.25f);
            g.strokePath(p, st);
            break;
        case Glyph::Grains:
        {
            juce::Random rng(7);
            for (int i = 0; i < 22; ++i)
            {
                const float gx = x + w * rng.nextFloat();
                const float spread = h * 0.45f * std::sin(juce::MathConstants<float>::pi * (gx - x) / w);
                const float gy = cy + (rng.nextFloat() * 2.0f - 1.0f) * spread;
                const float d = sw * (1.2f + 2.0f * rng.nextFloat());
                g.fillEllipse(gx - d * 0.5f, gy - d * 0.5f, d, d);
            }
            break;
        }
        case Glyph::Reverb:
            for (int i = 0; i < 7; ++i)
            {
                const float t = float(i) / 6.0f;
                const float lx = x + w * t;
                const float lh = h * 0.8f * std::exp(-2.6f * t);
                g.drawLine(lx, cy + h * 0.4f, lx, cy + h * 0.4f - lh, sw);
            }
            break;
        case Glyph::Delay:
            for (int i = 0; i < 5; ++i)
            {
                const float lx = x + w * float(i) / 4.0f;
                const float lh = h * 0.8f * std::pow(0.7f, float(i));
                g.drawLine(lx, cy + h * 0.4f, lx, cy + h * 0.4f - lh, sw * (i == 0 ? 1.6f : 1.0f));
            }
            break;
        case Glyph::Stereo:
            g.drawEllipse(juce::Rectangle<float>(h * 0.8f, h * 0.8f).withCentre({ r.getCentreX() - h * 0.25f, cy }), sw);
            g.drawEllipse(juce::Rectangle<float>(h * 0.8f, h * 0.8f).withCentre({ r.getCentreX() + h * 0.25f, cy }), sw);
            break;
        case Glyph::Lfo:
            g.strokePath(sine(x, x + w, h * 0.35f, 2.0f, cy), st);
            break;
        case Glyph::Matrix:
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 3; ++j)
                {
                    const auto cell = juce::Rectangle<float>(x + w * i / 4.0f, y + h * j / 3.0f, w / 4.0f, h / 3.0f).reduced(sw * 1.5f);
                    if ((i + j) % 3 == 0) g.fillRect(cell); else g.drawRect(cell, sw * 0.7f);
                }
            break;
        case Glyph::Sequencer:
            for (int i = 0; i < 8; ++i)
            {
                const float hh = h * (0.25f + 0.6f * std::abs(std::sin(float(i) * 1.7f)));
                auto bar = juce::Rectangle<float>(x + w * i / 8.0f, cy + h * 0.45f - hh, w / 8.0f, hh).reduced(sw, 0.0f);
                if (i % 3 == 0) g.fillRect(bar); else g.drawRect(bar, sw * 0.7f);
            }
            break;
        case Glyph::Synth:
            g.strokePath(sine(x, x + w, h * 0.3f, 1.5f, cy), st);
            g.drawLine(x, cy + h * 0.45f, x + w, cy + h * 0.45f, sw * 0.6f);
            break;
        case Glyph::None: break;
    }
}

// ============================================================================
// ModuleCard
// ============================================================================
ModuleCard::ModuleCard(const juce::String& t, Glyph gl) : title(t.toUpperCase()), glyph(gl) {}

void ModuleCard::setPlaceholder(const juce::String& description, int stg)
{
    placeholder = description;
    stage = stg;
    repaint();
}

juce::Rectangle<int> ModuleCard::getContentBounds() const
{
    return getLocalBounds().withTrimmedTop(int(cardHeader * scale)).reduced(int(10 * scale), int(6 * scale));
}

void ModuleCard::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    const float rad = cornerRadius * scale;
    const bool lit = active && ! isPlaceholder();

    // Body: clean graphite surface
    juce::ColourGradient bodyG(panelTop.withAlpha(0.96f), 0.0f, r.getY(), panel.withAlpha(0.96f), 0.0f, r.getBottom(), false);
    g.setGradientFill(bodyG);
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
        if (lit) // ultraviolet accent bar marks a live module
        {
            juce::ColourGradient acc(uv.withAlpha(0.9f), hdr.getX(), 0.0f, uv.withAlpha(0.0f), hdr.getX() + hdr.getWidth() * 0.45f, 0.0f, false);
            g.setGradientFill(acc);
            g.fillRect(hdr.withHeight(1.5f * scale));
        }
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRect(hdr.withY(hdr.getBottom() - 1.0f).withHeight(1.0f));
    }

    // Border + metallic corner brackets
    g.setColour(edge);
    g.drawRoundedRectangle(r, rad, 1.0f);
    {
        const float L = 7.0f * scale, i = 2.5f * scale;
        g.setColour(metal.withAlpha(0.28f));
        juce::Path br;
        auto corner = [&](float cx, float cy, float dx, float dy)
        {
            br.startNewSubPath(cx + dx * L, cy + dy * i);
            br.lineTo(cx + dx * i, cy + dy * i);
            br.lineTo(cx + dx * i, cy + dy * L);
        };
        corner(r.getX(), r.getBottom(), 1.0f, -1.0f);
        corner(r.getRight(), r.getBottom(), -1.0f, -1.0f);
        g.strokePath(br, juce::PathStrokeType(1.0f));
    }

    // Glyph + title
    float tx = hdr.getX() + 12.0f * scale;
    if (glyph != Glyph::None)
    {
        drawGlyph(g, glyph, juce::Rectangle<float>(tx, hdr.getY(), 18.0f * scale, hh).reduced(0.0f, hh * 0.32f),
                  lit ? lime : textFaint, 1.2f);
        tx += 26.0f * scale;
    }
    g.setColour(isPlaceholder() ? textDim : text);
    g.setFont(heading(13.0f * scale, false, 0.07f));
    g.drawText(title, juce::Rectangle<float>(tx, hdr.getY(), hdr.getRight() - tx - 8.0f, hh), juce::Justification::centredLeft);

    // Status LED at the right of the header
    if (headerLed)
    {
    const float led = 6.0f * scale;
    const auto ledR = juce::Rectangle<float>(led, led).withCentre({ hdr.getRight() - 14.0f * scale, hdr.getCentreY() });
    if (lit) { g.setColour(uv.withAlpha(0.3f)); g.fillEllipse(ledR.expanded(2.5f * scale)); }
    g.setColour(lit ? uv : edgeHi.withAlpha(0.6f));
    g.fillEllipse(ledR);
    }

    // Placeholder: glyph, description, stage badge. No controls.
    if (isPlaceholder())
    {
        auto c = getContentBounds().toFloat().reduced(4.0f * scale);
        const float gh = juce::jlimit(18.0f, 44.0f * scale, c.getHeight() * 0.22f);
        const float blockH = gh + 16.0f * scale + 40.0f * scale;
        auto block = c.withSizeKeepingCentre(c.getWidth(), juce::jmin(c.getHeight(), blockH));

        drawGlyph(g, glyph, block.removeFromTop(gh), edgeHi, 1.4f * scale);
        block.removeFromTop(10.0f * scale);

        g.setFont(body(12.5f * scale));
        g.setColour(textDim);
        g.drawFittedText(placeholder, block.removeFromTop(34.0f * scale).toNearestInt(), juce::Justification::centredTop, 2, 0.9f);

        if (stage > 0)
        {
            const auto label = "STAGE " + juce::String(stage);
            const auto f = caption(10.5f * scale);
            const float bw = textWidth(f, label) + 16.0f * scale;
            auto badge = juce::Rectangle<float>(bw, 18.0f * scale).withCentre({ c.getCentreX(), block.getY() + 9.0f * scale });
            g.setColour(uvDeep.withAlpha(0.45f));
            g.fillRoundedRectangle(badge, 9.0f * scale);
            g.setColour(uv.withAlpha(0.7f));
            g.drawRoundedRectangle(badge, 9.0f * scale, 1.0f);
            g.setColour(uvBright);
            g.setFont(f);
            g.drawText(label, badge, juce::Justification::centred);
        }
    }
}

juce::Rectangle<int> ModuleCard::getHeaderControlBounds() const
{
    const int hh = int(cardHeader * scale);
    const int sz = hh - int(8 * scale);
    return { getWidth() - int(8 * scale) - sz, (hh - sz) / 2, sz, sz };
}

// ============================================================================
// BypassButton
// ============================================================================
BypassButton::BypassButton(const juce::String& moduleName) : ToggleButton("Bypass " + moduleName)
{
    setTooltip("Bypass " + moduleName.toLowerCase());
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void BypassButton::paintButton(juce::Graphics& g, bool hi, bool down)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    const bool bypassed = getToggleState();
    const auto col = bypassed ? (hi ? theme::textDim : theme::textFaint) : theme::lime;

    if (hi || down)
    {
        g.setColour(theme::panelTop.brighter(down ? 0.15f : 0.08f));
        g.fillRoundedRectangle(r, 3.0f);
    }
    if (! bypassed)
    {
        g.setColour(theme::lime.withAlpha(0.12f));
        g.fillEllipse(r.reduced(r.getWidth() * 0.12f));
    }

    // power symbol: open ring + vertical stroke
    const auto c = r.getCentre();
    const float R = r.getWidth() * 0.27f;
    juce::Path ring;
    ring.addCentredArc(c.x, c.y + R * 0.08f, R, R, 0.0f, juce::degreesToRadians(35.0f), juce::degreesToRadians(325.0f), true);
    ring.startNewSubPath(c.x, c.y - R * 1.15f);
    ring.lineTo(c.x, c.y - R * 0.1f);
    g.setColour(col);
    g.strokePath(ring, juce::PathStrokeType(juce::jmax(1.3f, r.getWidth() * 0.08f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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

int TabBar::tabWidth() const { return juce::jmin(168, getWidth() / juce::jmax(1, names.size())); }

int TabBar::indexAt(juce::Point<int> p) const
{
    const int x = p.x - 8;
    if (x < 0) return -1;
    const int i = x / juce::jmax(1, tabWidth());
    return i < names.size() ? i : -1;
}

void TabBar::mouseDown(const juce::MouseEvent& e) { if (auto i = indexAt(e.getPosition()); i >= 0) setSelected(i); }
void TabBar::mouseMove(const juce::MouseEvent& e) { const int h = indexAt(e.getPosition()); if (h != hovered) { hovered = h; repaint(); } }
void TabBar::mouseExit(const juce::MouseEvent&)   { hovered = -1; repaint(); }

void TabBar::paint(juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour(bg.withAlpha(0.85f));
    g.fillRect(r);
    g.setColour(edge);
    g.drawHorizontalLine(r.getBottom() - 1, 0.0f, float(r.getRight()));

    const int w = tabWidth();
    g.setFont(heading(14.0f, false, 0.10f));
    for (int i = 0; i < names.size(); ++i)
    {
        auto cell = juce::Rectangle<int>(8 + i * w, 0, w, r.getHeight());
        const bool sel = i == selected;
        if (sel)
        {
            juce::ColourGradient glow(uv.withAlpha(0.18f), 0.0f, float(cell.getBottom()),
                                      uv.withAlpha(0.0f), 0.0f, float(cell.getY()), false);
            g.setGradientFill(glow);
            g.fillRect(cell);
            g.setColour(uv);
            g.fillRect(cell.withTop(cell.getBottom() - 2).reduced(14, 0));
        }
        // index numeral, modular-hardware style
        g.setFont(body(10.5f, Weight::Medium));
        g.setColour(sel ? uvBright : textFaint);
        const auto num = juce::String(i + 1).paddedLeft('0', 2);
        const auto f = heading(14.0f, false, 0.10f);
        const float tw = textWidth(f, names[i]);
        const float total = tw + 22.0f;
        const float x0 = cell.getCentreX() - total * 0.5f;
        g.drawText(num, juce::Rectangle<float>(x0, 0.0f, 18.0f, float(r.getHeight())), juce::Justification::centredLeft);
        g.setFont(f);
        g.setColour(sel ? text : (i == hovered ? text.withAlpha(0.85f) : textDim));
        g.drawText(names[i], juce::Rectangle<float>(x0 + 22.0f, 0.0f, tw + 4.0f, float(r.getHeight())), juce::Justification::centredLeft);
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
    g.setFont(heading(juce::jlimit(11.0f, 13.0f, r.getHeight() * 0.5f), false, 0.08f));
    for (int i = 0; i < names.size(); ++i)
    {
        auto cell = juce::Rectangle<float>(r.getX() + i * w, r.getY(), w, r.getHeight());
        if (i == selected)
        {
            g.setColour(uvDeep);
            g.fillRoundedRectangle(cell.reduced(2.0f), 2.5f);
            g.setColour(uv);
            g.drawRoundedRectangle(cell.reduced(2.0f), 2.5f, 1.0f);
        }
        else if (i > 0 && i != selected + 1)
        {
            g.setColour(edge);
            g.drawVerticalLine(int(cell.getX()), cell.getY() + 5.0f, cell.getBottom() - 5.0f);
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
    g.setColour(juce::Colours::black);
    g.fillEllipse(led);
    g.setColour(lime.withAlpha(0.12f + 0.88f * level));
    g.fillEllipse(led.reduced(1.0f));
    g.setColour(level > 0.05f ? text : textDim);
    g.setFont(caption(11.0f));
    g.drawText(label, r.withTrimmedLeft(d * 2.3f), juce::Justification::centredLeft);
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
        const float prev = c.db, prevHold = c.hold;
        c.db = db > c.db ? db : juce::jmax(db, c.db - dt * 26.0f); // ~26 dB/s fall
        if (c.db >= c.hold) { c.hold = c.db; c.holdTime = 1.5f; }
        else if ((c.holdTime -= dt) <= 0.0f) c.hold = juce::jmax(c.db, c.hold - dt * 15.0f);
        changed |= std::abs(prev - c.db) > 0.05f || std::abs(prevHold - c.hold) > 0.05f;
    }
    if (changed) repaint();
}

void LevelMeter::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(r, 2.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(r.reduced(0.5f), 2.0f, 1.0f);
    const float pad = 2.0f, gapW = 2.0f;
    const float barW = (r.getWidth() - pad * 2 - gapW) * 0.5f;
    const float top = r.getY() + pad, h = r.getHeight() - pad * 2;

    juce::ColourGradient grad(limeDeep, 0.0f, top + h, clip, 0.0f, top, false);
    grad.addColour(1.0 - (6.0 / 66.0) - 0.08, lime);
    grad.addColour(1.0 - (6.0 / 66.0), warn);

    for (int i = 0; i < 2; ++i)
    {
        const float x = r.getX() + pad + i * (barW + gapW);
        // unlit segments
        g.setColour(grid);
        g.fillRect(juce::Rectangle<float>(x, top, barW, h));
        const float y = toY(ch[i].db, top, h);
        g.setGradientFill(grad);
        g.fillRect(juce::Rectangle<float>(x, y, barW, top + h - y));
        if (ch[i].hold > -60.0f)
        {
            g.setColour(ch[i].hold > 0.0f ? clip : text);
            g.fillRect(juce::Rectangle<float>(x, toY(ch[i].hold, top, h), barW, 1.5f));
        }
    }
    // segment gaps for a hardware LED-ladder feel
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    for (float yy = top + 3.0f; yy < top + h; yy += 3.0f)
        g.fillRect(r.getX() + pad, yy, r.getWidth() - pad * 2, 1.0f);
    // 0 dB tick
    g.setColour(text.withAlpha(0.6f));
    g.drawHorizontalLine(int(toY(0.0f, top, h)), r.getX(), r.getRight());
}

// ============================================================================
// CorrelationMeter
// ============================================================================
void CorrelationMeter::setCorrelation(float c, float dt)
{
    const float prev = shown;
    const float k = 1.0f - std::exp(-dt / 0.12f); // ~120 ms ballistics
    shown += (juce::jlimit(-1.0f, 1.0f, c) - shown) * k;
    if (shown <= minHold) { minHold = shown; minHoldTime = 2.0f; }
    else if ((minHoldTime -= dt) <= 0.0f) minHold = juce::jmin(shown, minHold + dt * 0.5f);
    if (std::abs(prev - shown) > 0.002f) repaint();
}

void CorrelationMeter::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    juce::Rectangle<float> lbl;
    if (labels) lbl = r.removeFromBottom(14.0f);

    auto bar = r.reduced(0.0f, 1.0f);
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(bar, 2.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(bar.reduced(0.5f), 2.0f, 1.0f);

    const auto inner = bar.reduced(2.0f);
    auto xOf = [&](float v) { return inner.getX() + (v + 1.0f) * 0.5f * inner.getWidth(); };
    const float cx = xOf(0.0f), vx = xOf(shown);

    // tick marks at -1, -0.5, 0, 0.5, 1
    g.setColour(gridHi);
    for (float v : { -0.5f, 0.5f }) g.drawVerticalLine(int(xOf(v)), inner.getY(), inner.getBottom());

    const auto col = shown >= 0.0f ? lime : magenta;
    juce::ColourGradient fill(col.withAlpha(0.25f), cx, 0.0f, col, vx, 0.0f, false);
    g.setGradientFill(fill);
    g.fillRect(juce::Rectangle<float>(juce::jmin(cx, vx), inner.getY(), std::abs(vx - cx), inner.getHeight()));

    // minimum-hold marker (shows transient phase problems)
    if (minHold < shown - 0.02f)
    {
        g.setColour(minHold < 0.0f ? magenta : textDim);
        g.fillRect(xOf(minHold) - 0.75f, inner.getY(), 1.5f, inner.getHeight());
    }

    g.setColour(text.withAlpha(0.8f));
    g.fillRect(cx - 0.5f, bar.getY(), 1.0f, bar.getHeight());

    if (labels)
    {
        g.setFont(body(10.5f));
        g.setColour(textFaint);
        g.drawText("-1", lbl, juce::Justification::centredLeft);
        g.drawText("+1", lbl, juce::Justification::centredRight);
        g.setColour(shown < 0.0f ? magenta : textDim);
        g.drawText("CORR " + juce::String(shown, 2), lbl, juce::Justification::centred);
    }
}
} // namespace ambient::ui
