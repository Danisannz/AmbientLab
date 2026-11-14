#pragma once
#include <juce_data_structures/juce_data_structures.h>

namespace ambient
{
/** Upgrades a saved state tree in place to the current kStateVersion.
    Returns false if the state is from a NEWER version (caller should still load
    what it can, but must not overwrite unknown data blindly). */
bool migrateState(juce::ValueTree& state);

inline const juce::Identifier kVersionProp { "stateVersion" };
} // namespace ambient
