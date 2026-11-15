#include <ambient/dsp/Delay.h>
#include <ambient/dsp/Lfo.h>
#include "ModDestinations.h"
#include "Parameters.h"
#include <ambient/ParamIDs.h>

namespace ambient
{
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
using PID    = juce::ParameterID;

namespace
{
// Display formatting only - IDs, ranges and defaults are unchanged.
juce::String formatDb(float v, int)
{
    if (v <= -59.95f) return "-inf dB";
    return (v > 0.04f ? "+" : "") + juce::String(v, 1) + " dB";
}
float parseDb(const juce::String& t)
{
    return t.trim().startsWithIgnoreCase("-inf") ? -60.0f : t.getFloatValue();
}
juce::String formatPercent(float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; }
float parsePercent(const juce::String& t) { return t.getFloatValue() / 100.0f; }
juce::String formatMix(float v, int)
{
    const int in = juce::roundToInt(v * 100.0f);
    if (in == 0) return "Synth";
    if (in == 100) return "Input";
    return juce::String(100 - in) + " / " + juce::String(in);
}
} // namespace

Layout createParameterLayout()
{
    Layout layout;
    constexpr int v = 1; // parameter version hint for VST3 (do not change for existing params)

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PID { id(ids::masterGain), v }, "Master Gain",
        juce::NormalisableRange<float> { -60.0f, 12.0f, 0.01f, 2.0f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction(formatDb).withValueFromStringFunction(parseDb)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PID { id(ids::inputGain), v }, "Input Gain",
        juce::NormalisableRange<float> { -60.0f, 12.0f, 0.01f, 2.0f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction(formatDb).withValueFromStringFunction(parseDb)));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        PID { id(ids::sourceMix), v }, "Source Mix (legacy)",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(formatMix).withValueFromStringFunction(parsePercent)));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        PID { id(ids::bypassSynth), v }, "Bypass Synth", false));

    for (size_t i = 0; i < ids::macros.size(); ++i)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            PID { id(ids::macros[i]), v }, "Macro " + juce::String(int(i) + 1),
            juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction(formatPercent).withValueFromStringFunction(parsePercent)));

    // ---- Frequency sidechain (v2). Parameter version hint 2 for new parameters. ----
    constexpr int v2 = 2;
    auto pctAttr = juce::AudioParameterFloatAttributes().withStringFromValueFunction(formatPercent).withValueFromStringFunction(parsePercent);
    auto dbAttr = [](const char* unit)
    {
        return juce::AudioParameterFloatAttributes().withLabel(unit)
            .withStringFromValueFunction([](float x, int) { return (x > 0.04f ? "+" : "") + juce::String(x, 1) + " dB"; })
            .withValueFromStringFunction([](const juce::String& t) { return t.getFloatValue(); });
    };
    auto hzAttr = juce::AudioParameterFloatAttributes().withLabel("Hz")
        .withStringFromValueFunction([](float x, int) { return x >= 1000.0f ? juce::String(x / 1000.0f, 2) + " kHz" : juce::String(juce::roundToInt(x)) + " Hz"; })
        .withValueFromStringFunction([](const juce::String& t) { return t.getFloatValue() * (t.containsIgnoreCase("k") ? 1000.0f : 1.0f); });
    auto msAttr = juce::AudioParameterFloatAttributes().withLabel("ms")
        .withStringFromValueFunction([](float x, int) { return x >= 1000.0f ? juce::String(x / 1000.0f, x >= 10000.0f ? 1 : 2) + " s" : (x < 10.0f ? juce::String(x, 1) + " ms" : juce::String(juce::roundToInt(x)) + " ms"); })
        .withValueFromStringFunction([](const juce::String& t) { return t.getFloatValue(); });
    auto logRange = [](float lo, float hi) { juce::NormalisableRange<float> r { lo, hi }; r.setSkewForCentre(std::sqrt(lo * hi)); return r; };

    layout.add(std::make_unique<juce::AudioParameterBool>(PID { id(ids::sc::enable), v2 }, "SC Enable", true));
    layout.add(std::make_unique<juce::AudioParameterFloat>(PID { id(ids::sc::inputGain), v2 }, "SC Input Gain",
        juce::NormalisableRange<float> { -24.0f, 24.0f, 0.01f }, 0.0f, dbAttr("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(PID { id(ids::sc::sensitivity), v2 }, "SC Sensitivity",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.5f, pctAttr));
    layout.add(std::make_unique<juce::AudioParameterFloat>(PID { id(ids::sc::threshold), v2 }, "SC Threshold",
        juce::NormalisableRange<float> { -24.0f, 24.0f, 0.01f }, 0.0f, dbAttr("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(PID { id(ids::sc::depth), v2 }, "SC Depth",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.7f, pctAttr));
    layout.add(std::make_unique<juce::AudioParameterFloat>(PID { id(ids::sc::mix), v2 }, "SC Mix",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 1.0f, pctAttr));
    layout.add(std::make_unique<juce::AudioParameterBool>(PID { id(ids::sc::automatic), v2 }, "SC Auto", true));
    layout.add(std::make_unique<juce::AudioParameterBool>(PID { id(ids::sc::fullBand), v2 }, "SC Full-band", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(PID { id(ids::sc::rangeLow), v2 }, "SC Range Low",
        logRange(20.0f, 500.0f), 30.0f, hzAttr));
    layout.add(std::make_unique<juce::AudioParameterFloat>(PID { id(ids::sc::rangeHigh), v2 }, "SC Range High",
        logRange(60.0f, 2000.0f), 250.0f, hzAttr));
    layout.add(std::make_unique<juce::AudioParameterBool>(PID { id(ids::sc::listen), v2 }, "SC Listen", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(PID { id(ids::sc::delta), v2 }, "SC Delta", false));

    // Gentle defaults: two bands (kick fundamental + bass region), 6 dB max cut.
    const float defFreq[] = { 50.0f, 90.0f, 150.0f, 250.0f };
    for (int b = 0; b < ids::sc::kBands; ++b)
    {
        using F = ids::sc::BandField;
        const auto pid = [b](F f) { return juce::String(ids::sc::bandId(b, f)); };
        const auto nm = [b](const char* what) { return "SC Band " + juce::String(b + 1) + " " + what; };
        layout.add(std::make_unique<juce::AudioParameterBool>(PID { pid(F::BEnable), v2 }, nm("On"), b < 2));
        layout.add(std::make_unique<juce::AudioParameterFloat>(PID { pid(F::BFreq), v2 }, nm("Freq"), logRange(20.0f, 2000.0f), defFreq[b], hzAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(PID { pid(F::BQ), v2 }, nm("Q"), logRange(0.3f, 8.0f), 1.2f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float x, int) { return juce::String(x, 2); })));
        layout.add(std::make_unique<juce::AudioParameterFloat>(PID { pid(F::BThreshold), v2 }, nm("Threshold"),
            juce::NormalisableRange<float> { -60.0f, 0.0f, 0.1f }, -30.0f, dbAttr("dB")));
        layout.add(std::make_unique<juce::AudioParameterFloat>(PID { pid(F::BReduction), v2 }, nm("Max Reduction"),
            juce::NormalisableRange<float> { 0.0f, 24.0f, 0.1f }, 6.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")
                .withStringFromValueFunction([](float x, int) { return x < 0.05f ? juce::String("0 dB") : "-" + juce::String(x, 1) + " dB"; })
                .withValueFromStringFunction([](const juce::String& t) { return std::abs(t.getFloatValue()); })));
        layout.add(std::make_unique<juce::AudioParameterFloat>(PID { pid(F::BAttack), v2 }, nm("Attack"), logRange(0.5f, 100.0f), 5.0f, msAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(PID { pid(F::BRelease), v2 }, nm("Release"), logRange(10.0f, 1000.0f), 120.0f, msAttr));
    }

    // ---- Oscillator engine (Stage 2). Parameter version hint 3. ----
    {
        constexpr int v3 = 3;
        namespace o = ids::osc;
        const juce::StringArray waves { "Sine", "Triangle", "Saw", "Square", "Pulse" };
        auto cents = juce::AudioParameterFloatAttributes().withLabel("ct")
            .withStringFromValueFunction([](float x, int) { return (x > 0.05f ? "+" : "") + juce::String(x, 1) + " ct"; });
        auto deg = juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float x, int) { return juce::String(juce::roundToInt(x * 360.0f)) + juce::String(juce::CharPointer_UTF8("\xc2\xb0")); });
        for (int i = 0; i < 2; ++i)
        {
            const auto pid = [i](o::OscField f) { return juce::String(o::oscId(i, f)); };
            const juce::String nm = i == 0 ? "Osc A " : "Osc B ";
            layout.add(std::make_unique<juce::AudioParameterBool>(PID { pid(o::On), v3 }, nm + "On", i == 0));
            layout.add(std::make_unique<juce::AudioParameterChoice>(PID { pid(o::WaveF), v3 }, nm + "Wave", waves, i == 0 ? 2 : 3));
            layout.add(std::make_unique<juce::AudioParameterInt>(PID { pid(o::Octave), v3 }, nm + "Octave", -3, 3, 0));
            layout.add(std::make_unique<juce::AudioParameterInt>(PID { pid(o::Semi), v3 }, nm + "Semitone", -12, 12, i == 0 ? 0 : 7));
            layout.add(std::make_unique<juce::AudioParameterFloat>(PID { pid(o::Fine), v3 }, nm + "Fine", juce::NormalisableRange<float> { -100.0f, 100.0f, 0.1f }, 0.0f, cents));
            layout.add(std::make_unique<juce::AudioParameterFloat>(PID { pid(o::Pw), v3 }, nm + "Pulse Width", juce::NormalisableRange<float> { 0.05f, 0.95f }, 0.5f, pctAttr));
            layout.add(std::make_unique<juce::AudioParameterFloat>(PID { pid(o::Phase), v3 }, nm + "Phase", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, deg));
            layout.add(std::make_unique<juce::AudioParameterBool>(PID { pid(o::Free), v3 }, nm + "Free Phase", false));
            layout.add(std::make_unique<juce::AudioParameterFloat>(PID { pid(o::Level), v3 }, nm + "Level", juce::NormalisableRange<float> { 0.0f, 1.0f }, i == 0 ? 0.7f : 0.5f, pctAttr));
            layout.add(std::make_unique<juce::AudioParameterInt>(PID { pid(o::Unison), v3 }, nm + "Unison", 1, 7, i == 0 ? 3 : 1));
            layout.add(std::make_unique<juce::AudioParameterFloat>(PID { pid(o::Detune), v3 }, nm + "Detune", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 12.0f, cents));
            layout.add(std::make_unique<juce::AudioParameterFloat>(PID { pid(o::Spread), v3 }, nm + "Spread", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.6f, pctAttr));
        }
        auto P3 = [&](std::string_view i) { return PID { id(i), v3 }; };
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(o::fm), "FM B > A", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(o::pm), "PM B > A", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterBool>(P3(o::subOn), "Sub On", true));
        layout.add(std::make_unique<juce::AudioParameterChoice>(P3(o::subWave), "Sub Wave", juce::StringArray { "Sine", "Square", "Triangle" }, 0));
        layout.add(std::make_unique<juce::AudioParameterChoice>(P3(o::subOctave), "Sub Octave", juce::StringArray { "-1 oct", "-2 oct" }, 0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(o::subLevel), "Sub Level", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.3f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterBool>(P3(o::noiseOn), "Noise On", false));
        layout.add(std::make_unique<juce::AudioParameterChoice>(P3(o::noiseType), "Noise Type", juce::StringArray { "White", "Pink" }, 1));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(o::noiseLevel), "Noise Level", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.15f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(o::drift), "Analog Drift", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.25f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterChoice>(P3(o::satMode), "Saturation Mode", juce::StringArray { "Soft", "Asym", "Fold" }, 0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(o::satDrive), "Saturation Drive", juce::NormalisableRange<float> { 0.0f, 36.0f, 0.1f }, 0.0f, dbAttr("dB")));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(o::satMix), "Saturation Mix", juce::NormalisableRange<float> { 0.0f, 1.0f }, 1.0f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(o::velocity), "Velocity Sensitivity", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.6f, pctAttr));

        // ---- Filter + envelopes (Stage 3) ----
        namespace f = ids::flt; namespace e = ids::env;
        auto bip = juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float x, int) { return (x > 0.005f ? "+" : "") + juce::String(juce::roundToInt(x * 100.0f)) + " %"; });
        auto oct = juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float x, int) { return (x > 0.05f ? "+" : "") + juce::String(x, 1) + " st"; });
        layout.add(std::make_unique<juce::AudioParameterBool>(P3(f::on), "Filter On", true));
        layout.add(std::make_unique<juce::AudioParameterChoice>(P3(f::mode), "Filter Mode", juce::StringArray { "LP24", "LP12", "BP", "HP12", "HP24", "Notch" }, 0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(f::cutoff), "Cutoff", logRange(20.0f, 20000.0f), 6000.0f, hzAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(f::reso), "Resonance", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.2f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(f::drive), "Filter Drive", juce::NormalisableRange<float> { 0.0f, 24.0f, 0.1f }, 0.0f, dbAttr("dB")));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(f::env), "Filter Env Amount", juce::NormalisableRange<float> { -1.0f, 1.0f }, 0.25f, bip));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(f::key), "Keytracking", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.5f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(f::vel), "Velocity > Cutoff", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, pctAttr));
        auto envTime = [&](std::string_view pid, const char* name, float lo, float hi, float def)
        { layout.add(std::make_unique<juce::AudioParameterFloat>(P3(pid), name, logRange(lo, hi), def, msAttr)); };
        envTime(e::ampA, "Amp Attack", 0.5f, 20000.0f, 8.0f);
        envTime(e::ampD, "Amp Decay", 1.0f, 20000.0f, 400.0f);
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(e::ampS), "Amp Sustain", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.8f, pctAttr));
        envTime(e::ampR, "Amp Release", 1.0f, 30000.0f, 800.0f);
        envTime(e::modA, "Mod Attack", 0.5f, 20000.0f, 10.0f);
        envTime(e::modD, "Mod Decay", 1.0f, 20000.0f, 900.0f);
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(e::modS), "Mod Sustain", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.2f, pctAttr));
        envTime(e::modR, "Mod Release", 1.0f, 30000.0f, 900.0f);
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(e::modPitch), "Mod Env > Pitch", juce::NormalisableRange<float> { -24.0f, 24.0f, 0.01f }, 0.0f, oct));

        // ---- Granular (Stage 4) ----
        namespace g = ids::gr;
        layout.add(std::make_unique<juce::AudioParameterBool>(P3(g::on), "Granular On", false));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(g::size), "Grain Size", logRange(5.0f, 1000.0f), 120.0f, msAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(g::density), "Grain Density", logRange(0.5f, 200.0f), 12.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float x, int) { return juce::String(x, x < 10.0f ? 1 : 0) + " /s"; })));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(g::position), "Grain Position", logRange(1.0f, 9000.0f), 250.0f, msAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(g::scatter), "Grain Scatter", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.2f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(g::pitch), "Grain Pitch", juce::NormalisableRange<float> { -24.0f, 24.0f, 0.01f }, 0.0f, oct));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(g::pitchRandom), "Grain Pitch Random", juce::NormalisableRange<float> { 0.0f, 12.0f, 0.01f }, 0.0f, oct));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(g::reverse), "Grain Reverse", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterChoice>(P3(g::window), "Grain Window", juce::StringArray { "Hann", "Gauss", "Tukey", "Triangle", "Decay" }, 0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(g::spread), "Grain Spread", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.5f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(g::feedback), "Grain Feedback", juce::NormalisableRange<float> { 0.0f, 0.95f }, 0.0f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P3(g::mix), "Grain Mix", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.5f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterBool>(P3(g::freeze), "Grain Freeze", false));

        // ---- Reverb (Stage 5) ----
        namespace r = ids::rv;
        auto P4 = [&](std::string_view i) { return PID { id(i), 4 }; };
        auto secAttr = juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float x, int) { return juce::String(x, x < 10.0f ? 2 : 1) + " s"; });
        layout.add(std::make_unique<juce::AudioParameterBool>(P4(r::on), "Reverb On", false));
        layout.add(std::make_unique<juce::AudioParameterChoice>(P4(r::algo), "Reverb Type",
            juce::StringArray { "Hall", "Room", "Cathedral", "Plate", "Spring", "Shimmer", "Infinite" }, 0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P4(r::decay), "Reverb Decay", logRange(0.2f, 30.0f), 3.0f, secAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P4(r::predelay), "Reverb Pre-Delay", juce::NormalisableRange<float> { 0.0f, 500.0f, 0.1f, 0.5f }, 20.0f, msAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P4(r::size), "Reverb Size", juce::NormalisableRange<float> { 0.5f, 2.0f }, 1.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float x, int) { return juce::String(x, 2) + "x"; })));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P4(r::diffusion), "Reverb Diffusion", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.7f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P4(r::damping), "Reverb Damping", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.4f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P4(r::early), "Reverb Early Reflections", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.5f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P4(r::mod), "Reverb Modulation", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.3f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P4(r::feedback), "Reverb Feedback", juce::NormalisableRange<float> { 0.0f, 0.9f }, 0.0f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P4(r::shimmer), "Reverb Shimmer", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.5f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P4(r::mix), "Reverb Mix", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.3f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterBool>(P4(r::freeze), "Reverb Freeze", false));

        // ---- Stereo safety (reverb width + output stage) ----
        auto P5 = [&](std::string_view i) { return PID { id(i), 5 }; };
        layout.add(std::make_unique<juce::AudioParameterFloat>(P5(r::width), "Reverb Width", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.7f, pctAttr));
        namespace so = ids::out;
        layout.add(std::make_unique<juce::AudioParameterFloat>(P5(so::width), "Output Width", juce::NormalisableRange<float> { 0.0f, 1.5f }, 1.0f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterBool>(P5(so::bassMono), "Bass Mono", true));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P5(so::bassMonoHz), "Bass Mono Freq", logRange(40.0f, 400.0f), 120.0f, hzAttr));
        layout.add(std::make_unique<juce::AudioParameterBool>(P5(so::safety), "Stereo Safety", true));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P5(so::floor), "Correlation Floor", juce::NormalisableRange<float> { -0.5f, 0.5f, 0.01f }, 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float x, int) { return (x >= 0 ? "+" : "") + juce::String(x, 2); })));
        layout.add(std::make_unique<juce::AudioParameterBool>(P5(so::monoCheck), "Mono Check", false));

        // ---- Delay (Stage 6) ----
        namespace d = ids::dl;
        auto P6 = [&](std::string_view i) { return PID { id(i), 6 }; };
        juce::StringArray divs; for (int i = 0; i < ambient::dsp::Delay::kNumDivisions; ++i) divs.add(ambient::dsp::Delay::divisionName(i));
        layout.add(std::make_unique<juce::AudioParameterBool>(P6(d::on), "Delay On", false));
        layout.add(std::make_unique<juce::AudioParameterChoice>(P6(d::mode), "Delay Type",
            juce::StringArray { "Tape", "BBD", "Digital", "Ping-Pong", "Multi-Tap", "Reverse", "Pitch", "Granular" }, 0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P6(d::time), "Delay Time", logRange(1.0f, 4000.0f), 375.0f, msAttr));
        layout.add(std::make_unique<juce::AudioParameterBool>(P6(d::sync), "Delay Sync", true));
        layout.add(std::make_unique<juce::AudioParameterChoice>(P6(d::division), "Delay Division", divs, 7));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P6(d::feedback), "Delay Feedback", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.45f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P6(d::hp), "Delay Low Cut", logRange(20.0f, 2000.0f), 80.0f, hzAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P6(d::lp), "Delay High Cut", logRange(500.0f, 20000.0f), 9000.0f, hzAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P6(d::modDepth), "Delay Mod Depth", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.2f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P6(d::modRate), "Delay Mod Rate", logRange(0.05f, 10.0f), 0.6f, hzAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P6(d::drive), "Delay Drive", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.2f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P6(d::width), "Delay Width", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.7f, pctAttr));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P6(d::pitch), "Delay Pitch", juce::NormalisableRange<float> { -24.0f, 24.0f, 0.01f }, 12.0f, oct));
        layout.add(std::make_unique<juce::AudioParameterFloat>(P6(d::mix), "Delay Mix", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.3f, pctAttr));

        // ---- LFOs + modulation matrix (Stage 7) ----
        auto P7 = [&](const juce::String& i) { return PID { i, 7 }; };
        juce::StringArray lfoWaves; for (int w = 0; w < ambient::dsp::Lfo::kNumWaves; ++w) lfoWaves.add(ambient::dsp::Lfo::waveName(w));
        for (int l = 1; l <= 4; ++l)
        {
            const juce::String pre = "lfo." + juce::String(l) + ".", nm = "LFO " + juce::String(l) + " ";
            layout.add(std::make_unique<juce::AudioParameterChoice>(P7(pre + "wave"), nm + "Wave", lfoWaves, 0));
            layout.add(std::make_unique<juce::AudioParameterFloat>(P7(pre + "rate"), nm + "Rate", logRange(0.01f, 40.0f), l == 1 ? 0.5f : 0.15f * float(l),
                juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float x, int) { return juce::String(x, x < 1.0f ? 2 : (x < 10.0f ? 1 : 0)) + " Hz"; })));
            layout.add(std::make_unique<juce::AudioParameterBool>(P7(pre + "sync"), nm + "Sync", false));
            layout.add(std::make_unique<juce::AudioParameterChoice>(P7(pre + "division"), nm + "Division", divs, 9));
            layout.add(std::make_unique<juce::AudioParameterFloat>(P7(pre + "phase"), nm + "Phase", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, deg));
            layout.add(std::make_unique<juce::AudioParameterFloat>(P7(pre + "shape"), nm + "Shape", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.5f, pctAttr));
            layout.add(std::make_unique<juce::AudioParameterFloat>(P7(pre + "smooth"), nm + "Smooth", juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, pctAttr));
            layout.add(std::make_unique<juce::AudioParameterBool>(P7(pre + "unipolar"), nm + "Unipolar", false));
            layout.add(std::make_unique<juce::AudioParameterBool>(P7(pre + "retrig"), nm + "Retrigger", false));
            const float def[8] = { 0.0f, 0.7f, 1.0f, 0.4f, -0.2f, -0.8f, -1.0f, -0.4f };
            for (int k = 1; k <= 8; ++k)
                layout.add(std::make_unique<juce::AudioParameterFloat>(P7(pre + "pt" + juce::String(k)), nm + "Curve " + juce::String(k),
                    juce::NormalisableRange<float> { -1.0f, 1.0f }, def[k - 1], bip));
        }
        juce::StringArray srcNames; for (auto* n : ambient::modsrc::kNames) srcNames.add(n);
        juce::StringArray dstNames { "None" };
        for (auto* d : ambient::moddest::kDestinationNames) dstNames.add(d);
        for (int m = 1; m <= ambient::dsp::ModMatrix::kSlots; ++m)
        {
            const juce::String pre = "mm." + juce::String(m) + ".", nm = "Mod " + juce::String(m) + " ";
            layout.add(std::make_unique<juce::AudioParameterChoice>(P7(pre + "src"), nm + "Source", srcNames, 0));
            layout.add(std::make_unique<juce::AudioParameterChoice>(P7(pre + "dst"), nm + "Destination", dstNames, 0));
            layout.add(std::make_unique<juce::AudioParameterFloat>(P7(pre + "amt"), nm + "Amount", juce::NormalisableRange<float> { -1.0f, 1.0f }, 0.0f, bip));
        }
    }

    return layout;
}
} // namespace ambient
