#include "Presets.h"
#include "PluginProcessor.h"
#include "ModDestinations.h"

namespace ambient::presets
{
namespace
{
struct Mod { int source; const char* dest; float amount; };
struct Factory
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;   // plain values
    std::vector<Mod> mods;
    int seqPreset = -1;
};

const std::vector<Factory>& factory()
{
    using namespace ambient::modsrc;
    static const std::vector<Factory> list {
        { "Init", {}, {} },
        { "Glass Shimmer Pad",
          { { "osc.a.wave", 2 }, { "osc.a.unison", 5 }, { "osc.a.detune", 18 }, { "osc.b.wave", 1 }, { "osc.b.semi", 12 }, { "osc.b.level", 0.35f },
            { "flt.cutoff", 2400 }, { "flt.reso", 0.2f }, { "env.amp.a", 900 }, { "env.amp.r", 2500 },
            { "rv.on", 1 }, { "rv.algo", 5 }, { "rv.decay", 8 }, { "rv.mix", 0.45f }, { "rv.shimmer", 0.6f },
            { "dl.on", 1 }, { "dl.mode", 0 }, { "dl.mix", 0.2f }, { "lfo.1.rate", 0.15f } },
          { { Lfo1, "flt.cutoff", 0.15f } } },
        { "Frozen Cathedral",
          { { "osc.a.wave", 1 }, { "osc.a.unison", 3 }, { "osc.sub.on", 1 }, { "osc.sub.level", 0.3f }, { "flt.cutoff", 1600 },
            { "env.amp.a", 1500 }, { "env.amp.r", 4000 }, { "gr.on", 1 }, { "gr.mix", 0.35f }, { "gr.size", 180 }, { "gr.density", 18 },
            { "rv.on", 1 }, { "rv.algo", 2 }, { "rv.decay", 14 }, { "rv.mix", 0.6f } },
          { { Lfo2, "gr.position", 0.2f } } },
        { "Tape Echo Keys",
          { { "osc.a.wave", 3 }, { "osc.a.unison", 1 }, { "env.amp.a", 5 }, { "env.amp.d", 600 }, { "env.amp.s", 0.3f }, { "env.amp.r", 700 },
            { "flt.cutoff", 3200 }, { "flt.env", 0.4f }, { "dl.on", 1 }, { "dl.mode", 0 }, { "dl.sync", 1 }, { "dl.division", 7 },
            { "dl.feedback", 0.55f }, { "dl.mix", 0.35f }, { "dl.drive", 0.4f }, { "dl.modDepth", 0.5f },
            { "rv.on", 1 }, { "rv.algo", 0 }, { "rv.decay", 3 }, { "rv.mix", 0.25f } },
          { { Velocity, "flt.cutoff", 0.2f } } },
        { "Granular Cloud",
          { { "osc.a.wave", 2 }, { "osc.noise.on", 1 }, { "osc.noise.level", 0.1f }, { "env.amp.a", 600 }, { "env.amp.r", 2000 },
            { "gr.on", 1 }, { "gr.density", 40 }, { "gr.size", 120 }, { "gr.scatter", 0.6f }, { "gr.pitchRandom", 7 }, { "gr.spread", 0.9f },
            { "gr.mix", 0.6f }, { "gr.feedback", 0.3f }, { "rv.on", 1 }, { "rv.algo", 3 }, { "rv.mix", 0.3f } },
          { { Lfo1, "gr.density", 0.25f } } },
        { "Pulse Sequence",
          { { "seq.on", 1 }, { "seq.division", 6 }, { "osc.a.wave", 2 }, { "env.amp.a", 2 }, { "env.amp.d", 250 }, { "env.amp.s", 0 }, { "env.amp.r", 200 },
            { "flt.cutoff", 1800 }, { "flt.env", 0.5f }, { "dl.on", 1 }, { "dl.mode", 3 }, { "dl.sync", 1 }, { "dl.division", 7 }, { "dl.mix", 0.3f },
            { "rv.on", 1 }, { "rv.algo", 0 }, { "rv.mix", 0.25f } },
          { { Lfo1, "flt.cutoff", 0.2f } }, 0 },
        { "Pentatonic Drift",
          { { "seq.on", 1 }, { "seq.scale", 9 }, { "seq.direction", 3 }, { "seq.division", 6 }, { "osc.a.wave", 1 }, { "osc.b.on", 1 }, { "osc.b.wave", 0 },
            { "env.amp.a", 20 }, { "env.amp.d", 900 }, { "env.amp.s", 0.2f }, { "env.amp.r", 1200 },
            { "rv.on", 1 }, { "rv.algo", 5 }, { "rv.mix", 0.4f }, { "dl.on", 1 }, { "dl.mode", 2 }, { "dl.mix", 0.25f }, { "lfo.2.wave", 8 }, { "lfo.2.rate", 0.3f } },
          { { Lfo2, "flt.cutoff", 0.25f } }, 5 },
        { "Sidechain Pump Pad",
          { { "osc.a.wave", 2 }, { "osc.a.unison", 7 }, { "osc.a.detune", 22 }, { "env.amp.a", 400 }, { "env.amp.r", 1500 }, { "flt.cutoff", 3000 },
            { "sc.enable", 1 }, { "sc.depth", 0.9f }, { "rv.on", 1 }, { "rv.mix", 0.3f } },
          { { ScTransient, "flt.cutoff", -0.2f } } },
        { "BBD Chorus Bells",
          { { "osc.a.wave", 0 }, { "osc.fm", 0.3f }, { "osc.b.on", 1 }, { "osc.b.semi", 7 }, { "env.amp.a", 3 }, { "env.amp.d", 1500 }, { "env.amp.s", 0 }, { "env.amp.r", 1500 },
            { "dl.on", 1 }, { "dl.mode", 1 }, { "dl.sync", 0 }, { "dl.time", 14 }, { "dl.feedback", 0.1f }, { "dl.modDepth", 0.8f }, { "dl.modRate", 0.8f }, { "dl.mix", 0.4f },
            { "rv.on", 1 }, { "rv.algo", 3 }, { "rv.mix", 0.3f } },
          {} },
        { "Infinite Drone",
          { { "osc.a.wave", 2 }, { "osc.sub.on", 1 }, { "osc.sub.level", 0.4f }, { "flt.mode", 1 }, { "flt.cutoff", 900 }, { "env.amp.a", 2000 }, { "env.amp.r", 5000 },
            { "rv.on", 1 }, { "rv.algo", 6 }, { "rv.mix", 0.5f }, { "lfo.3.wave", 0 }, { "lfo.3.rate", 0.05f } },
          { { Lfo3, "flt.cutoff", 0.3f } } },
        { "Reverse Dreams",
          { { "osc.a.wave", 1 }, { "osc.a.unison", 3 }, { "env.amp.a", 300 }, { "env.amp.r", 1800 },
            { "dl.on", 1 }, { "dl.mode", 5 }, { "dl.sync", 1 }, { "dl.division", 11 }, { "dl.feedback", 0.4f }, { "dl.mix", 0.45f },
            { "rv.on", 1 }, { "rv.algo", 0 }, { "rv.decay", 5 }, { "rv.mix", 0.35f } },
          {} },
        { "Parallel Space",
          { { "osc.a.wave", 2 }, { "env.amp.a", 200 }, { "env.amp.r", 1500 }, { "route.mode", 1 },
            { "gr.on", 1 }, { "gr.mix", 0.5f }, { "dl.on", 1 }, { "dl.mode", 3 }, { "dl.mix", 0.4f }, { "rv.on", 1 }, { "rv.algo", 3 }, { "rv.mix", 0.4f } },
          {} },
    };
    return list;
}

void setPlain(AmbientProcessor& p, const juce::String& pid, float plain)
{
    if (auto* prm = p.apvts.getParameter(pid))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost(prm->convertTo0to1(plain));
        prm->endChangeGesture();
    }
    else jassertfalse;   // factory preset refers to an unknown parameter
}
} // namespace

