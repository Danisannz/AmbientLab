#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "StateMigration.h"
#include <ambient/ParamIDs.h>

using namespace ambient;

juce::AudioProcessor::BusesProperties AmbientProcessor::makeBuses()
{
    // Input bus 0 = main input (FX: the track being processed; Instrument: unused,
    // disabled by default). Input bus 1 = aux sidechain (Kick/Bass reference), so
    // VST3 hosts present it as a real sidechain. Reference is never mixed into the output.
    // The sidechain bus is ACTIVE by default: VST3 hosts that route audio without
    // calling activateBus() would otherwise deliver 0 channels (the original bug).
    // An unrouted sidechain simply receives silence.
#if AMBIENT_IS_INSTRUMENT
    return juce::AudioProcessor::BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), false)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
        .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true);
#else
    return juce::AudioProcessor::BusesProperties()
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
        .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true);
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

    auto raw = [this](std::string_view i) { return apvts.getRawParameterValue(id(i)); };
    scParams.enable = raw(ids::sc::enable);       scParams.inputGain = raw(ids::sc::inputGain);
    scParams.sensitivity = raw(ids::sc::sensitivity); scParams.threshold = raw(ids::sc::threshold);
    scParams.depth = raw(ids::sc::depth);         scParams.mix = raw(ids::sc::mix);
    scParams.automatic = raw(ids::sc::automatic); scParams.fullBand = raw(ids::sc::fullBand);
    scParams.rangeLow = raw(ids::sc::rangeLow);   scParams.rangeHigh = raw(ids::sc::rangeHigh);
    scParams.listen = raw(ids::sc::listen);       scParams.delta = raw(ids::sc::delta);
    for (int b = 0; b < ids::sc::kBands; ++b)
        for (int f = 0; f < 7; ++f)
            scParams.band[size_t(b)][size_t(f)] = apvts.getRawParameterValue(juce::String(ids::sc::bandId(b, ids::sc::BandField(f))));

    // Oscillator parameters: 2 x 12 per-oscillator fields, then the globals in a fixed order.
    namespace o = ids::osc;
    for (int i = 0; i < 2; ++i)
        for (int f = 0; f < 12; ++f)
            oscRaw.push_back(apvts.getRawParameterValue(juce::String(o::oscId(i, o::OscField(f)))));
    for (auto g : { o::fm, o::pm, o::subOn, o::subWave, o::subOctave, o::subLevel, o::noiseOn, o::noiseType,
                    o::noiseLevel, o::drift, o::satMode, o::satDrive, o::satMix, o::velocity })
        oscRaw.push_back(raw(g));
    namespace f = ids::flt; namespace e = ids::env; // indices 38..54
    for (auto g : { f::on, f::mode, f::cutoff, f::reso, f::drive, f::env, f::key, f::vel,
                    e::ampA, e::ampD, e::ampS, e::ampR, e::modA, e::modD, e::modS, e::modR, e::modPitch })
        oscRaw.push_back(raw(g));

    namespace gid = ids::gr;
    for (auto g : { gid::on, gid::size, gid::density, gid::position, gid::scatter, gid::pitch, gid::pitchRandom,
                    gid::reverse, gid::window, gid::spread, gid::feedback, gid::mix, gid::freeze })
        grRaw.push_back(raw(g));

    namespace rid = ids::rv;
    for (auto g : { rid::on, rid::algo, rid::decay, rid::predelay, rid::size, rid::diffusion, rid::damping,
                    rid::early, rid::mod, rid::feedback, rid::shimmer, rid::mix, rid::freeze, rid::width })
        rvRaw.push_back(raw(g));
    namespace oid = ids::out;
    for (auto g : { oid::width, oid::bassMono, oid::bassMonoHz, oid::safety, oid::floor, oid::monoCheck })
        outRaw.push_back(raw(g));
}

void AmbientProcessor::readReverbParams()
{
    ambient::dsp::Reverb::Params p;
    auto v = [this](size_t i) { return rvRaw[i]->load(); };
    p.on = v(0) > 0.5f;
    p.algo = ambient::dsp::Reverb::Algo(juce::jlimit(0, 6, juce::roundToInt(v(1))));
    p.decay = v(2); p.predelayMs = v(3); p.size = v(4); p.diffusion = v(5); p.damping = v(6);
    p.early = v(7); p.modulation = v(8); p.feedback = v(9); p.shimmer = v(10); p.mix = v(11);
    p.freeze = v(12) > 0.5f; p.width = v(13);
    reverb.setParams(p);
}

