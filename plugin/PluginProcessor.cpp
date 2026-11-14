#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "StateMigration.h"
#include <ambient/ParamIDs.h>

using namespace ambient;

juce::AudioProcessor::BusesProperties AmbientProcessor::makeBuses()
{
#if AMBIENT_IS_INSTRUMENT
    return juce::AudioProcessor::BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
        .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false);
#else
    return juce::AudioProcessor::BusesProperties()
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true);
#endif
}

AmbientProcessor::AmbientProcessor()
    : AudioProcessor(makeBuses()),
      apvts(*this, nullptr, "AmbientLab", createParameterLayout())
{
    pMasterGain  = apvts.getRawParameterValue(id(ids::masterGain));
    pInputGain   = apvts.getRawParameterValue(id(ids::inputGain));
    pSourceMix   = apvts.getRawParameterValue(id(ids::sourceMix));
    pBypassSynth = apvts.getRawParameterValue(id(ids::bypassSynth));
}

bool AmbientProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (const auto& in : layouts.inputBuses)
        if (! in.isDisabled() && in != juce::AudioChannelSet::stereo()) return false;
    return true;
}

void AmbientProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synthBuffer.setSize(1, samplesPerBlock, false, true, false);
    synth.prepare(sampleRate);
    for (auto* s : { &masterGain, &inputGain, &sourceMix }) s->prepare(sampleRate, 20.0f);
    masterGain.reset(juce::Decibels::decibelsToGain(pMasterGain->load(), -60.0f));
    inputGain.reset (juce::Decibels::decibelsToGain(pInputGain->load(),  -60.0f));
    sourceMix.reset (pSourceMix->load());
    for (auto& d : dc) d.prepare(sampleRate);
}

void AmbientProcessor::handleMidi(const juce::MidiMessage& m)
{
    if (m.isNoteOn())                                  synth.noteOn(m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())                            synth.noteOff(m.getNoteNumber());
    else if (m.isAllNotesOff() || m.isAllSoundOff())   synth.allNotesOff();
}

void AmbientProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    if (n > synthBuffer.getNumSamples()) // host exceeded announced block size: refuse to allocate
    {
        buffer.clear();
        return;
    }

    // ---- External input (FX main bus, or Instrument sidechain) ------------
    auto inBus = getBusBuffer(buffer, true, 0);
    const bool hasInput = getBusCount(true) > 0 && getBus(true, 0)->isEnabled()
                          && inBus.getNumChannels() >= 2;
    float* L = buffer.getWritePointer(0);
    float* R = buffer.getWritePointer(1);
    const float* inL = hasInput ? inBus.getReadPointer(0) : nullptr;
    const float* inR = hasInput ? inBus.getReadPointer(1) : nullptr;

    // ---- Synth: sample-accurate MIDI ---------------------------------------
    float* s = synthBuffer.getWritePointer(0);
    juce::FloatVectorOperations::clear(s, n);
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit(0, n, meta.samplePosition);
        synth.render(s, pos, at);
        pos = at;
        handleMidi(meta.getMessage());
    }
    synth.render(s, pos, n);
    const bool synthOn = pBypassSynth->load() < 0.5f && AMBIENT_IS_INSTRUMENT;

    // ---- Mix ----------------------------------------------------------------
    masterGain.setTarget(juce::Decibels::decibelsToGain(pMasterGain->load(), -60.0f));
    inputGain.setTarget (juce::Decibels::decibelsToGain(pInputGain->load(),  -60.0f));
    sourceMix.setTarget (pSourceMix->load());

    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float g = inputGain.next(), mix = sourceMix.next(), mg = masterGain.next();
        const float syn = synthOn ? s[i] : 0.0f;
        const float xl = inL ? inL[i] * g : 0.0f;
        const float xr = inR ? inR[i] * g : 0.0f;
        // FX target: input always passes; mix only matters for instrument.
        const float wSyn = AMBIENT_IS_INSTRUMENT ? (1.0f - mix) : 1.0f;
        const float wIn  = AMBIENT_IS_INSTRUMENT ? mix : 1.0f;
        float l = (syn * wSyn + xl * wIn) * mg;
        float r = (syn * wSyn + xr * wIn) * mg;
        l = softClip(dc[0].process(l));
        r = softClip(dc[1].process(r));
        L[i] = l; R[i] = r;
        peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
    }

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear(ch, 0, n);
    if (sanitize(L, size_t(n)) | sanitize(R, size_t(n))) for (auto& d : dc) d = {};
    outputPeak.store(std::max(peak, outputPeak.load(std::memory_order_relaxed) * 0.9f),
                     std::memory_order_relaxed);
}

juce::AudioProcessorEditor* AmbientProcessor::createEditor() { return new AmbientEditor(*this); }

// ---- State ------------------------------------------------------------------
void AmbientProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty(kVersionProp, ids::kStateVersion, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void AmbientProcessor::setStateInformation(const void* data, int size)
{
    auto xml = getXmlFromBinary(data, size);
    if (! xml || ! xml->hasTagName(apvts.state.getType())) return;
    auto tree = juce::ValueTree::fromXml(*xml);
    migrateState(tree); // newer-version states still load known params
    apvts.replaceState(tree);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AmbientProcessor(); }
