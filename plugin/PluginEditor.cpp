#include "PluginEditor.h"
#include "Parameters.h"
#include <ambient/ParamIDs.h>
#include <BinaryData.h>

using namespace ambient;
using namespace ambient::ui;

// UI-only state (stored in the "UI" child of the plugin state, never host parameters).
static const juce::Identifier kUiWidth { "uiWidth" }, kUiHeight { "uiHeight" }, kUiTab { "uiTab" },
                              kUiQuality { "quality" }, kAnExpanded { "an_expanded" };

// ============================================================================
// Page
// ============================================================================
ModuleCard& AmbientEditor::Page::add(const juce::String& title, Glyph glyph, int c, int r, int cs, int rs)
{
    slots.push_back({ std::make_unique<ModuleCard>(title, glyph), c, r, cs, rs });
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
    : AudioProcessorEditor(p), proc(p), settings(p.apvts), hub(p.captureRing)
{
    setLookAndFeel(&lnf);
    logo = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);

    header = std::make_unique<HeaderBar>(proc, logo);
    header->onScaleChosen = [this](float s) { setScale(s); };
    header->onQualityChosen = [this](GraphicsQuality q) { setQuality(q); };
    header->isRenderStatsOn = [this] { return analyzer->isShowingStats(); };
    header->onRenderStatsChanged = [this](bool on) { analyzer->setShowStats(on); };
    addAndMakeVisible(*header);

    tabs.onChange = [this](int i) { showPage(i); };
    addAndMakeVisible(tabs);

    buildPages();
    for (auto& pg : pages) addChildComponent(pg);

    analyzer = std::make_unique<AnalyzerPanel>(hub, settings);
    analyzer->onExpandChanged = [this](bool e)
    {
        analyzerExpanded = e;
        settings.set(kAnExpanded, e);
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

    analyzerExpanded = bool(settings.get(kAnExpanded, false));
    analyzer->setExpanded(analyzerExpanded);
    tabs.setSelected(int(settings.get(kUiTab, 0)), false);
    showPage(tabs.getSelected());

    setResizable(true, true);
    setResizeLimits(kMinW, kMinH, kMaxW, kMaxH);
    const int w = settings.get(kUiWidth, kBaseW);
    const int h = settings.get(kUiHeight, kBaseH);
    setSize(juce::jlimit(kMinW, kMaxW, w), juce::jlimit(kMinH, kMaxH, h));

    setQuality(settings.quality());

    proc.editorOpen.store(true);
    lastTick = juce::Time::getMillisecondCounterHiRes();
}

AmbientEditor::~AmbientEditor()
{
    stopTimer();
    proc.editorOpen.store(false);
    setLookAndFeel(nullptr);
}

void AmbientEditor::setQuality(GraphicsQuality q)
{
    quality = q;
    settings.set(kUiQuality, int(q));
    lnf.setQuality(q);
    header->setQuality(q);
    analyzer->setQuality(q);
    // Standard: 30 Hz UI/analyzer refresh. High quality: 60 Hz.
    startTimerHz(q == GraphicsQuality::High ? 60 : 30);
    repaint();
}

void AmbientEditor::showPage(int index)
{
    for (int k = 0; k < int(pages.size()); ++k)
        pages[size_t(k)].setVisible(k == index && ! analyzerExpanded);
    settings.set(kUiTab, index);
}

void AmbientEditor::buildPages()
{
    // ---- SOUND -------------------------------------------------------------
    auto& sound = pages[0];
    sound.cols = 3; sound.rows = 2;
#if AMBIENT_IS_INSTRUMENT
    sound.add("Oscillators", Glyph::Oscillator, 0, 0, 2).setPlaceholder("Analog-modelled oscillators, sub and noise", 2);
    sound.add("Filter", Glyph::Filter, 2, 0).setPlaceholder("Multimode filter", 2);
    sound.add("Envelopes", Glyph::Envelope, 0, 1).setPlaceholder("Amplitude and modulation envelopes", 2);
    auto& io = sound.add("Source & Input", Glyph::Input, 1, 1, 2);
#else
    sound.add("Synthesis", Glyph::Synth, 0, 0, 3).setPlaceholder("Oscillators live in the AmbientLab instrument. This FX version processes the track input.", 0);
    auto& io = sound.add("Input", Glyph::Input, 0, 1, 3);
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
        // Knobs shrink before anything overlaps; the toggle keeps its measured width.
        auto c = io.getContentBounds().reduced(4, 0);
        const int toggleW = juce::roundToInt(theme::textWidth(theme::body(12.5f, theme::Weight::Medium), bypassSynth.getButtonText())) + 48;
        const int knobs = mixKnob ? 2 : 1;
        const int fixed = 8 + (knobs - 1) * 12 + (mixKnob ? 22 + toggleW : 0);
        const int perKnob = (c.getWidth() - fixed) / knobs;
        const int d = juce::jlimit(36, 72, juce::jmin(perKnob - 28, c.getHeight() - 40));
        const int kh = RotaryKnob::heightFor(d, false), kw = d + 28;
        auto row = c.withSizeKeepingCentre(c.getWidth(), juce::jmin(c.getHeight(), kh));
        row.removeFromLeft(8);
        inputKnob->setBounds(row.removeFromLeft(kw));
        if (mixKnob)
        {
            row.removeFromLeft(12);
            mixKnob->setBounds(row.removeFromLeft(kw));
            row.removeFromLeft(22);
            bypassSynth.setBounds(row.removeFromLeft(toggleW).withSizeKeepingCentre(toggleW, 26));
        }
    };

    // ---- PROCESS -------------------------------------------------------------
    auto& process = pages[1];
    process.cols = 2; process.rows = 2;
    process.add("Granular", Glyph::Grains, 0, 0).setPlaceholder("Real-time granular engine with freeze and grain feedback", 3);
    process.add("Reverb", Glyph::Reverb, 1, 0).setPlaceholder("Shimmer, hall, plate, spring, room, cathedral and infinite reverbs", 4);
    process.add("Delay", Glyph::Delay, 0, 1).setPlaceholder("BBD, tape, digital, ping-pong, multi-tap and granular delays", 5);
    process.add("Stereo", Glyph::Stereo, 1, 1).setPlaceholder("Width, mid/side, Haas, auto-pan, orbit and routing", 8);

    // ---- MODULATION ------------------------------------------------------------
    auto& mod = pages[2];
    mod.cols = 4; mod.rows = 2;
    for (int i = 0; i < 4; ++i)
        mod.add("LFO " + juce::String(i + 1), Glyph::Lfo, i, 0).setPlaceholder("14 waveforms, sync, fade-in and stereo phase", 6);
    mod.add("Modulation matrix", Glyph::Matrix, 0, 1, 4).setPlaceholder("32 routing slots with depth, polarity, attenuation and inversion", 6);

    // ---- SEQUENCER ---------------------------------------------------------------
    auto& seq = pages[3];
    seq.add("Step sequencer", Glyph::Sequencer, 0, 0)
        .setPlaceholder("16/32 steps with gates, probability, ratchets, swing and parameter locks", 7);
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
    analyzer->tick(fresh, dt);
    header->tick(dt);
}