void AmbientProcessor::readStereoParams()
{
    ambient::dsp::StereoSafety::Params p;
    auto v = [this](size_t i) { return outRaw[i]->load(); };
    p.width = v(0); p.bassMono = v(1) > 0.5f; p.bassMonoHz = v(2); p.safety = v(3) > 0.5f; p.floor = v(4); p.monoCheck = v(5) > 0.5f;
    stereoSafety.setParams(p);
}

void AmbientProcessor::readGranularParams()
{
    ambient::dsp::Granular::Params p;
    auto v = [this](size_t i) { return grRaw[i]->load(); };
    p.on = v(0) > 0.5f; p.sizeMs = v(1); p.density = v(2); p.positionMs = v(3); p.scatter = v(4);
    p.pitch = v(5); p.pitchRandom = v(6); p.reverse = v(7);
    p.window = ambient::dsp::Granular::Window(juce::jlimit(0, 4, juce::roundToInt(v(8))));
    p.spread = v(9); p.feedback = v(10); p.mix = v(11); p.freeze = v(12) > 0.5f;
    granular.setParams(p);
}

void AmbientProcessor::readSynthParams()
{
    using P = ambient::dsp::SynthEngine::Params;
    P p;
    auto v = [this](size_t i) { return oscRaw[i]->load(); };
    for (int i = 0; i < 2; ++i)
    {
        auto& o = i == 0 ? p.a : p.b;
        const size_t b = size_t(i) * 12;
        o.on = v(b + 0) > 0.5f;
        o.wave = ambient::dsp::Wave(juce::jlimit(0, 4, juce::roundToInt(v(b + 1))));
        o.octave = juce::roundToInt(v(b + 2));
        o.semi = juce::roundToInt(v(b + 3));
        o.fine = v(b + 4);
        o.pw = v(b + 5);
        o.phase = v(b + 7) > 0.5f ? -1.0f : v(b + 6); // free-running = negative start phase
        o.level = v(b + 8);
        o.unison = juce::roundToInt(v(b + 9));
        o.detune = v(b + 10);
        o.spread = v(b + 11);
    }
    p.fm = v(24); p.pm = v(25);
    p.subOn = v(26) > 0.5f; p.subWave = juce::roundToInt(v(27)); p.subOctave = juce::roundToInt(v(28)) == 0 ? -1 : -2; p.subLevel = v(29);
    p.noiseOn = v(30) > 0.5f; p.noiseType = juce::roundToInt(v(31)); p.noiseLevel = v(32);
    p.drift = v(33);
    p.satMode = juce::roundToInt(v(34)); p.satDrive = v(35); p.satMix = v(36);
    p.velocitySens = v(37);
    p.filterOn = v(38) > 0.5f; p.filterMode = juce::roundToInt(v(39)); p.cutoff = v(40); p.resonance = v(41);
    p.filterDrive = v(42); p.filterEnv = v(43); p.keytrack = v(44); p.velToCutoff = v(45);
    p.ampEnv = { v(46), v(47), v(48), v(49) };
    p.modEnv = { v(50), v(51), v(52), v(53) };
    p.modEnvPitch = v(54);
    synth.setParams(p);
}

bool AmbientProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (layouts.inputBuses.size() > 0)
    {
        const auto& main = layouts.inputBuses[0];
       #if AMBIENT_IS_INSTRUMENT
        if (! main.isDisabled() && main != juce::AudioChannelSet::stereo()) return false;
       #else
        if (main != juce::AudioChannelSet::stereo() && main != juce::AudioChannelSet::mono()) return false;
       #endif
    }
    if (layouts.inputBuses.size() > 1)
    {
        const auto& sc = layouts.inputBuses[1]; // sidechain: off, mono or stereo
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void AmbientProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synthBuffer.setSize(2, samplesPerBlock, false, true, false);
    loadMeasurer.reset(sampleRate, samplesPerBlock);
    currentSampleRate.store(sampleRate);
    synth.prepare(sampleRate, samplesPerBlock);
    granular.prepare(sampleRate, samplesPerBlock); // 10 s stereo buffer, allocated here only
    readReverbParams();
    reverb.prepare(sampleRate, samplesPerBlock);   // delay lines sized for size x2 at this rate, allocated here only
    readStereoParams();
    stereoSafety.prepare(sampleRate);
    for (auto* s : { &masterGain, &inputGain, &sourceMix }) s->prepare(sampleRate, 20.0f);
    masterGain.reset(juce::Decibels::decibelsToGain(pMasterGain->load(), -60.0f));
    inputGain.reset (juce::Decibels::decibelsToGain(pInputGain->load(),  -60.0f));
    sourceMix.reset (pSourceMix->load());
    for (auto& d : dc) d.prepare(sampleRate);

    scGain.prepare(sampleRate, 20.0f);
    scGain.reset(juce::Decibels::decibelsToGain(scParams.inputGain->load()));
    scratch.setSize(4, samplesPerBlock, false, true, false); // refL, refR, intL, intR
    ducker.prepare(sampleRate, samplesPerBlock);
    setLatencySamples(ambient::dsp::SpectralDucker::getLatencySamples()); // 0: IIR-only audio path
}

