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

namespace rv = ambient::ids::rv;

ReverbPanel::ReverbPanel(juce::AudioProcessorValueTreeState& s) : state(s)
{
    power.setLitWhenOn(true);
    power.setTooltip("Reverb on/off");
    powerAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, id(rv::on), power);
    addAndMakeVisible(freeze);
    freeze.setTooltip("Freeze: input muted, tail held indefinitely (lossless loop)");
    freezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, id(rv::freeze), freeze);
    if (auto* p = s.getParameter(id(rv::algo)))
    {
        algo = std::make_unique<ParamSegment>(*p, juce::StringArray { "HALL", "ROOM", "CATHED", "PLATE", "SPRING", "SHIMMER", "INFINITE" });
        addAndMakeVisible(*algo);
    }
    time.push_back(knob(*this, s, rv::decay, "Decay"));
    time.push_back(knob(*this, s, rv::predelay, "Pre-delay"));
    time.push_back(knob(*this, s, rv::size, "Size"));
    character.push_back(knob(*this, s, rv::diffusion, "Diffusion"));
    character.push_back(knob(*this, s, rv::damping, "Damping"));
    character.push_back(knob(*this, s, rv::early, "Early"));
    character.push_back(knob(*this, s, rv::mod, "Mod"));
    out.push_back(knob(*this, s, rv::feedback, "Feedback"));
    out.push_back(knob(*this, s, rv::shimmer, "Shimmer"));
    shimmerKnob = out.back().get();
    shimmerKnob->getSlider().setTooltip("Octave-up regeneration (Shimmer algorithm only)");
    out.push_back(knob(*this, s, rv::width, "Width"));
    out.push_back(knob(*this, s, rv::mix, "Mix"));
    timerCallback();
    startTimerHz(10);
}

void ReverbPanel::timerCallback()
{
    const auto* a = state.getRawParameterValue(id(rv::algo));
    const bool shimmerAlgo = a != nullptr && juce::roundToInt(a->load()) == 5;
    if (shimmerKnob && shimmerKnob->isEnabled() != shimmerAlgo)
    {
        shimmerKnob->setEnabled(shimmerAlgo);
        shimmerKnob->setAlpha(shimmerAlgo ? 1.0f : 0.35f);
    }
}

void ReverbPanel::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop(30).withSizeKeepingCentre(r.getWidth(), 26);
    freeze.setBounds(top.removeFromLeft(96));
    top.removeFromLeft(12);
    if (algo) algo->setBounds(top.removeFromLeft(juce::jmin(520, top.getWidth())));
    r.removeFromTop(10);

    const int total = int(time.size() + character.size() + out.size());
    const int gapW = 18;
    const int kw = juce::jmin(84, (r.getWidth() - 2 * gapW) / total);
    const int d = juce::jlimit(36, 60, kw - 20);
    auto row = r.withHeight(juce::jmin(r.getHeight(), RotaryKnob::heightFor(d, true) + 18)).withTrimmedTop(26);
    for (auto* group : { &time, &character, &out })
    {
        for (auto& k : *group) k->setBounds(row.removeFromLeft(kw));
        row.removeFromLeft(gapW);
    }
}

void ReverbPanel::paint(juce::Graphics& gr)
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
    label(time, "TIME / SPACE");
    label(character, "CHARACTER");
    label(out, "FEEDBACK / OUTPUT");
}

namespace so = ambient::ids::out;

StereoPanel::StereoPanel(AmbientProcessor& p) : proc(p)
{
    auto& s = p.apvts;
    power.setLitWhenOn(true);
    power.setTooltip("Stereo safety: keeps the output correlation above the floor (reduces side only when needed)");
    powerAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, id(so::safety), power);
    for (auto* t : { &bassMono, &monoCheck }) addAndMakeVisible(*t);
    bassMono.setTooltip("Remove side (L-R) energy below the bass-mono frequency");
    monoCheck.setTooltip("Monitor: output (L+R)/2 on both channels");
    bassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, id(so::bassMono), bassMono);
    monoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, id(so::monoCheck), monoCheck);
    knobs.push_back(knob(*this, s, so::width, "Width"));
    knobs.push_back(knob(*this, s, so::floor, "Corr floor", true));
    knobs.push_back(knob(*this, s, so::bassMonoHz, "Bass mono"));
    startTimerHz(15);
}

void StereoPanel::timerCallback()
{
    const auto& t = proc.stereoTelemetry();
    corrIn = t.corrIn.load(std::memory_order_relaxed);
    corrOut = t.corrOut.load(std::memory_order_relaxed);
    midDb = t.midDb.load(std::memory_order_relaxed);
    sideDb = t.sideDb.load(std::memory_order_relaxed);
    corrDb = std::max(t.correctionDb.load(std::memory_order_relaxed), corrDb * 0.85f); // short hold for readability
    anti = t.antiPhase.load(std::memory_order_relaxed);
    repaint(readout);
}

void StereoPanel::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop(30).withSizeKeepingCentre(r.getWidth(), 26);
    bassMono.setBounds(top.removeFromLeft(110)); top.removeFromLeft(10);
    monoCheck.setBounds(top.removeFromLeft(110));
    r.removeFromTop(10);
    const int kw = juce::jmin(84, r.getWidth() / 8);
    const int d = juce::jlimit(36, 60, kw - 20);
    auto row = r.withHeight(juce::jmin(r.getHeight(), RotaryKnob::heightFor(d, true) + 18)).withTrimmedTop(26);
    for (auto& k : knobs) k->setBounds(row.removeFromLeft(kw));
    row.removeFromLeft(24);
    readout = row.withWidth(juce::jmin(row.getWidth(), 420));
}

