#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <ambient/Safety.h>
#include <ambient/Smoother.h>
#include <ambient/TestToneSynth.h>
#include <ambient/StereoCaptureRing.h>
#include <ambient/dsp/SpectralDucker.h>
#include <ambient/dsp/SynthEngine.h>
#include <ambient/dsp/Granular.h>
#include <ambient/ParamIDs.h>
#include <array>

class AmbientProcessor : public juce::AudioProcessor
{
public:
    AmbientProcessor();
    ~AmbientProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- Audio -> UI telemetry (lock-free; written by the audio thread) ----
    std::atomic<float> outputPeak { 0.0f };          // max(L,R), decaying
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f }; // per-block peaks
    std::atomic<uint32_t> midiEventCount { 0 };       // increments per MIDI event
    std::atomic<float> inputPeak { 0.0f };            // sidechain reference level (post SC input gain)
    std::atomic<double> currentSampleRate { 48000.0 };

    /** Set by the editor; the audio thread only feeds the capture ring while true. */
    std::atomic<bool> editorOpen { false };
    ambient::StereoCaptureRing captureRing { 1 << 18 }; // ~1.4 s at 192 kHz (scope up to 500 ms)
    ambient::StereoCaptureRing refRing { 1 << 16 };     // sidechain reference, for the sidechain graph

    /** Frequency sidechain telemetry (lock-free, written by the audio thread). */
    const ambient::dsp::SpectralDucker::Telemetry& sidechainTelemetry() const noexcept { return ducker.telemetry(); }

    double getCpuLoad() const noexcept { return loadMeasurer.getLoadAsProportion(); }

private:
    static BusesProperties makeBuses();
    void handleMidi(const juce::MidiMessage&);
    void readSidechainParams();

    struct ScParams
    {
        std::atomic<float> *enable = nullptr, *inputGain = nullptr, *sensitivity = nullptr, *threshold = nullptr,
                           *depth = nullptr, *mix = nullptr, *automatic = nullptr, *fullBand = nullptr,
                           *rangeLow = nullptr, *rangeHigh = nullptr, *listen = nullptr, *delta = nullptr;
        std::array<std::array<std::atomic<float>*, 7>, ambient::ids::sc::kBands> band {};
    } scParams;
    ambient::dsp::SpectralDucker ducker;
    ambient::Smoother scGain;
    juce::AudioBuffer<float> scratch; // preallocated: refL, refR, intL, intR

    std::atomic<float>* pMasterGain  = nullptr;
    std::atomic<float>* pInputGain   = nullptr;
    std::atomic<float>* pSourceMix   = nullptr;
    std::atomic<float>* pBypassSynth = nullptr;

    ambient::Smoother masterGain, inputGain, sourceMix;
    std::array<ambient::DcBlocker, 2> dc;
    ambient::dsp::SynthEngine synth;
    ambient::dsp::Granular granular;
    void readGranularParams();
    std::vector<std::atomic<float>*> grRaw;
    void readSynthParams();
    std::vector<std::atomic<float>*> oscRaw; // cached raw parameter pointers (filled in the constructor)
    juce::AudioBuffer<float> synthBuffer; // preallocated in prepareToPlay
    juce::AudioProcessLoadMeasurer loadMeasurer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmbientProcessor)
};
