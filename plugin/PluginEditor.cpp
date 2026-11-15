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
    std::vector<float> wts(size_t(rows), 1.0f);
    for (size_t i = 0; i < rowWeights.size() && i < wts.size(); ++i) wts[i] = rowWeights[i];
    float total = 0.0f;
    for (auto w : wts) total += w;
    std::vector<float> rowY(size_t(rows) + 1, 0.0f);
    for (int i = 0; i < rows; ++i) rowY[size_t(i) + 1] = rowY[size_t(i)] + float(r.getHeight() + g) * wts[size_t(i)] / total;
    for (auto& s : slots)
    {
        const int x = juce::roundToInt(s.col * cw), y = juce::roundToInt(rowY[size_t(s.row)]);
        const int w = juce::roundToInt(s.colSpan * cw) - g, h = juce::roundToInt(rowY[size_t(s.row + s.rowSpan)]) - y - g;
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
    addAndMakeVisible(content);
    content.setOpaque(true);
    content.painter = [this](juce::Graphics& g) { backdrop.draw(g, Backdrop::Region::Window, content.getLocalBounds(), quality); };
    logo = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);

    header = std::make_unique<HeaderBar>(proc, logo);
    header->onScaleChosen = [this](float s) { setScale(s); };
    header->onQualityChosen = [this](GraphicsQuality q) { setQuality(q); };
    header->isRenderStatsOn = [this] { return analyzer->isShowingStats(); };
    header->onRenderStatsChanged = [this](bool on) { analyzer->setShowStats(on); };
    header->isMascotOn = [this] { return bool(settings.get("mascot", true)); };
    header->onMascotChanged = [this](bool on) { setMascotAnimated(on); };
    content.addAndMakeVisible(*header);

    tabs.onChange = [this](int i) { showPage(i); };
    content.addAndMakeVisible(tabs);

    buildPages();
    for (auto& pg : pages) content.addChildComponent(pg);

    analyzer = std::make_unique<AnalyzerPanel>(hub, settings);
    analyzer->onExpandChanged = [this](bool e)
    {
        analyzerExpanded = e;
        settings.set(kAnExpanded, e);
        showPage(tabs.getSelected());
        resized();
    };
    content.addAndMakeVisible(*analyzer);

    // Macros footer
    content.addAndMakeVisible(macroCard);
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
    setMascotAnimated(bool(settings.get("mascot", true)));

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
    if (sidechain) sidechain->setQuality(q);
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
    // ---- SOUND: sound generation -------------------------------------------
    auto& sound = pages[0];
    sound.cols = 2; sound.rows = 2;
    sound.rowWeights = { 2.4f, 1.0f };
#if AMBIENT_IS_INSTRUMENT
    auto& osc = sound.add("Oscillators", Glyph::Oscillator, 0, 0, 2);
    oscillators = std::make_unique<OscillatorPanel>(proc.apvts);
    osc.addAndMakeVisible(*oscillators);
    osc.addAndMakeVisible(bypassSynth); // synth bypass (existing parameter) on the card header
    osc.setHeaderLedVisible(false);
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, id(ids::bypassSynth), bypassSynth);
    sound.add("Filter", Glyph::Filter, 0, 1).setPlaceholder("Multimode filter with drive and modulation", 3);
    sound.add("Envelopes", Glyph::Envelope, 1, 1).setPlaceholder("ADSR amplitude and modulation envelopes", 3);
    sound.layoutContents = [this, &osc]
    {
        oscillators->setBounds(osc.getContentBounds().withTrimmedTop(2));
        bypassSynth.setBounds(osc.getHeaderControlBounds());
    };
#else
    sound.add("Synthesis", Glyph::Synth, 0, 0, 1).setPlaceholder("Oscillators live in the AmbientLab instrument", 0);
    auto& track = sound.add("Track input", Glyph::Input, 1, 0);
    inputKnob = std::make_unique<RotaryKnob>(proc.apvts, id(ids::inputGain), "Input");
    inputKnob->setCompact(true);
    track.addAndMakeVisible(*inputKnob);
    sound.rowWeights = { 1.0f, 1.0f };
    sound.layoutContents = [this, &track]
    {
        auto c = track.getContentBounds();
        inputKnob->setBounds(c.withSizeKeepingCentre(juce::jmin(90, c.getWidth()), juce::jmin(RotaryKnob::heightFor(40, true), c.getHeight())));
    };