int numFactory() { return int(factory().size()); }
juce::String factoryName(int i) { return juce::isPositiveAndBelow(i, numFactory()) ? factory()[size_t(i)].name : juce::String(); }

void applyFactory(AmbientProcessor& p, int index)
{
    if (! juce::isPositiveAndBelow(index, numFactory())) return;
    const auto& f = factory()[size_t(index)];
    for (auto* prm : p.getParameters())                       // start from defaults ...
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(prm))
            if (r->getParameterID() != "global.masterGain")   // ... except the user's monitoring level
                r->setValueNotifyingHost(r->getDefaultValue());
    for (const auto& [pid, v] : f.values) setPlain(p, pid, v);
    int slot = 1;
    for (const auto& m : f.mods)
    {
        int dest = 0;
        for (int k = 0; k < ambient::moddest::kNumDestinations; ++k)
            if (ambient::moddest::kDestinations[size_t(k)] == m.dest) { dest = k + 1; break; }
        jassert(dest > 0);
        const juce::String pre = "mm." + juce::String(slot++) + ".";
        setPlain(p, pre + "src", float(m.source));
        setPlain(p, pre + "dst", float(dest));
        setPlain(p, pre + "amt", m.amount);
    }
    p.loadSeqPreset(f.seqPreset >= 0 ? f.seqPreset : 0);
    for (const auto& [pid, v] : f.values)                      // explicit values win over the pattern's suggestions
        if (juce::String(pid).startsWith("seq.")) setPlain(p, pid, v);
    p.setPresetName(f.name);
}

juce::File userDirectory()
{
    auto d = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("AmbientLab").getChildFile("Presets");
    d.createDirectory();
    return d;
}

juce::Array<juce::File> userPresets()
{
    auto files = userDirectory().findChildFiles(juce::File::findFiles, false, juce::String("*") + kExtension);
    files.sort();
    return files;
}

bool saveUser(AmbientProcessor& p, const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName(rawName.trim());
    if (name.isEmpty()) return false;
    p.setPresetName(name);
    juce::MemoryBlock mb;
    p.getStateInformation(mb);
    return userDirectory().getChildFile(name + kExtension).replaceWithData(mb.getData(), mb.getSize());
}

bool loadUser(AmbientProcessor& p, const juce::File& f)
{
    juce::MemoryBlock mb;
    if (! f.loadFileAsData(mb) || mb.getSize() == 0) return false;
    p.setStateInformation(mb.getData(), int(mb.getSize()));
    p.setPresetName(f.getFileNameWithoutExtension());
    return true;
}

bool deleteUser(const juce::File& f) { return f.hasFileExtension(kExtension) && f.deleteFile(); }
} // namespace ambient::presets
