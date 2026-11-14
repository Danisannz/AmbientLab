#pragma once
#include "AnalyzerView.h"

namespace ambient::ui
{
/** Docked analyzer: mode selector, freeze, expand, live/no-signal/frozen status,
    the active view, and a monitor strip (correlation, width, L/R levels) that is
    always visible. Optional render statistics for benchmarking. */
class AnalyzerPanel : public juce::Component
{
public:
    AnalyzerPanel(viz::VisualizerHub&, UiSettings&);
    ~AnalyzerPanel() override;

    std::function<void(bool)> onExpandChanged;
    void setExpanded(bool e);
    bool isExpanded() const noexcept { return expanded; }

    void setQuality(GraphicsQuality q);
    void setShowStats(bool s);
    bool isShowingStats() const noexcept { return showStats; }

    /** UI timer; `fresh` = hub analysed new audio this frame. */
    void tick(bool fresh, float dt);

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

    /** Average time spent painting the panel and its active view (ms). */
    double getAveragePaintMs() const noexcept { return avgPaintMs; }

private:
    void setMode(int m);
    AnalyzerView* activeView() const;

    viz::VisualizerHub& hub;
    UiSettings& settings;
    SegmentedControl modes { { "SCOPE", "STEREO", "SPECTRUM" } };
    juce::TextButton freeze { "Freeze" }, expand { "Expand" };
    CorrelationMeter correlation;
    std::array<std::unique_ptr<AnalyzerView>, 3> views;

    GraphicsQuality quality = GraphicsQuality::High;
    bool expanded = false, showStats = false;
    int framesSinceData = 1000;
    float monitorTimer = 0.0f, width = 0.0f, rmsL = 0.0f, rmsR = 0.0f;

    // render statistics
    double paintStart = 0.0, avgPaintMs = 0.0;
    int paintsThisSecond = 0, fps = 0;
    float statsTimer = 0.0f;

    juce::Rectangle<int> headerArea, monitorArea;
};
} // namespace ambient::ui
