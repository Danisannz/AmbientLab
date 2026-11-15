// AmbientVst3HostTest <plugin.vst3> [instrument|fx]
// Hosts the built plugin exactly like a DAW (VST3 wrapper + JUCE VST3 host) and verifies
// the sidechain path in every layout a host may present.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;
void check(bool ok, const juce::String& what) { std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what.toRawUTF8()); if (! ok) ++failures; }

juce::AudioProcessorParameter* findParam(juce::AudioProcessor& p, const juce::String& name)
{
    for (auto* prm : p.getParameters()) if (prm->getName(128) == name) return prm;
    return nullptr;
}
void setParam(juce::AudioProcessor& p, const juce::String& name, float plain01)
{
    if (auto* prm = findParam(p, name)) { prm->beginChangeGesture(); prm->setValueNotifyingHost(plain01); prm->endChangeGesture(); }
    else check(false, "parameter not found: " + name);
}
float norm(juce::AudioProcessor& p, const juce::String& name, float plain)
{
    if (auto* prm = dynamic_cast<juce::RangedAudioParameter*>(findParam(p, name))) return prm->convertTo0to1(plain);
    // hosted params are not Ranged: fall back to text parsing
    if (auto* prm = findParam(p, name)) return prm->getValueForText(juce::String(plain));
    return 0.0f;
}

struct Result { double outRms = 0, outPeak = 0; };

int busIndex(juce::AudioProcessor& p, const juce::String& name)
{
    for (int i = 0; i < p.getBusCount(true); ++i) if (p.getBus(true, i)->getName() == name) return i;
    return -1;
}

/** Runs `seconds` of audio. Main input (if any) gets a 220 Hz + noise pad, sidechain gets
    50 ms 60 Hz kicks every 500 ms. Instrument: a held chord. Returns stats over the 2nd half. */
Result run(juce::AudioPluginInstance& p, bool instrument, bool feedMain, bool feedSc, double seconds = 3.0)
{
    const double sr = 48000.0; const int bs = 512;
    p.releaseResources();
    p.setRateAndBufferSizeDetails(sr, bs);
    p.prepareToPlay(sr, bs);
    const int ch = juce::jmax(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buf(ch, bs);
    juce::MidiBuffer chord, none;
    if (instrument) for (int nn : { 36, 48, 55, 60 }) chord.addEvent(juce::MidiMessage::noteOn(1, nn, 0.8f), 0);
    juce::Random rng(3);
    Result r; double acc = 0; long cnt = 0;
    const int blocks = int(seconds * sr / bs);
    long t = 0;
    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        const int mainIdx = busIndex(p, "Input"), scIdx = busIndex(p, "Sidechain");
        if (feedMain && mainIdx >= 0 && p.getBus(true, mainIdx)->isEnabled())
        {
            auto in = p.getBusBuffer(buf, true, mainIdx);
            for (int c = 0; c < in.getNumChannels(); ++c)
                for (int i = 0; i < bs; ++i)
                    in.setSample(c, i, 0.25f * float(std::sin(2 * juce::MathConstants<double>::pi * 55.0 * double(t + i) / sr)) + 0.1f * (rng.nextFloat() - 0.5f));
        }
        if (feedSc && scIdx >= 0 && p.getBus(true, scIdx)->isEnabled())
        {
            auto sc = p.getBusBuffer(buf, true, scIdx);
            for (int c = 0; c < sc.getNumChannels(); ++c)
                for (int i = 0; i < bs; ++i)
                {
                    // kick: 150 -> 50 Hz pitch sweep, 120 ms decay, every 500 ms
                    const long k = (t + i) % 24000;
                    const double tk = double(k) / sr;
                    const double ph = 2 * juce::MathConstants<double>::pi * (50.0 * tk + 100.0 * 0.03 * (1.0 - std::exp(-tk / 0.03)));
                    sc.setSample(c, i, 0.9f * float(std::exp(-tk / 0.12) * std::sin(ph)));
                }
        }
        p.processBlock(buf, (b == 0 && instrument) ? chord : none);
        t += bs;
        if (b >= blocks / 2)
            for (int c = 0; c < juce::jmin(2, p.getTotalNumOutputChannels()); ++c)
                for (int i = 0; i < bs; ++i) { const double v = buf.getSample(c, i); acc += v * v; ++cnt; r.outPeak = std::max(r.outPeak, std::abs(v)); }
    }
    r.outRms = std::sqrt(acc / double(juce::jmax(1L, cnt)));
    p.releaseResources(); // a DAW deactivates the plugin before any bus change
    return r;
}

