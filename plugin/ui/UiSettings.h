#pragma once
// Interface settings: stored in the plugin state under a "UI" child tree, so they
// are saved with the session but are NOT host parameters (never automatable and
// never affect audio). Older sessions stored uiWidth/uiHeight/uiTab on the root;
// those are still read as a fallback.

#include <juce_audio_processors/juce_audio_processors.h>

namespace ambient::ui
{
enum class GraphicsQuality { Standard = 0, High = 1 };

class UiSettings
{
public:
    explicit UiSettings(juce::AudioProcessorValueTreeState& s) : apvts(s) {}

    juce::var get(const juce::Identifier& key, const juce::var& fallback) const
    {
        auto ui = apvts.state.getChildWithName(kUi);
        if (ui.isValid() && ui.hasProperty(key)) return ui.getProperty(key);
        if (apvts.state.hasProperty(key)) return apvts.state.getProperty(key); // legacy location
        return fallback;
    }

    void set(const juce::Identifier& key, const juce::var& value)
    {
        apvts.state.getOrCreateChildWithName(kUi, nullptr).setProperty(key, value, nullptr);
    }

    GraphicsQuality quality() const { return int(get("quality", 1)) == 0 ? GraphicsQuality::Standard : GraphicsQuality::High; }

    static inline const juce::Identifier kUi { "UI" };

private:
    juce::AudioProcessorValueTreeState& apvts;
};
} // namespace ambient::ui
