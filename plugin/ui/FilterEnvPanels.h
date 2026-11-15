#pragma once
// Filter and Envelopes modules (Stage 3): every control maps to a real parameter.
#include "OscillatorPanel.h"
#include "../PluginProcessor.h"

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

/** Output stereo safety + monitor (bass mono, width, correlation floor, mono check). */
class StereoPanel : public juce::Component, private juce::Timer
{
public:
    explicit StereoPanel(AmbientProcessor&);
    ~StereoPanel() override { stopTimer(); }
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::Button& getPowerButton() noexcept { return power; }
private:
    void timerCallback() override;
    AmbientProcessor& proc;
    BypassButton power { "Stereo safety" };
    PillToggle bassMono { "Bass mono", theme::lime }, monoCheck { "Mono check", theme::uvBright };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAtt, bassAtt, monoAtt;
    std::vector<std::unique_ptr<RotaryKnob>> knobs;
    juce::Rectangle<int> readout;
    float corrIn = 1, corrOut = 1, midDb = -120, sideDb = -120, corrDb = 0;
    bool anti = false;
};

/** Delay module (Stage 6). Time or Division is active depending on Sync; Pitch only in Pitch mode. */
class DelayPanel : public juce::Component, private juce::Timer
{
public:
    explicit DelayPanel(juce::AudioProcessorValueTreeState&);
    ~DelayPanel() override { stopTimer(); }
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::Button& getPowerButton() noexcept { return power; }
private:
    void timerCallback() override;
    juce::AudioProcessorValueTreeState& state;
    BypassButton power { "Delay" };
    PillToggle sync { "Sync", theme::lime };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAtt, syncAtt;
    std::unique_ptr<ParamSegment> mode;
    std::vector<std::unique_ptr<RotaryKnob>> time, tone, out;
    RotaryKnob *timeK = nullptr, *divK = nullptr, *pitchK = nullptr;
};
} // namespace ambient::ui