void AmbientProcessor::readSidechainParams()
{
    using P = ambient::dsp::SpectralDucker::Params;
    P p;
    p.enabled = scParams.enable->load() > 0.5f;
    p.sensitivity = scParams.sensitivity->load();
    p.thresholdOffset = scParams.threshold->load();
    p.depth = scParams.depth->load();
    p.mix = scParams.mix->load();
    p.automatic = scParams.automatic->load() > 0.5f;
    p.fullBand = scParams.fullBand->load() > 0.5f;
    p.rangeLow = scParams.rangeLow->load();
    p.rangeHigh = juce::jmax(p.rangeLow * 1.5f, scParams.rangeHigh->load());
    p.listen = scParams.listen->load() > 0.5f;
    p.delta = scParams.delta->load() > 0.5f;
    for (int b = 0; b < ids::sc::kBands; ++b)
    {
        auto& bp = p.bands[size_t(b)];
        const auto& src = scParams.band[size_t(b)];
        bp.enabled = src[0]->load() > 0.5f;
        bp.freq = src[1]->load();
        bp.q = src[2]->load();
        bp.threshold = src[3]->load();
        bp.maxReduction = src[4]->load();
        bp.attackMs = src[5]->load();
        bp.releaseMs = src[6]->load();
    }
    ducker.setParams(p);
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
    juce::AudioProcessLoadMeasurer::ScopedTimer loadTimer(loadMeasurer, buffer.getNumSamples());

    const int n = buffer.getNumSamples();
    if (n > synthBuffer.getNumSamples()) // host exceeded announced block size: refuse to allocate
    {
        buffer.clear();
        return;
    }

    // ---- 1. Copy inputs out first: input and output channels can alias. -------
    float* refL = scratch.getWritePointer(0);
    float* refR = scratch.getWritePointer(1);
    float* intL = scratch.getWritePointer(2);
    float* intR = scratch.getWritePointer(3);

    // Sidechain reference (aux bus 1): mono or stereo, with its own input gain.
    // Never assumes stereo or enabled; checks the channels really exist in this buffer.
    bool hasRef = false;
    const bool scEnabled = getBusCount(true) > 1 && getBus(true, 1)->isEnabled();
    int scChannels = 0;
    float scPk = 0.0f; double scSq = 0.0;
    if (scEnabled)
    {
        const int offset = getChannelIndexInProcessBlockBuffer(true, 1, 0);
        scChannels = juce::jlimit(0, juce::jmax(0, buffer.getNumChannels() - offset), getBus(true, 1)->getNumberOfChannels());
        if (scChannels >= 1)
        {
            auto sc = getBusBuffer(buffer, true, 1);
            scGain.setTarget(juce::Decibels::decibelsToGain(scParams.inputGain->load()));
            const float* a = sc.getReadPointer(0);
            const float* b = sc.getReadPointer(juce::jmin(1, scChannels - 1));
            for (int i = 0; i < n; ++i)
            {
                scPk = std::max(scPk, std::max(std::abs(a[i]), std::abs(b[i])));
                scSq += 0.5 * (double(a[i]) * a[i] + double(b[i]) * b[i]);
                const float g = scGain.next(); refL[i] = a[i] * g; refR[i] = b[i] * g;
            }
            hasRef = true;
        }
    }
    scDiag.busEnabled.store(scEnabled, std::memory_order_relaxed);
    scDiag.channels.store(scChannels, std::memory_order_relaxed);
    scDiag.bufferChannels.store(buffer.getNumChannels(), std::memory_order_relaxed);
    scDiag.peak.store(std::max(scPk, scDiag.peak.load(std::memory_order_relaxed) * 0.95f), std::memory_order_relaxed);
    scDiag.rms.store(float(std::sqrt(scSq / double(juce::jmax(1, n)))), std::memory_order_relaxed);
    scDiag.blocks.fetch_add(1, std::memory_order_relaxed);
    if (scPk > 0.0f) scDiag.nonZeroBlocks.fetch_add(1, std::memory_order_relaxed);

    // FX: the track input is the internal signal (bus 0). Instrument: no main input.
    bool hasMain = false;
   #if ! AMBIENT_IS_INSTRUMENT
    if (getBusCount(true) > 0 && getBus(true, 0)->isEnabled())
    {
        auto in = getBusBuffer(buffer, true, 0);
        if (in.getNumChannels() >= 1)
        {
            inputGain.setTarget(juce::Decibels::decibelsToGain(pInputGain->load(), -60.0f));
            const float* a = in.getReadPointer(0);
            const float* b = in.getReadPointer(juce::jmin(1, in.getNumChannels() - 1)); // mono track -> both sides
            for (int i = 0; i < n; ++i) { const float g = inputGain.next(); intL[i] = a[i] * g; intR[i] = b[i] * g; }
            hasMain = true;
        }
    }
   #endif
    if (! hasMain) { juce::FloatVectorOperations::clear(intL, n); juce::FloatVectorOperations::clear(intR, n); }

    // ---- 2. Synth: sample-accurate MIDI ---------------------------------------
    float* s = synthBuffer.getWritePointer(0);
    float* sR = synthBuffer.getWritePointer(1);
    juce::FloatVectorOperations::clear(s, n);
    juce::FloatVectorOperations::clear(sR, n);
    readSynthParams();
    int pos = 0;
    if (const int nm = midi.getNumEvents(); nm > 0)
        midiEventCount.fetch_add(uint32_t(nm), std::memory_order_relaxed);
    for (const auto meta : midi)
    {
        const int at = juce::jlimit(0, n, meta.samplePosition);
        synth.render(s, sR, pos, at);
        pos = at;
        handleMidi(meta.getMessage());
    }
    synth.render(s, sR, pos, n);
    const bool synthOn = pBypassSynth->load() < 0.5f && AMBIENT_IS_INSTRUMENT;
    if (synthOn)
    {
        juce::FloatVectorOperations::add(intL, s, n);
        juce::FloatVectorOperations::add(intR, sR, n);
    }

    // ---- 3. Processing chain (fixed order until the Stage 9 router): granular -> reverb ----
    readGranularParams();
    granular.process(intL, intR, n);
    readReverbParams();
    reverb.process(intL, intR, n);

    // ---- 4. Frequency sidechain: attenuates the INTERNAL signal only -----------
    readSidechainParams();
    ducker.process(intL, intR, hasRef ? refL : nullptr, hasRef ? refR : nullptr, n);

    // ---- 5. Stereo safety (bass mono, width, correlation floor), zero latency ----
    readStereoParams();
    stereoSafety.process(intL, intR, n);

    // ---- 6. Output -------------------------------------------------------------
    masterGain.setTarget(juce::Decibels::decibelsToGain(pMasterGain->load(), -60.0f));
    float peak = 0.0f, pkL = 0.0f, pkR = 0.0f, pkIn = 0.0f;
    float* L = buffer.getWritePointer(0);
    float* R = buffer.getWritePointer(1);
    for (int i = 0; i < n; ++i)
    {
        const float mg = masterGain.next();
        float l = softClip(dc[0].process(intL[i] * mg));
        float r = softClip(dc[1].process(intR[i] * mg));
        L[i] = l; R[i] = r;
        pkL = std::max(pkL, std::abs(l));
        pkR = std::max(pkR, std::abs(r));
        if (hasRef) pkIn = std::max(pkIn, std::max(std::abs(refL[i]), std::abs(refR[i])));
    }
    peak = std::max(pkL, pkR);

    if (hasRef && editorOpen.load(std::memory_order_relaxed) && n <= refRing.getCapacity() / 4)
        refRing.write(refL, refR, n);

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear(ch, 0, n);
    if (sanitize(L, size_t(n)) | sanitize(R, size_t(n))) for (auto& d : dc) d = {};
    peakL.store(pkL, std::memory_order_relaxed);
    peakR.store(pkR, std::memory_order_relaxed);
    inputPeak.store(pkIn, std::memory_order_relaxed);
    if (editorOpen.load(std::memory_order_relaxed) && n <= captureRing.getCapacity() / 4)
        captureRing.write(L, R, n); // memcpy-like, no allocation
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
