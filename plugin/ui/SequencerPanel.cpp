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

    // ---- contextual views ----
    const char* viewNames[3] = { "MAIN", "RANDOM", "SEQ LFO" };
    for (int v = 0; v < 3; ++v)
    {
        auto& b = viewButtons[size_t(v)];
        b.setButtonText(viewNames[v]);
        b.setClickingTogglesState(true);
        b.setRadioGroupId(4712);
        b.setColour(juce::TextButton::buttonOnColourId, uvDeep);
        b.onClick = [this, v] { setView(v); };
        addAndMakeVisible(b);
    }
    auto attachBtn = [&](juce::Button& b, const char* pid, const char* tip)
    {
        addChildComponent(b);
        b.setTooltip(tip);
        btnAtt.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, pid, b));
    };
    attachBtn(rndOn, "seq.rnd.on", "Random mode: every pattern cycle plays a scale-aware variation of Amount of the steps (the stored pattern is kept)");
    attachBtn(rndChromatic, "seq.rnd.chromatic", "Allow notes outside the scale (playback quantisation off)");
    attachBtn(rndSteps, "seq.rnd.steps", "Randomise which steps are active (Density)");
    attachBtn(lockNotes, "seq.rnd.lockNotes", "Keep notes unchanged when randomising");
    attachBtn(lockVel, "seq.rnd.lockVel", "Keep velocities unchanged when randomising");
    attachBtn(lockGate, "seq.rnd.lockGate", "Keep gate lengths unchanged when randomising");
    attachBtn(lockSteps, "seq.rnd.lockSteps", "Keep active / inactive steps unchanged when randomising");
    generate.setTooltip("Generate a new pattern from the Random settings (scale and root aware, respects the locks). Playback continues.");
    generate.onClick = [this] { proc.generateSeqPattern(); grid.repaint(); };
    addChildComponent(generate);
    for (auto [pid, name] : { std::pair<const char*, const char*> { "seq.rnd.amount", "Amount" }, { "seq.rnd.density", "Density" },
                              { "seq.rnd.range", "Range" }, { "seq.rnd.vel", "Velocity" }, { "seq.rnd.gate", "Gate" } })
    {
        auto k = std::make_unique<RotaryKnob>(s, pid, name);
        k->setCompact(true); addChildComponent(*k); rndKnobs.push_back(std::move(k));
    }
    lfoWave.addItemList({ "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random S&H" }, 1);
    lfoDest.addItemList({ "Notes", "Speed", "Evolution", "Notes + Evolution" }, 1);
    lfoWave.setTooltip("Sequencer LFO waveform");
    lfoDest.setTooltip("Notes: shifts scale degrees. Speed: x1/2, x1, x2 per bar (smooth with the internal clock). Evolution: drives the variation amount.");
    for (auto [c, pid] : { std::pair<juce::ComboBox*, const char*> { &lfoWave, "seq.lfo.wave" }, { &lfoDest, "seq.lfo.dest" } })
    {
        addChildComponent(*c);
        comboAtt.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(s, pid, *c));
    }
    attachBtn(lfoSync, "seq.lfo.sync", "Tempo-synced LFO (phase locked to the song position)");
    attachBtn(lfoRetrig, "seq.lfo.retrig", "Restart the LFO at every pattern cycle (repeating melodic movement)");
    attachBtn(lfoSmoothSpeed, "seq.lfo.smoothSpeed", "Continuous speed changes - internal clock only (host sync always uses x1/2, x1, x2 per bar)");
    for (auto [pid, name, bip] : { std::tuple<const char*, const char*, bool> { "seq.lfo.rate", "Rate", false }, { "seq.lfo.division", "Division", false },
                                   { "seq.lfo.depth", "Depth", true }, { "seq.lfo.phase", "Phase", false }, { "seq.lfo.smooth", "Smooth", false } })
    {
        auto k = std::make_unique<RotaryKnob>(s, pid, name, bip);
        k->setCompact(true); addChildComponent(*k); lfoKnobs.push_back(std::move(k));
    }
    for (auto& k : knobs) mainItems.push_back(k.get());
    for (auto& c : lockTargets) mainItems.push_back(&c);
    for (auto* c : std::initializer_list<juce::Component*> { &rndOn, &rndChromatic, &rndSteps, &lockNotes, &lockVel, &lockGate, &lockSteps, &generate }) rndItems.push_back(c);
    for (auto& k : rndKnobs) rndItems.push_back(k.get());
    for (auto* c : std::initializer_list<juce::Component*> { &lfoWave, &lfoDest, &lfoSync, &lfoRetrig, &lfoSmoothSpeed }) lfoItems.push_back(c);
    for (auto& k : lfoKnobs) lfoItems.push_back(k.get());
    viewButtons[0].setToggleState(true, juce::dontSendNotification);
    setView(0);
    startTimerHz(30);
}

void SequencerPanel::setView(int v)
{
    view = v;
    for (int i = 0; i < 3; ++i) viewButtons[size_t(i)].setToggleState(i == v, juce::dontSendNotification);
    for (auto* c : mainItems) c->setVisible(v == 0);
    for (auto* c : rndItems) c->setVisible(v == 1);
    for (auto* c : lfoItems) c->setVisible(v == 2);
    timerCallback();
    resized();
}

