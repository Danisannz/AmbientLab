#pragma once
// Reusable AMBIENTLAB UI components. All painting goes through theme:: so
// panels added with future engines inherit the same visual language.

#include "AmbientLookAndFeel.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

namespace ambient::ui
{
// ============================================================================
/** Parameter knob: name above, metallic rotary, live value readout below.
    Drag = adjust, Shift-drag = fine, double-click = default, Alt/Cmd-click = default. */
class RotaryKnob : public juce::Component
{
public:
    RotaryKnob(juce::AudioProcessorValueTreeState&, const juce::String& paramID,
               const juce::String& displayName, bool bipolar = false);

    void setCompact(bool c) { compact = c; resized(); }
    juce::Slider& getSlider() noexcept { return slider; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct FineSlider : juce::Slider
    {
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseEnter(const juce::MouseEvent& e) override { Slider::mouseEnter(e); if (auto* p = getParentComponent()) p->repaint(); }
        void mouseExit(const juce::MouseEvent& e) override  { Slider::mouseExit(e);  if (auto* p = getParentComponent()) p->repaint(); }
        void mouseUp(const juce::MouseEvent& e) override    { Slider::mouseUp(e);    if (auto* p = getParentComponent()) p->repaint(); }
    };
    FineSlider slider;
    juce::String name;
    bool compact = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// ============================================================================
/** A module panel: metallic card, header strip with title and state LED.
    A card with no engine behind it is a *placeholder*: it shows when the engine
    arrives and contains no controls (nothing fake). */
class ModuleCard : public juce::Component
{
public:
    explicit ModuleCard(const juce::String& title);

    void setPlaceholder(const juce::String& message) { placeholder = message; repaint(); }
    bool isPlaceholder() const noexcept { return placeholder.isNotEmpty(); }
    void setActive(bool a) { active = a; repaint(); }
    void setScale(float s) { scale = s; repaint(); }

    /** Area below the header available to children. */
    juce::Rectangle<int> getContentBounds() const;

    void paint(juce::Graphics&) override;

private:
    juce::String title, placeholder;
    bool active = true;
    float scale = 1.0f;
};

// ============================================================================
/** Main tab strip. */
class TabBar : public juce::Component
{
public:
    explicit TabBar(juce::StringArray names);
    std::function<void(int)> onChange;
    void setSelected(int index, bool notify = true);
    int getSelected() const noexcept { return selected; }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    int indexAt(juce::Point<int>) const;
    juce::StringArray names;
    int selected = 0, hovered = -1;
};

// ============================================================================
/** Small segmented selector (e.g. SCOPE | STEREO | SPECTRUM). */
class SegmentedControl : public juce::Component
{
public:
    explicit SegmentedControl(juce::StringArray names);
    std::function<void(int)> onChange;
    void setSelected(int index, bool notify = true);
    int getSelected() const noexcept { return selected; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    juce::StringArray names;
    int selected = 0;
};

// ============================================================================
/** Labelled LED that glows lime with activity (decays smoothly). */
class StatusLed : public juce::Component
{
public:
    explicit StatusLed(const juce::String& label);
    void trigger() { level = 1.0f; }
    void setLevel(float l) { level = juce::jmax(level, l); }
    void tick(float dt);
    void paint(juce::Graphics&) override;

private:
    juce::String label;
    float level = 0.0f, shown = -1.0f;
};

// ============================================================================
/** Vertical stereo peak meter with hold, -60..+6 dB. */
class LevelMeter : public juce::Component
{
public:
    void setLevels(float linL, float linR, float dt);
    void paint(juce::Graphics&) override;

private:
    struct Ch { float db = -100.0f, hold = -100.0f, holdTime = 0.0f; };
    Ch ch[2];
    static float toY(float db, float top, float height);
};
} // namespace ambient::ui
