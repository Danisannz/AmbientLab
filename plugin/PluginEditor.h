#pragma once
#include "PluginProcessor.h"

/** Stage 1 test UI: header with branding slot, global controls and 8 macros.
    Replaced by the full interface in Stage 9. */
class AmbientEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AmbientEditor(AmbientProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };

    AmbientProcessor& proc;
    std::vector<std::unique_ptr<Knob>> globals, macros;
    juce::ToggleButton bypassSynth { "Bypass Synth" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;
    float meter = 0.0f;
    juce::Image logo;
};
