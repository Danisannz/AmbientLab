#include "Parameters.h"
#include <ambient/ParamIDs.h>

namespace ambient
{
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
using PID    = juce::ParameterID;

namespace
{
// Display formatting only - IDs, ranges and defaults are unchanged.
juce::String formatDb(float v, int)
{
    if (v <= -59.95f) return "-inf dB";
    return (v > 0.04f ? "+" : "") + juce::String(v, 1) + " dB";
}
float parseDb(const juce::String& t)
{
    return t.trim().startsWithIgnoreCase("-inf") ? -60.0f : t.getFloatValue();
}
juce::String formatPercent(float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; }
float parsePercent(const juce::String& t) { return t.getFloatValue() / 100.0f; }
juce::String formatMix(float v, int)
{
    const int in = juce::roundToInt(v * 100.0f);
    if (in == 0) return "Synth";
    if (in == 100) return "Input";
    return juce::String(100 - in) + " / " + juce::String(in);
}
} // namespace

Layout createParameterLayout()
{
    Layout layout;
    constexpr int v = 1; // parameter version hint for VST3 (do not change for existing params)

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PID { id(ids::masterGain), v }, "Master Gain",
        juce::NormalisableRange<float> { -60.0f, 12.0f, 0.01f, 2.0f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction(formatDb).withValueFromStringFunction(parseDb)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PID { id(ids::inputGain), v }, "Input Gain",
        juce::NormalisableRange<float> { -60.0f, 12.0f, 0.01f, 2.0f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction(formatDb).withValueFromStringFunction(parseDb)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PID { id(ids::sourceMix), v }, "Source Mix",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(formatMix).withValueFromStringFunction(parsePercent)));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        PID { id(ids::bypassSynth), v }, "Bypass Synth", false));

    for (size_t i = 0; i < ids::macros.size(); ++i)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            PID { id(ids::macros[i]), v }, "Macro " + juce::String(int(i) + 1),
            juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction(formatPercent).withValueFromStringFunction(parsePercent)));

    return layout;
}
} // namespace ambient
