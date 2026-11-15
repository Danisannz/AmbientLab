#include "OscillatorPanel.h"
#include "../Parameters.h"
#include <ambient/ParamIDs.h>

namespace ambient::ui
{
using namespace theme;
namespace o = ambient::ids::osc;

ParamSegment::ParamSegment(juce::RangedAudioParameter& p, juce::StringArray labels)
    : SegmentedControl(std::move(labels)), param(p),
      attachment(p, [this](float v) { setSelected(juce::roundToInt(param.convertTo0to1(v) * float(param.getNumSteps() - 1)), false); })
{
    onChange = [this](int i)
    {
        const float norm = param.getNumSteps() > 1 ? float(i) / float(param.getNumSteps() - 1) : 0.0f;
        attachment.setValueAsCompleteGesture(param.convertFrom0to1(norm));
    };
    attachment.sendInitialUpdate();
}

OscillatorPanel::Row& OscillatorPanel::addRow(const juce::String& name)
{
    rows.push_back(std::make_unique<Row>());
    rows.back()->name = name;
    return *rows.back();
}

void OscillatorPanel::addPower(Row& r, const juce::String& pid)
{
    r.power = std::make_unique<BypassButton>(r.name);
    r.power->setLitWhenOn(true);
    r.power->setTooltip(r.name + " on/off");
    addAndMakeVisible(*r.power);
    r.powerAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, pid, *r.power);
}

void OscillatorPanel::addSegment(Row& r, const juce::String& pid, juce::StringArray labels)
{
    if (auto* p = apvts.getParameter(pid))
    {
        r.segments.push_back(std::make_unique<ParamSegment>(*p, std::move(labels)));
        addAndMakeVisible(*r.segments.back());
    }
}

void OscillatorPanel::addToggle(Row& r, const juce::String& pid, const juce::String& text)
{
    r.toggles.push_back(std::make_unique<PillToggle>(text, lime));
    addAndMakeVisible(*r.toggles.back());
    r.toggleAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, pid, *r.toggles.back()));
}

void OscillatorPanel::addKnob(Row& r, const juce::String& pid, const juce::String& name, bool bipolar)
{
    auto k = std::make_unique<RotaryKnob>(apvts, pid, name, bipolar);
    k->setCompact(true);
    addAndMakeVisible(*k);
    r.knobs.push_back(std::move(k));
}

OscillatorPanel::OscillatorPanel(juce::AudioProcessorValueTreeState& s) : apvts(s)
{
    const juce::StringArray waves { "SIN", "TRI", "SAW", "SQR", "PLS" };
    for (int i = 0; i < 2; ++i)
    {
        auto pid = [i](o::OscField f) { return juce::String(o::oscId(i, f)); };
        auto& r = addRow(i == 0 ? "Osc A" : "Osc B");
        addPower(r, pid(o::On));
        addSegment(r, pid(o::WaveF), waves);
        addToggle(r, pid(o::Free), "Free");
        addKnob(r, pid(o::Octave), "Octave", true);
        addKnob(r, pid(o::Semi), "Semi", true);
        addKnob(r, pid(o::Fine), "Fine", true);
        addKnob(r, pid(o::Pw), "PW");
        addKnob(r, pid(o::Phase), "Phase");
        addKnob(r, pid(o::Level), "Level");
        r.groupBreaks.push_back(int(r.knobs.size()));
        addKnob(r, pid(o::Unison), "Unison");
        addKnob(r, pid(o::Detune), "Detune");
        addKnob(r, pid(o::Spread), "Spread");
        if (i == 1)
        {
            r.groupBreaks.push_back(int(r.knobs.size()));
            addKnob(r, id(o::fm), "FM > A");
            addKnob(r, id(o::pm), "PM > A");
        }
    }
    auto& sub = addRow("Sub");
    addPower(sub, id(o::subOn));
    addSegment(sub, id(o::subWave), { "SIN", "SQR", "TRI" });
    addSegment(sub, id(o::subOctave), { "-1", "-2" });
    addKnob(sub, id(o::subLevel), "Sub");
    sub.groupBreaks.push_back(int(sub.knobs.size()));
    addKnob(sub, id(o::noiseLevel), "Noise");
    sub.groupBreaks.push_back(int(sub.knobs.size()));
    addKnob(sub, id(o::drift), "Drift");
    addKnob(sub, id(o::velocity), "Velocity");
    sub.groupBreaks.push_back(int(sub.knobs.size()));
    addKnob(sub, id(o::satDrive), "Drive");
    addKnob(sub, id(o::satMix), "Sat mix");

    // noise + saturation selectors live in the third row as well
    auto& extras = *rows.back();
    addToggle(extras, id(o::noiseOn), "Noise");
    addSegment(extras, id(o::noiseType), { "WHT", "PNK" });
    addSegment(extras, id(o::satMode), { "SOFT", "ASYM", "FOLD" });
}

