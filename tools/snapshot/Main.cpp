// Renders the AmbientLab editor in several states to PNG, feeding it real audio
// through processBlock so the analyzer shows live data.
// Usage: AmbientSnapshot <outputDir> [scale]

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <ctime>
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>

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
    std::vector<std::pair<juce::String, juce::var>> extra {}; // additional UI settings
};

/** Stereo test programme: synth chord via MIDI + a decorrelated pad/noise on the sidechain. */
int gSignal = 0;
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
            if (gSignal == 1) buf.setSample(1, i, buf.getSample(0, i));
            if (gSignal == 2) buf.setSample(1, i, -buf.getSample(0, i));
            phase += 1.0;
        }
        proc.processBlock(buf, midi);
        midi.clear();
        if (b % 4 == 0)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
    }
}
} // namespace


/** Real-time benchmark: editor on a (virtual) desktop, audio paced at real time.
    Reports analyzer paint time, frame rate and whole-process CPU use. */
static int runBenchmark(const juce::String& filter)
{
    struct Cfg { const char* name; int mode; bool expanded; int quality; bool frozen; };
    const Cfg cfgs[] = {
        { "scope    docked   STD", 0, false, 0, false }, { "scope    docked   HQ ", 0, false, 1, false },
        { "scope    expanded HQ ", 0, true,  1, false },
        { "stereo   docked   STD", 1, false, 0, false }, { "stereo   docked   HQ ", 1, false, 1, false },
        { "stereo   expanded HQ ", 1, true,  1, false },
        { "spectrum docked   STD", 2, false, 0, false }, { "spectrum docked   HQ ", 2, false, 1, false },
        { "spectrum expanded HQ ", 2, true,  1, false }, { "spectrum expanded HQ frozen", 2, true, 1, true },
    };
    const double sr = 48000.0; const int block = 512;
    std::printf("%-28s %10s %8s %10s\n", "configuration", "paint ms", "fps", "CPU %");

    // Audio-thread cost of capture: processBlock with editor closed vs open.
    {
        AmbientProcessor proc; proc.enableAllBuses(); proc.setRateAndBufferSizeDetails(sr, block); proc.prepareToPlay(sr, block);
        juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi; juce::Random rng(1);
        auto timeBlocks = [&](bool open)
        {
            proc.editorOpen.store(open);
            const int N = 20000;
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            for (int b = 0; b < N; ++b)
            {
                for (int i = 0; i < block; ++i) { buf.setSample(0, i, rng.nextFloat() - 0.5f); buf.setSample(1, i, rng.nextFloat() - 0.5f); }
                proc.processBlock(buf, midi);
            }
            return (juce::Time::getMillisecondCounterHiRes() - t0) * 1000.0 / N; // us per block
        };
        timeBlocks(false);
        const double closed = timeBlocks(false), opened = timeBlocks(true);
        std::printf("processBlock (512 frames, incl. test-signal generation): editor closed %.2f us, open (capture) %.2f us\n", closed, opened);
    }

    for (auto& c : cfgs)
    {
        if (filter.isNotEmpty() && ! juce::String(c.name).contains(filter)) continue;
        AmbientProcessor proc; proc.enableAllBuses(); proc.setRateAndBufferSizeDetails(sr, block); proc.prepareToPlay(sr, block);
        if (auto* p = proc.apvts.getParameter("global.sourceMix")) p->setValueNotifyingHost(0.6f);
        auto st = proc.apvts.state.getOrCreateChildWithName("UI", nullptr);
        st.setProperty("an_mode", c.mode, nullptr); st.setProperty("an_expanded", c.expanded, nullptr);
        st.setProperty("quality", c.quality, nullptr);
        juce::MidiBuffer chord; for (int note : { 57, 64, 69, 72 }) chord.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
        juce::AudioBuffer<float> tmp(2, block); tmp.clear(); proc.processBlock(tmp, chord);

        std::unique_ptr<AmbientEditor> ed(dynamic_cast<AmbientEditor*>(proc.createEditor()));
        ed->setSize(1280, 800); ed->setOpaque(true);
        ed->addToDesktop(juce::ComponentPeer::windowIsTemporary); ed->setVisible(true);
        if (c.frozen) { juce::Random r0(3); double ph0 = 0; runAudio(proc, 0.5, sr, block, r0, ph0); ed->setAnalyzerFrozen(true); }

        juce::Random rng(1234); double phase = 0.0;
        // warm-up then measure 3 s at real-time pace
        auto paced = [&](double seconds)
        {
            const double start = juce::Time::getMillisecondCounterHiRes();
            int blocks = 0;
            while (juce::Time::getMillisecondCounterHiRes() - start < seconds * 1000.0)
            {
                const double due = start + blocks * block * 1000.0 / sr;
                const double now = juce::Time::getMillisecondCounterHiRes();
                if (now >= due) { runAudio(proc, block / sr, sr, block, rng, phase); ++blocks; }
                else juce::MessageManager::getInstance()->runDispatchLoopUntil(juce::jmax(1, int(due - now)));
            }
        };
        paced(1.0);
        const std::clock_t c0 = std::clock(); const double w0 = juce::Time::getMillisecondCounterHiRes();
        int framesBefore = 0; juce::ignoreUnused(framesBefore);
        paced(3.0);
        const double cpu = double(std::clock() - c0) / CLOCKS_PER_SEC, wall = (juce::Time::getMillisecondCounterHiRes() - w0) / 1000.0;
        std::printf("%-28s %10.2f %8d %9.1f%%\n", c.name, ed->getAnalyzerPaintMs(), ed->getAnalyzerFps(), 100.0 * cpu / wall);
        ed.reset();
    }
    return 0;
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 1 && juce::String(argv[1]) == "--bench") return runBenchmark(argc > 2 ? juce::String(argv[2]) : juce::String());
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
        { "scope_split_500ms", 0, 0, true, 1280, 800, 1, { { "sc_view", 3 }, { "sc_time", 500.0 }, { "sc_persist", 0.0 } } },
        { "large_2560x1600", 1, 2, false, 2560, 1600 },
        { "small_expanded_scope", 0, 0, true, 960, 600 },
        { "small_stereo", 2, 1, false, 960, 600 },
        { "stereo_lr_docked", 1, 1, false, 1280, 800, 1, { { "vs_display", 1 } } },
        { "stereo_mono_input", 0, 1, true, 1280, 800, 1, { { "test_signal", 1 } } },
        { "stereo_inverted",   0, 1, true, 1280, 800, 1, { { "test_signal", 2 } } },
        { "scope_mono_falling", 0, 0, false, 1280, 800, 1, { { "sc_view", 4 }, { "sc_time", 8.0 }, { "sc_trig", 2 }, { "sc_level", 0.2 }, { "sc_gain", 2.0 } } },
    };
    if (argc > 3) // optional filter: only shots whose name contains argv[3]
        shots.erase(std::remove_if(shots.begin(), shots.end(), [&](const Shot& s) { return ! s.name.contains(argv[3]); }), shots.end());

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
        gSignal = 0;
        for (auto& [k, v] : s.extra)
        {
            if (k == "test_signal") { gSignal = int(v); continue; }
            st.setProperty(juce::Identifier(k), v, nullptr);
        }
        if (gSignal != 0) // only the sidechain test signal, no synth (exact mono / inverted)
            if (auto* p = proc.apvts.getParameter("global.sourceMix")) p->setValueNotifyingHost(1.0f);

        // MIDI chord for the test synth
        juce::MidiBuffer chord;
        for (int note : { 57, 64, 69, 72 })
            chord.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
        juce::AudioBuffer<float> tmp(2, block);
        tmp.clear();
        proc.processBlock(tmp, chord);

        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        ed->setSize(s.width, s.height);
        ed->setOpaque(true);
        ed->addToDesktop(juce::ComponentPeer::windowIsTemporary); // real on-screen painting (Xvfb)
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
