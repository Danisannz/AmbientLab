#pragma once
// Modulation tab (Stage 7): four LFO panels + the 32-slot modulation matrix.
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Components.h"
#include "../PluginProcessor.h"

namespace ambient::ui
{
/** Waveform display with a live phase dot; in Custom mode the 8 curve points can be dragged. */
class LfoDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    LfoDisplay(AmbientProcessor&, int lfoIndex);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void refresh();
private:
    bool isCustom() const;
    void setPoint(const juce::MouseEvent&);
    AmbientProcessor& proc;
    int index;
    std::array<juce::RangedAudioParameter*, 8> pts {};
    int dragPoint = -1;
    float phase = 0.0f, value = 0.0f;
};

class LfoPanel : public juce::Component, private juce::Timer
{
public:
    LfoPanel(AmbientProcessor&, int lfoIndex);
    ~LfoPanel() override { stopTimer(); }
    void resized() override;
private:
    void timerCallback() override;
    AmbientProcessor& proc;
    int index;
    LfoDisplay display;
    juce::ComboBox wave;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveAtt;
    PillToggle sync { "Sync", theme::lime }, uni { "Uni", theme::uvBright }, retrig { "Retrig", theme::uvBright };
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> btnAtt;
    std::unique_ptr<RotaryKnob> rate, division, phaseK, shape, smooth;
};

class ModMatrixPanel : public juce::Component, private juce::Timer
{
public:
    explicit ModMatrixPanel(AmbientProcessor&);
    ~ModMatrixPanel() override { stopTimer(); }
    void resized() override;
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    struct Row
    {
        juce::ComboBox src, dst;
        juce::Slider amount { juce::Slider::LinearBar, juce::Slider::NoTextBox };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> srcAtt, dstAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amtAtt;
        juce::Rectangle<int> label, meter;
        float live = 0.0f;
    };
    AmbientProcessor& proc;
    struct Inner : juce::Component { ModMatrixPanel* owner = nullptr; void paint(juce::Graphics& g) override { owner->paintRows(g); } } inner;
    juce::Viewport viewport;
    std::array<Row, 32> rows;
    void paintRows(juce::Graphics&);
    friend struct Inner;
};
} // namespace ambient::ui
