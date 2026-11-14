#include "AnalyzerView.h"

namespace ambient::ui
{
using namespace theme;

ControlChip::ControlChip(const juce::String& l, juce::StringArray c, int initial)
    : label(l), choices(std::move(c)), choice(juce::jlimit(0, juce::jmax(0, choices.size() - 1), initial))
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

ControlChip::ControlChip(const juce::String& l, float mn, float mx, float initial, float def,
                         std::function<juce::String(float)> fmt, bool logarithmic)
    : label(l), value(juce::jlimit(mn, mx, initial)), minV(mn), maxV(mx), defV(def),
      continuous(true), logScale(logarithmic), format(std::move(fmt))
{
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

float ControlChip::toNorm(float v) const
{
    if (logScale && minV > 0.0f) return std::log(v / minV) / std::log(maxV / minV);
    return (v - minV) / (maxV - minV);
}

float ControlChip::fromNorm(float n) const
{
    n = juce::jlimit(0.0f, 1.0f, n);
    if (logScale && minV > 0.0f) return minV * std::pow(maxV / minV, n);
    return minV + n * (maxV - minV);
}

juce::String ControlChip::valueText() const
{
    if (continuous) return format ? format(value) : juce::String(value, 2);
    return choices[choice];
}

void ControlChip::setChoice(int i, bool notify)
{
    i = juce::jlimit(0, juce::jmax(0, choices.size() - 1), i);
    if (i == choice) return;
    choice = i;
    repaint();
    if (notify && onChoice) onChoice(choice);
}

void ControlChip::setValue(float v, bool notify)
{
    v = juce::jlimit(minV, maxV, v);
    if (v == value) return;
    value = v;
    repaint();
    if (notify && onValue) onValue(value);
}

int ControlChip::getPreferredWidth() const
{
    const float lw = textWidth(caption(10.5f), label.toUpperCase());
    float vw = 0.0f;
    const auto vf = body(12.5f, Weight::Medium);
    if (continuous) vw = juce::jmax(textWidth(vf, format ? format(minV) : "0.00"), textWidth(vf, format ? format(maxV) : "0.00"),
                                    textWidth(vf, valueText()));
    else for (auto& c : choices) vw = juce::jmax(vw, textWidth(vf, c));
    return juce::roundToInt(lw + vw + (continuous ? 30.0f : 36.0f));
}

void ControlChip::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    const bool hot = isMouseOver() || dragging;
    g.setColour(hot ? panelHdr : bgRaised);
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(hot ? edgeHi : edge);
    g.drawRoundedRectangle(r, 3.0f, 1.0f);

    auto t = r.reduced(8.0f, 0.0f);
    const auto lf = caption(10.5f);
    const auto lbl = label.toUpperCase();
    g.setFont(lf);
    g.setColour(textFaint);
    g.drawText(lbl, t.removeFromLeft(textWidth(lf, lbl) + 6.0f), juce::Justification::centredLeft);

    if (! continuous)
    {
        // small caret
        auto caret = t.removeFromRight(8.0f);
        juce::Path p;
        const auto cc = caret.getCentre();
        p.addTriangle(cc.x - 3.0f, cc.y - 1.5f, cc.x + 3.0f, cc.y - 1.5f, cc.x, cc.y + 2.0f);
        g.setColour(hot ? text : textDim);
        g.fillPath(p);
    }
    else if (dragging || hot)
    {
        // value bar along the bottom edge
        const float n = juce::jlimit(0.0f, 1.0f, toNorm(value));
        g.setColour(uv.withAlpha(0.8f));
        g.fillRect(r.getX() + 2.0f, r.getBottom() - 2.5f, (r.getWidth() - 4.0f) * n, 1.5f);
    }

    g.setFont(body(12.5f, Weight::Medium));
    g.setColour(hot ? juce::Colours::white : text);
    g.drawText(valueText(), t, juce::Justification::centredRight);
}

void ControlChip::mouseDown(const juce::MouseEvent& e)
{
    if (! continuous)
    {
        juce::PopupMenu m;
        for (int i = 0; i < choices.size(); ++i) m.addItem(i + 1, choices[i], true, i == choice);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                        [safe = juce::Component::SafePointer<ControlChip>(this)](int r)
                        { if (safe != nullptr && r > 0) safe->setChoice(r - 1, true); });
        return;
    }
    dragging = true;
    dragStartNorm = toNorm(value);
    juce::ignoreUnused(e);
    repaint();
}

void ControlChip::mouseDrag(const juce::MouseEvent& e)
{
    if (! continuous) return;
    const float sens = e.mods.isShiftDown() ? 1200.0f : 200.0f;
    setValue(fromNorm(dragStartNorm - float(e.getDistanceFromDragStartY()) / sens), true);
}

void ControlChip::mouseUp(const juce::MouseEvent&) { dragging = false; repaint(); }

void ControlChip::mouseDoubleClick(const juce::MouseEvent&)
{
    if (continuous) setValue(defV, true);
}

void ControlChip::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (continuous)
        setValue(fromNorm(toNorm(value) + w.deltaY * (e.mods.isShiftDown() ? 0.01f : 0.05f)), true);
    else
        setChoice(choice + (w.deltaY < 0.0f ? 1 : -1), true);
}

static int chipWidth(juce::Component* c)
{
    if (auto* chip = dynamic_cast<ControlChip*>(c)) return chip->getPreferredWidth();
    return c->getWidth() > 0 ? c->getWidth() : 80;
}

int chipRowsNeeded(int width, const std::vector<juce::Component*>& chips, int gap)
{
    int rows = 1, x = 0;
    for (auto* c : chips)
    {
        const int w = chipWidth(c);
        if (x > 0 && x + w > width) { ++rows; x = 0; }
        x += w + gap;
    }
    return rows;
}

void layoutChips(juce::Rectangle<int> area, const std::vector<juce::Component*>& chips, int rowHeight, int gap)
{
    int x = area.getX(), y = area.getY();
    for (auto* c : chips)
    {
        const int w = juce::jmin(chipWidth(c), area.getWidth());
        if (x > area.getX() && x + w > area.getRight()) { x = area.getX(); y += rowHeight + gap / 2; }
        const bool fits = y + rowHeight <= area.getBottom();
        c->setVisible(fits);
        if (fits) c->setBounds(x, y, w, rowHeight);
        x += w + gap;
    }
}
} // namespace ambient::ui
