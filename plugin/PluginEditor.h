#pragma once
#include "PluginProcessor.h"
#include "ui/AnalyzerPanel.h"
#include "ui/Backdrop.h"
#include "ui/HeaderBar.h"
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
    static constexpr int kMinW = 960, kMinH = 600, kMaxW = 2560, kMaxH = 1600;

private:
    /** A tab page: a grid of module cards. */
    struct Page : juce::Component
    {
        struct Slot { std::unique_ptr<ambient::ui::ModuleCard> card; int col, row, colSpan, rowSpan; };
        int cols = 1, rows = 1;
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

    AmbientProcessor& proc;
    ambient::ui::UiSettings settings;
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
    std::unique_ptr<ambient::ui::RotaryKnob> inputKnob, mixKnob;
    ambient::ui::BypassButton bypassSynth { "Synth" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;

    ambient::ui::ModuleCard macroCard { "Macros" };
    std::vector<std::unique_ptr<ambient::ui::RotaryKnob>> macros;

    double lastTick = 0.0;
    bool analyzerExpanded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmbientEditor)
};
