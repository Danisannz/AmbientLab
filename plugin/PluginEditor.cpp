#include "PluginEditor.h"
#include "Parameters.h"
#include <ambient/ParamIDs.h>

using namespace ambient;

namespace
{
const juce::Colour kBg      { 0xff1c1d20 };
const juce::Colour kPanel   { 0xff26282c };
const juce::Colour kAccent  { 0xffd8a657 };
const juce::Colour kText    { 0xffd6d6d6 };
}

AmbientEditor::AmbientEditor(AmbientProcessor& p) : AudioProcessorEditor(p), proc(p)
{
    auto make = [this](std::vector<std::unique_ptr<Knob>>& list, std::string_view pid, const juce::String& name)
    {
        auto k = std::make_unique<Knob>();
        k->slider.setColour(juce::Slider::rotarySliderFillColourId, kAccent);
        k->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
        k->label.setText(name, juce::dontSendNotification);
        k->label.setJustificationType(juce::Justification::centred);
        k->label.setColour(juce::Label::textColourId, kText);
        addAndMakeVisible(k->slider);
        addAndMakeVisible(k->label);
        k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, id(pid), k->slider);
        list.push_back(std::move(k));
    };

    make(globals, ids::inputGain,  "Input");
#if AMBIENT_IS_INSTRUMENT
    make(globals, ids::sourceMix,  "Synth / Input");
#endif
    make(globals, ids::masterGain, "Master");
    for (size_t i = 0; i < ids::macros.size(); ++i)
        make(macros, ids::macros[i], "Macro " + juce::String(int(i) + 1));

#if AMBIENT_IS_INSTRUMENT
    addAndMakeVisible(bypassSynth);
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, id(ids::bypassSynth), bypassSynth);
#endif

    setResizable(true, true);
    setResizeLimits(720, 420, 1800, 1050);
    getConstrainer()->setFixedAspectRatio(720.0 / 420.0);
    setSize(900, 525);
    startTimerHz(30);
}

void AmbientEditor::timerCallback()
{
    meter = proc.outputPeak.load(std::memory_order_relaxed);
    repaint(getLocalBounds().removeFromRight(30));
}

void AmbientEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBg);
    auto r = getLocalBounds();
    auto header = r.removeFromTop(56);
    g.setColour(kPanel);
    g.fillRect(header);

    // Branding slot (user logo goes here later)
    auto logo = header.reduced(10).removeFromLeft(180);
    g.setColour(kAccent.withAlpha(0.35f));
    g.drawRoundedRectangle(logo.toFloat(), 4.0f, 1.0f);
    g.setColour(kText);
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.drawText("AMBIENTLAB", logo, juce::Justification::centred);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(juce::String("Stage 1 build  |  ") + (AMBIENT_IS_INSTRUMENT ? "Instrument" : "FX"),
               header.reduced(12), juce::Justification::centredRight);

    // Output meter
    auto m = getLocalBounds().removeFromRight(30).withTrimmedTop(66).reduced(8, 10);
    g.setColour(kPanel); g.fillRect(m);
    const float db = juce::Decibels::gainToDecibels(meter, -60.0f);
    const int h = int(m.getHeight() * juce::jmap(db, -60.0f, 0.0f, 0.0f, 1.0f));
    g.setColour(db > -1.0f ? juce::Colours::orangered : kAccent);
    g.fillRect(m.removeFromBottom(h));
}

void AmbientEditor::resized()
{
    auto r = getLocalBounds().withTrimmedTop(66).withTrimmedRight(34).reduced(12);
    auto top = r.removeFromTop(r.getHeight() / 2);
    auto layoutRow = [](juce::Rectangle<int> row, std::vector<std::unique_ptr<Knob>>& ks, int slots)
    {
        const int w = row.getWidth() / slots;
        for (auto& k : ks)
        {
            auto cell = row.removeFromLeft(w).reduced(4);
            k->label.setBounds(cell.removeFromTop(18));
            k->slider.setBounds(cell);
        }
    };
    layoutRow(top.removeFromLeft(top.getWidth() * 3 / 4), globals, 3);
    bypassSynth.setBounds(top.reduced(10).withSizeKeepingCentre(140, 28));
    layoutRow(r, macros, 8);
}
