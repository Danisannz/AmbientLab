#include "PluginEditor.h"
#include "Parameters.h"
#include <ambient/ParamIDs.h>
#include <BinaryData.h>

using namespace ambient;
using namespace ambient::ui;

// UI-only state stored alongside (not inside) the parameters.
static const juce::Identifier kUiWidth { "uiWidth" }, kUiHeight { "uiHeight" }, kUiTab { "uiTab" };

// ============================================================================
// Page
// ============================================================================
ModuleCard& AmbientEditor::Page::add(const juce::String& title, int c, int r, int cs, int rs)
{
    slots.push_back({ std::make_unique<ModuleCard>(title), c, r, cs, rs });
    addAndMakeVisible(*slots.back().card);
    return *slots.back().card;
}

void AmbientEditor::Page::resized()
{
    const int g = theme::gap;
    const auto r = getLocalBounds();
    const float cw = float(r.getWidth() + g) / float(cols);
    const float rh = float(r.getHeight() + g) / float(rows);
    for (auto& s : slots)
    {
        const int x = juce::roundToInt(s.col * cw), y = juce::roundToInt(s.row * rh);
        const int w = juce::roundToInt(s.colSpan * cw) - g, h = juce::roundToInt(s.rowSpan * rh) - g;
        s.card->setScale(scale);
        s.card->setBounds(x, y, w, h);
    }
    if (layoutContents) layoutContents();
}

// ============================================================================
// Editor
// ============================================================================
AmbientEditor::AmbientEditor(AmbientProcessor& p)
    : AudioProcessorEditor(p), proc(p), hub(p.captureRing)
{
    setLookAndFeel(&lnf);
    logo = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);

    header = std::make_unique<HeaderBar>(proc, logo);
    header->onScaleChosen = [this](float s) { setScale(s); };
    addAndMakeVisible(*header);

    tabs.onChange = [this](int i) { showPage(i); };
    addAndMakeVisible(tabs);

    buildPages();
    for (auto& pg : pages) addChildComponent(pg);

    analyzer = std::make_unique<AnalyzerPanel>(hub);
    analyzer->onExpandChanged = [this](bool e)
    {
        analyzerExpanded = e;
        showPage(tabs.getSelected());
        resized();
    };
    addAndMakeVisible(*analyzer);

    // Macros footer
    addAndMakeVisible(macroCard);
    for (size_t i = 0; i < ids::macros.size(); ++i)
    {
        auto k = std::make_unique<RotaryKnob>(proc.apvts, id(ids::macros[i]), "Macro " + juce::String(int(i) + 1));
        k->setCompact(true);
        macroCard.addAndMakeVisible(*k);
        macros.push_back(std::move(k));
    }

    tabs.setSelected(int(proc.apvts.state.getProperty(kUiTab, 0)), false);
    showPage(tabs.getSelected());

    setResizable(true, true);
    setResizeLimits(960, 600, 2560, 1600);
    const int w = proc.apvts.state.getProperty(kUiWidth, kBaseW);
    const int h = proc.apvts.state.getProperty(kUiHeight, kBaseH);
    setSize(juce::jlimit(960, 2560, w), juce::jlimit(600, 1600, h));

    proc.editorOpen.store(true);
    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(60);
}

AmbientEditor::~AmbientEditor()
{
    stopTimer();
    proc.editorOpen.store(false);
    setLookAndFeel(nullptr);
}

void AmbientEditor::showPage(int index)
{
    for (int k = 0; k < int(pages.size()); ++k)
        pages[size_t(k)].setVisible(k == index && ! analyzerExpanded);
    proc.apvts.state.setProperty(kUiTab, index, nullptr);
}

