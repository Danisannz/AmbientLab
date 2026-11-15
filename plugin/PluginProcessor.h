#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <ambient/Safety.h>
#include <ambient/Smoother.h>
#include <ambient/TestToneSynth.h>
#include <ambient/StereoCaptureRing.h>
#include <ambient/dsp/SpectralDucker.h>
#include <ambient/dsp/SynthEngine.h>
#include <ambient/dsp/Granular.h>
#include <ambient/dsp/Reverb.h>
#include <ambient/dsp/StereoSafety.h>
#include <ambient/dsp/Delay.h>
#include <ambient/dsp/Lfo.h>
#include "ModDestinations.h"
#include "TripleBuffer.h"
#include <ambient/dsp/Sequencer.h>
#include <ambient/ParamIDs.h>
#include <array>

/** Parameter reference used by every engine read. If the parameter is a modulation
    destination, load() returns its modulated value (computed once per control sub-block on
    the audio thread), otherwise the raw host value. `ref->load()` keeps the old call syntax. */
struct PRef
{
    std::atomic<float>* raw = nullptr;
    const float* mod = nullptr;
    float load() const noexcept { return mod != nullptr ? *mod : raw->load(std::memory_order_relaxed); }
    const PRef* operator->() const noexcept { return this; }
};

class AmbientProcessor : public juce::AudioProcessor
{
public:
    /** Input bus indices. Instrument: ONE input bus, the sidechain, declared VST3 kAux (no main
        input) - Ableton Live and other hosts route their sidechain chooser into it. FX: main input
        (the track) + sidechain aux. */
   #if AMBIENT_IS_INSTRUMENT
    static constexpr int kMainBus = -1, kScBus = 0;
   #else
    static constexpr int kMainBus = 0, kScBus = 1;
   #endif
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return &vst3Extensions; }

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

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
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
    struct Vst3Ext : juce::VST3ClientExtensions
    {
        bool getPluginHasMainInput() const override { return AMBIENT_IS_INSTRUMENT == 0; }
    } vst3Extensions;

    /** Sidechain input diagnostics (audio thread -> UI, lock-free). Raw samples, before SC gain. */
    struct SidechainDiagnostics
    {
        std::atomic<bool> busEnabled { false };
        std::atomic<int> channels { 0 };          // channels actually read from the aux bus
        std::atomic<int> bufferChannels { 0 };    // channels in the host buffer
        std::atomic<float> rms { 0.0f }, peak { 0.0f };
        std::atomic<uint32_t> blocks { 0 }, nonZeroBlocks { 0 };
    } scDiag;
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
        PRef enable, inputGain, sensitivity, threshold, depth, mix, automatic, fullBand, rangeLow, rangeHigh, listen, delta;
        std::array<std::array<std::atomic<float>*, 7>, ambient::ids::sc::kBands> band {};
    } scParams;
    ambient::dsp::SpectralDucker ducker;
    ambient::Smoother scGain;
    juce::AudioBuffer<float> scratch; // preallocated: refL, refR, intL, intR

    PRef pMasterGain;
    PRef pInputGain;
    std::atomic<float>* pSourceMix   = nullptr;
    std::atomic<float>* pBypassSynth = nullptr;

    ambient::Smoother masterGain, inputGain, sourceMix;
    std::array<ambient::DcBlocker, 2> dc;
    ambient::dsp::SynthEngine synth;
    ambient::dsp::Granular granular;
    void readGranularParams();
    std::vector<PRef> grRaw;
    ambient::dsp::Reverb reverb;
    void readReverbParams();
    std::vector<PRef> rvRaw;
    ambient::dsp::StereoSafety stereoSafety;
    ambient::dsp::Delay delay;
    void readDelayParams();
    std::vector<PRef> dlRaw;
    double hostBpm = 120.0;

    // ---- Modulation (Stage 7) -------------------------------------------------
    static constexpr int kControlBlock = 32;          // modulation / parameter update rate
    std::array<float, ambient::moddest::kNumDestinations> modValue {};
    std::array<std::atomic<float>*, ambient::moddest::kNumDestinations> destRaw {};
    std::array<juce::RangedAudioParameter*, ambient::moddest::kNumDestinations> destParam {};
    std::array<ambient::dsp::Lfo, 4> lfos;
    std::array<std::vector<PRef>, 4> lfoRaw;          // wave rate sync division phase shape smooth unipolar retrig pt1..8
    std::vector<std::atomic<float>*> mmRaw;           // per slot: src dst amt
    std::array<std::atomic<float>*, 8> macroRaw {};
    std::array<float, ambient::modsrc::kCount> srcValue {};
    std::array<float, ambient::moddest::kNumDestinations + 1> modOffset {};
    std::array<ambient::dsp::ModMatrix::Slot, ambient::dsp::ModMatrix::kSlots> slots {};
    float midiVelocity = 0.0f, midiWheel = 0.0f, midiAftertouch = 0.0f, midiBend = 0.0f, midiKey = 0.0f, midiRandom = 0.0f;
    uint32_t noteRng = 0x1234567u;
    bool transportPlaying = false; double transportPpq = 0.0;
    void updateModulation(int startSample, int numSamples) noexcept;

    // ---- Sequencer (Stage 8) ----------------------------------------------------
    using SeqPattern = std::array<ambient::dsp::Sequencer::Step, ambient::dsp::Sequencer::kMaxSteps>;
    ambient::dsp::Sequencer sequencer;                 // audio thread
    ambient::dsp::Sequencer seqModel;                  // message thread: editing model (presets, randomise)
    ambient::TripleBuffer<SeqPattern> seqPattern;      // message -> audio
    std::vector<PRef> seqRaw;                          // on clock bpm length division swing direction root scale octave transpose gate patternStart
    std::array<std::atomic<float>*, 4> seqLockDst {};
    int seqNotesOn = 0; float seqLastVelocity = 0.0f;
    void publishPattern() noexcept;
    void readSequencerParams() noexcept;

    // ---- Routing (Stage 9) -----------------------------------------------------
    std::vector<PRef> routeRaw;                        // mode order feedback fxGain
    void processChain(float* L, float* R, int n) noexcept;
    void processModule(int module, float* L, float* R, int n) noexcept;   // 0 granular, 1 delay, 2 reverb
    std::array<std::array<float, 32>, 2> fbRing {}, parSrc {}, parWork {}, parAcc {};
    std::array<float, 2> fbHp {}, fbDc {};
    float fbEnv = 0.0f;   // loop level follower (safety: holds the return around -12 dBFS)
    ambient::Smoother fxGainSm, fbSm;
    juce::ValueTree patternToTree() const;
    void patternFromTree(const juce::ValueTree&);
