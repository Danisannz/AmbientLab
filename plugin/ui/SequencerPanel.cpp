#include "SequencerPanel.h"
#include "Fonts.h"
#include "../ModDestinations.h"
#include <ambient/dsp/Scales.h>
#include <ambient/dsp/Delay.h>
#include <cmath>

namespace ambient::ui
{
using namespace theme;
using Step = ambient::dsp::Sequencer::Step;

// ---------------------------------------------------------------- StepGrid
StepGrid::StepGrid(AmbientProcessor& p) : proc(p)
{
    setTooltip("Drag bars to edit the selected lane. Top row: step on/off, bottom row: skip. Double-click a lock to clear it.");
}

juce::Rectangle<float> StepGrid::barArea() const { auto r = getLocalBounds().toFloat(); r.removeFromBottom(40); return r; }
juce::Rectangle<float> StepGrid::toggleRow(int row) const
{
    auto r = getLocalBounds().toFloat();
    r = r.removeFromBottom(40);
    return row == 0 ? r.removeFromTop(20).reduced(0, 2) : r.removeFromBottom(20).reduced(0, 2);
}
int StepGrid::stepAt(float x) const { return juce::jlimit(0, 31, int(x / (float(getWidth()) / 32.0f))); }

float StepGrid::laneValue(const Step& s) const
{
    switch (lane)
    {
        case Note: return (float(s.note) + 24.0f) / 48.0f;
        case Velocity: return s.velocity;
        case Gate: return s.gate;
        case Probability: return s.probability;
        case Ratchet: return float(s.ratchet - 1) / 3.0f;
        default: { const float v = s.locks[size_t(lane - Lock1)]; return std::isnan(v) ? -1.0f : v; }
    }
}

void StepGrid::setLaneValue(Step& s, float v) const
{
    v = juce::jlimit(0.0f, 1.0f, v);
    switch (lane)
    {
        case Note: s.note = juce::roundToInt(v * 48.0f) - 24; break;
        case Velocity: s.velocity = v; break;
        case Gate: s.gate = juce::jmax(0.05f, v); break;
        case Probability: s.probability = v; break;
        case Ratchet: s.ratchet = 1 + juce::roundToInt(v * 3.0f); break;
        default: s.locks[size_t(lane - Lock1)] = v; break;
    }
}

void StepGrid::paint(juce::Graphics& g)
{
    const int len = juce::roundToInt(proc.apvts.getRawParameterValue("seq.length")->load());
    const float cw = float(getWidth()) / 32.0f;
    auto bars = barArea();
    g.setColour(bgDeep); g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    if (lane == Note) { g.setColour(grid); g.drawHorizontalLine(int(bars.getCentreY()), bars.getX(), bars.getRight()); }
    g.setFont(caption(9.0f));
    for (int i = 0; i < 32; ++i)
    {
        const auto& s = proc.getSeqStep(i);
        const bool inLen = i < len;
        auto col = juce::Rectangle<float>(float(i) * cw, bars.getY(), cw, bars.getHeight()).reduced(2.0f, 3.0f);
        if (i == currentStep) { g.setColour(lime.withAlpha(0.12f)); g.fillRect(col.expanded(1.0f, 3.0f)); }
        if (i % 4 == 0) { g.setColour(gridHi); g.drawVerticalLine(int(col.getX() - 2.0f), bars.getY(), float(getHeight())); }
        const float v = laneValue(s);
        juce::Colour c = (lane >= Lock1 ? magenta : uvBright).withAlpha(inLen ? (s.active && ! s.skip ? 0.95f : 0.35f) : 0.12f);
        if (v >= 0.0f)
        {
            g.setColour(c);
            if (lane == Note)
            {
                const float y = col.getBottom() - v * col.getHeight();
                g.fillRect(col.withY(y - 3.0f).withHeight(6.0f));
            }
            else g.fillRect(col.withTop(col.getBottom() - juce::jmax(2.0f, v * col.getHeight())));
        }
        else { g.setColour(textFaint.withAlpha(0.4f)); g.drawText("-", col, juce::Justification::centred); }
        // value label
        g.setColour(inLen ? textDim : textFaint.withAlpha(0.4f));
        juce::String label;
        if (lane == Note) label = (s.note > 0 ? "+" : "") + juce::String(s.note);
        else if (lane == Ratchet) label = juce::String(s.ratchet) + "x";
        else if (v >= 0.0f) label = juce::String(juce::roundToInt(v * 100.0f));
        g.drawText(label, col.withHeight(12.0f), juce::Justification::centred);
        // toggles
        for (int row = 0; row < 2; ++row)
        {
            auto t = toggleRow(row).withX(float(i) * cw).withWidth(cw).reduced(2.0f, 0.0f);
            const bool on = row == 0 ? s.active : s.skip;
            g.setColour(on ? (row == 0 ? lime : magenta).withAlpha(inLen ? 0.85f : 0.25f) : edge);
            g.fillRoundedRectangle(t, 2.0f);
        }
    }
    g.setColour(textFaint); g.setFont(caption(8.5f));
    g.drawText("ON", toggleRow(0).withWidth(30.0f).translated(-32.0f, 0.0f), juce::Justification::centredRight);
}

void StepGrid::edit(const juce::MouseEvent& e, bool start)
{
    const int i = stepAt(float(e.x));
    auto s = proc.getSeqStep(i);
    if (start)
    {
        dragRow = toggleRow(0).contains(float(e.x), float(e.y)) ? 0 : (toggleRow(1).contains(float(e.x), float(e.y)) ? 1 : -1);
        if (dragRow >= 0) dragSet = ! (dragRow == 0 ? s.active : s.skip);
    }
    if (dragRow == 0) s.active = dragSet;
    else if (dragRow == 1) s.skip = dragSet;
    else
    {
        auto bars = barArea().reduced(0.0f, 3.0f);
        setLaneValue(s, (bars.getBottom() - float(e.y)) / bars.getHeight());
    }
    proc.setSeqStep(i, s);
    repaint();
}

void StepGrid::mouseDown(const juce::MouseEvent& e) { edit(e, true); }
void StepGrid::mouseDrag(const juce::MouseEvent& e) { edit(e, false); }
void StepGrid::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (lane < Lock1) return;
    const int i = stepAt(float(e.x));
    auto s = proc.getSeqStep(i);
    s.locks[size_t(lane - Lock1)] = std::numeric_limits<float>::quiet_NaN();
    proc.setSeqStep(i, s);
    repaint();
}

