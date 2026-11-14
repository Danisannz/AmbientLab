#pragma once
#include "Components.h"
#include "../viz/VisualizerHub.h"

namespace ambient::ui
{
/** Docked analyzer: mode selector, freeze, expand. Hosts the three views.
    UI-A/B: shell + live engine status. The scope / vectorscope / spectrum
    views themselves are implemented in UI-C. */
class AnalyzerPanel : public juce::Component
{
public:
    explicit AnalyzerPanel(viz::VisualizerHub&);

    std::function<void(bool)> onExpandChanged;
    void setExpanded(bool e);
    bool isExpanded() const noexcept { return expanded; }

    /** UI timer; `fresh` = hub analysed new audio this frame. */
    void tick(bool fresh);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<int> screenBounds() const;

    viz::VisualizerHub& hub;
    SegmentedControl modes { { "SCOPE", "STEREO", "SPECTRUM" } };
    juce::TextButton freeze { "FREEZE" }, expand { "EXPAND" };
    bool expanded = false;
    int framesSinceData = 1000;
};
} // namespace ambient::ui
