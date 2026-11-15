#include "ModulationPanels.h"
#include "Fonts.h"
#include "../ModDestinations.h"
#include <ambient/dsp/Lfo.h>

namespace ambient::ui
{
using namespace theme;

namespace
{
juce::String lfoId(int i, const char* f) { return "lfo." + juce::String(i + 1) + "." + f; }
std::unique_ptr<RotaryKnob> makeKnob(juce::Component& parent, juce::AudioProcessorValueTreeState& s, const juce::String& pid, const juce::String& name, bool bip = false)
{
    auto k = std::make_unique<RotaryKnob>(s, pid, name, bip);
    k->setCompact(true);
    parent.addAndMakeVisible(*k);
    return k;
}
} // namespace

// ---------------------------------------------------------------- LfoDisplay
LfoDisplay::LfoDisplay(AmbientProcessor& p, int i) : proc(p), index(i)
{
    for (int k = 0; k < 8; ++k) pts[size_t(k)] = p.apvts.getParameter(lfoId(i, ("pt" + juce::String(k + 1)).toRawUTF8()));
    setTooltip("LFO shape. Custom: drag the 8 points.");
}

bool LfoDisplay::isCustom() const
{
    const auto* w = proc.apvts.getRawParameterValue(lfoId(index, "wave"));
    return w && juce::roundToInt(w->load()) == int(ambient::dsp::Lfo::Wave::Custom);
}

void LfoDisplay::refresh()
{
    phase = proc.lfoPhaseOut[size_t(index)].load(std::memory_order_relaxed);
    value = proc.lfoOut[size_t(index)].load(std::memory_order_relaxed);
    repaint();
}

void LfoDisplay::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(bgDeep); g.fillRoundedRectangle(r, 4.0f);
    g.setColour(grid); g.drawHorizontalLine(int(r.getCentreY()), r.getX(), r.getRight());
    auto& s = proc.apvts;
    ambient::dsp::Lfo::Params p;
    p.wave = ambient::dsp::Lfo::Wave(juce::roundToInt(s.getRawParameterValue(lfoId(index, "wave"))->load()));
    p.shape = s.getRawParameterValue(lfoId(index, "shape"))->load();
    for (int k = 0; k < 8; ++k) p.custom[size_t(k)] = s.getRawParameterValue(lfoId(index, ("pt" + juce::String(k + 1)).toRawUTF8()))->load();
    const bool deterministic = int(p.wave) < int(ambient::dsp::Lfo::Wave::SmoothRandom) || p.wave == ambient::dsp::Lfo::Wave::Custom;
    auto yOf = [&](float v) { return r.getCentreY() - v * (r.getHeight() * 0.42f); };
    juce::Path path;
    if (deterministic)
    {
        for (int x = 0; x <= int(r.getWidth()); x += 2)
        {
            const float v = ambient::dsp::Lfo::shapeAt(p, float(x) / r.getWidth());
            const float px = r.getX() + float(x), py = yOf(v);
            if (x == 0) path.startNewSubPath(px, py); else path.lineTo(px, py);
        }
        g.setColour(uvBright.withAlpha(0.9f));
        g.strokePath(path, juce::PathStrokeType(1.5f));
    }
    else
    {
        g.setColour(textFaint); g.setFont(caption(10.0f));
        g.drawText("RANDOM / CHAOTIC", r, juce::Justification::centred);
    }
    if (p.wave == ambient::dsp::Lfo::Wave::Custom)
        for (int k = 0; k < 8; ++k)
        {
            const float px = r.getX() + r.getWidth() * float(k) / 8.0f, py = yOf(p.custom[size_t(k)]);
            g.setColour(dragPoint == k ? lime : uvBright);
            g.fillEllipse(px - 3.5f, py - 3.5f, 7.0f, 7.0f);
        }
    // live position (bipolar output assumed; unipolar maps 0..1 -> -1..1 for display)
    const bool unip = s.getRawParameterValue(lfoId(index, "unipolar"))->load() > 0.5f;
    const float dv = unip ? value * 2.0f - 1.0f : value;
    g.setColour(lime);
    g.fillEllipse(r.getX() + phase * r.getWidth() - 3.0f, yOf(dv) - 3.0f, 6.0f, 6.0f);
}

