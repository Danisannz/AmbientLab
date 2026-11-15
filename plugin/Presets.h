#pragma once
// Preset management (Stage 9): factory presets (also exposed as host programs) and a user
// preset library on disk. A preset is the complete plugin state (parameters + sequencer
// pattern), the same data the DAW stores in its session. Message thread only.
#include <juce_audio_processors/juce_audio_processors.h>

class AmbientProcessor;

namespace ambient::presets
{
int numFactory();
juce::String factoryName(int index);
void applyFactory(AmbientProcessor&, int index);

juce::File userDirectory();                       // %APPDATA%/AmbientLab/Presets (created on demand)
juce::Array<juce::File> userPresets();            // sorted by name
bool saveUser(AmbientProcessor&, const juce::String& name);
bool loadUser(AmbientProcessor&, const juce::File&);
bool deleteUser(const juce::File&);
inline constexpr const char* kExtension = ".ambientpreset";
} // namespace ambient::presets
