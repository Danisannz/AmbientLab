#pragma once
// FREQUENCY SIDECHAIN module UI: frequency graph (reference spectrum, AmbientLab
// output spectrum, live reduction curve, band handles, Auto search range),
// global controls, mode toggles, meters and a per-band editor.
// All values shown come from the processor's lock-free telemetry / capture rings.

#include "Components.h"
#include "UiSettings.h"
#include "../viz/SpectrumAnalyser.h"

class AmbientProcessor;

namespace ambient::ui
{
class Mascot;

class SidechainGraph : public juce::Component
{
public:
    SidechainGraph(AmbientProcessor&);
    ~SidechainGraph() override;

    void tick(float dt, bool analyse);
    void setQuality(GraphicsQuality q) { quality = q; repaint(); }
    void setSelectedBand(int b) { selected = b; repaint(); }
    std::function<void(int)> onBandSelected;
    void setMascot(Mascot* m) { mascot = m; }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    float xOf(float f) const;
    float fOfX(float x) const;
    float ySpec(float db) const;
    float yGr(float grDb) const;
    float reductionAt(float f) const; // dB (<= 0) of the combined band cuts
    int bandAt(juce::Point<float>) const;
    juce::RangedAudioParameter* bandParam(int band, int field) const;

    AmbientProcessor& proc;
    viz::SpectrumAnalyser refSpec, outSpec;
    std::vector<float> scratchL, scratchR, mono, refCurve, outCurve;
    std::array<float, 4> bandFreq {}, bandRed {}, bandQ {};
    std::array<bool, 4> bandOn {};
    bool refPresent = false, fullBand = false, automatic = true;
    float maxRed = 0.0f, rangeLo = 30.0f, rangeHi = 250.0f, noSignalTime = 0.0f;
    juce::String diagLine; bool diagBusOn = false; uint32_t lastNz = 0, lastBlocks = 0; float sigHold = 0.0f;
    int selected = 0, dragging = -1;
    juce::Rectangle<int> plot;
    GraphicsQuality quality = GraphicsQuality::High;
    Mascot* mascot = nullptr;
};

class SidechainPanel : public juce::Component
{
public:
    SidechainPanel(AmbientProcessor&, UiSettings&);
    ~SidechainPanel() override;

    void tick(float dt, bool visible);
    void setQuality(GraphicsQuality q);
    void resized() override;
    void paint(juce::Graphics&) override;

    /** Power button for the card header (sc.enable). */
    juce::Button& getEnableButton() noexcept { return enableButton; }
    Mascot& getMiniMascot() noexcept { return *miniMascot; }

private:
    void selectBand(int b);

    struct GrMeter : juce::Component
    {
        float db = 0.0f, hold = 0.0f, holdT = 0.0f;
        void set(float v, float dt);
        void paint(juce::Graphics&) override;
    };

    AmbientProcessor& proc;
    UiSettings& settings;
    SidechainGraph graph;
    std::unique_ptr<Mascot> miniMascot;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    BypassButton enableButton { "Frequency sidechain" };
    std::unique_ptr<BA> enableAtt;

    std::vector<std::unique_ptr<RotaryKnob>> globals;
    PillToggle autoT { "Auto", theme::lime }, fullT { "Full-band", theme::warn },
               listenT { "Listen", theme::magenta }, deltaT { "Delta", theme::uvBright };
    std::vector<std::unique_ptr<BA>> toggleAtts;

    SegmentedControl bandSel { { "1", "2", "3", "4" } };
    std::array<std::unique_ptr<BypassButton>, 4> bandOn;
    std::vector<std::unique_ptr<BA>> bandOnAtts;
    std::array<std::vector<std::unique_ptr<RotaryKnob>>, 4> bandKnobs;
    int selected = 0;

    LevelMeter scMeter;
    GrMeter grMeter;
    juce::Rectangle<int> rowA, rowB, captionA, captionB;
};
} // namespace ambient::ui
