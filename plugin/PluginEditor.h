#pragma once
#include "PluginProcessor.h"
#include "ui/AnalyzerPanel.h"
#include "ui/Backdrop.h"
#include "ui/HeaderBar.h"
#include "ui/SidechainPanel.h"
#include "ui/FilterEnvPanels.h"
#include "ui/ModulationPanels.h"
#include "ui/SequencerPanel.h"
#include "ui/UiSettings.h"
#include "viz/VisualizerHub.h"

/** AMBIENTLAB editor.
    Layout:  header | tab bar | [ active page | docked analyzer ] | macros footer
    The analyzer can expand over the page area. */
class AmbientEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AmbientEditor(AmbientProcessor&);
    ~AmbientEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseW = 1280, kBaseH = 800;

    /** Benchmark/diagnostic access (snapshot tool). */
    double getAnalyzerPaintMs() const { return analyzer ? analyzer->getAveragePaintMs() : 0.0; }
    int getAnalyzerFps() const { return analyzer ? analyzer->getFps() : 0; }
    void setAnalyzerFrozen(bool f);
    static constexpr int kMinW = 960, kMinH = 600, kMaxW = 2560, kMaxH = 1600;

private:
    /** A tab page: a grid of module cards. */
    struct Page : juce::Component
    {
        struct Slot { std::unique_ptr<ambient::ui::ModuleCard> card; int col, row, colSpan, rowSpan; };
        int cols = 1, rows = 1;
        std::vector<float> rowWeights; // optional relative row heights
        std::vector<Slot> slots;
        std::function<void()> layoutContents; // positions real controls inside cards
        ambient::ui::ModuleCard& add(const juce::String& title, ambient::ui::Glyph, int c, int r, int cs = 1, int rs = 1);
        void resized() override;
        float scale = 1.0f;
    };

    void timerCallback() override;
    void buildPages();
    void setScale(float s);
    void showPage(int index);
    void setQuality(ambient::ui::GraphicsQuality q);
    void setMascotAnimated(bool on);

    AmbientProcessor& proc;
    ambient::ui::UiSettings settings;
    /** Holds the whole interface. Laid out on a virtual canvas and scaled uniformly
        (vector rendering stays sharp; image layers render at physical resolution). */
    struct Content : juce::Component
    {
        std::function<void(juce::Graphics&)> painter;
        void paint(juce::Graphics& g) override { if (painter) painter(g); }
    } content;
    float uiScale = 1.0f;
    ambient::ui::AmbientLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 700 };
    juce::Image logo;
    ambient::ui::Backdrop backdrop;
    ambient::ui::GraphicsQuality quality = ambient::ui::GraphicsQuality::High;

    ambient::viz::VisualizerHub hub;
    std::unique_ptr<ambient::ui::HeaderBar> header;
    ambient::ui::TabBar tabs { { "SOUND", "PROCESS", "MODULATION", "SEQUENCER" } };
    std::array<Page, 4> pages;
    std::unique_ptr<ambient::ui::AnalyzerPanel> analyzer;

    // Real controls (only parameters that exist)
    std::unique_ptr<ambient::ui::RotaryKnob> inputKnob; // FX: track input gain
    std::unique_ptr<ambient::ui::SidechainPanel> sidechain;
    std::unique_ptr<ambient::ui::OscillatorPanel> oscillators;
    std::unique_ptr<ambient::ui::FilterPanel> filterPanel;
    std::unique_ptr<ambient::ui::EnvelopePanel> envPanel;
    std::unique_ptr<ambient::ui::GranularPanel> granularPanel;
    std::unique_ptr<ambient::ui::ReverbPanel> reverbPanel;
    std::unique_ptr<ambient::ui::StereoPanel> stereoPanel;
    std::unique_ptr<ambient::ui::DelayPanel> delayPanel;
    std::array<std::unique_ptr<ambient::ui::LfoPanel>, 4> lfoPanels;
    std::unique_ptr<ambient::ui::ModMatrixPanel> matrixPanel;
    std::unique_ptr<ambient::ui::SequencerPanel> sequencerPanel;
    ambient::ui::SegmentedControl processSel { { "SIDECHAIN", "GRANULAR", "REVERB", "DELAY", "STEREO", "ROUTING" } };
    std::vector<ambient::ui::ModuleCard*> processCards;
    ambient::ui::BypassButton bypassSynth { "Synth" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;

    ambient::ui::ModuleCard macroCard { "Macros" };
    std::vector<std::unique_ptr<ambient::ui::RotaryKnob>> macros;

    double lastTick = 0.0;
    bool analyzerExpanded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmbientEditor)
};
