#pragma once
#include "Components.h"

class AmbientProcessor;

namespace ambient::ui
{
/** Top bar: logo + wordmark, preset display, MIDI / audio status, sample rate,
    CPU, main menu, master gain and output meter. Only real functionality:
    there is no preset library yet, so the preset area shows the current state
    name and a menu with working actions only. */
class HeaderBar : public juce::Component
{
public:
    HeaderBar(AmbientProcessor&, juce::Image logo);

    /** Called from the editor's UI timer. */
    void tick(float dt);

    /** Main-menu actions handled by the editor. */
    std::function<void(float)> onScaleChosen;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void showPresetMenu();
    void showMainMenu();

    AmbientProcessor& proc;
    juce::Image logo;
    juce::TextButton presetMenuButton { "MENU" }, mainMenuButton { "SETTINGS" };
    StatusLed midiLed { "MIDI" }, audioLed { "AUDIO IN" };
    RotaryKnob master;
    LevelMeter meter;

    juce::Rectangle<int> brandArea, presetArea, infoArea;
    juce::String cpuText, srText;
    uint32_t lastMidiCount = 0;
    float infoTimer = 0.0f;
};
} // namespace ambient::ui
