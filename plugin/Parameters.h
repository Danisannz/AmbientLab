#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace ambient
{
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

inline juce::String id(std::string_view s) { return juce::String(s.data(), s.size()); }
} // namespace ambient
