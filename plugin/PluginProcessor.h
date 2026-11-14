#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <ambient/Safety.h>
#include <ambient/Smoother.h>
#include <ambient/TestToneSynth.h>
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

    // UI-readable output peak (written by audio thread, read by UI timer).
    std::atomic<float> outputPeak { 0.0f };

private:
    static BusesProperties makeBuses();
    void handleMidi(const juce::MidiMessage&);

    std::atomic<float>* pMasterGain  = nullptr;
    std::atomic<float>* pInputGain   = nullptr;
    std::atomic<float>* pSourceMix   = nullptr;
    std::atomic<float>* pBypassSynth = nullptr;

    ambient::Smoother masterGain, inputGain, sourceMix;
    std::array<ambient::DcBlocker, 2> dc;
    ambient::TestToneSynth synth;
    juce::AudioBuffer<float> synthBuffer; // preallocated in prepareToPlay

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmbientProcessor)
};
