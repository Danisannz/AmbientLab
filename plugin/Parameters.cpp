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
        .withStringFromValueFunction([](float x, int) { return x < 10.0f ? juce::String(x, 1) + " ms" : juce::String(juce::roundToInt(x)) + " ms"; })
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
    }

    return layout;
}
} // namespace ambient
