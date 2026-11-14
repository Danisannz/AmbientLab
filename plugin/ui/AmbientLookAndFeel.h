#pragma once
#include "Theme.h"
#include "UiSettings.h"

namespace ambient::ui
{
/** Slider property keys understood by the look-and-feel. */
namespace props
{
inline const juce::Identifier bipolar  { "bipolar" };   // value arc drawn from centre
inline const juce::Identifier modDepth { "modDepth" };  // -1..1, lime modulation ring (when a source exists)
}

class AmbientLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AmbientLookAndFeel();

    void setQuality(GraphicsQuality q) noexcept { quality = q; }
    GraphicsQuality getQuality() const noexcept { return quality; }

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos,
                          float startAngle, float endAngle, juce::Slider&) override;

    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuSectionHeader(juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;

    juce::Rectangle<int> getTooltipBounds(const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    void drawTooltip(juce::Graphics&, const juce::String&, int w, int h) override;

    juce::Label* createSliderTextBox(juce::Slider&) override;

private:
    GraphicsQuality quality = GraphicsQuality::High;
};
} // namespace ambient::ui
