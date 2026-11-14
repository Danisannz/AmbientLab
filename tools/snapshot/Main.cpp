// Renders the AmbientLab editor in several states to PNG, feeding it real audio
// through processBlock so the analyzer shows live data.
// Usage: AmbientSnapshot <outputDir> [scale]

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
struct Shot
{
    juce::String name;
    int tab = 0;
    int analyzerMode = 2;    // 0 scope, 1 stereo, 2 spectrum
    bool expanded = false;
    int width = 1280, height = 800;
    int quality = 1;         // 0 standard, 1 high
};

/** Stereo test programme: synth chord via MIDI + a decorrelated pad/noise on the sidechain. */
void runAudio(AmbientProcessor& proc, double seconds, double sr, int block, juce::Random& rng, double& phase)
{
    juce::AudioBuffer<float> buf(2, block);
    juce::MidiBuffer midi;
    const int blocks = int(seconds * sr / block);
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < block; ++i)
        {
            const double t = phase / sr;
            const float pad = 0.25f * float(std::sin(2.0 * juce::MathConstants<double>::pi * 110.0 * t)
                                             + 0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 331.0 * t));
            const float n = 0.03f * (rng.nextFloat() * 2.0f - 1.0f);
            const float lfo = float(std::sin(2.0 * juce::MathConstants<double>::pi * 0.3 * t));
            buf.setSample(0, i, pad * (0.8f + 0.2f * lfo) + n);
            buf.setSample(1, i, pad * (0.8f - 0.2f * lfo) * 0.9f
                                + 0.12f * float(std::sin(2.0 * juce::MathConstants<double>::pi * 220.5 * t)) - n);
            phase += 1.0;
        }
        proc.processBlock(buf, midi);
        midi.clear();
        if (b % 4 == 0)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
    }
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir(argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1])
                                     : juce::File::getCurrentWorkingDirectory().getChildFile("snapshots"));
    const float scale = argc > 2 ? (float) std::atof(argv[2]) : 1.0f;
    outDir.createDirectory();

    std::vector<Shot> shots {
        { "sound_spectrum",   0, 2 },
        { "process_scope",    1, 0 },
        { "mod_stereo",       2, 1 },
        { "seq_spectrum",     3, 2 },
        { "expanded_spectrum",0, 2, true },
        { "expanded_scope",   0, 0, true },
        { "expanded_stereo",  0, 1, true },
        { "small_960",        0, 2, false, 960, 600 },
        { "wide_1600x800",    0, 0, false, 1600, 800 },
        { "standard_quality", 0, 2, false, 1280, 800, 0 },
    };

    const double sr = 48000.0;
    const int block = 512;

    for (auto& s : shots)
    {
        AmbientProcessor proc;
        proc.enableAllBuses();
        proc.setRateAndBufferSizeDetails(sr, block);
        proc.prepareToPlay(sr, block);
        if (auto* p = proc.apvts.getParameter("global.sourceMix")) p->setValueNotifyingHost(0.6f);

        auto st = proc.apvts.state.getOrCreateChildWithName("UI", nullptr);
        st.setProperty("uiTab", s.tab, nullptr);
        st.setProperty("uiWidth", s.width, nullptr);
        st.setProperty("uiHeight", s.height, nullptr);
        st.setProperty("an_mode", s.analyzerMode, nullptr);
        st.setProperty("an_expanded", s.expanded, nullptr);
        st.setProperty("quality", s.quality, nullptr);

        // MIDI chord for the test synth
        juce::MidiBuffer chord;
        for (int note : { 57, 64, 69, 72 })
            chord.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
        juce::AudioBuffer<float> tmp(2, block);
        tmp.clear();
        proc.processBlock(tmp, chord);

        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        ed->setSize(s.width, s.height);
        ed->setVisible(true);

        juce::Random rng(1234);
        double phase = 0.0;
        runAudio(proc, 1.5, sr, block, rng, phase); // fills averaging, persistence

        // let real time pass so 4 Hz readouts (CPU, rate) and ballistics settle
        for (int k = 0; k < 20; ++k) { runAudio(proc, 0.02, sr, block, rng, phase); juce::Thread::sleep(20); }

        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, scale);
        auto f = outDir.getChildFile(s.name + ".png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
        std::printf("wrote %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());

        ed.reset();
        proc.releaseResources();
    }
    return 0;
}
