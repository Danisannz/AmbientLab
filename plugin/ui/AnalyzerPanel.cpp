#include "AnalyzerPanel.h"
#include "AnalyzerViews.h"

namespace ambient::ui
{
using namespace theme;

static const juce::Identifier kAnMode { "an_mode" };

AnalyzerPanel::AnalyzerPanel(viz::VisualizerHub& h, UiSettings& s) : hub(h), settings(s)
{
    views[0] = createScopeView(hub, settings);
    views[1] = createStereoView(hub, settings);
    views[2] = createSpectrumView(hub, settings);
    for (auto& v : views) addChildComponent(*v);

    modes.onChange = [this](int i) { setMode(i); };
    addAndMakeVisible(modes);

    freeze.setClickingTogglesState(true);
    freeze.onClick = [this] { hub.setFrozen(freeze.getToggleState()); repaint(headerArea); };
    freeze.setTooltip("Freeze the display (analysis pauses; audio is unaffected)");
    addAndMakeVisible(freeze);

    expand.setClickingTogglesState(true);
    expand.onClick = [this]
    {
        expanded = expand.getToggleState();
        expand.setButtonText(expanded ? "Dock" : "Expand");
        if (onExpandChanged) onExpandChanged(expanded);
    };
    expand.setTooltip("Expand the analyzer over the work area");
    addAndMakeVisible(expand);

    addAndMakeVisible(correlation);
    correlation.setShowLabels(false);

    const int mode = juce::jlimit(0, 2, int(settings.get(kAnMode, 2)));
    modes.setSelected(mode, false);
    setMode(mode);
}

AnalyzerPanel::~AnalyzerPanel() = default;

AnalyzerView* AnalyzerPanel::activeView() const { return views[size_t(int(hub.getMode()))].get(); }

void AnalyzerPanel::setMode(int m)
{
    hub.setMode(viz::AnalyzerMode(m));
    settings.set(kAnMode, m);
    for (int i = 0; i < 3; ++i) views[size_t(i)]->setVisible(i == m);
    views[size_t(m)]->activated();
    repaint();
}

void AnalyzerPanel::setExpanded(bool e)
{
    expanded = e;
    expand.setToggleState(e, juce::dontSendNotification);
    expand.setButtonText(e ? "Dock" : "Expand");
}

void AnalyzerPanel::setQuality(GraphicsQuality q)
{
    quality = q;
    for (auto& v : views) v->setQuality(q);
    repaint();
}

void AnalyzerPanel::setShowStats(bool s)
{
    showStats = s;
    repaint(headerArea);
}

void AnalyzerPanel::tick(bool fresh, float dt)
{
    const bool wasLive = framesSinceData < 15;
    framesSinceData = fresh ? 0 : framesSinceData + 1;
    const bool live = framesSinceData < 15;
    if (live != wasLive) repaint(headerArea);

    if (auto* v = activeView()) v->refresh(fresh, dt);

    // Monitor strip (always visible). Frozen -> hold values.
    if (! hub.isFrozen())
    {
        const auto& st = hub.getStereoStats();
        correlation.setCorrelation(live ? st.correlation : 0.0f, dt);
        if ((monitorTimer += dt) > 0.1f)
        {
            monitorTimer = 0.0f;
            width = st.width; rmsL = st.rmsL; rmsR = st.rmsR;
            repaint(monitorArea);
        }
    }

    if ((statsTimer += dt) >= 1.0f)
    {
        statsTimer = 0.0f;
        fps = paintsThisSecond;
        paintsThisSecond = 0;
        if (showStats) repaint(headerArea);
    }
}

void AnalyzerPanel::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop(cardHeader);

    auto bar = r.removeFromTop(40).reduced(10, 6);
    expand.setBounds(bar.removeFromRight(76));
    bar.removeFromRight(6);
    freeze.setBounds(bar.removeFromRight(76));
    bar.removeFromRight(12);
    modes.setBounds(bar.removeFromLeft(juce::jmin(330, bar.getWidth())));

    monitorArea = r.removeFromBottom(34).reduced(10, 0).withTrimmedBottom(8);
    {
        auto m = monitorArea;
        m.removeFromLeft(48);                         // "PHASE" caption
        correlation.setBounds(m.removeFromLeft(juce::jmax(80, m.getWidth() - 260)).reduced(0, 6));
    }

    const auto viewArea = r.reduced(10, 4);
    for (auto& v : views) v->setBounds(viewArea);
}

