#pragma once
// Oscillators module: exposes every Stage 2 parameter (no placeholder controls).
#include "Components.h"

namespace ambient::ui
{
/** SegmentedControl bound to a choice / int / bool parameter (undo + automation aware). */
class ParamSegment : public SegmentedControl
{
public:
    ParamSegment(juce::RangedAudioParameter& p, juce::StringArray labels);
private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
};

class OscillatorPanel : public juce::Component
{
public:
    explicit OscillatorPanel(juce::AudioProcessorValueTreeState&);
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    struct Row
    {
        juce::String name;
        std::unique_ptr<BypassButton> power;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAtt;
        std::vector<std::unique_ptr<ParamSegment>> segments;
        std::vector<std::unique_ptr<PillToggle>> toggles;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> toggleAtts;
        std::vector<std::unique_ptr<RotaryKnob>> knobs;
        std::vector<int> groupBreaks; // knob indices that start a new group (extra gap + separator)
    };

    Row& addRow(const juce::String& name);
    void addPower(Row&, const juce::String& pid);
    void addSegment(Row&, const juce::String& pid, juce::StringArray labels);
    void addToggle(Row&, const juce::String& pid, const juce::String& text);
    void addKnob(Row&, const juce::String& pid, const juce::String& name, bool bipolar = false);

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<std::unique_ptr<Row>> rows;
    std::vector<juce::Rectangle<int>> separators;
};
} // namespace ambient::ui
