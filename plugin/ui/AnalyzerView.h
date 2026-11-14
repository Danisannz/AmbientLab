#pragma once
// Base class for analyzer views (scope, vectorscope, spectrum). Views draw only
// on the message thread from data already analysed by the VisualizerHub.

#include "Components.h"
#include "UiSettings.h"
#include "../viz/VisualizerHub.h"

namespace ambient::ui
{
class AnalyzerView : public juce::Component
{
public:
    AnalyzerView(viz::VisualizerHub& h, UiSettings& s) : hub(h), settings(s) {}

    /** UI timer. `fresh` = hub analysed new audio this frame. */
    virtual void refresh(bool fresh, float dt) = 0;
    virtual void setQuality(GraphicsQuality q) { quality = q; repaint(); }
    /** Called when the view becomes the active analyzer mode. */
    virtual void activated() {}

protected:
    /** Physical pixel scale of the current paint context (HiDPI aware). */
    static float physicalScale(juce::Graphics& g)
    {
        return juce::jlimit(1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    }

    /** Standard screen: deep background, inner shadow, border. */
    static void drawScreen(juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour(theme::bgDeep);
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(juce::Colours::black);
        g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.0f);
        g.setColour(theme::edge.withAlpha(0.6f));
        g.drawRoundedRectangle(r.expanded(0.5f), 3.5f, 1.0f);
    }

    viz::VisualizerHub& hub;
    UiSettings& settings;
    GraphicsQuality quality = GraphicsQuality::High;
};

// ============================================================================
/** Phosphor-style persistence layer: a transparent ARGB image at the physical
    resolution of the screen area. Each frame it fades by a time-based factor
    (identical decay at 30 or 60 fps) and new traces are drawn on top. */
class PersistenceLayer
{
public:
    /** (Re)allocates when the logical area or pixel scale changes. Returns true if reset. */
    bool ensure(juce::Rectangle<int> logicalArea, float physScale)
    {
        const int w = juce::jmax(1, juce::roundToInt(logicalArea.getWidth() * physScale));
        const int h = juce::jmax(1, juce::roundToInt(logicalArea.getHeight() * physScale));
        if (image.isValid() && image.getWidth() == w && image.getHeight() == h && area == logicalArea) return false;
        image = juce::Image(juce::Image::ARGB, w, h, true);
        area = logicalArea; scale = physScale;
        return true;
    }

    /** persistence 0 = none, 0.95 = long trails. dt in seconds. */
    void fade(float persistence, float dt)
    {
        if (! image.isValid()) return;
        if (persistence <= 0.001f) { image.clear(image.getBounds()); return; }
        const float keep = std::pow(juce::jlimit(0.0f, 0.985f, persistence), dt * 60.0f);
        image.multiplyAllAlphas(keep);
    }

    void clear() { if (image.isValid()) image.clear(image.getBounds()); }

    /** Graphics drawing in *logical* coordinates relative to the area origin. */
    std::unique_ptr<juce::Graphics> context()
    {
        auto g = std::make_unique<juce::Graphics>(image);
        g->addTransform(juce::AffineTransform::scale(scale));
        return g;
    }

    void draw(juce::Graphics& g) const
    {
        if (image.isValid()) g.drawImage(image, area.toFloat());
    }

    juce::Image& getImage() noexcept { return image; }
    float getScale() const noexcept { return scale; }
    juce::Rectangle<int> getArea() const noexcept { return area; }

private:
    juce::Image image;
    juce::Rectangle<int> area;
    float scale = 1.0f;
};

// ============================================================================
/** Compact analyzer control ("chip"): label + value. Click opens a menu of
    choices, or drag/wheel adjusts a continuous value; double-click resets. */
class ControlChip : public juce::Component, public juce::SettableTooltipClient
{
public:
    /** Discrete choice. */
    ControlChip(const juce::String& label, juce::StringArray choices, int initial);
    /** Continuous value with formatter. */
    ControlChip(const juce::String& label, float min, float max, float initial, float defaultValue,
                std::function<juce::String(float)> format, bool logarithmic = false);

    std::function<void(int)> onChoice;
    std::function<void(float)> onValue;

    int getChoice() const noexcept { return choice; }
    float getValue() const noexcept { return value; }
    void setChoice(int i, bool notify);
    void setValue(float v, bool notify);

    int getPreferredWidth() const;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

private:
    float toNorm(float v) const;
    float fromNorm(float n) const;
    juce::String valueText() const;

    juce::String label;
    juce::StringArray choices;
    int choice = 0;
    float value = 0.0f, minV = 0.0f, maxV = 1.0f, defV = 0.0f, dragStartNorm = 0.0f;
    bool continuous = false, logScale = false, dragging = false;
    std::function<juce::String(float)> format;
};

/** Number of rows of `rowHeight` needed to lay `chips` out in `width`. */
int chipRowsNeeded(int width, const std::vector<juce::Component*>& chips, int gap = 6);

/** Lays chips out left-to-right, wrapping into further rows of `rowHeight`.
    Never overlaps: chips that do not fit in `area` are hidden. */
void layoutChips(juce::Rectangle<int> area, const std::vector<juce::Component*>& chips, int rowHeight, int gap = 6);
} // namespace ambient::ui