void AmbientEditor::buildPages()
{
    // ---- SOUND -------------------------------------------------------------
    auto& sound = pages[0];
    sound.cols = 3; sound.rows = 2;
#if AMBIENT_IS_INSTRUMENT
    sound.add("Oscillators", 0, 0, 2).setPlaceholder("Oscillators, sub and noise\narrive in Stage 2");
    sound.add("Filter", 2, 0).setPlaceholder("Filter arrives in Stage 2");
    sound.add("Envelopes", 0, 1).setPlaceholder("Amp and mod envelopes\narrive in Stage 2");
    auto& io = sound.add("Source & Input", 1, 1, 2);
#else
    sound.add("Synthesis", 0, 0, 3).setPlaceholder("Oscillators live in the AmbientLab instrument.\nThis FX version processes the track input.");
    auto& io = sound.add("Input", 0, 1, 3);
#endif

    inputKnob = std::make_unique<RotaryKnob>(proc.apvts, id(ids::inputGain), "Input");
    io.addAndMakeVisible(*inputKnob);
#if AMBIENT_IS_INSTRUMENT
    mixKnob = std::make_unique<RotaryKnob>(proc.apvts, id(ids::sourceMix), "Synth / Input");
    io.addAndMakeVisible(*mixKnob);
    io.addAndMakeVisible(bypassSynth);
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, id(ids::bypassSynth), bypassSynth);
#endif

    sound.layoutContents = [this, &io]
    {
        auto c = io.getContentBounds();
        const int k = juce::jmin(96, c.getHeight());
        auto row = c.withSizeKeepingCentre(c.getWidth(), k);
        inputKnob->setBounds(row.removeFromLeft(88));
        if (mixKnob) { row.removeFromLeft(8); mixKnob->setBounds(row.removeFromLeft(88)); }
        row.removeFromLeft(20);
        bypassSynth.setBounds(row.removeFromLeft(170).withSizeKeepingCentre(170, 24));
    };

    // ---- PROCESS -------------------------------------------------------------
    auto& process = pages[1];
    process.cols = 2; process.rows = 2;
    process.add("Granular", 0, 0).setPlaceholder("Granular engine arrives in Stage 3");
    process.add("Reverb", 1, 0).setPlaceholder("Reverb algorithms arrive in Stage 4");
    process.add("Delay", 0, 1).setPlaceholder("Delay algorithms arrive in Stage 5");
    process.add("Stereo", 1, 1).setPlaceholder("Stereo engine and routing arrive in Stage 8");

    // ---- MODULATION ------------------------------------------------------------
    auto& mod = pages[2];
    mod.cols = 4; mod.rows = 2;
    for (int i = 0; i < 4; ++i)
        mod.add("LFO " + juce::String(i + 1), i, 0).setPlaceholder("Arrives in Stage 6");
    mod.add("Modulation matrix", 0, 1, 4).setPlaceholder("32-slot modulation matrix arrives in Stage 6");

    // ---- SEQUENCER ---------------------------------------------------------------
    auto& seq = pages[3];
    seq.add("Step sequencer", 0, 0).setPlaceholder("16/32-step sequencer with gates, probability,\nratchets and parameter locks arrives in Stage 7");
}

void AmbientEditor::setScale(float s)
{
    setSize(juce::roundToInt(kBaseW * s), juce::roundToInt(kBaseH * s));
}

void AmbientEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = float(juce::jlimit(0.0, 0.25, (now - lastTick) * 0.001));
    lastTick = now;

    const double sr = proc.currentSampleRate.load();
    if (sr != hub.getSampleRate()) hub.setSampleRate(sr);

    const bool fresh = hub.update(dt);
    analyzer->tick(fresh);
    header->tick(dt);
}

void AmbientEditor::paint(juce::Graphics& g)
{
    g.fillAll(theme::bg);
}

void AmbientEditor::resized()
{
    proc.apvts.state.setProperty(kUiWidth, getWidth(), nullptr);
    proc.apvts.state.setProperty(kUiHeight, getHeight(), nullptr);

    const float s = juce::jlimit(0.75f, 1.5f, float(getWidth()) / float(kBaseW));
    auto r = getLocalBounds();
    header->setBounds(r.removeFromTop(theme::headerHeight));
    tabs.setBounds(r.removeFromTop(theme::tabHeight));

    r.reduce(theme::gap, theme::gap);
    auto foot = r.removeFromBottom(theme::footerHeight);
    r.removeFromBottom(theme::gap);

    macroCard.setScale(s);
    macroCard.setBounds(foot);
    {
        auto c = macroCard.getContentBounds();
        const int w = c.getWidth() / int(macros.size());
        for (auto& k : macros) k->setBounds(c.removeFromLeft(w).reduced(4, 0));
    }

    // Analyzer: generous by default (~38% of the width), the whole body when expanded.
    if (analyzerExpanded)
    {
        analyzer->setBounds(r);
    }
    else
    {
        const int aw = juce::jlimit(380, 760, juce::roundToInt(r.getWidth() * 0.38f));
        analyzer->setBounds(r.removeFromRight(aw));
        r.removeFromRight(theme::gap);
        for (auto& pg : pages) { pg.scale = s; pg.setBounds(r); }
    }
}