void StereoPanel::paint(juce::Graphics& gr)
{
    gr.setFont(caption(10.5f));
    gr.setColour(textFaint);
    if (! knobs.empty())
    {
        const auto a = knobs.front()->getBounds(), b = knobs.back()->getBounds();
        gr.drawText("OUTPUT STEREO", juce::Rectangle<int>(a.getX(), a.getY() - 18, b.getRight() - a.getX(), 14), juce::Justification::centredLeft);
        gr.setColour(edge);
        gr.drawHorizontalLine(a.getY() - 3, float(a.getX()), float(b.getRight()));
    }
    auto r = readout;
    gr.setColour(textFaint);
    gr.drawText("MONITOR", r.removeFromTop(14).translated(0, -18), juce::Justification::centredLeft);
    auto line = [&](const juce::String& k, const juce::String& v, juce::Colour c)
    {
        auto l = r.removeFromTop(18);
        gr.setColour(textDim); gr.setFont(caption(10.5f));
        gr.drawText(k, l.removeFromLeft(150), juce::Justification::centredLeft);
        gr.setColour(c); gr.setFont(body(12.0f));
        gr.drawText(v, l, juce::Justification::centredLeft);
    };
    auto cc = [](float c) { return c < 0.0f ? juce::Colour(0xffff4d6d) : (c < 0.3f ? juce::Colour(0xffffc04d) : lime); };
    auto f2 = [](float x) { return (x >= 0 ? "+" : "") + juce::String(x, 2); };
    line("CORRELATION IN", f2(corrIn), cc(corrIn));
    line("CORRELATION OUT", f2(corrOut), cc(corrOut));
    line("MID / SIDE", juce::String(midDb, 1) + " / " + juce::String(sideDb, 1) + " dB", textDim);
    line("SIDE CORRECTION", corrDb > 0.05f ? "-" + juce::String(corrDb, 1) + " dB" : juce::String("none"), corrDb > 0.05f ? uvBright : textDim);
    if (anti) line("WARNING", "ANTI-PHASE CONTENT DETECTED", juce::Colour(0xffff4d6d));
}

namespace dl = ambient::ids::dl;

DelayPanel::DelayPanel(juce::AudioProcessorValueTreeState& s) : state(s)
{
    power.setLitWhenOn(true);
    power.setTooltip("Delay on/off");
    powerAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, id(dl::on), power);
    addAndMakeVisible(sync);
    sync.setTooltip("Sync the delay time to the host tempo (120 BPM when the host sends none)");
    syncAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, id(dl::sync), sync);
    if (auto* p = s.getParameter(id(dl::mode)))
    {
        mode = std::make_unique<ParamSegment>(*p, juce::StringArray { "TAPE", "BBD", "DIGITAL", "PING-PONG", "MULTI", "REVERSE", "PITCH", "GRAIN" });
        addAndMakeVisible(*mode);
    }
    time.push_back(knob(*this, s, dl::time, "Time"));        timeK = time.back().get();
    time.push_back(knob(*this, s, dl::division, "Division")); divK = time.back().get();
    time.push_back(knob(*this, s, dl::feedback, "Feedback"));
    tone.push_back(knob(*this, s, dl::hp, "Low cut"));
    tone.push_back(knob(*this, s, dl::lp, "High cut"));
    tone.push_back(knob(*this, s, dl::drive, "Drive"));
    tone.push_back(knob(*this, s, dl::modDepth, "Mod"));
    tone.push_back(knob(*this, s, dl::modRate, "Rate"));
    out.push_back(knob(*this, s, dl::pitch, "Pitch", true)); pitchK = out.back().get();
    pitchK->getSlider().setTooltip("Semitones per repeat (Pitch type only)");
    out.push_back(knob(*this, s, dl::width, "Width"));
    out.push_back(knob(*this, s, dl::mix, "Mix"));
    timerCallback();
    startTimerHz(10);
}

void DelayPanel::timerCallback()
{
    auto val = [this](std::string_view pid) { const auto* a = state.getRawParameterValue(id(pid)); return a ? a->load() : 0.0f; };
    const bool synced = val(dl::sync) > 0.5f, pitchMode = juce::roundToInt(val(dl::mode)) == 6;
    auto setActive = [](RotaryKnob* k, bool on) { if (k && k->isEnabled() != on) { k->setEnabled(on); k->setAlpha(on ? 1.0f : 0.35f); } };
    setActive(timeK, ! synced);
    setActive(divK, synced);
    setActive(pitchK, pitchMode);
}

void DelayPanel::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop(30).withSizeKeepingCentre(r.getWidth(), 26);
    sync.setBounds(top.removeFromLeft(96));
    top.removeFromLeft(12);
    if (mode) mode->setBounds(top.removeFromLeft(juce::jmin(600, top.getWidth())));
    r.removeFromTop(10);
    const int total = int(time.size() + tone.size() + out.size());
    const int gapW = 18;
    const int kw = juce::jmin(84, (r.getWidth() - 2 * gapW) / total);
    const int d = juce::jlimit(36, 60, kw - 20);
    auto row = r.withHeight(juce::jmin(r.getHeight(), RotaryKnob::heightFor(d, true) + 18)).withTrimmedTop(26);
    for (auto* group : { &time, &tone, &out })
    {
        for (auto& k : *group) k->setBounds(row.removeFromLeft(kw));
        row.removeFromLeft(gapW);
    }
}

void DelayPanel::paint(juce::Graphics& gr)
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
    label(time, "TIME / FEEDBACK");
    label(tone, "TONE / MOTION");
    label(out, "PITCH / OUTPUT");
}
} // namespace ambient::ui
