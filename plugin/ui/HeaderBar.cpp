#include "HeaderBar.h"
#include "../Presets.h"
#include "../PluginProcessor.h"
#include "../Parameters.h"
#include <ambient/ParamIDs.h>

namespace ambient::ui
{
using namespace theme;

namespace
{
const juce::String kWordmark { "AMBIENTLAB" };
const juce::String kSubtitle { "MODULAR AMBIENT" };
juce::Font wordFont() { return heading(29.0f, true, 0.10f); }
juce::Font subFont()  { return body(11.5f, Weight::Medium, 0.34f); }
}

HeaderBar::HeaderBar(AmbientProcessor& p, juce::Image img)
    : proc(p), logo(std::move(img)),
      master(p.apvts, id(ids::masterGain), "Master")
{
    master.setCompact(true);
    addAndMakeVisible(master);
    addAndMakeVisible(meter);
    addAndMakeVisible(midiLed);
    addAndMakeVisible(audioLed); // sidechain reference level
    addAndMakeVisible(mascot);
    addAndMakeVisible(presetMenuButton);
    addAndMakeVisible(settingsButton);
    presetMenuButton.onClick = [this] { showPresetMenu(); };
    settingsButton.onClick   = [this] { showMainMenu(); };
    presetMenuButton.setTooltip("Factory and user presets: load, save, delete");
    settingsButton.setTooltip("Settings");
}

void HeaderBar::tick(float dt)
{
    const auto mc = proc.midiEventCount.load(std::memory_order_relaxed);
    if (mc != lastMidiCount) { lastMidiCount = mc; midiLed.trigger(); }
    midiLed.tick(dt);
    if (const auto pn = proc.getPresetName(); pn != shownPreset) { shownPreset = pn; repaint(presetArea); }   // host/session loads

    const float in = proc.inputPeak.load(std::memory_order_relaxed);
    audioLed.setLevel(juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(in, -100.0f) + 60.0f) / 54.0f));
    audioLed.tick(dt);

    meter.setLevels(proc.peakL.load(std::memory_order_relaxed), proc.peakR.load(std::memory_order_relaxed), dt);

    const auto& t = proc.sidechainTelemetry();
    const bool scOn = proc.apvts.getRawParameterValue("sc.enable")->load() > 0.5f;
    mascot.tick(dt, t.transientCount.load(std::memory_order_relaxed), t.lastTransientStrength.load(std::memory_order_relaxed),
                scOn && t.referencePresent.load(std::memory_order_relaxed), t.maxReductionDb.load(std::memory_order_relaxed));

    if ((infoTimer += dt) >= 0.25f) // readouts at 4 Hz: readable and cheap
    {
        infoTimer = 0.0f;
        const auto cpu = juce::String(proc.getCpuLoad() * 100.0, 1) + " %";
        const auto sr  = juce::String(proc.currentSampleRate.load() / 1000.0, 1) + " kHz";
        if (cpu != cpuText) { cpuText = cpu; repaint(cpuCell); }
        if (sr != srText)   { srText = sr;   repaint(rateCell); }
    }
}

void HeaderBar::showPresetMenu()
{
    namespace pr = ambient::presets;
    juce::PopupMenu m, fac, usr;
    for (int i = 0; i < pr::numFactory(); ++i) fac.addItem(1000 + i, pr::factoryName(i));
    const auto files = pr::userPresets();
    for (int i = 0; i < files.size(); ++i) usr.addItem(2000 + i, files[i].getFileNameWithoutExtension());
    if (files.isEmpty()) usr.addItem(1, "(no user presets yet)", false);
    m.addSubMenu("Factory presets", fac);
    m.addSubMenu("User presets", usr);
    m.addSeparator();
    m.addItem(10, "Save as...");
    juce::File current;
    for (const auto& f : files) if (f.getFileNameWithoutExtension() == proc.getPresetName()) current = f;
    m.addItem(11, "Delete user preset \"" + proc.getPresetName() + "\"", current.existsAsFile());
    m.addItem(12, "Open preset folder");
    m.addSeparator();
    m.addItem(13, "Reset all parameters to default");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetMenuButton),
        [this, files, current](int r)
        {
            if (r >= 1000 && r < 2000) { ambient::presets::applyFactory(proc, r - 1000); proc.currentProgram = r - 1000; }
            else if (r >= 2000) ambient::presets::loadUser(proc, files[r - 2000]);
            else if (r == 10)
            {
                auto* w = new juce::AlertWindow("Save preset", "Preset name:", juce::MessageBoxIconType::NoIcon);
                w->addTextEditor("name", proc.getPresetName());
                w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
                w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int res)
                {
                    if (res == 1) ambient::presets::saveUser(proc, w->getTextEditorContents("name"));
                    repaint();
                }), true);
            }
            else if (r == 11) { ambient::presets::deleteUser(current); proc.setPresetName("Init"); }
            else if (r == 12) ambient::presets::userDirectory().startAsProcess();
            else if (r == 13) { ambient::presets::applyFactory(proc, 0); }
            repaint();
        });
}

