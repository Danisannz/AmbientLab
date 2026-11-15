#pragma once
// Sequencer tab (Stage 8): globals, lane selector, 32-step grid editor.
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Components.h"
#include "../PluginProcessor.h"

namespace ambient::ui
{
class StepGrid : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum Lane { Note = 0, Velocity, Gate, Probability, Ratchet, Lock1, Lock2, Lock3, Lock4, kLanes };
    explicit StepGrid(AmbientProcessor&);
    void setLane(int l) { lane = l; repaint(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    int currentStep = -1;
private:
    juce::Rectangle<float> barArea() const;
    juce::Rectangle<float> toggleRow(int row) const;     // 0 = gate on/off, 1 = skip
    int stepAt(float x) const;
    void edit(const juce::MouseEvent&, bool start);
    float laneValue(const ambient::dsp::Sequencer::Step&) const;           // 0..1 for drawing
    void setLaneValue(ambient::dsp::Sequencer::Step&, float v01) const;
    AmbientProcessor& proc;
    int lane = Note;
    int dragRow = -1; bool dragSet = true;
};

class SequencerPanel : public juce::Component, private juce::Timer
{
public:
    explicit SequencerPanel(AmbientProcessor&);
    ~SequencerPanel() override { stopTimer(); }
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::Button& getPowerButton() noexcept { return power; }
private:
    void timerCallback() override;
    AmbientProcessor& proc;
    BypassButton power { "Sequencer" };
    PillToggle patternStart { "Scale @ pattern start", theme::uvBright };
    juce::ComboBox clock, direction, root, scale, octave, presets;
    juce::TextButton randomise { "Randomise" };
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> btnAtt;
    std::vector<std::unique_ptr<RotaryKnob>> knobs;
    std::array<juce::TextButton, StepGrid::kLanes> laneButtons;
    std::array<juce::ComboBox, 4> lockTargets;
    StepGrid grid;
};
} // namespace ambient::ui
