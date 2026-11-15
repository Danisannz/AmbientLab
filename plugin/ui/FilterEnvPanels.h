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

namespace ambient::ui
{
/** Granular module (Stage 4). */
class GranularPanel : public juce::Component
{
public:
    explicit GranularPanel(juce::AudioProcessorValueTreeState&);
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::Button& getPowerButton() noexcept { return power; }
private:
    BypassButton power { "Granular" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAtt, freezeAtt;
    PillToggle freeze { "Freeze", theme::uvBright };
    std::unique_ptr<ParamSegment> window;
    std::vector<std::unique_ptr<RotaryKnob>> grain, pitch, out;
};

/** Reverb module (Stage 5). Shimmer amount is only active on the Shimmer algorithm. */
class ReverbPanel : public juce::Component, private juce::Timer
{
public:
    explicit ReverbPanel(juce::AudioProcessorValueTreeState&);
    ~ReverbPanel() override { stopTimer(); }
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::Button& getPowerButton() noexcept { return power; }
private:
    void timerCallback() override;
    juce::AudioProcessorValueTreeState& state;
    BypassButton power { "Reverb" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAtt, freezeAtt;
    PillToggle freeze { "Freeze", theme::uvBright };
    std::unique_ptr<ParamSegment> algo;
    std::vector<std::unique_ptr<RotaryKnob>> time, character, out;
    RotaryKnob* shimmerKnob = nullptr;
};
} // namespace ambient::ui