void SequencerPanel::timerCallback()
{
    const int cur = proc.seqCurrentStep.load(std::memory_order_relaxed);
    if (cur != grid.currentStep) { grid.currentStep = cur; grid.repaint(); }
    const bool internal = proc.apvts.getRawParameterValue("seq.clock")->load() > 0.5f;
    if (! knobs.empty() && knobs[0]->isEnabled() != internal) { knobs[0]->setEnabled(internal); knobs[0]->setAlpha(internal ? 1.0f : 0.35f); }
    if (view == 2 && lfoKnobs.size() >= 2)
    {
        const bool synced = proc.apvts.getRawParameterValue("seq.lfo.sync")->load() > 0.5f;
        lfoKnobs[0]->setVisible(! synced);
        lfoKnobs[1]->setVisible(synced);
        const bool speed = juce::roundToInt(proc.apvts.getRawParameterValue("seq.lfo.dest")->load()) == 1;
        lfoSmoothSpeed.setEnabled(speed && internal);
        lfoSmoothSpeed.setAlpha(speed && internal ? 1.0f : 0.35f);
    }
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
    // row 2: contextual controls (view selector on the left)
    auto row = r.removeFromTop(juce::jmin(84, RotaryKnob::heightFor(40, true)));
    {
        auto vs = row.removeFromLeft(78);
        const int bh = vs.getHeight() / 3;
        for (auto& b : viewButtons) b.setBounds(vs.removeFromTop(bh).reduced(0, 2));
        row.removeFromLeft(10);
    }
    const int kw = juce::jmin(72, row.getWidth() / 12);
    if (view == 0)
    {
        auto rr = row;
        for (auto& k : knobs) k->setBounds(rr.removeFromLeft(kw));
        rr.removeFromLeft(12);
        auto locks = rr.withSizeKeepingCentre(rr.getWidth(), 50);
        auto l1 = locks.removeFromTop(24), l2 = locks.removeFromBottom(24);
        for (int l = 0; l < 4; ++l) lockTargets[size_t(l)].setBounds((l < 2 ? l1 : l2).removeFromLeft(rr.getWidth() / 2).reduced(2, 0));
    }
    else if (view == 1)
    {
        auto rr = row;
        auto pills = rr.removeFromLeft(220);
        auto p1 = pills.removeFromTop(pills.getHeight() / 2).reduced(0, 3), p2 = pills.reduced(0, 3);
        rndOn.setBounds(p1.removeFromLeft(72)); p1.removeFromLeft(4); rndChromatic.setBounds(p1.removeFromLeft(84)); p1.removeFromLeft(4); rndSteps.setBounds(p1);
        generate.setBounds(p2.removeFromLeft(110)); 
        rr.removeFromLeft(8);
        for (auto& k : rndKnobs) k->setBounds(rr.removeFromLeft(kw));
        rr.removeFromLeft(8);
        lockCaption = rr.withHeight(14).withY(rr.getY() + 2);
        rr.removeFromTop(14);
        auto lk = rr.withSizeKeepingCentre(rr.getWidth(), 50);
        auto a = lk.removeFromTop(24), b2 = lk.removeFromBottom(24);
        const int lw = juce::jmin(110, rr.getWidth() / 2);
        lockNotes.setBounds(a.removeFromLeft(lw).reduced(2, 1)); lockVel.setBounds(a.removeFromLeft(lw).reduced(2, 1));
        lockGate.setBounds(b2.removeFromLeft(lw).reduced(2, 1)); lockSteps.setBounds(b2.removeFromLeft(lw).reduced(2, 1));
    }
    else
    {
        auto rr = row;
        auto col = rr.removeFromLeft(170);
        lfoWave.setBounds(col.removeFromTop(col.getHeight() / 2).reduced(0, 3));
        lfoDest.setBounds(col.reduced(0, 3));
        rr.removeFromLeft(8);
        auto first = rr.removeFromLeft(kw);
        lfoKnobs[0]->setBounds(first); lfoKnobs[1]->setBounds(first);
        for (size_t i = 2; i < lfoKnobs.size(); ++i) lfoKnobs[i]->setBounds(rr.removeFromLeft(kw));
        rr.removeFromLeft(8);
        auto pills = rr.withSizeKeepingCentre(juce::jmin(rr.getWidth(), 240), 50);
        auto a = pills.removeFromTop(24), b2 = pills.removeFromBottom(24);
        lfoSync.setBounds(a.removeFromLeft(80).reduced(2, 1)); lfoRetrig.setBounds(a.removeFromLeft(90).reduced(2, 1));
        lfoSmoothSpeed.setBounds(b2.removeFromLeft(140).reduced(2, 1));
    }
    r.removeFromTop(6);
    // row 3: lanes
    auto lanes = r.removeFromTop(22);
    const int lw = lanes.getWidth() / StepGrid::kLanes;
    for (auto& b : laneButtons) b.setBounds(lanes.removeFromLeft(lw).reduced(1, 0));
    r.removeFromTop(6);
    grid.setBounds(r);
}

void SequencerPanel::paint(juce::Graphics& g)
{
    if (view == 1 && ! lockCaption.isEmpty())
    {
        g.setFont(caption(9.5f));
        g.setColour(textFaint);
        g.drawText("LOCKED FIELDS", lockCaption.translated(2, 0), juce::Justification::centredLeft);
    }
}
} // namespace ambient::ui
