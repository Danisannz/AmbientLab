#include "Parameters.h"
#include <ambient/ParamIDs.h>

namespace ambient
{
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
using PID    = juce::ParameterID;

Layout createParameterLayout()
{
    Layout layout;
    constexpr int v = 1; // parameter version hint for VST3 (do not change for existing params)

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PID { id(ids::masterGain), v }, "Master Gain",
        juce::NormalisableRange<float> { -60.0f, 12.0f, 0.01f, 2.0f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PID { id(ids::inputGain), v }, "Input Gain",
        juce::NormalisableRange<float> { -60.0f, 12.0f, 0.01f, 2.0f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PID { id(ids::sourceMix), v }, "Source Mix",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        PID { id(ids::bypassSynth), v }, "Bypass Synth", false));

    for (size_t i = 0; i < ids::macros.size(); ++i)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            PID { id(ids::macros[i]), v }, "Macro " + juce::String(int(i) + 1),
            juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));

    return layout;
}
} // namespace ambient