// ---------------------------------------------------------------- SequencerPanel
SequencerPanel::SequencerPanel(AmbientProcessor& p) : proc(p), grid(p)
{
    auto& s = p.apvts;
    power.setLitWhenOn(true);
    power.setTooltip("Sequencer on/off");
    btnAtt.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, "seq.on", power));
    addAndMakeVisible(patternStart);
    patternStart.setTooltip("Scale / root changes (also from modulation) wait for the pattern start");
    btnAtt.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, "seq.patternStart", patternStart));

    auto fill = [&](juce::ComboBox& c, const juce::StringArray& items, const juce::String& pid, const juce::String& tip)
    {
        c.addItemList(items, 1);
        c.setTooltip(tip);
        addAndMakeVisible(c);
        comboAtt.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(s, pid, c));
    };
    juce::StringArray scales; for (int k = 0; k < ambient::dsp::scales::kNumScales; ++k) scales.add(ambient::dsp::scales::name(k));
    fill(clock, { "Host", "Internal" }, "seq.clock", "Clock: host transport (sync) or internal tempo");
    fill(direction, { "Forward", "Reverse", "Ping-Pong", "Random" }, "seq.direction", "Playback direction");
    fill(root, { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, "seq.root", "Root note (modulatable)");
    fill(scale, scales, "seq.scale", "Scale (modulatable)");
    fill(octave, { "C1", "C2", "C3", "C4", "C5" }, "seq.octave", "Base octave");

    for (int i = 0; i < ambient::dsp::Sequencer::kNumPresets; ++i) presets.addItem(ambient::dsp::Sequencer::presetName(i), i + 1);
    presets.setTextWhenNothingSelected("Pattern preset...");
    presets.onChange = [this] { if (presets.getSelectedId() > 0) { proc.loadSeqPreset(presets.getSelectedId() - 1); grid.repaint(); } };
    addAndMakeVisible(presets);
    randomise.onClick = [this] { proc.randomiseSeq(0.5f); grid.repaint(); };
    randomise.setTooltip("Randomise half of the steps (notes, gates, velocity, probability)");
    addAndMakeVisible(randomise);

    for (auto [pid, name] : { std::pair<const char*, const char*> { "seq.bpm", "Tempo" }, { "seq.length", "Length" }, { "seq.division", "Rate" },
                              { "seq.swing", "Swing" }, { "seq.transpose", "Transpose" }, { "seq.gate", "Gate" } })
    {
        auto k = std::make_unique<RotaryKnob>(s, pid, name, juce::String(pid) == "seq.transpose");
        k->setCompact(true);
        addAndMakeVisible(*k);
        knobs.push_back(std::move(k));
    }

    const char* laneNames[StepGrid::kLanes] = { "NOTE", "VELOCITY", "GATE", "PROB", "RATCHET", "LOCK 1", "LOCK 2", "LOCK 3", "LOCK 4" };
    for (int l = 0; l < StepGrid::kLanes; ++l)
    {
        auto& b = laneButtons[size_t(l)];
        b.setButtonText(laneNames[l]);
        b.setClickingTogglesState(true);
        b.setRadioGroupId(4711);
        b.setColour(juce::TextButton::buttonOnColourId, l >= StepGrid::Lock1 ? magentaDeep : uvDeep);
        b.onClick = [this, l] { grid.setLane(l); };
        addAndMakeVisible(b);
    }
    laneButtons[0].setToggleState(true, juce::dontSendNotification);

    juce::StringArray dst { "None" }; for (auto* d : ambient::moddest::kDestinationNames) dst.add(d);
    for (int l = 0; l < 4; ++l)
    {
        lockTargets[size_t(l)].addItemList(dst, 1);
        lockTargets[size_t(l)].setTooltip("Parameter lock lane " + juce::String(l + 1) + " target: locked steps SET this parameter");
        addAndMakeVisible(lockTargets[size_t(l)]);
        comboAtt.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(s, "seq.lock" + juce::String(l + 1), lockTargets[size_t(l)]));
    }
    addAndMakeVisible(grid);
    startTimerHz(30);
}