void HeaderBar::showMainMenu()
{
    juce::PopupMenu size;
    for (auto s : { 0.75f, 1.0f, 1.25f, 1.5f })
        size.addItem(juce::roundToInt(s * 100), juce::String(juce::roundToInt(s * 100)) + " %");

    juce::PopupMenu gfx;
    gfx.addItem(201, "Standard - lighter rendering, 30 fps analyzer", true, quality == GraphicsQuality::Standard);
    gfx.addItem(202, "High quality - full-resolution graphics, 60 fps analyzer", true, quality == GraphicsQuality::High);
    gfx.addSeparator();
    gfx.addItem(203, "Show render statistics", true, isRenderStatsOn && isRenderStatsOn());
    gfx.addSeparator();
    gfx.addItem(204, "Mascot animation (reacts to sidechain kicks)", true, isMascotOn && isMascotOn());

    juce::PopupMenu audio;
    audio.addItem(301, "No audio quality options yet - they arrive with the DSP engines", false, false);

    juce::PopupMenu m;
    m.addSectionHeader("Settings");
    m.addSubMenu("Interface size", size);
    m.addSubMenu("Graphics quality", gfx);
    m.addSubMenu("Audio quality", audio);
    m.addSeparator();
    m.addItem(1, juce::String("AmbientLab ") + JucePlugin_VersionString + "  -  " + (AMBIENT_IS_INSTRUMENT ? "Instrument" : "FX"), false, false);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&settingsButton),
                    [this](int r)
                    {
                        if (r >= 50 && r <= 200 && onScaleChosen) onScaleChosen(float(r) / 100.0f);
                        else if (r == 201 && onQualityChosen) onQualityChosen(GraphicsQuality::Standard);
                        else if (r == 202 && onQualityChosen) onQualityChosen(GraphicsQuality::High);
                        else if (r == 203 && onRenderStatsChanged) onRenderStatsChanged(! (isRenderStatsOn && isRenderStatsOn()));
                        else if (r == 204 && onMascotChanged) onMascotChanged(! (isMascotOn && isMascotOn()));
                    });
}

void HeaderBar::GearButton::paintButton(juce::Graphics& g, bool hi, bool down)
{
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(down ? panelHdr.brighter(0.1f) : (hi ? panelHdr : bgRaised.withAlpha(0.85f)));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(hi ? edgeHi : edge);
    g.drawRoundedRectangle(r, 4.0f, 1.0f);

    const auto c = r.getCentre();
    const float R = juce::jmin(r.getWidth(), r.getHeight()) * 0.30f;
    juce::Path gear;
    const int teeth = 8;
    for (int i = 0; i < teeth * 2; ++i)
    {
        const float a0 = juce::MathConstants<float>::twoPi * float(i) / float(teeth * 2);
        const float a1 = juce::MathConstants<float>::twoPi * float(i + 1) / float(teeth * 2);
        const float rr = (i % 2 == 0) ? R : R * 0.78f;
        const juce::Point<float> p0 { c.x + rr * std::sin(a0), c.y - rr * std::cos(a0) };
        const juce::Point<float> p1 { c.x + rr * std::sin(a1), c.y - rr * std::cos(a1) };
        if (i == 0) gear.startNewSubPath(p0); else gear.lineTo(p0);
        gear.lineTo(p1);
    }
    gear.closeSubPath();
    g.setColour(hi ? theme::text : theme::textDim);
    g.strokePath(gear, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved));
    g.drawEllipse(juce::Rectangle<float>(R * 0.8f, R * 0.8f).withCentre(c), 1.4f);
}

