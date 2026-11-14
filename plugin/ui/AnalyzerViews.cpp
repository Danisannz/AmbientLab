// Temporary (Stage 1): labelled screens until each view is implemented.
#include "AnalyzerViews.h"

namespace ambient::ui
{
namespace
{
struct PendingView : AnalyzerView
{
    PendingView(viz::VisualizerHub& h, UiSettings& s, juce::String n) : AnalyzerView(h, s), name(std::move(n)) {}
    void refresh(bool, float) override {}
    void paint(juce::Graphics& g) override
    {
        auto sc = getLocalBounds().toFloat();
        drawScreen(g, sc);
        g.setColour(theme::grid);
        for (int i = 1; i < 8; ++i) g.drawVerticalLine(int(sc.getX() + sc.getWidth() * i / 8.0f), sc.getY() + 2, sc.getBottom() - 2);
        for (int i = 1; i < 6; ++i) g.drawHorizontalLine(int(sc.getY() + sc.getHeight() * i / 6.0f), sc.getX() + 2, sc.getRight() - 2);
        g.setColour(theme::textFaint);
        g.setFont(theme::body(12.5f));
        g.drawText(name + " - in development", sc.toNearestInt(), juce::Justification::centred);
    }
    juce::String name;
};
} // namespace

std::unique_ptr<AnalyzerView> createSpectrumView(viz::VisualizerHub& h, UiSettings& s) { return std::make_unique<PendingView>(h, s, "Spectrum analyzer"); }
} // namespace ambient::ui