#endif

    // ---- PROCESS: Frequency Sidechain + upcoming processors -------------------
    auto& process = pages[1];
    process.cols = 4; process.rows = 2;
    process.rowWeights = { 2.6f, 1.0f };
    auto& sc = process.add("Frequency sidechain", Glyph::Filter, 0, 0, 4);
    sidechain = std::make_unique<SidechainPanel>(proc, settings);
    sc.addAndMakeVisible(*sidechain);
    sc.addAndMakeVisible(sidechain->getEnableButton());
    sc.setHeaderLedVisible(false);
    process.add("Granular", Glyph::Grains, 0, 1).setPlaceholder("Real-time granular engine", 4);
    process.add("Reverb", Glyph::Reverb, 1, 1).setPlaceholder("Seven reverb algorithms", 5);
    process.add("Delay", Glyph::Delay, 2, 1).setPlaceholder("Eight delay models", 6);
    process.add("Routing", Glyph::Stereo, 3, 1).setPlaceholder("Serial / parallel / flexible routing", 9);
    process.layoutContents = [this, &sc]
    {
        sidechain->setBounds(sc.getContentBounds().withTrimmedTop(4));
        sidechain->getEnableButton().setBounds(sc.getHeaderControlBounds());
    };

    // ---- MODULATION ------------------------------------------------------------
    auto& mod = pages[2];
    mod.cols = 4; mod.rows = 2;
    for (int i = 0; i < 4; ++i)
        mod.add("LFO " + juce::String(i + 1), Glyph::Lfo, i, 0).setPlaceholder("14 waveforms, sync, fade-in and stereo phase", 6);
    mod.add("Modulation matrix", Glyph::Matrix, 0, 1, 4).setPlaceholder("32 routing slots with depth, polarity, attenuation and inversion", 6);

    // ---- SEQUENCER ---------------------------------------------------------------
    auto& seq = pages[3];
    seq.add("Step sequencer", Glyph::Sequencer, 0, 0)
        .setPlaceholder("16/32 steps, gates, probability, ratchets, parameter locks and scale mode (16 scales, LFO-modulatable scale and root)", 7);
}

void AmbientEditor::setMascotAnimated(bool on)
{
    settings.set("mascot", on);
    header->setMascotAnimated(on);
    if (sidechain) sidechain->getMiniMascot().setAnimated(on);
}

void AmbientEditor::setAnalyzerFrozen(bool f)
{
    hub.setFrozen(f);
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
    if (sidechain) sidechain->tick(dt, pages[1].isVisible());
}

void AmbientEditor::paint(juce::Graphics& g)
{
    g.fillAll(theme::bg); // fully covered by `content`
}

void AmbientEditor::resized()
{
    settings.set(kUiWidth, getWidth());
    settings.set(kUiHeight, getHeight());

    // Uniform scale: 1x up to 2x, from the smaller of the width/height ratios.
    // Below the 1280x800 design size nothing shrinks (text stays readable); the
    // layout reflows instead. Different aspect ratios widen/heighten the canvas.
    uiScale = juce::jlimit(1.0f, 2.0f, juce::jmin(float(getWidth()) / float(kBaseW), float(getHeight()) / float(kBaseH)));
    content.setTransform(juce::AffineTransform::scale(uiScale));
    content.setBounds(0, 0, juce::roundToInt(float(getWidth()) / uiScale), juce::roundToInt(float(getHeight()) / uiScale));

    const float s = 1.0f; // card metrics are now scaled by the transform
    auto r = content.getLocalBounds();
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
        // Sound tab: on short layouts the placeholder row collapses to header strips
        // so the Frequency Sidechain keeps a usable graph.
        // Short layouts: placeholder rows collapse to header strips so real modules keep their space.
        const bool shortLayout = r.getHeight() < 520;
       #if AMBIENT_IS_INSTRUMENT
        pages[0].rowWeights = shortLayout ? std::vector<float> { 1.0f, 0.12f } : std::vector<float> { 2.4f, 1.0f };
       #endif
        pages[1].rowWeights = shortLayout ? std::vector<float> { 1.0f, 0.12f } : std::vector<float> { 2.6f, 1.0f };
        for (auto& pg : pages) { pg.scale = s; pg.setBounds(r); pg.resized(); }
    }
}