public:
    // Sequencer pattern API (message thread only)
    const ambient::dsp::Sequencer::Step& getSeqStep(int i) const noexcept { return seqModel.getStep(i); }
    void setSeqStep(int i, const ambient::dsp::Sequencer::Step& st) { seqModel.setStep(i, st); publishPattern(); }
    void loadSeqPreset(int index);
    void randomiseSeq(float amount);
    std::atomic<int> seqCurrentStep { -1 };
    // Presets (message thread). The name is stored in the state, so it survives sessions.
    juce::String getPresetName() const { return apvts.state.getProperty("presetName", "Init").toString(); }
    void setPresetName(const juce::String& n) { apvts.state.setProperty("presetName", n, nullptr); }
    int currentProgram = 0;
    std::atomic<uint32_t> seqNoteOnCount { 0 };   // diagnostics: note-ons generated by the sequencer
public:
    std::array<std::atomic<float>, 4> lfoOut {}, lfoPhaseOut {};
    std::array<std::atomic<float>, ambient::modsrc::kCount> sourceOut {};
    std::array<std::atomic<float>, ambient::dsp::ModMatrix::kSlots> slotOut {};   // current amount x source
private:
    void readStereoParams();
    std::vector<PRef> outRaw;
public:
    const ambient::dsp::StereoSafety::Telemetry& stereoTelemetry() const noexcept { return stereoSafety.telemetry(); }
private:
    void readSynthParams();
    std::vector<PRef> oscRaw; // cached raw parameter pointers (filled in the constructor)
    juce::AudioBuffer<float> synthBuffer; // preallocated in prepareToPlay
    juce::AudioProcessLoadMeasurer loadMeasurer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmbientProcessor)
};
