#pragma once
// Filter and Envelopes modules (Stage 3): every control maps to a real parameter.
#include "OscillatorPanel.h"

namespace ambient::ui
{
class FilterPanel : public juce::Component
{
public:
    explicit FilterPanel(juce::AudioProcessorValueTreeState&);
    void resized() override;
    juce::Button& getPowerButton() noexcept { return power; }
private:
    BypassButton power { "Filter" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAtt;
    std::unique_ptr<ParamSegment> mode;
    std::vector<std::unique_ptr<RotaryKnob>> knobs;
};

class EnvelopePanel : public juce::Component
{
public:
    explicit EnvelopePanel(juce::AudioProcessorValueTreeState&);
    void resized() override;
    void paint(juce::Graphics&) override;
private:
    std::vector<std::unique_ptr<RotaryKnob>> amp, mod;
};
} // namespace ambient::ui
