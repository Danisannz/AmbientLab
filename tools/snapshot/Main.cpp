#include "../../tests/StereoMetrics.h"
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

        // Listen: the reference is monitored on purpose.
        setParam(p, "sc.listen", 1.0f);
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
    Mascot::State old; old.orb = 0.6f; old.aura = 0.3f;
    Mascot::draw(g, { 620, 80, 200, 240 }, old);
    label("current (v1)", { 620, 322, 200, 20 });
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
