#pragma once
#include "Theme.h"

namespace ambient::ui
{
/** Slider property keys understood by the look-and-feel. */
namespace props
{
inline const juce::Identifier bipolar  { "bipolar" };   // value arc drawn from centre
inline const juce::Identifier modDepth { "modDepth" };  // -1..1, lime modulation ring (future)
}

class AmbientLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AmbientLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos,
                          float startAngle, float endAngle, juce::Slider&) override;

    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool highlighted, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    juce::Font getPopupMenuFont() override;

    juce::Label* createSliderTextBox(juce::Slider&) override;
};
} // namespace ambient::ui
