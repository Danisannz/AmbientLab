#pragma once
// Reusable AMBIENTLAB UI components. All painting goes through theme:: so panels
// added with future engines inherit the same visual language.

#include "AmbientLookAndFeel.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

namespace ambient::ui
{
// ============================================================================
/** Parameter knob: name above, metallic rotary, live value readout below.
    Drag = adjust, Shift-drag = fine, double-click = reset to default. */
class RotaryKnob : public juce::Component
{
public:
    RotaryKnob(juce::AudioProcessorValueTreeState&, const juce::String& paramID,
               const juce::String& displayName, bool bipolar = false);

    void setCompact(bool c) { compact = c; resized(); }
    juce::Slider& getSlider() noexcept { return slider; }

    /** Preferred height for a given knob diameter (label + knob + value). */
    static int heightFor(int diameter, bool compact) { return diameter + (compact ? 34 : 38); }

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
/** Line-art glyphs for module identification (also used on placeholder cards). */
enum class Glyph { None, Oscillator, Filter, Envelope, Input, Grains, Reverb, Delay, Stereo, Lfo, Matrix, Sequencer, Synth };
void drawGlyph(juce::Graphics&, Glyph, juce::Rectangle<float>, juce::Colour, float strokeWidth = 1.4f);

// ============================================================================
/** A module panel: metallic card, header strip with title and state LED.
    A card with no engine behind it is a *placeholder*: it shows the module's
    glyph, what it will contain and the development stage that delivers it.
    It never contains controls. */
class ModuleCard : public juce::Component
{
public:
    explicit ModuleCard(const juce::String& title, Glyph glyph = Glyph::None);

    void setPlaceholder(const juce::String& description, int stage = 0);
    bool isPlaceholder() const noexcept { return placeholder.isNotEmpty(); }
    void setActive(bool a) { active = a; repaint(); }
    void setScale(float s) { scale = s; repaint(); }
    void setGlyph(Glyph g) { glyph = g; repaint(); }
    /** Hide the header status LED (e.g. when a header button takes its place). */
    void setHeaderLedVisible(bool v) { headerLed = v; repaint(); }
    /** Square slot at the right end of the header for a small header control. */
    juce::Rectangle<int> getHeaderControlBounds() const;

    /** Area below the header available to children. */
    juce::Rectangle<int> getContentBounds() const;

    void paint(juce::Graphics&) override;

private:
    juce::String title, placeholder;
    Glyph glyph = Glyph::None;
    int stage = 0;
    bool active = true, headerLed = true;
    float scale = 1.0f;
};

// ============================================================================
/** Minimal power-symbol bypass button for card headers. The toggle state is the
    *bypass* state (attach it to a bypass parameter): the symbol glows lime while
    the module is running and dims when bypassed. */
class BypassButton : public juce::ToggleButton
{
public:
    explicit BypassButton(const juce::String& moduleName);
    /** For *enable* parameters: the symbol glows when the toggle is ON. */
    void setLitWhenOn(bool b) { litWhenOn = b; repaint(); }
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
private:
    bool litWhenOn = false;
};

/** Small pill toggle for boolean parameters (Auto, Listen, Delta ...). */
class PillToggle : public juce::Button
{
public:
    PillToggle(const juce::String& text, juce::Colour onColour);
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
private:
    juce::Colour onColour;
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
    int tabWidth() const;
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

// ============================================================================
/** Horizontal correlation meter, -1 .. +1. Negative = magenta (phase problem),
    positive = lime. Smoothed display, numeric readout optional. */
class CorrelationMeter : public juce::Component
{
public:
    void setCorrelation(float c, float dt);
    void setShowLabels(bool s) { labels = s; repaint(); }
    void paint(juce::Graphics&) override;
    float getDisplayed() const noexcept { return shown; }

private:
    float shown = 0.0f, minHold = 1.0f, minHoldTime = 0.0f;
    bool labels = true;
};
} // namespace ambient::ui
