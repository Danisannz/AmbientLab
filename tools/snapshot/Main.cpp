#include "../../tests/StereoMetrics.h"
#include "../../plugin/ModDestinations.h"
#include "../../plugin/Presets.h"
#include <ambient/dsp/Scales.h>
#include "../../plugin/ui/ModAssign.h"
// Renders the AmbientLab editor in several states to PNG, feeding it real audio
// through processBlock so the analyzer shows live data.
// Usage: AmbientSnapshot <outputDir> [scale]

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/Mascot.h"
#include "ui/MascotArt.h"
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
/** Feeds the processor through its real buses: kick reference on the aux sidechain
    (bus 1), a decorrelated pad on the FX main input (bus 0). The instrument plays
    its own synth chord (MIDI) as the internal signal. */
void runAudio(AmbientProcessor& proc, double seconds, double sr, int block, juce::Random& rng, double& phase)
{
    const int totalCh = juce::jmax(proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels(), 2);
    juce::AudioBuffer<float> buf(totalCh, block);
    juce::MidiBuffer midi;
    const int blocks = juce::jmax(1, int(seconds * sr / block));
    const double period = sr * 60.0 / 145.0;
    constexpr double twoPi = 2.0 * juce::MathConstants<double>::pi;
    const bool hasMain = ! AMBIENT_IS_INSTRUMENT && proc.getBusCount(true) > 0 && proc.getBus(true, 0)->isEnabled();
    const bool hasSc = proc.getBusCount(true) > AmbientProcessor::kScBus && proc.getBus(true, AmbientProcessor::kScBus)->isEnabled();
    static double kickPhase = 0.0;
    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        juce::AudioBuffer<float> main, sc;
        if (hasMain) main = proc.getBusBuffer(buf, true, 0);
        if (hasSc) sc = proc.getBusBuffer(buf, true, AmbientProcessor::kScBus);
        for (int i = 0; i < block; ++i)
        {
            const double t = phase / sr;
            if (hasMain && main.getNumChannels() >= 2)
            {
                const float pad = 0.25f * float(std::sin(twoPi * 55.0 * t) + 0.5 * std::sin(twoPi * 331.0 * t) + 0.3 * std::sin(twoPi * 880.0 * t));
                const float n = 0.03f * (rng.nextFloat() * 2.0f - 1.0f);
                const float lfo = float(std::sin(twoPi * 0.3 * t));
                float l = pad * (0.8f + 0.2f * lfo) + n;
                float r = pad * (0.8f - 0.2f * lfo) * 0.9f + 0.12f * float(std::sin(twoPi * 220.5 * t)) - n;
                if (gSignal == 1) r = l;
                if (gSignal == 2) r = -l;
                main.setSample(0, i, l); main.setSample(1, i, r);
            }
            if (hasSc)
            {
                const double tk = std::fmod(phase, period) / sr;
                kickPhase += twoPi * (52.0 + 90.0 * std::exp(-tk / 0.025)) / sr;
                const float k = 0.8f * float(std::exp(-tk / 0.16) * std::sin(kickPhase)) + 0.15f * float(std::sin(twoPi * 110.0 * t)); // kick + bass
                for (int c = 0; c < sc.getNumChannels(); ++c) sc.setSample(c, i, k);
            }
            phase += 1.0;
        }
        proc.processBlock(buf, midi);
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
        juce::AudioBuffer<float> buf(juce::jmax(proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels(), 2), block); juce::MidiBuffer midi; juce::Random rng(1);
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
        juce::MidiBuffer chord; for (int note : { 33, 57, 64, 69 }) chord.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
        juce::AudioBuffer<float> tmp(juce::jmax(proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels(), 2), block); tmp.clear(); proc.processBlock(tmp, chord);

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

// ---------------------------------------------------------------------------
// Processor-level self test: real AmbientProcessor, real bus layouts.
static int runSelfTest()
{
    int failures = 0;
    auto check = [&](bool ok, const char* what) { std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what); if (! ok) ++failures; };
    const double sr = 48000.0; const int block = 480;
    constexpr double kPi = 3.14159265358979323846;

    auto setParam = [](AmbientProcessor& p, const juce::String& pid, float plain)
    {
        if (auto* prm = p.apvts.getParameter(pid)) prm->setValueNotifyingHost(prm->convertTo0to1(plain));
    };

    // Layout: buses in this order -> inputs: [0] main, [1] sidechain.
    auto makeLayout = [](const juce::AudioChannelSet& main, const juce::AudioChannelSet& sc)
    {
        juce::AudioProcessor::BusesLayout l;
        if (AmbientProcessor::kMainBus >= 0) l.inputBuses.add(main);
        l.inputBuses.add(sc);
        l.outputBuses.add(juce::AudioChannelSet::stereo());
        return l;
    };

    for (bool monoSc : { false, true })
    {
        AmbientProcessor p;
        const auto layout = makeLayout(AMBIENT_IS_INSTRUMENT ? juce::AudioChannelSet::disabled() : juce::AudioChannelSet::stereo(),
                                       monoSc ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        check(p.setBusesLayout(layout), monoSc ? "mono sidechain layout accepted" : "stereo sidechain layout accepted");
        p.setRateAndBufferSizeDetails(sr, block);
        p.prepareToPlay(sr, block);
        check(p.getLatencySamples() == 0, "reported latency is 0 samples");

        // Loud kick-like reference, synth bypassed, no main input -> output must be silent.
        setParam(p, "global.bypassSynth", 1.0f);
        const int scCh = monoSc ? 1 : 2;
        const int totalCh = juce::jmax(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
        juce::AudioBuffer<float> buf(totalCh, block);
        juce::MidiBuffer midi;
        float leak = 0.0f; double phase = 0.0;
        for (int b = 0; b < 200; ++b)
        {
            buf.clear();
            auto sc = p.getBusBuffer(buf, true, AmbientProcessor::kScBus);
            for (int i = 0; i < block; ++i)
            {
                phase += 2.0 * kPi * 55.0 / sr;
                for (int c = 0; c < scCh; ++c) sc.setSample(c, i, 0.9f * float(std::sin(phase)));
            }
            p.processBlock(buf, midi);
            for (int c = 0; c < 2; ++c) leak = juce::jmax(leak, buf.getMagnitude(c, 0, block));
        }
        check(leak == 0.0f, "no sidechain leakage into the output (synth bypassed)");
        check(p.sidechainTelemetry().referencePresent.load(), "sidechain reference detected");

        // Listen: the reference is monitored on purpose (unity master for an absolute level check).
        setParam(p, "sc.listen", 1.0f);
        setParam(p, "global.masterGain", 0.0f);
        float heard = 0.0f;
        for (int b = 0; b < 20; ++b)
        {
            buf.clear();
            auto sc = p.getBusBuffer(buf, true, AmbientProcessor::kScBus);
            for (int i = 0; i < block; ++i) { phase += 2.0 * kPi * 55.0 / sr; for (int c = 0; c < scCh; ++c) sc.setSample(c, i, 0.5f * float(std::sin(phase))); }
            p.processBlock(buf, midi);
            heard = juce::jmax(heard, buf.getMagnitude(0, 0, block));
        }
        check(heard > 0.3f, "Listen monitors the sidechain reference");
        p.releaseResources();
    }

    // Ducking through the real plugin: internal 55 Hz + 880 Hz, reference kicks at 55 Hz.
    {
        AmbientProcessor p;
        p.setBusesLayout(makeLayout(AMBIENT_IS_INSTRUMENT ? juce::AudioChannelSet::disabled() : juce::AudioChannelSet::stereo(),
                                    juce::AudioChannelSet::stereo()));
        p.setRateAndBufferSizeDetails(sr, block);
        p.prepareToPlay(sr, block);
        setParam(p, "sc.auto", 0.0f);
        setParam(p, "sc.band1.freq", 55.0f);
        setParam(p, "sc.band1.threshold", -36.0f);
        float maxGr = 0.0f;
        const int totalCh = juce::jmax(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
        juce::AudioBuffer<float> buf(totalCh, block);
        juce::MidiBuffer midi;
       #if AMBIENT_IS_INSTRUMENT
        { juce::MidiBuffer on; on.addEvent(juce::MidiMessage::noteOn(1, 33, 1.0f), 0); buf.clear(); p.processBlock(buf, on); } // A1 = 55 Hz test tone
       #endif
        double ph = 0.0, ph2 = 0.0; size_t t = 0;
        const size_t period = size_t(sr * 60.0 / 145.0);
        for (int b = 0; b < 300; ++b)
        {
            buf.clear();
            auto sc = p.getBusBuffer(buf, true, AmbientProcessor::kScBus);
           #if ! AMBIENT_IS_INSTRUMENT
            auto in = p.getBusBuffer(buf, true, 0);
           #endif
            for (int i = 0; i < block; ++i, ++t)
            {
                const double tt = double(t % period) / sr;
                ph += 2.0 * kPi * (55.0 + 85.0 * std::exp(-tt / 0.025)) / sr;
                const float k = 0.8f * float(std::exp(-tt / 0.16) * std::sin(ph));
                sc.setSample(0, i, k); sc.setSample(1, i, k);
               #if ! AMBIENT_IS_INSTRUMENT
                ph2 += 2.0 * kPi * 55.0 / sr;
                const float d = 0.25f * float(std::sin(ph2) + std::sin(ph2 * 16.0));
                in.setSample(0, i, d); in.setSample(1, i, d);
               #endif
            }
            p.processBlock(buf, midi);
            maxGr = juce::jmax(maxGr, p.sidechainTelemetry().maxReductionDb.load());
        }
        std::printf("       max gain reduction through the plugin: %.2f dB, transients: %u\n", maxGr, p.sidechainTelemetry().transientCount.load());
        check(maxGr > 1.0f && maxGr <= 6.0f + 1e-3f, "plugin ducks the conflicting band within the default 6 dB cap");
        const auto expected = uint32_t((t + period - 1) / period); // kicks actually sent
        check(p.sidechainTelemetry().transientCount.load() == expected, "every kick detected exactly once through the sidechain bus");
    }

    // Mascot synchronisation: poll telemetry at UI rate (60 Hz) while kicks arrive;
    // every detected transient must produce exactly one reaction, nothing else.
    {
        AmbientProcessor p;
        p.setBusesLayout(makeLayout(AMBIENT_IS_INSTRUMENT ? juce::AudioChannelSet::disabled() : juce::AudioChannelSet::stereo(),
                                    juce::AudioChannelSet::stereo()));
        p.setRateAndBufferSizeDetails(sr, block);
        p.prepareToPlay(sr, block);
        ambient::ui::Mascot mascot;
        const int totalCh = juce::jmax(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
        juce::AudioBuffer<float> buf(totalCh, block);
        juce::MidiBuffer midi;
        double ph = 0.0; size_t t = 0;
        const size_t period = size_t(sr * 60.0 / 172.0); // fast psytrance tempo
        const int blocksPerFrame = int(sr / 60.0 / block + 0.5);
        uint32_t idleReactions = 0;
        for (int b = 0; b < 600; ++b)
        {
            buf.clear();
            auto sc = p.getBusBuffer(buf, true, AmbientProcessor::kScBus);
            const bool kicksOn = b >= 100; // first 1 s: silence -> idle only
            for (int i = 0; i < block; ++i, ++t)
            {
                const double tt = double(t % period) / sr;
                ph += 2.0 * kPi * (52.0 + 90.0 * std::exp(-tt / 0.025)) / sr;
                const float k = kicksOn ? 0.8f * float(std::exp(-tt / 0.16) * std::sin(ph)) : 0.0f;
                sc.setSample(0, i, k); sc.setSample(1, i, k);
            }
            p.processBlock(buf, midi);
            if (b % juce::jmax(1, blocksPerFrame) == 0)
            {
                const auto& tl = p.sidechainTelemetry();
                mascot.tick(1.0f / 60.0f, tl.transientCount.load(), tl.lastTransientStrength.load(),
                            tl.referencePresent.load(), tl.maxReductionDb.load());
            }
            if (b == 99) idleReactions = mascot.getReactionCount();
        }
        const auto detected = p.sidechainTelemetry().transientCount.load();
        std::printf("       transients detected: %u, mascot reactions: %u\n", detected, mascot.getReactionCount());
        check(idleReactions == 0, "mascot idle without sidechain signal (no random reactions)");
        check(mascot.getReactionCount() == detected && detected > 0, "mascot reacts exactly once per detected kick");
    }

    // CPU: processBlock cost with the frequency sidechain active (4 bands, Auto).
    {
        std::printf("       processBlock cost (sidechain active, 4 bands, Auto):\n");
        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (int bs : { 64, 512 })
            {
                AmbientProcessor p;
                p.setBusesLayout(makeLayout(AMBIENT_IS_INSTRUMENT ? juce::AudioChannelSet::disabled() : juce::AudioChannelSet::stereo(),
                                            juce::AudioChannelSet::stereo()));
                p.setRateAndBufferSizeDetails(rate, bs);
                p.prepareToPlay(rate, bs);
                for (int b = 0; b < 4; ++b) setParam(p, "sc.band" + juce::String(b + 1) + ".enable", 1.0f);
                const int totalCh = juce::jmax(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
                juce::AudioBuffer<float> buf(totalCh, bs);
                juce::MidiBuffer midi; juce::Random rng(5);
                const int blocks = int(rate * 2.0 / bs); // 2 s of audio
                double t0 = 0.0;
                for (int b = 0; b < blocks; ++b)
                {
                    buf.clear();
                    auto sc = p.getBusBuffer(buf, true, AmbientProcessor::kScBus);
                    for (int c = 0; c < sc.getNumChannels(); ++c)
                        for (int i = 0; i < bs; ++i) sc.setSample(c, i, 0.5f * (rng.nextFloat() - 0.5f));
                    if (b == blocks / 4) t0 = juce::Time::getMillisecondCounterHiRes();
                    p.processBlock(buf, midi);
                }
                const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
                const double audioMs = (blocks - blocks / 4) * bs * 1000.0 / rate;
                std::printf("         %6.1f kHz, block %4d: %5.2f %% of real time (one core)\n", rate / 1000.0, bs, 100.0 * ms / audioMs);
            }
    }

   #if AMBIENT_IS_INSTRUMENT
    // CPU: worst-case synth load - 8 voices, Osc A + B at 7-voice unison, FM, sub, noise, saturation.
    {
        std::printf("       synth cost (8 voices, A+B unison 7, FM, sub, noise, drive):\n");
        for (double rate : { 48000.0, 96000.0, 192000.0 })
        {
            AmbientProcessor p;
            p.setBusesLayout(makeLayout(juce::AudioChannelSet::disabled(), juce::AudioChannelSet::disabled()));
            p.setRateAndBufferSizeDetails(rate, 512);
            p.prepareToPlay(rate, 512);
            setParam(p, "osc.a.unison", 7.0f); setParam(p, "osc.b.unison", 7.0f); setParam(p, "osc.b.on", 1.0f);
            setParam(p, "osc.fm", 0.4f); setParam(p, "osc.noise.on", 1.0f); setParam(p, "osc.sat.drive", 12.0f);
            juce::AudioBuffer<float> buf(juce::jmax(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels(), 2), 512);
            juce::MidiBuffer notes;
            for (int nn : { 36, 43, 48, 55, 60, 64, 67, 72 }) notes.addEvent(juce::MidiMessage::noteOn(1, nn, 0.8f), 0);
            juce::MidiBuffer none;
            const int blocks = int(rate * 2.0 / 512);
            double t0 = 0.0;
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
                if (b == blocks / 4) t0 = juce::Time::getMillisecondCounterHiRes();
                p.processBlock(buf, b == 0 ? notes : none);
            }
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            const double audioMs = (blocks - blocks / 4) * 512 * 1000.0 / rate;
            std::printf("         %6.1f kHz: %5.1f %% of real time (one core)\n", rate / 1000.0, 100.0 * ms / audioMs);
        }
    }
   #endif

    // Reverb (Stage 5): a short burst rings on with the reverb on, stops dead with it off; per-algorithm CPU.
    {
        auto tailEnergy = [&](bool reverbOn, int algo, double rate, int bs, double* cpuPct)
        {
            AmbientProcessor p;
            p.setBusesLayout(makeLayout(AMBIENT_IS_INSTRUMENT ? juce::AudioChannelSet::disabled() : juce::AudioChannelSet::stereo(),
                                        juce::AudioChannelSet::stereo()));
            p.setRateAndBufferSizeDetails(rate, bs);
            p.prepareToPlay(rate, bs);
            setParam(p, "env.amp.r", 1.0f);
            setParam(p, "rv.on", reverbOn ? 1.0f : 0.0f);
            setParam(p, "rv.algo", float(algo));
            setParam(p, "rv.decay", 2.0f); setParam(p, "rv.mix", 1.0f); setParam(p, "rv.predelay", 0.0f);
            const int totalCh = juce::jmax(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels(), 2);
            juce::AudioBuffer<float> buf(totalCh, bs);
            juce::MidiBuffer on, off, none;
            on.addEvent(juce::MidiMessage::noteOn(1, 57, 0.9f), 0);
            off.addEvent(juce::MidiMessage::noteOff(1, 57), 0);
            const int burstBlocks = juce::jmax(1, int(rate * 0.1 / bs));
            const int blocks = int(rate * 1.5 / bs);
            double tail = 0.0, t0 = 0.0, ph = 0.0;
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
               #if ! AMBIENT_IS_INSTRUMENT
                if (b < burstBlocks)
                    for (int i = 0; i < bs; ++i, ph += 2.0 * kPi * 220.0 / rate)
                        for (int c = 0; c < 2; ++c) buf.setSample(c, i, 0.5f * float(std::sin(ph)));
               #endif
                if (b == blocks / 4) t0 = juce::Time::getMillisecondCounterHiRes();
                p.processBlock(buf, b == 0 ? on : (b == burstBlocks ? off : none));
                if (b * bs > int(rate * 0.6))
                    for (int c = 0; c < 2; ++c) tail += double(buf.getRMSLevel(c, 0, bs));
            }
            if (cpuPct) *cpuPct = 100.0 * (juce::Time::getMillisecondCounterHiRes() - t0) / ((blocks - blocks / 4) * bs * 1000.0 / rate);
            return tail;
        };
        check(tailEnergy(false, 0, sr, block, nullptr) < 1e-6, "reverb off: no tail after the source stops");
        bool allRing = true;
        for (int a = 0; a < 7; ++a) allRing = allRing && tailEnergy(true, a, sr, block, nullptr) > 1e-3;
        check(allRing, "reverb on: every algorithm rings after the source stops");
        std::printf("       reverb cost (processBlock total, block 512):\n");
        const char* names[] = { "Hall", "Room", "Cathedral", "Plate", "Spring", "Shimmer", "Infinite" };
        for (double rate : { 48000.0, 192000.0 })
            for (int a = 0; a < 7; ++a)
            {
                double cpu = 0.0, base = 0.0;
                tailEnergy(false, a, rate, 512, &base);
                tailEnergy(true, a, rate, 512, &cpu);
                std::printf("         %6.1f kHz %-10s: %5.2f %% (reverb adds %5.2f %%)\n", rate / 1000.0, names[a], cpu, cpu - base);
            }
    }

    // Delay (Stage 6): synced echo position through the whole plugin (no host playhead = 120 BPM),
    // and CPU per type.
    {
        auto runDelay = [&](int mode, double rate, double* cpuPct, size_t* echoAt)
        {
            AmbientProcessor p;
            p.setBusesLayout(makeLayout(AMBIENT_IS_INSTRUMENT ? juce::AudioChannelSet::disabled() : juce::AudioChannelSet::stereo(),
                                        juce::AudioChannelSet::stereo()));
            p.setRateAndBufferSizeDetails(rate, 512);
            p.prepareToPlay(rate, 512);
            setParam(p, "dl.on", 1.0f); setParam(p, "dl.mode", float(mode)); setParam(p, "dl.mix", 0.5f);
            setParam(p, "dl.division", 9.0f);   // 1/4 -> 500 ms at 120 BPM
            setParam(p, "env.amp.a", 0.5f); setParam(p, "env.amp.d", 1.0f); setParam(p, "env.amp.s", 0.0f); setParam(p, "env.amp.r", 1.0f);
            const int ch = juce::jmax(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels(), 2);
            juce::AudioBuffer<float> buf(ch, 512);
            juce::MidiBuffer on, none; on.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 0);
            const int blocks = int(rate * 1.0 / 512);
            std::vector<float> L; double t0 = 0;
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
               #if ! AMBIENT_IS_INSTRUMENT
                if (b == 0) for (int i = 0; i < 64; ++i) { buf.setSample(0, i, 0.8f); buf.setSample(1, i, 0.8f); }
               #endif
                if (b == blocks / 4) t0 = juce::Time::getMillisecondCounterHiRes();
                p.processBlock(buf, b == 0 ? on : none);
                for (int i = 0; i < 512; ++i) L.push_back(buf.getSample(0, i));
            }
            if (cpuPct) *cpuPct = 100.0 * (juce::Time::getMillisecondCounterHiRes() - t0) / ((blocks - blocks / 4) * 512 * 1000.0 / rate);
            if (echoAt)
            {   // first strong onset after 300 ms
                size_t k = 0; float pk = 0; for (size_t i = size_t(rate * 0.3); i < size_t(rate * 0.7); ++i) if (std::abs(L[i]) > pk) { pk = std::abs(L[i]); k = i; }
                size_t on1 = k; while (on1 > size_t(rate * 0.3) && std::abs(L[on1]) > 0.1f * pk) --on1;
                *echoAt = on1;
            }
        };
        size_t at = 0;
        runDelay(2, sr, nullptr, &at);
        std::printf("       digital delay 1/4 @ 120 BPM: echo onset at %.1f ms\n", 1000.0 * double(at) / sr);
        check(std::abs(double(at) / sr - 0.5) < 0.012, "synced delay echo arrives at 500 ms (1/4 @ 120 BPM)");
        std::printf("       delay cost (processBlock total, block 512):\n");
        const char* names[] = { "Tape", "BBD", "Digital", "Ping-pong", "Multi-tap", "Reverse", "Pitch", "Granular" };
        for (double rate : { 48000.0, 192000.0 })
            for (int m = 0; m < 8; ++m) { double c = 0; runDelay(m, rate, &c, nullptr); std::printf("         %6.1f kHz %-10s: %5.2f %%\n", rate / 1000.0, names[m], c); }
    }

    // Modulation (Stage 7): LFO / macro / sidechain sources really move a destination through
    // the whole plugin; 32 active slots stay cheap.
    {
        auto destIndex = [](const char* pid) { for (int k = 0; k < ambient::moddest::kNumDestinations; ++k) if (ambient::moddest::kDestinations[size_t(k)] == pid) return k + 1; return 0; };
        auto render = [&](std::function<void(AmbientProcessor&)> setup, double seconds, std::vector<float>& env, double* cpu = nullptr, bool kicks = false)
        {
            AmbientProcessor p;
            p.setBusesLayout(makeLayout(AMBIENT_IS_INSTRUMENT ? juce::AudioChannelSet::disabled() : juce::AudioChannelSet::stereo(),
                                        juce::AudioChannelSet::stereo()));
            p.setRateAndBufferSizeDetails(sr, 512);
            p.prepareToPlay(sr, 512);
            setup(p);
            const int ch = juce::jmax(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels(), 2);
            juce::AudioBuffer<float> buf(ch, 512);
            juce::MidiBuffer on, none; on.addEvent(juce::MidiMessage::noteOn(1, 57, 0.9f), 0);
            const int blocks = int(sr * seconds / 512);
            env.clear(); long t = 0; double t0 = 0;
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
               #if ! AMBIENT_IS_INSTRUMENT
                for (int i = 0; i < 512; ++i) { const float x = 0.3f * float(std::sin(2 * kPi * 220.0 * double(t + i) / sr)); buf.setSample(0, i, x); buf.setSample(1, i, x); }
               #endif
                if (kicks)
                {
                    auto sc = p.getBusBuffer(buf, true, AmbientProcessor::kScBus);
                    for (int c = 0; c < sc.getNumChannels(); ++c)
                        for (int i = 0; i < 512; ++i) { const long k = (t + i) % 24000; sc.setSample(c, i, k < 2400 ? 0.9f * float(std::exp(-double(k) / 600.0) * std::sin(2 * kPi * 60.0 * double(k) / sr)) : 0.0f); }
                }
                if (b == blocks / 4) t0 = juce::Time::getMillisecondCounterHiRes();
                p.processBlock(buf, b == 0 ? on : none);
                t += 512;
                env.push_back(buf.getRMSLevel(0, 0, 512));
            }
            if (cpu) *cpu = 100.0 * (juce::Time::getMillisecondCounterHiRes() - t0) / ((blocks - blocks / 4) * 512 * 1000.0 / sr);
        };
        auto spread = [](const std::vector<float>& e, size_t from) { float lo = 1e9f, hi = 0; for (size_t i = from; i < e.size(); ++i) { lo = std::min(lo, e[i]); hi = std::max(hi, e[i]); } return 20.0f * std::log10((hi + 1e-9f) / (lo + 1e-9f)); };
        const int master = destIndex("global.masterGain");
        std::vector<float> e0, e1;
        render([&](AmbientProcessor&) {}, 3.0, e0);
        render([&](AmbientProcessor& p) { setParam(p, "lfo.1.wave", 4.0f); setParam(p, "lfo.1.rate", 2.0f);
                                          setParam(p, "mm.1.src", 1.0f); setParam(p, "mm.1.dst", float(master)); setParam(p, "mm.1.amt", -0.3f); }, 3.0, e1);
        std::printf("       LFO1 (square 2 Hz) -> master gain: level swing %.1f dB (no modulation: %.1f dB)\n", spread(e1, 100), spread(e0, 100));
        check(spread(e1, 100) > spread(e0, 100) + 10.0f, "LFO -> master gain modulates the output (vs the unmodulated note)");
        // count square transitions = 2 per cycle -> ~2 Hz
        int edges = 0; const float mid = 0.5f * (*std::max_element(e1.begin() + 100, e1.end()) + *std::min_element(e1.begin() + 100, e1.end()));
        std::vector<size_t> edgeAt;
        for (size_t i = 101; i < e1.size(); ++i) if ((e1[i] > mid) != (e1[i - 1] > mid)) { ++edges; edgeAt.push_back(i); }
        if (std::getenv("AMBIENT_DEBUG_EDGES")) for (auto ea : edgeAt) std::printf("         edge at block %zu (%.3f s)\n", ea, double(ea) * 512.0 / sr);
        // rate from the first to the last edge (whole half-periods), 512-sample block resolution
        const double hz = edgeAt.size() >= 3 ? double(edgeAt.size() - 1) / 2.0 / (double(edgeAt.back() - edgeAt.front()) * 512.0 / sr) : 0.0;
        std::printf("       LFO rate measured through the plugin: %.2f Hz (set 2.00)\n", hz);
        check(std::abs(hz - 2.0) < 0.1, "LFO rate is reproduced through the plugin");

        std::vector<float> m0, m1;
        auto macroSetup = [&](float macro) { return [=, &setParam](AmbientProcessor& p) { setParam(p, "macro.1", macro);
            setParam(p, "mm.1.src", 5.0f); setParam(p, "mm.1.dst", float(master)); setParam(p, "mm.1.amt", -0.4f); }; };
        render(macroSetup(0.0f), 1.5, m0); render(macroSetup(1.0f), 1.5, m1);
        const float dm = 20.0f * std::log10((m1.back() + 1e-9f) / (m0.back() + 1e-9f));
        std::printf("       Macro 1 -> master gain: %.1f dB between macro 0 and 1\n", dm);
        check(dm < -6.0f, "macro knob drives its destination");

        std::vector<float> k0, k1;
        render([&](AmbientProcessor&) {}, 3.0, k0, nullptr, true);
        render([&](AmbientProcessor& p) { setParam(p, "sc.enable", 0.0f);
                                          setParam(p, "mm.1.src", 20.0f); setParam(p, "mm.1.dst", float(master)); setParam(p, "mm.1.amt", -0.6f); }, 3.0, k1, nullptr, true);
        std::printf("       SC transient -> master gain: swing %.1f dB with kicks\n", spread(k1, 100));
        check(spread(k1, 100) > 3.0f, "sidechain transient works as a modulation source");

        double cpu0 = 0, cpu32 = 0; std::vector<float> tmp;
        render([&](AmbientProcessor&) {}, 3.0, tmp, &cpu0);
        render([&](AmbientProcessor& p) { for (int m = 1; m <= 32; ++m) { const juce::String pre = "mm." + juce::String(m) + ".";
                    setParam(p, pre + "src", float(1 + (m % 4))); setParam(p, pre + "dst", float(1 + (m * 7) % ambient::moddest::kNumDestinations)); setParam(p, pre + "amt", 0.05f); } }, 3.0, tmp, &cpu32);
        std::printf("       processBlock with 0 / 32 active mod slots: %.2f %% / %.2f %%\n", cpu0, cpu32);
        check(cpu32 < cpu0 + 3.0, "32 active modulation slots cost < 3 % of a core");
        bool finite = true; for (float v : tmp) finite = finite && std::isfinite(v);
        check(finite, "32 random modulation routings stay finite");
    }

    // Sequencer (Stage 8): internal clock drives the synth; host-synced steps land on the grid;
    // scale quantisation; pattern persists through the state; parameter lock reaches its target.
    {
        auto makeProc = [&](std::unique_ptr<AmbientProcessor>& p)
        {
            p = std::make_unique<AmbientProcessor>();
            p->setBusesLayout(makeLayout(AMBIENT_IS_INSTRUMENT ? juce::AudioChannelSet::disabled() : juce::AudioChannelSet::stereo(),
                                         juce::AudioChannelSet::stereo()));
            p->setRateAndBufferSizeDetails(sr, 512);
            p->prepareToPlay(sr, 512);
        };
        std::unique_ptr<AmbientProcessor> p; makeProc(p);
        setParam(*p, "seq.on", 1.0f); setParam(*p, "seq.clock", 1.0f); setParam(*p, "seq.bpm", 120.0f);
        setParam(*p, "seq.division", 3.0f);  // 1/16 = 125 ms at 120 BPM
        setParam(*p, "env.amp.a", 1.0f); setParam(*p, "env.amp.r", 5.0f);
        for (int i = 0; i < 16; ++i) { auto st = p->getSeqStep(i); st.active = true; st.probability = 1.0f; st.ratchet = 1; st.skip = false; st.gate = 0.5f; st.velocity = 1.0f; p->setSeqStep(i, st); }
        juce::AudioBuffer<float> buf(juce::jmax(p->getTotalNumInputChannels(), 2), 512);
        juce::MidiBuffer none;
        double energy = 0;
        for (int b = 0; b < int(sr * 2.0 / 512); ++b) { buf.clear(); p->processBlock(buf, none); energy += buf.getRMSLevel(0, 0, 512); }
        const auto notes = p->seqNoteOnCount.load();
        std::printf("       sequencer (internal 120 BPM, 1/16): %u note-ons in 2 s (expected 16)\n", notes);
        check(notes >= 15 && notes <= 17, "sequencer runs on its internal clock at the right rate");
       #if AMBIENT_IS_INSTRUMENT
        check(energy > 1.0, "sequencer notes play the synth");
       #endif
        check(p->seqCurrentStep.load() >= 0, "sequencer reports the current step");

        // pattern persistence
        auto st5 = p->getSeqStep(5); st5.note = 7; st5.ratchet = 3; st5.locks[0] = 0.25f; p->setSeqStep(5, st5);
        juce::MemoryBlock mb; p->getStateInformation(mb);
        std::unique_ptr<AmbientProcessor> q; makeProc(q);
        q->setStateInformation(mb.getData(), int(mb.getSize()));
        const auto r5 = q->getSeqStep(5);
        check(r5.note == 7 && r5.ratchet == 3 && std::abs(r5.locks[0] - 0.25f) < 1e-6f && std::isnan(q->getSeqStep(6).locks[0]),
              "sequencer pattern (notes, ratchets, locks) persists through the plugin state");
        check(q->apvts.getRawParameterValue("seq.on")->load() > 0.5f, "sequencer parameters persist");
    }

    // Macros + direct (right-click) modulation assignment: every macro moves its real DSP
    // parameter and changes the audio; multiple destinations; negative amounts; persistence.
    {
        namespace ma = ambient::ui::modassign;
        auto fresh = [&]()
        {
            auto p = std::make_unique<AmbientProcessor>();
            p->setBusesLayout(makeLayout(AMBIENT_IS_INSTRUMENT ? juce::AudioChannelSet::disabled() : juce::AudioChannelSet::stereo(),
                                         juce::AudioChannelSet::stereo()));
            p->setRateAndBufferSizeDetails(sr, 512);
            p->prepareToPlay(sr, 512);
           #if ! AMBIENT_IS_INSTRUMENT
            for (auto id : { "gr.on", "dl.on", "rv.on" }) setParam(*p, id, 1.0f);   // FX macros 5-8 act on the effects
           #endif
            return p;
        };
        auto renderWith = [&](AmbientProcessor& p, std::vector<float>& out)
        {
            const int ch = juce::jmax(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels(), 2);
            juce::AudioBuffer<float> buf(ch, 512);
            juce::MidiBuffer on, off, none;
            for (int nn : { 48, 55, 60 }) { on.addEvent(juce::MidiMessage::noteOn(1, nn, 0.8f), 0); off.addEvent(juce::MidiMessage::noteOff(1, nn), 0); }
            out.clear(); long t = 0;
            const int blocks = int(sr * 1.5 / 512), offBlock = int(sr * 1.0 / 512);
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
               #if ! AMBIENT_IS_INSTRUMENT
                for (int i = 0; i < 512; ++i)   // stereo source (width / bass-mono macros need a side signal)
                {
                    const double tt = double(t + i) / sr;
                    buf.setSample(0, i, float(0.25 * std::sin(2 * kPi * 110.0 * tt) + 0.1 * std::sin(2 * kPi * 660.0 * tt)));
                    buf.setSample(1, i, float(0.25 * std::sin(2 * kPi * 110.0 * tt + 1.2) + 0.1 * std::sin(2 * kPi * 440.0 * tt)));
                }
               #endif
                p.processBlock(buf, b == 0 ? on : (b == offBlock ? off : none));
                t += 512;
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) out.push_back(buf.getSample(c, i));
            }
        };
        bool allMove = true, allAudible = true;
        for (int m = 1; m <= 8; ++m)
        {
            const auto list = [&] { auto p = fresh(); return ma::forSource(p->apvts, ambient::modsrc::Macro1 + m - 1); }();
            if (list.empty()) { allMove = false; std::printf("       macro %d: NOT ASSIGNED\n", m); continue; }
            const auto destId = juce::String(std::string(ambient::moddest::kDestinations[size_t(list[0].destination - 1)]));
            auto a = fresh(), b = fresh();
            setParam(*b, "macro." + juce::String(m), 1.0f);     // host automation path (setValueNotifyingHost)
            std::vector<float> oa, ob;
            renderWith(*a, oa); renderWith(*b, ob);
            const float va = a->getModulatedValue(destId), vb = b->getModulatedValue(destId);
            double diff = 0, ref = 0; for (size_t i = 0; i < oa.size(); ++i) { diff += std::pow(double(oa[i] - ob[i]), 2); ref += double(oa[i]) * oa[i]; }
            const double diffDb = 10.0 * std::log10((diff + 1e-20) / (ref + 1e-20));
            std::printf("       macro %d -> %-22s %8.3f -> %8.3f   output change %6.1f dB\n", m, destId.toRawUTF8(), va, vb, diffDb);
            allMove = allMove && std::abs(vb - va) > 1e-4f;
            allAudible = allAudible && diffDb > -60.0;
        }
        check(allMove, "every macro (1-8) moves its assigned DSP parameter (via host automation of the macro)");
        check(allAudible, "every macro produces an audible output change (> -60 dB difference)");

        // multiple destinations + negative amount, through the right-click assignment helper
        {
            auto p = fresh();
            const int src = ambient::modsrc::Macro2;
            const int d1 = ma::destinationOf("flt.cutoff"), d2 = ma::destinationOf("rv.mix");
            const int s1 = ma::add(p->apvts, src, d1, -0.5f), s2 = ma::add(p->apvts, src, d2, 0.4f);
            const auto list = ma::forSource(p->apvts, src);
            check(s1 > 0 && s2 > 0 && list.size() >= 3, "a macro accepts several destinations (assigned through the right-click helper)");
            const float cut0 = [&] { std::vector<float> o; renderWith(*p, o); return p->getModulatedValue("flt.cutoff"); }();
            setParam(*p, "macro.2", 1.0f);
            std::vector<float> o; renderWith(*p, o);
            const float cut1 = p->getModulatedValue("flt.cutoff"), mix1 = p->getModulatedValue("rv.mix");
            std::printf("       macro 2 (3 targets): cutoff %.0f -> %.0f Hz (amount -50 %%), reverb mix -> %.2f\n", cut0, cut1, mix1);
            check(cut1 < cut0 && mix1 > p->apvts.getRawParameterValue("rv.mix")->load(), "negative and positive amounts move their targets in opposite directions");
            check(juce::roundToInt(p->apvts.getRawParameterValue("mm." + juce::String(s1) + ".dst")->load()) == d1,
                  "right-click assignments are ordinary matrix slots (the Modulation tab shows them)");

            // persistence
            juce::MemoryBlock mb; p->getStateInformation(mb);
            auto q = fresh(); q->setStateInformation(mb.getData(), int(mb.getSize()));
            std::vector<float> o2; renderWith(*q, o2);
            check(ma::forSource(q->apvts, src).size() == list.size() && std::abs(q->getModulatedValue("flt.cutoff") - cut1) < 1.0f,
                  "macro assignments, amounts and macro values survive save / reload");

            // edit + remove
            ma::setAmount(p->apvts, s1, 0.25f);
            check(std::abs(p->apvts.getRawParameterValue("mm." + juce::String(s1) + ".amt")->load() - 0.25f) < 1e-4f, "amount edit writes the matrix slot");
            ma::remove(p->apvts, s1);
            check(ma::forSource(p->apvts, src).size() == list.size() - 1, "remove clears the matrix slot");

            // full matrix reports 0 instead of overwriting
            auto f = fresh();
            int filled = 0; for (int k = 0; k < 40; ++k) if (ma::add(f->apvts, ambient::modsrc::Lfo1 + (k % 4), 1 + k, 0.1f) > 0) ++filled;
            check(filled == 24, "with 8 default macro slots, 24 free slots can be filled; a full matrix refuses further assignments");
        }
    }

    // Master default -12 dB + High Quality mode (Instrument).
    {
        auto fresh = [&]()
        {
            auto p = std::make_unique<AmbientProcessor>();
            p->setBusesLayout(makeLayout(AMBIENT_IS_INSTRUMENT ? juce::AudioChannelSet::disabled() : juce::AudioChannelSet::stereo(),
                                         juce::AudioChannelSet::stereo()));
            p->setRateAndBufferSizeDetails(sr, 512);
            p->prepareToPlay(sr, 512);
            return p;
        };
        {
            auto p = fresh();
            const float def = p->apvts.getRawParameterValue("global.masterGain")->load();
            std::printf("       master default: %.2f dB (\"%s\")\n", def, p->apvts.getParameter("global.masterGain")->getCurrentValueAsText().toRawUTF8());
            check(std::abs(def + 12.0f) < 1e-3f, "master output defaults to -12 dB");
            // the master is the last gain stage: output at -12 dB is the 0 dB output x 0.251
            auto q = fresh(); setParam(*q, "global.masterGain", 0.0f);
            const int ch = juce::jmax(p->getTotalNumInputChannels(), p->getTotalNumOutputChannels(), 2);
            juce::AudioBuffer<float> a(ch, 512), b(ch, 512); juce::MidiBuffer on, none; on.addEvent(juce::MidiMessage::noteOn(1, 45, 0.5f), 0);
            double ea = 0, eb = 0; long t = 0;
            for (int blk = 0; blk < 120; ++blk)
            {
                a.clear(); b.clear();
               #if ! AMBIENT_IS_INSTRUMENT
                for (int i = 0; i < 512; ++i) { const float x = 0.2f * float(std::sin(2 * kPi * 220.0 * double(t + i) / sr)); for (auto* bb : { &a, &b }) { bb->setSample(0, i, x); bb->setSample(1, i, x); } }
               #endif
                p->processBlock(a, blk == 0 ? on : none); q->processBlock(b, blk == 0 ? on : none); t += 512;
                if (blk > 40) { ea += a.getRMSLevel(0, 0, 512); eb += b.getRMSLevel(0, 0, 512); }
            }
            const double diffDb = 20.0 * std::log10(ea / eb);
            std::printf("       output -12 dB vs 0 dB master: %.2f dB\n", diffDb);
            check(std::abs(diffDb + 12.0) < 0.3, "master gain is an exact final-stage gain (-12 dB = 12 dB quieter, nothing compensates)");
            setParam(*q, "global.masterGain", -7.5f);
            juce::MemoryBlock mb; q->getStateInformation(mb);
            auto r = fresh(); r->setStateInformation(mb.getData(), int(mb.getSize()));
            check(std::abs(r->apvts.getRawParameterValue("global.masterGain")->load() + 7.5f) < 0.02f, "a user master level persists through the state");
            r->setCurrentProgram(2);
            check(std::abs(r->apvts.getRawParameterValue("global.masterGain")->load() + 7.5f) < 0.02f, "loading a factory preset keeps the user's master level");
        }
       #if AMBIENT_IS_INSTRUMENT
        {
            // CPU: default patch and worst case, Standard vs HQ
            auto cpuOf = [&](bool hq, bool worst)
            {
                auto p = fresh();
                setParam(*p, "global.quality", hq ? 1.0f : 0.0f);
                p->prepareToPlay(sr, 512);
                if (worst) { setParam(*p, "osc.a.unison", 7.0f); setParam(*p, "osc.b.on", 1.0f); setParam(*p, "osc.b.unison", 7.0f); setParam(*p, "osc.sat.drive", 12.0f); }
                juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer on, none;
                for (int nn = 0; nn < 8; ++nn) on.addEvent(juce::MidiMessage::noteOn(1, 48 + nn * 3, 0.8f), 0);
                const int blocks = int(sr * 3.0 / 512);
                double t0 = 0;
                for (int b = 0; b < blocks; ++b) { if (b == blocks / 4) t0 = juce::Time::getMillisecondCounterHiRes(); buf.clear(); p->processBlock(buf, b == 0 ? on : none); }
                return 100.0 * (juce::Time::getMillisecondCounterHiRes() - t0) / ((blocks - blocks / 4) * 512 * 1000.0 / sr);
            };
            const double s0 = cpuOf(false, false), h0 = cpuOf(true, false), s1 = cpuOf(false, true), h1 = cpuOf(true, true);
            std::printf("       CPU 8 voices, default patch: Standard %.2f %%, HQ %.2f %%;  worst case (A+B unison 7): Standard %.2f %%, HQ %.2f %%\n", s0, h0, s1, h1);
            check(h1 < 60.0, "HQ worst case stays usable (< 60 % of one core)");

            // switching modes during a held note: no click, voices keep sounding, same level
            auto p = fresh();
            setParam(*p, "osc.a.wave", 0.0f);   // sine: any discontinuity would show as a large sample step
            setParam(*p, "osc.a.unison", 1.0f);
            setParam(*p, "osc.drift", 0.0f);      // drift is a random walk stepped per control block: its path differs per rate
            juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer on, none; on.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 0);
            std::vector<float> outL; std::vector<float> rms;
            for (int b = 0; b < 300; ++b)
            {
                if (b == 100) setParam(*p, "global.quality", 1.0f);
                if (b == 200) setParam(*p, "global.quality", 0.0f);
                buf.clear(); p->processBlock(buf, b == 0 ? on : none);
                for (int i = 0; i < 512; ++i) outL.push_back(buf.getSample(0, i));
                rms.push_back(buf.getRMSLevel(0, 0, 512));
                if (b == 150) check(p->isHighQualityActive(), "HQ becomes active after the switch");
            }
            float maxStep = 0; for (size_t i = 20000; i < outL.size(); ++i) maxStep = std::max(maxStep, std::abs(outL[i] - outL[i - 1]));
            const float sineStep = float(2 * kPi * 220.0 / sr) * 0.6f;   // ~ max step of the steady note
            const double lvl = 20 * std::log10(rms[150] / rms[90]);
            std::printf("       mode switch during a held note: max sample step %.4f (steady ~%.4f), HQ vs Standard level %.2f dB\n", maxStep, sineStep, lvl);
            check(maxStep < 3.0f * sineStep + 0.01f, "switching Standard <-> HQ mid-note does not click");
            // level: an always-Standard instance vs this one at the same moment, while it runs in HQ
            auto ref = fresh();
            setParam(*ref, "osc.a.wave", 0.0f); setParam(*ref, "osc.a.unison", 1.0f); setParam(*ref, "osc.drift", 0.0f);
            std::vector<float> refRms;
            for (int b = 0; b < 300; ++b) { buf.clear(); ref->processBlock(buf, b == 0 ? on : none); refRms.push_back(buf.getRMSLevel(0, 0, 512)); }
            const double hqVsStd = 20 * std::log10(rms[150] / refRms[150]);
            std::printf("       same moment, HQ instance vs Standard instance: %.3f dB (block-to-block drift of the note itself: %.2f dB)\n", hqVsStd, lvl);
            check(rms[290] > 0.5f * refRms[290] && std::abs(hqVsStd) < 0.25, "the note keeps sounding at the same level through both switches");
            check(p->getLatencySamples() == 0, "HQ adds no reported latency");
        }
       #endif
    }

    // Sequencer Random mode + sequencer LFO through the plugin.
    {
        auto fresh = [&]()
        {
            auto p = std::make_unique<AmbientProcessor>();
            p->setBusesLayout(makeLayout(AMBIENT_IS_INSTRUMENT ? juce::AudioChannelSet::disabled() : juce::AudioChannelSet::stereo(),
                                         juce::AudioChannelSet::stereo()));
            p->setRateAndBufferSizeDetails(sr, 512);
            p->prepareToPlay(sr, 512);
            setParam(*p, "seq.on", 1.0f); setParam(*p, "seq.clock", 1.0f); setParam(*p, "seq.bpm", 120.0f); setParam(*p, "seq.division", 3.0f);
            for (int i = 0; i < 16; ++i) { auto st = p->getSeqStep(i); st.active = true; st.probability = 1; st.ratchet = 1; st.skip = false; p->setSeqStep(i, st); }
            return p;
        };
        auto play = [&](AmbientProcessor& p, double seconds, std::function<void(int)> at = nullptr)
        {
            juce::AudioBuffer<float> buf(juce::jmax(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels(), 2), 512);
            juce::MidiBuffer none;
            for (int b = 0; b < int(sr * seconds / 512); ++b) { if (at) at(b); buf.clear(); p.processBlock(buf, none); }
        };
        // generate while playing
        {
            auto p = fresh();
            setParam(*p, "seq.scale", 4.0f); setParam(*p, "seq.root", 2.0f);   // D dorian
            setParam(*p, "seq.rnd.lockSteps", 1.0f);   // keep all 16 steps active: the note count must not change
            uint32_t before = 0, after = 0;
            play(*p, 2.0);
            before = p->seqNoteOnCount.load();
            p->generateSeqPattern();
            play(*p, 2.0);
            after = p->seqNoteOnCount.load() - before;
            bool inScale = true;
            for (int i = 0; i < 16; ++i) inScale = inScale && ambient::dsp::scales::contains(48 + 2 + p->getSeqStep(i).note, 4, 2);
            std::printf("       generate while playing: %u notes in 2 s before, %u after (pattern swapped live)\n", before, after);
            check(after == before && inScale, "Generate swaps in a new in-scale pattern without stopping playback (same note count)");

            juce::MemoryBlock mb; setParam(*p, "seq.lfo.depth", 0.6f); setParam(*p, "seq.rnd.on", 1.0f); p->getStateInformation(mb);
            auto q = fresh(); q->setStateInformation(mb.getData(), int(mb.getSize()));
            bool same = true; for (int i = 0; i < 16; ++i) same = same && q->getSeqStep(i).note == p->getSeqStep(i).note && q->getSeqStep(i).active == p->getSeqStep(i).active;
            check(same && std::abs(q->apvts.getRawParameterValue("seq.lfo.depth")->load() - 0.6f) < 1e-3f && q->apvts.getRawParameterValue("seq.rnd.on")->load() > 0.5f
                  && std::abs(q->apvts.getRawParameterValue("seq.rnd.seed")->load() - p->apvts.getRawParameterValue("seq.rnd.seed")->load()) < 0.5f,
                  "generated pattern, random and sequencer-LFO settings persist");
        }
        // speed modulation through the plugin: square LFO over 2 bars -> x2 then x1/2
        {
            auto p = fresh();
            setParam(*p, "seq.lfo.dest", 1.0f); setParam(*p, "seq.lfo.wave", 4.0f); setParam(*p, "seq.lfo.depth", 1.0f);
            setParam(*p, "seq.lfo.sync", 1.0f); setParam(*p, "seq.lfo.division", 14.0f);
            play(*p, 8.0);
            const auto n = p->seqNoteOnCount.load();
            std::printf("       sequencer LFO -> speed (4 bars, 1/16): %u notes (constant speed: 64; x2 + x1/2 alternating: 80)\n", n);
            check(n >= 79 && n <= 81, "sequencer LFO speed modulation works in the plugin");
        }
    }

    // Stage 9: presets, routing, automation fuzz.
    {
        auto fresh = [&]()
        {
            auto p = std::make_unique<AmbientProcessor>();
            p->setBusesLayout(makeLayout(AMBIENT_IS_INSTRUMENT ? juce::AudioChannelSet::disabled() : juce::AudioChannelSet::stereo(),
                                         juce::AudioChannelSet::stereo()));
            p->setRateAndBufferSizeDetails(sr, 512);
            p->prepareToPlay(sr, 512);
            return p;
        };
        struct Stats { double rms = 0; float peak = 0; bool finite = true; double cpu = 0; };
        auto play = [&](AmbientProcessor& p, double seconds, std::function<void(int)> perBlock = nullptr)
        {
            Stats st;
            const int ch = juce::jmax(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels(), 2);
            juce::AudioBuffer<float> buf(ch, 512);
            juce::MidiBuffer on, none;
            for (int nn : { 48, 55, 60, 64 }) on.addEvent(juce::MidiMessage::noteOn(1, nn, 0.8f), 0);
            const int blocks = int(sr * seconds / 512);
            double acc = 0; long cnt = 0; long t = 0;
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            for (int b = 0; b < blocks; ++b)
            {
                if (perBlock) perBlock(b);
                buf.clear();
               #if ! AMBIENT_IS_INSTRUMENT
                for (int i = 0; i < 512; ++i) { double x = 0; for (double f : { 110.0, 164.8, 220.5 }) x += 2.0 * std::fmod(f * double(t + i) / sr, 1.0) - 1.0;
                                                buf.setSample(0, i, float(0.1 * x)); buf.setSample(1, i, float(0.1 * x)); }
               #endif
                p.processBlock(buf, b == 0 ? on : none);
                t += 512;
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i)
                {
                    const float v = buf.getSample(c, i);
                    if (! std::isfinite(v)) st.finite = false;
                    st.peak = std::max(st.peak, std::abs(v)); acc += double(v) * v; ++cnt;
                }
            }
            st.cpu = 100.0 * (juce::Time::getMillisecondCounterHiRes() - t0) / (seconds * 1000.0);
            st.rms = std::sqrt(acc / double(juce::jmax(1L, cnt)));
            return st;
        };

        // factory presets
        bool presetsOk = true;
        std::printf("       factory presets (%d): rms / peak / cpu\n", ambient::presets::numFactory());
        for (int i = 0; i < ambient::presets::numFactory(); ++i)
        {
            auto p = fresh();
            p->setCurrentProgram(i);   // message thread here -> applied immediately
            setParam(*p, "seq.clock", 1.0f);   // sequenced presets: internal clock (no host transport here)
            const auto st = play(*p, 3.0);
            std::printf("         %-20s %6.3f  %5.3f  %5.2f %%  %s\n", ambient::presets::factoryName(i).toRawUTF8(), st.rms, st.peak, st.cpu, p->getPresetName().toRawUTF8());
            presetsOk = presetsOk && st.finite && st.rms > 0.0008 &&   /* -12 dB master default */ st.peak <= 1.0f && p->getPresetName() == ambient::presets::factoryName(i);
        }
        check(presetsOk, "every factory preset loads, plays, stays finite and below full scale");

        // user preset round trip
        {
            auto p = fresh();
            p->setCurrentProgram(3);
            setParam(*p, "rv.decay", 11.0f);
            auto st5 = p->getSeqStep(4); st5.note = -5; p->setSeqStep(4, st5);
            const bool saved = ambient::presets::saveUser(*p, "zz SelfTest Preset");
            auto q = fresh();
            const auto file = ambient::presets::userDirectory().getChildFile(juce::String("zz SelfTest Preset") + ambient::presets::kExtension);
            const bool loaded = ambient::presets::loadUser(*q, file);
            const float decay = q->apvts.getRawParameterValue("rv.decay")->load();
            check(saved && loaded && std::abs(decay - 11.0f) < 0.05f && q->getSeqStep(4).note == -5 && q->getPresetName() == "zz SelfTest Preset",
                  "user preset save -> load restores parameters, sequencer pattern and name");
            check(ambient::presets::deleteUser(file) && ! file.existsAsFile(), "user preset delete");
        }

        // routing: one active module -> parallel == serial; all orders + max loop feedback bounded
        {
            auto render = [&](float mode, float order, float fb, std::vector<float>& out)
            {
                auto p = fresh();
                setParam(*p, "rv.on", 1.0f); setParam(*p, "rv.mix", 0.5f);
                setParam(*p, "route.mode", mode); setParam(*p, "route.order", order); setParam(*p, "route.feedback", fb);
                out.clear();
                const int ch = juce::jmax(p->getTotalNumInputChannels(), p->getTotalNumOutputChannels(), 2);
                juce::AudioBuffer<float> buf(ch, 512); juce::MidiBuffer on, none; on.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 0);
                long t = 0;
                for (int b = 0; b < int(sr * 2.0 / 512); ++b)
                {
                    buf.clear();
                   #if ! AMBIENT_IS_INSTRUMENT
                    for (int i = 0; i < 512; ++i) { const float x = 0.3f * float(std::sin(2 * kPi * 220.0 * double(t + i) / sr)); buf.setSample(0, i, x); buf.setSample(1, i, x); }
                   #endif
                    p->processBlock(buf, b == 0 ? on : none); t += 512;
                    for (int i = 0; i < 512; ++i) out.push_back(buf.getSample(0, i));
                }
            };
            std::vector<float> a, b;
            render(0.0f, 0.0f, 0.0f, a); render(1.0f, 0.0f, 0.0f, b);
            float diff = 0; for (size_t i = 0; i < a.size(); ++i) diff = std::max(diff, std::abs(a[i] - b[i]));
            std::printf("       parallel vs serial with one active module: max diff %.2e\n", double(diff));
            check(diff < 1.0e-4f, "parallel routing with one active module equals serial");
            bool bounded = true;
            for (int o = 0; o < 6; ++o)
            {
                render(0.0f, float(o), 0.9f, a);
                double late = 0; for (size_t i = a.size() / 2; i < a.size(); ++i) { if (! std::isfinite(a[i])) bounded = false; late += double(a[i]) * a[i]; }
                late = std::sqrt(late / double(a.size() / 2));
                bounded = bounded && late < 0.7;
            }
            render(1.0f, 0.0f, 0.9f, a);
            for (float v : a) bounded = bounded && std::isfinite(v);
            check(bounded, "all 6 serial orders and parallel stay bounded at 90 % loop feedback");
        }

        // automation fuzz: every parameter randomised every 4 blocks while playing
        {
            auto p = fresh();
            juce::Random rng(77);
            auto params = p->getParameters();
            const auto st = play(*p, 6.0, [&](int b)
            {
                if (b % 4 != 0) return;
                for (auto* prm : params)
                    if (rng.nextFloat() < 0.15f) prm->setValueNotifyingHost(rng.nextFloat());
                setParam(*p, "global.masterGain", -12.0f);
            });
            std::printf("       automation fuzz (%d params, 6 s): peak %.3f, cpu %.2f %%\n", params.size(), st.peak, st.cpu);
            check(st.finite && st.peak <= 1.0f, "random automation of all parameters while playing stays finite");
        }
    }

    // Stereo: whole plugin output with each reverb algorithm, before (wide reverb, no safety)
    // and after (defaults). Source: chord (instrument) or mono pad on the track input (FX).
    {
        auto render = [&](int algo, bool safeDefaults)
        {
            AmbientProcessor p;
            p.setBusesLayout(makeLayout(AMBIENT_IS_INSTRUMENT ? juce::AudioChannelSet::disabled() : juce::AudioChannelSet::stereo(),
                                        juce::AudioChannelSet::stereo()));
            p.setRateAndBufferSizeDetails(sr, 512);
            p.prepareToPlay(sr, 512);
            setParam(p, "rv.on", 1.0f); setParam(p, "rv.algo", float(algo)); setParam(p, "rv.mix", 0.5f);
            if (! safeDefaults) { setParam(p, "rv.width", 1.0f); setParam(p, "out.safety", 0.0f); setParam(p, "out.bassMono", 0.0f); }
            const int ch = juce::jmax(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels(), 2);
            juce::AudioBuffer<float> buf(ch, 512);
            juce::MidiBuffer on, off, none;
            for (int nn : { 48, 55, 60, 64 }) on.addEvent(juce::MidiMessage::noteOn(1, nn, 0.8f), 0);
            for (int nn : { 48, 55, 60, 64 }) off.addEvent(juce::MidiMessage::noteOff(1, nn), 0);
            const int blocks = int(sr * 6.0 / 512), offBlock = int(sr * 2.0 / 512);
            std::vector<float> L, R; long t = 0;
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
               #if ! AMBIENT_IS_INSTRUMENT
                if (b < offBlock)
                    for (int i = 0; i < 512; ++i)
                    {
                        double x = 0; for (double f : { 110.0, 164.8, 220.5, 277.2 }) x += 2.0 * std::fmod(f * double(t + i) / sr, 1.0) - 1.0;
                        buf.setSample(0, i, float(0.1 * x)); buf.setSample(1, i, float(0.1 * x));
                    }
               #endif
                p.processBlock(buf, b == 0 ? on : (b == offBlock ? off : none));
                t += 512;
                for (int i = 0; i < 512; ++i) { L.push_back(buf.getSample(0, i)); R.push_back(buf.getSample(1, i)); }
            }
            return stereo_test::measure(L, R, sr, size_t(sr * 0.5));
        };
        const char* names[] = { "Hall", "Room", "Cathedral", "Plate", "Spring", "Shimmer", "Infinite" };
        std::printf("       output stereo (reverb mix 50 %%): before = width 100 %%, no safety, no bass mono | after = defaults\n");
        bool ok = true;
        for (int a = 0; a < 7; ++a)
        {
            const auto b = render(a, false), d = render(a, true);
            std::printf("         %-10s corr %+5.2f -> %+5.2f   low %+5.2f -> %+5.2f   mono-sum %+5.2f -> %+5.2f dB   neg windows %3.0f%% -> %3.0f%%\n",
                        names[a], b.corr, d.corr, b.lowCorr, d.lowCorr, b.monoLossDb, d.monoLossDb, 100 * b.negFraction, 100 * d.negFraction);
            ok = ok && d.corr > 0.3 && d.lowCorr > 0.8 && d.negFraction < 0.05 && d.monoLossDb > -1.5;
        }
        check(ok, "output stereo with defaults: correlation > 0.3, bass > 0.8, < 5 % negative windows, mono loss < 1.5 dB");
    }

    // State: v1 session (no sc.* params) loads, migrates to v2, keeps known values.
    {
        AmbientProcessor p;
        juce::ValueTree v1("AmbientLab");
        v1.setProperty("stateVersion", 1, nullptr);
        juce::ValueTree prm("PARAM"); prm.setProperty("id", "global.masterGain", nullptr); prm.setProperty("value", -6.0f, nullptr);
        v1.addChild(prm, -1, nullptr);
        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary(*v1.createXml(), mb);
        p.setStateInformation(mb.getData(), int(mb.getSize()));
        const float mg = p.apvts.getRawParameterValue("global.masterGain")->load();
        check(std::abs(mg + 6.0f) < 1e-3f, "v1 state: existing parameter value restored");
        check(p.apvts.getRawParameterValue("sc.depth")->load() == 0.7f, "v1 state: new sidechain parameters take defaults");
        juce::MemoryBlock out; p.getStateInformation(out);
        auto xml = juce::AudioProcessor::getXmlFromBinary(out.getData(), int(out.getSize()));
        check(xml != nullptr && xml->getIntAttribute("stateVersion") == 2, "state saved as version 2");
    }

    std::printf("%s (%d failure%s)\n", failures == 0 ? "SELFTEST PASSED" : "SELFTEST FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

// Mascot proposal sheet: current vs evolved design, sizes and expression states.
static int renderMascotSheet(const juce::File& out, float scale)
{
    using namespace ambient::ui;
    const int W = 1600, H = 1100;
    juce::Image img(juce::Image::RGB, int(W * scale), int(H * scale), true);
    juce::Graphics g(img);
    g.addTransform(juce::AffineTransform::scale(scale));
    g.fillAll(theme::bg);

    auto title = [&](const juce::String& t, float x, float y)
    {
        g.setColour(theme::text); g.setFont(theme::heading(18.0f, false, 0.08f));
        g.drawText(t, juce::Rectangle<float>(x, y, 700.0f, 24.0f), juce::Justification::centredLeft);
    };
    auto label = [&](const juce::String& t, juce::Rectangle<float> r)
    {
        g.setColour(theme::textDim); g.setFont(theme::body(13.0f));
        g.drawFittedText(t, r.toNearestInt(), juce::Justification::centredTop, 2);
    };
    auto panel = [&](juce::Rectangle<float> r)
    {
        g.setColour(theme::panel); g.fillRoundedRectangle(r, 6.0f);
        g.setColour(theme::edge); g.drawRoundedRectangle(r, 6.0f, 1.0f);
    };

    // Hero
    panel({ 30, 30, 520, 1040 });
    title("GLIM v2 - PROPOSAL", 50, 44);
    MascotPose hero; hero.glow = 0.6f; hero.orb = 0.7f; hero.aura = 0.15f;
    MascotArt::draw(g, { 60, 90, 460, 736 }, hero);
    label("Sidechain active - lime details lit", { 50, 840, 480, 40 });
    g.setColour(theme::textDim); g.setFont(theme::body(13.0f));
    g.drawFittedText("Kept: purple pointed hat, glowing lime tip, pointed alien ears, dark face, purple/lime palette.\n"
                     "New: crinkled hat with brim, band stitch and sparse embroidery; slit-pupil eyes with heavy lids;\n"
                     "third-eye mark; bioluminescent cheek spots; wispy forest-spirit beard; robe with glowing trim;\n"
                     "long three-fingered hands holding a patch cable (lives in the synth lab).",
                     juce::Rectangle<int>(50, 880, 480, 120), juce::Justification::topLeft, 6);

    // Before / after
    panel({ 580, 30, 990, 330 });
    title("BEFORE  /  AFTER", 600, 44);
    label("(v1 retired)", { 620, 322, 200, 20 });
    MascotArt::draw(g, { 870, 80, 150, 240 }, hero);
    label("proposed (v2)", { 845, 322, 200, 20 });

    // Sizes
    title("SIZES (same drawing, level of detail drops when small)", 1090, 44);
    float x = 1100;
    for (float h : { 160.0f, 96.0f, 64.0f, 40.0f, 24.0f })
    {
        const float w = h * MascotArt::kAspect;
        g.setColour(theme::bgRaised); g.fillRoundedRectangle(juce::Rectangle<float>(x - 4, 300 - h - 4, w + 8, h + 8), 4.0f);
        MascotArt::draw(g, { x, 300 - h, w, h }, hero);
        label(juce::String(int(h)) + " px", { x - 20, 310, w + 40, 20 });
        x += w + 26;
    }

    // Expressions
    panel({ 580, 380, 990, 690 });
    title("STATES - all driven by detector data", 600, 394);
    struct S { const char* name; const char* desc; MascotPose p; };
    MascotPose idle;  idle.lids = 0.42f; idle.orb = 0.35f;
    MascotPose blink = idle; blink.eyeOpen = 0.1f;
    MascotPose kick;  kick.lids = 0.25f; kick.pupil = 1.0f; kick.orb = 0.9f; kick.glow = 0.5f;
    MascotPose strong; strong.surprise = 1.0f; strong.pupil = 0.2f; strong.orb = 1.0f; strong.glow = 0.7f; strong.hatTilt = -0.18f; strong.ears = 0.6f;
    MascotPose active; active.lids = 0.3f; active.glow = 1.0f; active.orb = 0.6f; active.aura = 0.25f;
    MascotPose gr;    gr.lids = 0.15f; gr.glow = 1.0f; gr.orb = 0.8f; gr.aura = 0.85f; gr.ears = 1.0f;
    MascotPose none;  none.lids = 0.62f; none.orb = 0.2f;
    const S states[] = {
        { "Idle", "breathing, periodic blink", idle },
        { "Idle - blink", "every ~5 s (deterministic)", blink },
        { "Kick", "pupils pulse, tip flashes", kick },
        { "Strong kick", "surprised: wide eyes, 'o' mouth, hat jolts", strong },
        { "Sidechain active", "lime details illuminate", active },
        { "High reduction", "aura + ears perk with GR", gr },
        { "No signal", "relaxed, heavy lids", none },
    };
    for (int i = 0; i < 7; ++i)
    {
        const float cx = 600.0f + float(i % 4) * 240.0f, cy = 440.0f + float(i / 4) * 310.0f;
        MascotArt::draw(g, { cx + 40, cy, 150, 240 }, states[i].p);
        g.setColour(theme::text); g.setFont(theme::heading(14.0f, false, 0.06f));
        g.drawText(juce::String(states[i].name).toUpperCase(), juce::Rectangle<float>(cx, cy + 244, 230, 18), juce::Justification::centred);
        label(states[i].desc, { cx, cy + 264, 230, 36 });
    }

    // In context: header strip at 1x
    g.setColour(theme::bgRaised); g.fillRoundedRectangle(juce::Rectangle<float>(1560 - 230, 760 + 220, 220, 70), 4.0f);
    MascotArt::draw(g, { 1560 - 230 + 80, 760 + 222, 66 * MascotArt::kAspect * 1.0f + 20, 66 }, active);
    label("header size, in context", { 1560 - 230, 760 + 292, 220, 20 });

    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat().writeImageToStream(img, os);
    std::printf("wrote %s\n", out.getFullPathName().toRawUTF8());
    return 0;
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 1 && juce::String(argv[1]) == "--selftest") return runSelfTest();
    if (argc > 1 && juce::String(argv[1]) == "--mascot-states")
    {
        // Renders the live Mascot driven through its real tick() inputs (no hand-made poses).
        using ambient::ui::Mascot;
        juce::Image img(juce::Image::ARGB, 1400, 300, true);
        juce::Graphics g(img);
        g.fillAll(juce::Colour(0xff0e0e12));
        struct Case { const char* label; std::function<void(Mascot&)> drive; };
        auto run = [](Mascot& m, float secs, Mascot::Inputs in, std::function<void(Mascot::Inputs&, int)> per = nullptr)
        { for (int f = 0; f < int(secs * 60); ++f) { if (per) per(in, f); m.tick(1.0f / 60.0f, in); } };
        std::vector<Case> cases {
            { "IDLE / NO SIGNAL", [&](Mascot& m) { run(m, 2.0f, {}); } },
            { "AUDIO PLAYING", [&](Mascot& m) { Mascot::Inputs in; in.outputLevel = 0.5f; run(m, 2.0f, in); } },
            { "SIDECHAIN ACTIVE", [&](Mascot& m) { Mascot::Inputs in; in.sidechainActive = true; in.outputLevel = 0.3f; run(m, 2.0f, in); } },
            { "KICK", [&](Mascot& m) { Mascot::Inputs in; in.sidechainActive = true; in.outputLevel = 0.3f; run(m, 1.0f, in);
                                      in.transientCount = 1; in.transientStrength = 0.5f; in.reductionDb = 4; run(m, 0.05f, in); } },
            { "STRONG KICK", [&](Mascot& m) { Mascot::Inputs in; in.sidechainActive = true; in.outputLevel = 0.5f; run(m, 1.0f, in);
                                      in.transientCount = 1; in.transientStrength = 1.0f; in.reductionDb = 9; run(m, 0.05f, in); } },
            { "HIGH REDUCTION", [&](Mascot& m) { Mascot::Inputs in; in.sidechainActive = true; in.outputLevel = 0.4f; in.reductionDb = 14; run(m, 1.5f, in); } },
            { "LFO SWAY", [&](Mascot& m) { Mascot::Inputs in; in.outputLevel = 0.3f; in.lfo = 1.0f; run(m, 1.5f, in); } },
        };
        int x = 20;
        for (auto& c : cases)
        {
            Mascot m; m.setBounds(0, 0, 150, 240);
            c.drive(m);
            ambient::ui::MascotArt::draw(g, juce::Rectangle<float>(float(x), 20.0f, 150.0f, 240.0f), m.getPose());
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.setFont(12.0f);
            g.drawText(c.label, x - 20, 268, 190, 20, juce::Justification::centred);
            x += 195;
        }
        auto out = juce::File::getCurrentWorkingDirectory().getChildFile(argc > 2 ? argv[2] : "mascot_states.png");
        out.deleteFile();
        juce::FileOutputStream os(out);
        juce::PNGImageFormat().writeImageToStream(img, os);
        std::printf("wrote %s\n", out.getFullPathName().toRawUTF8());
        return 0;
    }
    if (argc > 1 && juce::String(argv[1]) == "--list-params")
    {
        AmbientProcessor p;
        for (auto* prm : p.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(prm))
                std::printf("%s\t%s\t%s\n", r->getParameterID().toRawUTF8(),
                            dynamic_cast<juce::AudioParameterFloat*>(r) ? "float" : (dynamic_cast<juce::AudioParameterChoice*>(r) ? "choice" : "bool"),
                            r->getName(64).toRawUTF8());
        return 0;
    }
    if (argc > 2 && juce::String(argv[1]) == "--mascot-sheet")
        return renderMascotSheet(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]), argc > 3 ? float(std::atof(argv[3])) : 1.0f);
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
        { "process_granular", 1, 2, false, 1280, 800, 1, { { "processModule", 1 } } },
        { "process_reverb", 1, 2, false, 1280, 800, 1, { { "processModule", 2 } } },
        { "process_stereo", 1, 2, false, 1280, 800, 1, { { "processModule", 4 } } },
        { "process_delay", 1, 2, false, 1280, 800, 1, { { "processModule", 3 } } },
        { "seq_random", 3, 2, false, 1280, 800, 1, { { "seqView", 1 } } },
        { "seq_lfo", 3, 2, false, 1280, 800, 1, { { "seqView", 2 } } },
        { "stereo_lr_docked", 1, 1, false, 1280, 800, 1, { { "vs_display", 1 } } },
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
        for (int note : { 33, 57, 64, 69 })
            chord.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
        juce::AudioBuffer<float> tmp(juce::jmax(proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels(), 2), block);
        tmp.clear();
        proc.processBlock(tmp, chord);

        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        ed->setSize(s.width, s.height);
        ed->setOpaque(true);
        ed->addToDesktop(juce::ComponentPeer::windowIsTemporary); // real on-screen painting (Xvfb)
        ed->setVisible(true);
        for (const auto& [k, v] : s.extra)       // sequencer view (MAIN / RANDOM / SEQ LFO)
            if (k == "seqView")
            {
                std::function<void(juce::Component*)> find = [&](juce::Component* c)
                {
                    if (auto* sp = dynamic_cast<ambient::ui::SequencerPanel*>(c)) sp->setView(int(v));
                    for (auto* ch : c->getChildren()) find(ch);
                };
                find(ed.get());
            }

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
