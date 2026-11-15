#include "StateMigration.h"
#include <ambient/ParamIDs.h>

namespace ambient
{
// Each step upgrades version N -> N+1. Add new steps at the end; never edit old ones.
static void migrate_0_to_1(juce::ValueTree&) { /* pre-release states: nothing to change */ }

// v1 -> v2: the external input became a reference-only frequency sidechain.
// global.sourceMix is kept (stable ID) but no longer mixes external audio into
// the output; global.inputGain now only affects the FX track input. New sc.*
// parameters take their defaults (APVTS supplies them when absent).
static void migrate_1_to_2(juce::ValueTree&) {}

bool migrateState(juce::ValueTree& state)
{
    int version = state.getProperty(kVersionProp, 0);
    if (version > ids::kStateVersion) return false;

    using Step = void (*)(juce::ValueTree&);
    static constexpr Step steps[] = { migrate_0_to_1, migrate_1_to_2 };

    for (; version < ids::kStateVersion; ++version)
        steps[version](state);

    state.setProperty(kVersionProp, ids::kStateVersion, nullptr);
    return true;
}
} // namespace ambient