void AnalyzerPanel::paint(juce::Graphics& g)
{
    paintStart = juce::Time::getMillisecondCounterHiRes();

    auto r = getLocalBounds().toFloat().reduced(0.5f);
    juce::ColourGradient bodyGrad(panelTop.withAlpha(0.97f), 0.0f, r.getY(), panel.withAlpha(0.97f), 0.0f, r.getBottom(), false);
    g.setGradientFill(bodyGrad);
    g.fillRoundedRectangle(r, cornerRadius);
    g.setColour(edge);
    g.drawRoundedRectangle(r, cornerRadius, 1.0f);

    // Header
    auto hdr = headerArea.toFloat();
    {
        juce::Graphics::ScopedSaveState s(g);
        juce::Path cardShape; cardShape.addRoundedRectangle(r, cornerRadius);
        g.reduceClipRegion(cardShape);
        g.setColour(panelHdr);
        g.fillRect(hdr);
        juce::ColourGradient acc(lime.withAlpha(0.8f), hdr.getX(), 0.0f, lime.withAlpha(0.0f), hdr.getX() + hdr.getWidth() * 0.4f, 0.0f, false);
        g.setGradientFill(acc);
        g.fillRect(hdr.withHeight(1.5f));
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRect(hdr.withY(hdr.getBottom() - 1.0f).withHeight(1.0f));
    }
    drawGlyph(g, Glyph::Lfo, juce::Rectangle<float>(hdr.getX() + 12.0f, hdr.getY(), 18.0f, hdr.getHeight()).reduced(0.0f, 9.0f), lime, 1.2f);
    g.setColour(text);
    g.setFont(heading(13.0f, false, 0.07f));
    g.drawText("ANALYZER", hdr.withTrimmedLeft(38.0f), juce::Justification::centredLeft);

    // Status pill: LIVE / NO SIGNAL / FROZEN
    const bool frozen = hub.isFrozen();
    const bool live = framesSinceData < 15 && ! frozen;
    const juce::String status = frozen ? "FROZEN" : (live ? "LIVE" : "NO SIGNAL");
    const auto col = frozen ? uvBright : (live ? lime : textFaint);
    const auto sf = caption(10.5f);
    const float pw = textWidth(sf, status) + 26.0f;
    auto pill = juce::Rectangle<float>(pw, 18.0f).withCentre({ hdr.getRight() - 12.0f - pw * 0.5f, hdr.getCentreY() });
    g.setColour(col.withAlpha(0.12f));
    g.fillRoundedRectangle(pill, 9.0f);
    g.setColour(col.withAlpha(0.6f));
    g.drawRoundedRectangle(pill, 9.0f, 1.0f);
    g.setColour(col);
    g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({ pill.getX() + 11.0f, pill.getCentreY() }));
    g.setFont(sf);
    g.drawText(status, pill.withTrimmedLeft(18.0f), juce::Justification::centredLeft);

    if (showStats)
    {
        g.setFont(body(11.5f));
        g.setColour(textDim);
        g.drawText(juce::String(avgPaintMs, 2) + " ms/frame  " + juce::String(fps) + " fps  "
                       + (quality == GraphicsQuality::High ? "HQ" : "STD"),
                   hdr.withTrimmedRight(pw + 24.0f), juce::Justification::centredRight);
    }

    // Monitor strip
    auto m = monitorArea.toFloat();
    g.setFont(caption(10.5f));
    g.setColour(textFaint);
    g.drawText("PHASE", m.withWidth(46.0f), juce::Justification::centredLeft);
    auto right = m.withLeft(float(correlation.getRight()) + 12.0f);
    auto cellText = [&](juce::Rectangle<float> c, const juce::String& lbl, const juce::String& val, juce::Colour vc)
    {
        g.setFont(caption(10.5f));
        g.setColour(textFaint);
        const float lw = textWidth(caption(10.5f), lbl) + 6.0f;
        g.drawText(lbl, c.removeFromLeft(lw), juce::Justification::centredLeft);
        g.setFont(body(12.5f, Weight::Medium));
        g.setColour(vc);
        g.drawText(val, c, juce::Justification::centredLeft);
    };
    const float cw = right.getWidth() / 3.0f;
    const auto corrCol = correlation.getDisplayed() < 0.0f ? magenta : text;
    cellText(right.removeFromLeft(cw * 0.85f), "CORR", juce::String(correlation.getDisplayed(), 2), corrCol);
    cellText(right.removeFromLeft(cw * 0.95f), "WIDTH", juce::String(juce::roundToInt(width * 100.0f)) + " %", text);
    auto db = [](float lin) { const float d = juce::Decibels::gainToDecibels(lin, -100.0f); return d <= -99.0f ? juce::String("-inf") : juce::String(d, 1); };
    cellText(right, "RMS", db(rmsL) + " | " + db(rmsR), text);
}

void AnalyzerPanel::paintOverChildren(juce::Graphics&)
{
    // paint() of this panel + the visible view's paint() happened in between.
    const double ms = juce::Time::getMillisecondCounterHiRes() - paintStart;
    avgPaintMs = avgPaintMs * 0.9 + ms * 0.1;
    ++paintsThisSecond;
}
} // namespace ambient::ui