void HeaderBar::resized()
{
    auto r = getLocalBounds().reduced(14, 6);
    const int h = r.getHeight();

    // Brand: logo (aspect preserved) + wordmark, widths measured from the fonts.
    const int logoSize = h; // ~68 px at 100 %
    logoArea = r.removeFromLeft(logoSize);
    r.removeFromLeft(16);
    const int wordW = juce::roundToInt(juce::jmax(textWidth(wordFont(), kWordmark), textWidth(subFont(), kSubtitle))) + 6;
    wordArea = r.removeFromLeft(wordW);
    r.removeFromLeft(26);

    // Right: meter, master, settings
    meter.setBounds(r.removeFromRight(16).reduced(0, 4));
    r.removeFromRight(6);
    master.setBounds(r.removeFromRight(72));
    r.removeFromRight(14);
    settingsButton.setBounds(r.removeFromRight(40).withSizeKeepingCentre(40, 40));
    r.removeFromRight(14);

    // Status module (right-aligned before the settings button)
    const int statusW = juce::jlimit(300, 380, getWidth() / 4);
    statusArea = juce::Rectangle<int>(settingsButton.getX() - 14 - statusW, r.getCentreY() - 22, statusW, 44);
    {
        auto s = statusArea.reduced(10, 0);
        const int cw = s.getWidth() / 4;
        midiLed.setBounds(s.removeFromLeft(cw - 8).withSizeKeepingCentre(cw - 8, 20));
        audioLed.setBounds(s.removeFromLeft(cw + 14).withSizeKeepingCentre(cw + 14, 20));
        rateCell = s.removeFromLeft(cw);
        cpuCell = s;
    }

    // Preset display fills what is left (bounded)
    auto pr = juce::Rectangle<int>(r.getX(), r.getY(), juce::jmax(160, statusArea.getX() - 18 - r.getX()), h);
    presetArea = pr.withWidth(juce::jmin(pr.getWidth(), 340)).withSizeKeepingCentre(juce::jmin(pr.getWidth(), 340), 46);
    presetArea.setX(r.getX());
    presetMenuButton.setBounds(presetArea.withLeft(presetArea.getRight() - 66).reduced(8, 10));

    // Mascot: centred in the gap between the preset display and the status module.
    {
        const int gapL = presetArea.getRight() + 12, gapR = statusArea.getX() - 12;
        const int mw = 54;
        mascot.setVisible(gapR - gapL >= mw);
        mascot.setBounds(juce::Rectangle<int>(mw, h + 4).withCentre({ (gapL + gapR) / 2, r.getCentreY() }));
    }
}

void HeaderBar::paint(juce::Graphics& g)
{
    backdrop.draw(g, Backdrop::Region::Header, getLocalBounds(), quality, logoArea.getCentre().toFloat());

    // Logo - original artwork, aspect ratio and transparency preserved
    if (logo.isValid())
    {
        g.setImageResamplingQuality(quality == GraphicsQuality::High ? juce::Graphics::highResamplingQuality
                                                                    : juce::Graphics::mediumResamplingQuality);
        g.drawImageWithin(logo, logoArea.getX(), logoArea.getY(), logoArea.getWidth(), logoArea.getHeight(),
                          juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
    }

    // Wordmark + subtitle
    auto w = wordArea.toFloat();
    const auto wf = wordFont(), sf = subFont();
    const float wh = wf.getHeight(), sh = sf.getHeight();
    const float blockH = wh + 4.0f + sh;
    const float y0 = w.getCentreY() - blockH * 0.5f;
    g.setFont(wf);
    g.setColour(text);
    g.drawText(kWordmark, juce::Rectangle<float>(w.getX(), y0, w.getWidth(), wh), juce::Justification::centredLeft, false);
    g.setFont(sf);
    g.setColour(lime);
    g.drawText(kSubtitle, juce::Rectangle<float>(w.getX() + 1.0f, y0 + wh + 4.0f, w.getWidth(), sh), juce::Justification::centredLeft, false);

    // Preset display (LCD-style inset)
    auto pr = presetArea.toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.75f));
    g.fillRoundedRectangle(pr, 4.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(pr.reduced(0.5f), 4.0f, 1.0f);
    auto pt = pr.withTrimmedRight(66.0f).reduced(12.0f, 5.0f);
    g.setColour(textFaint);
    g.setFont(caption(10.5f));
    g.drawText("PRESET", pt.removeFromTop(14.0f), juce::Justification::centredLeft);
    g.setColour(text);
    g.setFont(body(15.0f, Weight::Medium));
    g.drawText(proc.getPresetName(), pt, juce::Justification::centredLeft, true);

    // Status module
    auto sa = statusArea.toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.7f));
    g.fillRoundedRectangle(sa, 4.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(sa.reduced(0.5f), 4.0f, 1.0f);
    g.setColour(edge);
    for (auto cx : { midiLed.getRight(), audioLed.getRight(), rateCell.getRight() })
        g.drawVerticalLine(cx, sa.getY() + 9.0f, sa.getBottom() - 9.0f);

    auto cell = [&](juce::Rectangle<int> c, const juce::String& lbl, const juce::String& val)
    {
        auto cr = c.toFloat().reduced(10.0f, 6.0f);
        g.setFont(caption(10.0f));
        g.setColour(textFaint);
        g.drawText(lbl, cr.removeFromTop(13.0f), juce::Justification::centredLeft);
        g.setFont(body(13.5f, Weight::Medium));
        g.setColour(text);
        g.drawText(val, cr, juce::Justification::centredLeft);
    };
    cell(rateCell, "RATE", srText);
    cell(cpuCell, "CPU", cpuText);
}
} // namespace ambient::ui