void AmbientEditor::paint(juce::Graphics& g)
{
    backdrop.draw(g, Backdrop::Region::Window, getLocalBounds(), quality);
}

void AmbientEditor::resized()
{
    settings.set(kUiWidth, getWidth());
    settings.set(kUiHeight, getHeight());

    const float s = juce::jlimit(0.85f, 1.35f, float(getWidth()) / float(kBaseW));
    auto r = getLocalBounds();
    header->setBounds(r.removeFromTop(theme::headerHeight));
    tabs.setBounds(r.removeFromTop(theme::tabHeight));

    r.reduce(theme::gap + 2, theme::gap);
    auto foot = r.removeFromBottom(theme::footerHeight);
    r.removeFromBottom(theme::gap);

    macroCard.setScale(s);
    macroCard.setBounds(foot);
    {
        auto c = macroCard.getContentBounds();
        const int w = c.getWidth() / int(macros.size());
        for (auto& k : macros) k->setBounds(c.removeFromLeft(w).reduced(6, 0));
    }

    // Analyzer: generous (~40 % of the width), the whole body when expanded.
    if (analyzerExpanded)
    {
        analyzer->setBounds(r);
    }
    else
    {
        const int aw = juce::jlimit(400, 900, juce::roundToInt(r.getWidth() * 0.40f));
        analyzer->setBounds(r.removeFromRight(aw));
        r.removeFromRight(theme::gap);
        for (auto& pg : pages) { pg.scale = s; pg.setBounds(r); }
    }
}