void LfoDisplay::setPoint(const juce::MouseEvent& e)
{
    if (dragPoint < 0) return;
    const auto r = getLocalBounds().toFloat();
    const float v = juce::jlimit(-1.0f, 1.0f, (r.getCentreY() - float(e.y)) / (r.getHeight() * 0.42f));
    if (auto* prm = pts[size_t(dragPoint)]) prm->setValueNotifyingHost(prm->convertTo0to1(v));
    repaint();
}

void LfoDisplay::mouseDown(const juce::MouseEvent& e)
{
    if (! isCustom()) return;
    dragPoint = juce::jlimit(0, 7, juce::roundToInt(float(e.x) / float(getWidth()) * 8.0f) % 8);
    if (auto* prm = pts[size_t(dragPoint)]) prm->beginChangeGesture();
    setPoint(e);
}
void LfoDisplay::mouseDrag(const juce::MouseEvent& e) { setPoint(e); }
void LfoDisplay::mouseUp(const juce::MouseEvent&)
{
    if (dragPoint >= 0) if (auto* prm = pts[size_t(dragPoint)]) prm->endChangeGesture();
    dragPoint = -1;
    repaint();
}

// ---------------------------------------------------------------- LfoPanel
LfoPanel::LfoPanel(AmbientProcessor& p, int i) : proc(p), index(i), display(p, i)
{
    auto& s = p.apvts;
    addAndMakeVisible(display);
    for (int w = 0; w < ambient::dsp::Lfo::kNumWaves; ++w) wave.addItem(ambient::dsp::Lfo::waveName(w), w + 1);
    addAndMakeVisible(wave);
    waveAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(s, lfoId(i, "wave"), wave);
    for (auto [b, f, tip] : { std::tuple<PillToggle*, const char*, const char*> { &sync, "sync", "Sync to host tempo (phase-locked while the host plays)" },
                              { &uni, "unipolar", "Unipolar output 0..1" }, { &retrig, "retrig", "Restart on every note" } })
    {
        addAndMakeVisible(*b);
        b->setTooltip(tip);
        btnAtt.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, lfoId(i, f), *b));
    }
    rate = makeKnob(*this, s, lfoId(i, "rate"), "Rate");
    division = makeKnob(*this, s, lfoId(i, "division"), "Division");
    phaseK = makeKnob(*this, s, lfoId(i, "phase"), "Phase");
    shape = makeKnob(*this, s, lfoId(i, "shape"), "Shape");
    smooth = makeKnob(*this, s, lfoId(i, "smooth"), "Smooth");
    startTimerHz(30);
}

void LfoPanel::timerCallback()
{
    display.refresh();
    const bool synced = proc.apvts.getRawParameterValue(lfoId(index, "sync"))->load() > 0.5f;
    rate->setVisible(! synced);
    division->setVisible(synced);
}

void LfoPanel::resized()
{
    auto r = getLocalBounds();
    display.setBounds(r.removeFromTop(juce::jmax(40, r.getHeight() / 3)).reduced(2));
    r.removeFromTop(6);
    wave.setBounds(r.removeFromTop(24).reduced(1, 0));
    r.removeFromTop(4);
    auto row = r.removeFromTop(22);
    const int bw = row.getWidth() / 3;
    sync.setBounds(row.removeFromLeft(bw).reduced(1, 0));
    uni.setBounds(row.removeFromLeft(bw).reduced(1, 0));
    retrig.setBounds(row.reduced(1, 0));
    r.removeFromTop(6);
    const int kw = r.getWidth() / 4;
    auto k = r.withHeight(juce::jmin(r.getHeight(), RotaryKnob::heightFor(juce::jlimit(30, 52, kw - 18), true)));
    auto first = k.removeFromLeft(kw);
    rate->setBounds(first); division->setBounds(first);
    phaseK->setBounds(k.removeFromLeft(kw));
    shape->setBounds(k.removeFromLeft(kw));
    smooth->setBounds(k);
}