void OscillatorPanel::resized()
{
    separators.clear();
    auto r = getLocalBounds();
    const int rowGap = 4;
    const int rowH = (r.getHeight() - rowGap * (int(rows.size()) - 1)) / int(rows.size());
    for (size_t ri = 0; ri < rows.size(); ++ri)
    {
        auto& row = *rows[ri];
        auto a = r.removeFromTop(rowH);
        r.removeFromTop(rowGap);

        // left column: power + name + selectors
        auto left = a.removeFromLeft(juce::jmin(ri == 2 ? 250 : 230, a.getWidth() * 2 / 5));
        auto top = left.removeFromTop(left.getHeight() / 2).withSizeKeepingCentre(left.getWidth(), 24);
        auto bottom = left.withSizeKeepingCentre(left.getWidth(), 24);
        if (row.power) row.power->setBounds(top.removeFromLeft(24));
        top.removeFromLeft(56); // name drawn in paint()

        if (ri < 2)
        {
            row.segments[0]->setBounds(bottom.withTrimmedRight(58));
            row.toggles[0]->setBounds(bottom.removeFromRight(52));
        }
        else
        {
            // Sub: wave + octave on top; Noise toggle + type and saturation mode below
            row.segments[0]->setBounds(top.removeFromLeft(top.getWidth() * 3 / 5));
            top.removeFromLeft(6);
            row.segments[1]->setBounds(top);
            row.toggles[0]->setBounds(bottom.removeFromLeft(64));
            bottom.removeFromLeft(4);
            row.segments[2]->setBounds(bottom.removeFromLeft(70));
            bottom.removeFromLeft(6);
            row.segments[3]->setBounds(bottom);
        }

        a.removeFromLeft(10);
        const int groups = int(row.groupBreaks.size());
        const int gapW = 12;
        const int kw = juce::jmin(64, (a.getWidth() - groups * gapW) / juce::jmax(1, int(row.knobs.size()))); // consistent knob size across rows
        for (int k = 0; k < int(row.knobs.size()); ++k)
        {
            if (std::find(row.groupBreaks.begin(), row.groupBreaks.end(), k) != row.groupBreaks.end())
            {
                separators.push_back(juce::Rectangle<int>(a.getX() + gapW / 2, a.getY() + 10, 1, a.getHeight() - 20));
                a.removeFromLeft(gapW);
            }
            row.knobs[size_t(k)]->setBounds(a.removeFromLeft(kw));
        }
    }
}

void OscillatorPanel::paint(juce::Graphics& g)
{
    g.setColour(edge);
    for (auto& s : separators) g.fillRect(s);
    const int rowH = (getHeight() - 4 * (int(rows.size()) - 1)) / int(rows.size());
    for (size_t ri = 0; ri < rows.size(); ++ri)
    {
        const int y = int(ri) * (rowH + 4);
        if (ri > 0) { g.setColour(edge.withAlpha(0.6f)); g.drawHorizontalLine(y - 2, 0.0f, float(getWidth())); }
        g.setColour(text);
        g.setFont(heading(13.0f, false, 0.06f));
        const juce::String name = rows[ri]->name.toUpperCase();
        g.drawText(name, juce::Rectangle<int>(30, y + rowH / 4 - 12, 220, 24), juce::Justification::centredLeft);
    }
}
} // namespace ambient::ui
