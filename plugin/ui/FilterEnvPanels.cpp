#include "FilterEnvPanels.h"
#include "../Parameters.h"
#include <ambient/ParamIDs.h>

namespace ambient::ui
{
using namespace theme;
namespace f = ambient::ids::flt;
namespace e = ambient::ids::env;

static std::unique_ptr<RotaryKnob> knob(juce::Component& parent, juce::AudioProcessorValueTreeState& s,
                                        std::string_view pid, const juce::String& name, bool bipolar = false)
{
    auto k = std::make_unique<RotaryKnob>(s, id(pid), name, bipolar);
    k->setCompact(true);
    parent.addAndMakeVisible(*k);
    return k;
}

FilterPanel::FilterPanel(juce::AudioProcessorValueTreeState& s)
{
    power.setLitWhenOn(true);
    power.setTooltip("Filter on/off");
    powerAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, id(f::on), power);
    if (auto* p = s.getParameter(id(f::mode)))
    {
        mode = std::make_unique<ParamSegment>(*p, juce::StringArray { "LP24", "LP12", "BP", "HP12", "HP24", "NOTCH" });
        addAndMakeVisible(*mode);
    }
    knobs.push_back(knob(*this, s, f::cutoff, "Cutoff"));
    knobs.push_back(knob(*this, s, f::reso, "Reso"));
    knobs.push_back(knob(*this, s, f::drive, "Drive"));
    knobs.push_back(knob(*this, s, f::env, "Env amt", true));
    knobs.push_back(knob(*this, s, f::key, "Key"));
    knobs.push_back(knob(*this, s, f::vel, "Vel"));
}

void FilterPanel::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop(juce::jmin(30, r.getHeight() / 3)).withSizeKeepingCentre(r.getWidth(), 24);
    if (mode) mode->setBounds(top.withWidth(juce::jmin(top.getWidth(), 360)));
    r.removeFromTop(4);
    const int kw = juce::jmin(72, r.getWidth() / int(knobs.size()));
    auto row = r.withSizeKeepingCentre(r.getWidth(), juce::jmin(r.getHeight(), RotaryKnob::heightFor(46, true)));
    for (auto& k : knobs) k->setBounds(row.removeFromLeft(kw));
}

EnvelopePanel::EnvelopePanel(juce::AudioProcessorValueTreeState& s)
{
    amp.push_back(knob(*this, s, e::ampA, "Attack"));
    amp.push_back(knob(*this, s, e::ampD, "Decay"));
    amp.push_back(knob(*this, s, e::ampS, "Sustain"));
    amp.push_back(knob(*this, s, e::ampR, "Release"));
    mod.push_back(knob(*this, s, e::modA, "Attack"));
    mod.push_back(knob(*this, s, e::modD, "Decay"));
    mod.push_back(knob(*this, s, e::modS, "Sustain"));
    mod.push_back(knob(*this, s, e::modR, "Release"));
    mod.push_back(knob(*this, s, e::modPitch, "Pitch", true));
}

void EnvelopePanel::resized()
{
    auto r = getLocalBounds();
    auto layoutRow = [](juce::Rectangle<int> row, std::vector<std::unique_ptr<RotaryKnob>>& ks)
    {
        row.removeFromLeft(44); // row label
        const int kw = juce::jmin(66, row.getWidth() / 5);
        for (auto& k : ks) k->setBounds(row.removeFromLeft(kw));
    };
    auto top = r.removeFromTop(r.getHeight() / 2);
    layoutRow(top, amp);
    layoutRow(r, mod);
}

void EnvelopePanel::paint(juce::Graphics& g)
{
    g.setFont(heading(12.0f, false, 0.08f));
    g.setColour(text);
    const int h = getHeight() / 2;
    g.drawText("AMP", juce::Rectangle<int>(0, 0, 44, h), juce::Justification::centredLeft);
    g.drawText("MOD", juce::Rectangle<int>(0, h, 44, h), juce::Justification::centredLeft);
    g.setColour(edge.withAlpha(0.6f));
    g.drawHorizontalLine(h, 0.0f, float(getWidth()));
}
} // namespace ambient::ui

namespace ambient::ui
{
namespace g = ambient::ids::gr;

GranularPanel::GranularPanel(juce::AudioProcessorValueTreeState& s)
{
    power.setLitWhenOn(true);
    power.setTooltip("Granular on/off");
    powerAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, id(g::on), power);
    addAndMakeVisible(freeze);
    freeze.setTooltip("Freeze the grain buffer (stops recording; grains keep playing)");
    freezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, id(g::freeze), freeze);
    if (auto* p = s.getParameter(id(g::window)))
    {
        window = std::make_unique<ParamSegment>(*p, juce::StringArray { "HANN", "GAUSS", "TUKEY", "TRI", "DECAY" });
        addAndMakeVisible(*window);
    }
    grain.push_back(knob(*this, s, g::size, "Size"));
    grain.push_back(knob(*this, s, g::density, "Density"));
    grain.push_back(knob(*this, s, g::position, "Position"));
    grain.push_back(knob(*this, s, g::scatter, "Scatter"));
    pitch.push_back(knob(*this, s, g::pitch, "Pitch", true));
    pitch.push_back(knob(*this, s, g::pitchRandom, "Pitch rnd"));
    pitch.push_back(knob(*this, s, g::reverse, "Reverse"));
    out.push_back(knob(*this, s, g::spread, "Spread"));
    out.push_back(knob(*this, s, g::feedback, "Feedback"));
    out.push_back(knob(*this, s, g::mix, "Mix"));
}

void GranularPanel::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop(30).withSizeKeepingCentre(r.getWidth(), 26);
    freeze.setBounds(top.removeFromLeft(96));
    top.removeFromLeft(12);
    if (window) window->setBounds(top.removeFromLeft(juce::jmin(360, top.getWidth())));
    r.removeFromTop(10);

    // three labelled groups: GRAIN | PITCH | OUTPUT
    const int total = int(grain.size() + pitch.size() + out.size());
    const int gapW = 18;
    const int kw = juce::jmin(84, (r.getWidth() - 2 * gapW) / total);
    const int d = juce::jlimit(36, 60, kw - 20);
    auto row = r.withHeight(juce::jmin(r.getHeight(), RotaryKnob::heightFor(d, true) + 18)).withTrimmedTop(26); // top-aligned under the selectors
    for (auto* group : { &grain, &pitch, &out })
    {
        for (auto& k : *group) k->setBounds(row.removeFromLeft(kw));
        row.removeFromLeft(gapW);
    }
}

void GranularPanel::paint(juce::Graphics& gr)
{
    gr.setFont(caption(10.5f));
    gr.setColour(textFaint);
    auto label = [&](const std::vector<std::unique_ptr<RotaryKnob>>& ks, const char* t)
    {
        if (ks.empty()) return;
        const auto a = ks.front()->getBounds(), b = ks.back()->getBounds();
        gr.drawText(t, juce::Rectangle<int>(a.getX(), a.getY() - 18, b.getRight() - a.getX(), 14), juce::Justification::centredLeft);
        gr.setColour(edge);
        gr.drawHorizontalLine(a.getY() - 3, float(a.getX()), float(b.getRight()));
        gr.setColour(textFaint);
    };
    label(grain, "GRAIN");
    label(pitch, "PITCH / DIRECTION");
    label(out, "STEREO / OUTPUT");
}
} // namespace ambient::ui