// ---------------------------------------------------------------- ModMatrixPanel
ModMatrixPanel::ModMatrixPanel(AmbientProcessor& p) : proc(p)
{
    auto& s = p.apvts;
    inner.owner = this;
    viewport.setViewedComponent(&inner, false);
    viewport.setScrollBarsShown(true, false);
    viewport.setScrollBarThickness(8);
    addAndMakeVisible(viewport);
    for (int m = 0; m < 32; ++m)
    {
        auto& row = rows[size_t(m)];
        for (int k = 0; k < ambient::modsrc::kCount; ++k) row.src.addItem(ambient::modsrc::kNames[size_t(k)], k + 1);
        row.dst.addItem("None", 1);
        for (int k = 0; k < ambient::moddest::kNumDestinations; ++k) row.dst.addItem(ambient::moddest::kDestinationNames[size_t(k)], k + 2);
        const juce::String pre = "mm." + juce::String(m + 1) + ".";
        for (auto* c : { &row.src, &row.dst }) inner.addAndMakeVisible(*c);
        inner.addAndMakeVisible(row.amount);
        row.amount.setSliderStyle(juce::Slider::LinearBar);
        row.amount.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 0, 0);   // LinearBar shows the value inside
        row.amount.textFromValueFunction = [](double v) { return (v > 0 ? "+" : "") + juce::String(juce::roundToInt(v * 100.0)) + " %"; };
        row.amount.setColour(juce::Slider::trackColourId, uv.withAlpha(0.6f));
        row.amount.setTooltip("Amount -100 % .. +100 % of the destination range (double-click = 0)");
        row.amount.setDoubleClickReturnValue(true, 0.0);
        row.srcAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(s, pre + "src", row.src);
        row.dstAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(s, pre + "dst", row.dst);
        row.amtAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(s, pre + "amt", row.amount);
    }
    startTimerHz(20);
}

void ModMatrixPanel::timerCallback()
{
    for (size_t m = 0; m < rows.size(); ++m) rows[m].live = proc.slotOut[m].load(std::memory_order_relaxed);
    inner.repaint();
}

void ModMatrixPanel::resized()
{
    // 2 columns x 16 rows, scrollable
    viewport.setBounds(getLocalBounds());
    const int cols = 2, perCol = 16, rh = 24;
    const int width = viewport.getWidth() - viewport.getScrollBarThickness() - 2;
    inner.setSize(width, perCol * rh);
    const int cw = width / cols;
    for (int m = 0; m < 32; ++m)
    {
        auto& row = rows[size_t(m)];
        auto cell = juce::Rectangle<int>((m / perCol) * cw, (m % perCol) * rh, cw - 14, rh - 3);
        row.label = cell.removeFromLeft(22);
        row.meter = cell.removeFromRight(6);
        cell.removeFromRight(4);
        const int w = cell.getWidth();
        row.src.setBounds(cell.removeFromLeft(int(w * 0.30f)).reduced(1, 0));
        row.dst.setBounds(cell.removeFromLeft(int(w * 0.44f)).reduced(1, 0));
        row.amount.setBounds(cell.reduced(1, 0));
    }
}

void ModMatrixPanel::paint(juce::Graphics&) {}

void ModMatrixPanel::paintRows(juce::Graphics& g)
{
    g.setFont(caption(9.5f));
    for (size_t m = 0; m < rows.size(); ++m)
    {
        const auto& row = rows[m];
        g.setColour(textFaint);
        g.drawText(juce::String(int(m) + 1), row.label, juce::Justification::centredLeft);
        auto mr = row.meter.toFloat();
        g.setColour(bgDeep); g.fillRect(mr);
        const float v = juce::jlimit(-1.0f, 1.0f, row.live);
        const float h = mr.getHeight() * 0.5f * std::abs(v);
        g.setColour(v >= 0 ? lime : magenta);
        g.fillRect(v >= 0 ? mr.withTop(mr.getCentreY() - h).withBottom(mr.getCentreY()) : mr.withTop(mr.getCentreY()).withHeight(h));
    }
}
} // namespace ambient::ui
