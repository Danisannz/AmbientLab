#include "HeaderBar.h"
#include "../PluginProcessor.h"
#include "../Parameters.h"
#include <ambient/ParamIDs.h>

namespace ambient::ui
{
using namespace theme;

HeaderBar::HeaderBar(AmbientProcessor& p, juce::Image img)
    : proc(p), logo(std::move(img)),
      master(p.apvts, id(ids::masterGain), "Master")
{
    master.setCompact(true);
    addAndMakeVisible(master);
    addAndMakeVisible(meter);
    addAndMakeVisible(midiLed);
    addAndMakeVisible(audioLed); // instrument: sidechain; FX: track input
    addAndMakeVisible(presetMenuButton);
    addAndMakeVisible(mainMenuButton);
    presetMenuButton.onClick = [this] { showPresetMenu(); };
    mainMenuButton.onClick   = [this] { showMainMenu(); };
    presetMenuButton.setTooltip("Preset actions");
    mainMenuButton.setTooltip("Settings");
}

void HeaderBar::tick(float dt)
{
    const auto mc = proc.midiEventCount.load(std::memory_order_relaxed);
    if (mc != lastMidiCount) { lastMidiCount = mc; midiLed.trigger(); }
    midiLed.tick(dt);

    const float in = proc.inputPeak.load(std::memory_order_relaxed);
    audioLed.setLevel(juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(in, -100.0f) + 60.0f) / 54.0f));
    audioLed.tick(dt);

    meter.setLevels(proc.peakL.load(std::memory_order_relaxed), proc.peakR.load(std::memory_order_relaxed), dt);

    if ((infoTimer += dt) >= 0.25f) // text readouts at 4 Hz: readable, cheap
    {
        infoTimer = 0.0f;
        const auto cpu = juce::String(proc.getCpuLoad() * 100.0, 1) + "%";
        const auto sr  = juce::String(proc.currentSampleRate.load() / 1000.0, 1) + " kHz";
        if (cpu != cpuText || sr != srText) { cpuText = cpu; srText = sr; repaint(infoArea); }
    }
}

void HeaderBar::showPresetMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader("PRESET");
    m.addItem(1, "Reset all parameters to default");
    m.addSeparator();
    m.addItem(2, "Preset library (arrives in Stage 10)", false, false);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetMenuButton),
                    [this](int r)
                    {
                        if (r == 1)
                            for (auto* prm : proc.getParameters())
                                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(prm))
                                    rp->setValueNotifyingHost(rp->getDefaultValue());
                    });
}

void HeaderBar::showMainMenu()
{
    juce::PopupMenu size;
    for (auto s : { 0.75f, 1.0f, 1.25f, 1.5f })
        size.addItem(juce::roundToInt(s * 100), juce::String(juce::roundToInt(s * 100)) + "%");
    juce::PopupMenu m;
    m.addSubMenu("Interface size", size);
    m.addSeparator();
    m.addItem(1, juce::String("AmbientLab ") + JucePlugin_VersionString + "  -  " + (AMBIENT_IS_INSTRUMENT ? "Instrument" : "FX"), false, false);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&mainMenuButton),
                    [this](int r) { if (r >= 50 && onScaleChosen) onScaleChosen(float(r) / 100.0f); });
}

void HeaderBar::resized()
{
    auto r = getLocalBounds().reduced(10, 6);
    const int h = r.getHeight();

    brandArea = r.removeFromLeft(h + 170);
    r.removeFromLeft(14);

    presetArea = r.removeFromLeft(juce::jmin(320, r.getWidth() / 3));
    presetMenuButton.setBounds(presetArea.removeFromRight(56).reduced(4, 8));

    // Right side: master knob + meter
    meter.setBounds(r.removeFromRight(14).reduced(0, 2));
    r.removeFromRight(6);
    master.setBounds(r.removeFromRight(64));
    r.removeFromRight(10);
    mainMenuButton.setBounds(r.removeFromRight(78).reduced(0, 10));
    r.removeFromRight(12);

    infoArea = r.removeFromRight(130);
    r.removeFromRight(8);
    auto leds = r.removeFromRight(170);
    midiLed.setBounds(leds.removeFromLeft(70).withSizeKeepingCentre(70, 18));
    audioLed.setBounds(leds.withSizeKeepingCentre(leds.getWidth(), 18));
}

void HeaderBar::paint(juce::Graphics& g)
{
    auto r = getLocalBounds();
    juce::ColourGradient grad(panelTop, 0.0f, 0.0f, bg, 0.0f, float(r.getBottom()), false);
    g.setGradientFill(grad);
    g.fillRect(r);
    g.setColour(edge);
    g.drawHorizontalLine(r.getBottom() - 1, 0.0f, float(r.getRight()));

    // Brand: logo + wordmark
    auto b = brandArea;
    auto logoR = b.removeFromLeft(b.getHeight());
    if (logo.isValid())
        g.drawImageWithin(logo, logoR.getX(), logoR.getY(), logoR.getWidth(), logoR.getHeight(),
                          juce::RectanglePlacement::centred);
    b.removeFromLeft(10);
    g.setColour(text);
    g.setFont(font(21.0f, true).withExtraKerningFactor(0.12f));
    g.drawText("AMBIENTLAB", b.removeFromTop(b.getHeight() * 6 / 10), juce::Justification::bottomLeft);
    g.setColour(purple);
    g.setFont(labelFont(9.0f));
    g.drawText(AMBIENT_IS_INSTRUMENT ? "MODULAR AMBIENT INSTRUMENT" : "MODULAR AMBIENT PROCESSOR", b, juce::Justification::topLeft);

    // Preset display (LCD-style)
    auto pr = presetArea.reduced(0, 6).toFloat();
    g.setColour(bgDeep);
    g.fillRoundedRectangle(pr, 4.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(pr.reduced(0.5f), 4.0f, 1.0f);
    auto pt = pr.reduced(10.0f, 3.0f);
    g.setColour(textFaint);
    g.setFont(labelFont(8.5f));
    g.drawText("PRESET", pt.removeFromTop(pt.getHeight() * 0.38f), juce::Justification::bottomLeft);
    g.setColour(text);
    g.setFont(font(14.0f));
    g.drawText("Default", pt, juce::Justification::centredLeft);

    // Info: sample rate + CPU
    auto ia = infoArea.reduced(0, 10);
    g.setFont(labelFont(9.0f));
    g.setColour(textFaint);
    auto top = ia.removeFromTop(ia.getHeight() / 2);
    g.drawText("RATE", top.removeFromLeft(40), juce::Justification::centredLeft);
    g.setColour(textDim);
    g.drawText(srText, top, juce::Justification::centredRight);
    g.setColour(textFaint);
    g.drawText("CPU", ia.removeFromLeft(40), juce::Justification::centredLeft);
    g.setColour(textDim);
    g.drawText(cpuText, ia, juce::Justification::centredRight);
}
} // namespace ambient::ui
