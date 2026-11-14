#include "StateMigration.h"
#include <ambient/ParamIDs.h>

namespace ambient
{
// Each step upgrades version N -> N+1. Add new steps at the end; never edit old ones.
static void migrate_0_to_1(juce::ValueTree&) { /* pre-release states: nothing to change */ }

bool migrateState(juce::ValueTree& state)
{
    int version = state.getProperty(kVersionProp, 0);
    if (version > ids::kStateVersion) return false;

    using Step = void (*)(juce::ValueTree&);
    static constexpr Step steps[] = { migrate_0_to_1 };

    for (; version < ids::kStateVersion; ++version)
        steps[version](state);

    state.setProperty(kVersionProp, ids::kStateVersion, nullptr);
    return true;
}
} // namespace ambient