void SequencerPanel::timerCallback()
{
    const int cur = proc.seqCurrentStep.load(std::memory_order_relaxed);
    if (cur != grid.currentStep) { grid.currentStep = cur; grid.repaint(); }
    const bool internal = proc.apvts.getRawParameterValue("seq.clock")->load() > 0.5f;
    if (! knobs.empty() && knobs[0]->isEnabled() != internal) { knobs[0]->setEnabled(internal); knobs[0]->setAlpha(internal ? 1.0f : 0.35f); }
}

void SequencerPanel::resized()
{
    auto r = getLocalBounds();
    // row 1: combos
    auto top = r.removeFromTop(24);
    const int cw = top.getWidth() / 8;
    clock.setBounds(top.removeFromLeft(cw).reduced(2, 0));
    direction.setBounds(top.removeFromLeft(cw).reduced(2, 0));
    root.setBounds(top.removeFromLeft(cw * 2 / 3).reduced(2, 0));
    scale.setBounds(top.removeFromLeft(cw * 4 / 3).reduced(2, 0));
    octave.setBounds(top.removeFromLeft(cw * 2 / 3).reduced(2, 0));
    patternStart.setBounds(top.removeFromLeft(cw * 4 / 3).reduced(2, 0));
    presets.setBounds(top.removeFromLeft(top.getWidth() * 3 / 5).reduced(2, 0));
    randomise.setBounds(top.reduced(2, 0));
    r.removeFromTop(6);
    // row 2: knobs + lock targets
    auto row = r.removeFromTop(juce::jmin(84, RotaryKnob::heightFor(40, true)));
    const int kw = juce::jmin(72, row.getWidth() / 12);
    for (auto& k : knobs) k->setBounds(row.removeFromLeft(kw));
    row.removeFromLeft(12);
    auto locks = row.withSizeKeepingCentre(row.getWidth(), 50);
    auto l1 = locks.removeFromTop(24), l2 = locks.removeFromBottom(24);
    for (int l = 0; l < 4; ++l) lockTargets[size_t(l)].setBounds((l < 2 ? l1 : l2).removeFromLeft(row.getWidth() / 2).reduced(2, 0));
    r.removeFromTop(6);
    // row 3: lanes
    auto lanes = r.removeFromTop(22);
    const int lw = lanes.getWidth() / StepGrid::kLanes;
    for (auto& b : laneButtons) b.setBounds(lanes.removeFromLeft(lw).reduced(1, 0));
    r.removeFromTop(6);
    grid.setBounds(r);
}

void SequencerPanel::paint(juce::Graphics&) {}
} // namespace ambient::ui