juce::String describe(juce::AudioPluginInstance& p)
{
    juce::String s;
    for (bool in : { true, false })
        for (int i = 0; i < p.getBusCount(in); ++i)
        {
            auto* b = p.getBus(in, i);
            s << (in ? "in" : "out") << i << " '" << b->getName() << "' " << (b->isEnabled() ? "on " : "off ")
              << b->getCurrentLayout().getDescription() << " (" << b->getNumberOfChannels() << " ch)  ";
        }
    return s;
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3) { std::printf("usage: AmbientVst3HostTest <plugin.vst3> instrument|fx\n"); return 2; }
    const bool instrument = juce::String(argv[2]) == "instrument";
    juce::VST3PluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> types;
    fmt.findAllTypesForFile(types, juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]).getFullPathName());
    if (types.isEmpty()) { std::printf("FAIL: no VST3 found in %s\n", argv[1]); return 1; }
    juce::String err;
    auto p = fmt.createInstanceFromDescription(*types[0], 48000.0, 512, err);
    if (! p) { std::printf("FAIL: could not instantiate: %s\n", err.toRawUTF8()); return 1; }
    std::printf("loaded %s\n", p->getName().toRawUTF8());

    // ---- 1. What the host sees by default (no layout negotiation at all) ----
    std::printf("default layout: %s\n", describe(*p).toRawUTF8());
    const int sc = busIndex(*p, "Sidechain");
    check(sc >= 0, "plugin exposes a 'Sidechain' input bus");
    if (sc < 0) return 1;
    check(p->getBus(true, sc)->isEnabled(), "sidechain bus is active by default (hosts that never call activateBus still deliver audio)");
    if (instrument) check(p->getBusCount(true) == 1, "instrument: the sidechain is the only input bus");

    // What a DAW reads: VST3 bus type of every input bus (kMain / kAux).
    Steinberg::Vst::IComponent* comp = nullptr;
   #if JUCE_MAJOR_VERSION >= 9
    if (auto* v3 = p->getVST3Client()) comp = v3->getIComponentPtr();
   #else
    struct Visitor : juce::ExtensionsVisitor
    {
        Steinberg::Vst::IComponent* c = nullptr;
        void visitVST3Client(const VST3Client& v) override { c = static_cast<Steinberg::Vst::IComponent*>(v.getIComponentPtr()); }
    } visitor;
    p->getExtensions(visitor);
    comp = visitor.c;
   #endif
    check(comp != nullptr, "VST3 component interface reachable");
    if (comp != nullptr)
        {
            const auto count = comp->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput);
            for (Steinberg::int32 i = 0; i < count; ++i)
            {
                Steinberg::Vst::BusInfo info {};
                comp->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, i, info);
                const bool aux = info.busType == Steinberg::Vst::kAux;
                const bool defActive = (info.flags & Steinberg::Vst::BusInfo::kDefaultActive) != 0;
                std::printf("  VST3 input bus %d: %d ch, %s, %s\n", int(i), int(info.channelCount), aux ? "kAux" : "kMain", defActive ? "default active" : "default inactive");
                if (i == sc)
                {
                    check(aux, "sidechain is declared to the host as a VST3 aux bus");
                    check(defActive, "sidechain bus is flagged default-active to the host");
                }
                else check(! aux, "track input is declared as the VST3 main bus");
            }
        }

    setParam(*p, "Master Gain", norm(*p, "Master Gain", 0.0f));
    if (! instrument) setParam(*p, "Input Gain", norm(*p, "Input Gain", 0.0f));

    // ---- 1b. Lazy host: never negotiates or activates buses, just sends sidechain audio ----
    {
        setParam(*p, "SC Listen", 1.0f);
        const auto heard = run(*p, false, false, true);
        setParam(*p, "SC Listen", 0.0f);
        check(heard.outPeak > 0.05, "default layout: sidechain audio reaches the detector without any bus negotiation");
    }

    // ---- 2. Every sidechain layout a host can request ----
    for (auto scSet : { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono() })
    {
        auto layout = p->getBusesLayout();
        layout.inputBuses.getReference(sc) = scSet;
        const juce::String tag = "[" + scSet.getDescription() + " sidechain] ";
        p->releaseResources();
        const bool accepted = p->setBusesLayout(layout);
        check(accepted, tag + "layout accepted");
        if (! accepted) continue;
        std::printf("  layout: %s\n", describe(*p).toRawUTF8());

        // Listen: output = the detector's reference -> proves samples reach the detector.
        setParam(*p, "SC Listen", 1.0f);
        const auto heard = run(*p, false, false, true);
        setParam(*p, "SC Listen", 0.0f);
        std::printf("  listen: out RMS %.4f peak %.3f\n", heard.outRms, heard.outPeak);
        check(heard.outPeak > 0.05, tag + "sidechain samples reach the detector (SC Listen)");

        // No leak: sidechain only, no internal source -> output silent.
        const auto leak = run(*p, false, false, true);
        std::printf("  leak: out peak %.2e\n", leak.outPeak);
        check(leak.outPeak < 1.0e-4, tag + "sidechain never leaks into the output");

        // Ducking: internal source with vs without sidechain.
        const auto base = run(*p, instrument, ! instrument, false);
        const auto duck = run(*p, instrument, ! instrument, true);
        const double grDb = 20.0 * std::log10(duck.outRms / juce::jmax(1.0e-9, base.outRms));
        std::printf("  ducking: RMS %.4f -> %.4f (%.2f dB)\n", base.outRms, duck.outRms, grDb);
        check(base.outRms > 1.0e-3, tag + "internal source is audible");
        check(grDb < -0.5, tag + "sidechain reduces the internal signal");
    }

    // ---- 3. Host disables the sidechain: plugin keeps working, no crash ----
    {
        auto layout = p->getBusesLayout();
        {
            p->releaseResources();
            layout.inputBuses.getReference(sc) = juce::AudioChannelSet::disabled();
            check(p->setBusesLayout(layout), "sidechain bus can be disabled");
            const auto r = run(*p, instrument, ! instrument, true);
            check(r.outRms > 1.0e-3, "plugin works normally without a sidechain");
        }
    }
    p->releaseResources();
    p.reset();
    std::printf("%s (%d failure%s)\n", failures == 0 ? "VST3 HOST TEST PASSED" : "VST3 HOST TEST FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
