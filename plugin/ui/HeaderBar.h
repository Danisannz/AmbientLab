#pragma once
#include "Backdrop.h"
#include "Components.h"
#include "Mascot.h"

class AmbientProcessor;

namespace ambient::ui
{
/** Top bar: logo + wordmark, preset display, status module (MIDI, audio in,
    sample rate, CPU), settings, master gain and output meter.
    Only real functionality: there is no preset library yet, so the preset area
    shows the current state name and a menu with working actions only. */
class HeaderBar : public juce::Component
{
public:
    HeaderBar(AmbientProcessor&, juce::Image logo);

    void tick(float dt);
    void setQuality(GraphicsQuality q) { quality = q; repaint(); }

    // Settings menu hooks (implemented by the editor)
    std::function<void(float)> onScaleChosen;
    std::function<void(GraphicsQuality)> onQualityChosen;
    std::function<void(bool)> onRenderStatsChanged;
    std::function<bool()> isRenderStatsOn;
    std::function<void(bool)> onMascotChanged;
    std::function<bool()> isMascotOn;
    void setMascotAnimated(bool on) { mascot.setAnimated(on); }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct GearButton : juce::Button
    {
        GearButton() : Button("Settings") {}
        void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    };

    void showPresetMenu();
    void showMainMenu();

    AmbientProcessor& proc;
    juce::Image logo;
    Backdrop backdrop;
    GraphicsQuality quality = GraphicsQuality::High;

    juce::TextButton presetMenuButton { "Reset" };
    GearButton settingsButton;
    StatusLed midiLed { "MIDI" }, audioLed { "Sidechain" };
    Mascot mascot;
    RotaryKnob master;
    LevelMeter meter;

    juce::Rectangle<int> logoArea, wordArea, presetArea, statusArea, rateCell, cpuCell;
    juce::String cpuText { "-" }, srText { "-" };
    uint32_t lastMidiCount = 0;
    float infoTimer = 1.0f;
};
} // namespace ambient::ui
