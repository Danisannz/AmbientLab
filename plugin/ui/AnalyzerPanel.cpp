#include "AnalyzerPanel.h"

namespace ambient::ui
{
using namespace theme;

AnalyzerPanel::AnalyzerPanel(viz::VisualizerHub& h) : hub(h)
{
    modes.setSelected(int(hub.getMode()), false);
    modes.onChange = [this](int i) { hub.setMode(viz::AnalyzerMode(i)); repaint(); };
    addAndMakeVisible(modes);

    freeze.setClickingTogglesState(true);
    freeze.onClick = [this] { hub.setFrozen(freeze.getToggleState()); repaint(); };
    freeze.setTooltip("Freeze the display");
    addAndMakeVisible(freeze);

    expand.setClickingTogglesState(true);
    expand.onClick = [this] { expanded = expand.getToggleState(); if (onExpandChanged) onExpandChanged(expanded); };
    expand.setTooltip("Expand the analyzer over the work area");
    addAndMakeVisible(expand);
}

void AnalyzerPanel::setExpanded(bool e)
{
    expanded = e;
    expand.setToggleState(e, juce::dontSendNotification);
}

void AnalyzerPanel::tick(bool fresh)
{
    const bool wasLive = framesSinceData < 15;
    framesSinceData = fresh ? 0 : framesSinceData + 1;
    const bool live = framesSinceData < 15;
    if (live != wasLive) repaint(getLocalBounds().removeFromTop(cardHeader + 8));
}

juce::Rectangle<int> AnalyzerPanel::screenBounds() const
{
    return getLocalBounds().withTrimmedTop(cardHeader + 34).reduced(8);
}

void AnalyzerPanel::resized()
{
    auto bar = getLocalBounds().withTrimmedTop(cardHeader).removeFromTop(34).reduced(8, 5);
    expand.setBounds(bar.removeFromRight(70));
    bar.removeFromRight(6);
    freeze.setBounds(bar.removeFromRight(66));
    bar.removeFromRight(10);
    modes.setBounds(bar.removeFromLeft(juce::jmin(280, bar.getWidth())));
}

void AnalyzerPanel::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    juce::ColourGradient body(panelTop, 0.0f, r.getY(), panel, 0.0f, r.getBottom(), false);
    g.setGradientFill(body);
    g.fillRoundedRectangle(r, cornerRadius);
    g.setColour(edge);
    g.drawRoundedRectangle(r, cornerRadius, 1.0f);

    // Header
    auto hdr = r.withHeight(float(cardHeader));
    {
        juce::Graphics::ScopedSaveState s(g);
        juce::Path clip; clip.addRoundedRectangle(r, cornerRadius);
        g.reduceClipRegion(clip);
        g.setColour(panelHdr);
        g.fillRect(hdr);
    }
    const bool live = framesSinceData < 15 && ! hub.isFrozen();
    auto led = juce::Rectangle<float>(6, 6).withCentre({ hdr.getX() + 12.0f, hdr.getCentreY() });
    if (live) { g.setColour(lime.withAlpha(0.35f)); g.fillEllipse(led.expanded(2.5f)); }
    g.setColour(live ? lime : textFaint);
    g.fillEllipse(led);
    g.setColour(text);
    g.setFont(labelFont(11.0f));
    g.drawText("ANALYZER", hdr.withTrimmedLeft(22.0f), juce::Justification::centredLeft);
    g.setColour(textFaint);
    g.setFont(labelFont(9.0f));
    g.drawText(hub.isFrozen() ? "FROZEN" : (live ? "LIVE" : "NO SIGNAL"), hdr.reduced(10.0f, 0.0f),
               juce::Justification::centredRight);

    // Screen
    auto sc = screenBounds().toFloat();
    g.setColour(bgDeep);
    g.fillRoundedRectangle(sc, 3.0f);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawRoundedRectangle(sc.reduced(0.5f), 3.0f, 1.0f);

    // Reticle grid
    g.setColour(grid);
    for (int i = 1; i < 8; ++i) g.drawVerticalLine(int(sc.getX() + sc.getWidth() * i / 8.0f), sc.getY() + 2, sc.getBottom() - 2);
    for (int i = 1; i < 6; ++i) g.drawHorizontalLine(int(sc.getY() + sc.getHeight() * i / 6.0f), sc.getX() + 2, sc.getRight() - 2);

    static const char* names[] = { "OSCILLOSCOPE", "STEREO VECTORSCOPE", "SPECTRUM ANALYZER" };
    g.setColour(textFaint);
    g.setFont(labelFont(10.0f));
    g.drawText(juce::String(names[int(hub.getMode())]) + "  -  VIEW ARRIVES IN UI-C",
               sc.toNearestInt(), juce::Justification::centred);
}
} // namespace ambient::ui
