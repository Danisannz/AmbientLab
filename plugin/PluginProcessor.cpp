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
    // Instrument: the sidechain is the only input bus (reported to VST3 hosts as kAux).
    return juce::AudioProcessor::BusesProperties()
        .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true);
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
    // modulation destinations first: every PRef to a destination reads modValue[k]
    for (int k = 0; k < ambient::moddest::kNumDestinations; ++k)
    {
        const auto did = ambient::moddest::kDestinations[size_t(k)];
        const juce::String sid(did.data(), did.size());
        destRaw[size_t(k)] = apvts.getRawParameterValue(sid);
        destParam[size_t(k)] = apvts.getParameter(sid);
        jassert(destRaw[size_t(k)] != nullptr && destParam[size_t(k)] != nullptr);
        modValue[size_t(k)] = destRaw[size_t(k)] ? destRaw[size_t(k)]->load() : 0.0f;
    }
    auto rawId = [this](const juce::String& sid)
    {
        PRef r { apvts.getRawParameterValue(sid), nullptr };
        for (int k = 0; k < ambient::moddest::kNumDestinations; ++k)
        {
            const auto d = ambient::moddest::kDestinations[size_t(k)];
            if (sid == juce::String(d.data(), d.size())) { r.mod = &modValue[size_t(k)]; break; }
        }
        return r;
    };
    auto raw = [&rawId](std::string_view i) { return rawId(id(i)); };
    pMasterGain  = raw(ids::masterGain);
    pInputGain   = raw(ids::inputGain);
    pSourceMix   = apvts.getRawParameterValue(id(ids::sourceMix));
    pBypassSynth = apvts.getRawParameterValue(id(ids::bypassSynth));

    for (int l = 0; l < 4; ++l)
    {
        const juce::String pre = "lfo." + juce::String(l + 1) + ".";
        for (auto f : { "wave", "rate", "sync", "division", "phase", "shape", "smooth", "unipolar", "retrig" })
            lfoRaw[size_t(l)].push_back(rawId(pre + f));
        for (int k = 1; k <= 8; ++k) lfoRaw[size_t(l)].push_back(rawId(pre + "pt" + juce::String(k)));
    }
    for (int m = 1; m <= ambient::dsp::ModMatrix::kSlots; ++m)
        for (auto f : { "src", "dst", "amt" })
            mmRaw.push_back(apvts.getRawParameterValue("mm." + juce::String(m) + "." + f));
    for (size_t i = 0; i < 8; ++i) macroRaw[i] = apvts.getRawParameterValue(id(ids::macros[i]));
    for (auto f : { "on", "clock", "bpm", "length", "division", "swing", "direction", "root", "scale", "octave", "transpose", "gate", "patternStart" })
        seqRaw.push_back(rawId("seq." + juce::String(f)));
    for (int l = 0; l < 4; ++l) seqLockDst[size_t(l)] = apvts.getRawParameterValue("seq.lock" + juce::String(l + 1));
    seqModel.loadPresetPattern(0);   // default pattern (sequencer itself is off by default)
    publishPattern();
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
            oscRaw.push_back(rawId(juce::String(o::oscId(i, o::OscField(f)))));
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
    namespace did = ids::dl;
    for (auto g : { did::on, did::mode, did::time, did::sync, did::division, did::feedback, did::hp, did::lp,
                    did::modDepth, did::modRate, did::drive, did::width, did::pitch, did::mix })
        dlRaw.push_back(raw(g));
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

void AmbientProcessor::readDelayParams()
{
    using D = ambient::dsp::Delay;
    D::Params p;
    auto v = [this](size_t i) { return dlRaw[i]->load(); };
    p.on = v(0) > 0.5f;
    p.mode = D::Mode(juce::jlimit(0, D::kNumModes - 1, juce::roundToInt(v(1))));
    p.timeMs = v(3) > 0.5f ? D::syncedMs(juce::roundToInt(v(4)), hostBpm) : v(2);
    p.feedback = v(5); p.hpHz = v(6); p.lpHz = v(7); p.modDepth = v(8); p.modRate = v(9);
    p.drive = v(10); p.width = v(11); p.pitch = v(12); p.mix = v(13);
    delay.setParams(p);
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
    if (kMainBus >= 0 && layouts.inputBuses.size() > kMainBus)
    {
        const auto& main = layouts.inputBuses[kMainBus];   // FX track input: mono or stereo
        if (main != juce::AudioChannelSet::stereo() && main != juce::AudioChannelSet::mono()) return false;
    }
    if (layouts.inputBuses.size() > kScBus)
    {
        const auto& sc = layouts.inputBuses[kScBus];       // sidechain: off, mono or stereo
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
    readDelayParams();
    delay.prepare(sampleRate, samplesPerBlock);    // 8 s stereo buffer, allocated here only
    for (auto& l : lfos) l.prepare(sampleRate);
    sequencer.prepare(sampleRate);
    seqPattern.update();
    for (int i = 0; i < ambient::dsp::Sequencer::kMaxSteps; ++i) sequencer.setStep(i, seqPattern.front()[size_t(i)]);
    seqNotesOn = 0;
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
    if (m.isNoteOn())
    {
        synth.noteOn(m.getNoteNumber(), m.getFloatVelocity());
        midiVelocity = m.getFloatVelocity();
        midiKey = juce::jlimit(-1.0f, 1.0f, float(m.getNoteNumber() - 60) / 48.0f);
        noteRng = noteRng * 1664525u + 1013904223u;
        midiRandom = float(int32_t(noteRng >> 8) - (1 << 23)) / float(1 << 23);
        for (auto& l : lfos) l.retrigger();
    }
    else if (m.isNoteOff())                            synth.noteOff(m.getNoteNumber());
    else if (m.isAllNotesOff() || m.isAllSoundOff())   synth.allNotesOff();
    else if (m.isController() && m.getControllerNumber() == 1) midiWheel = float(m.getControllerValue()) / 127.0f;
    else if (m.isChannelPressure())                    midiAftertouch = float(m.getChannelPressureValue()) / 127.0f;
    else if (m.isAftertouch())                         midiAftertouch = float(m.getAfterTouchValue()) / 127.0f;
    else if (m.isPitchWheel())                         midiBend = juce::jlimit(-1.0f, 1.0f, float(m.getPitchWheelValue() - 8192) / 8191.0f);
}

void AmbientProcessor::publishPattern() noexcept
{
    auto& b = seqPattern.back();
    for (int i = 0; i < ambient::dsp::Sequencer::kMaxSteps; ++i) b[size_t(i)] = seqModel.getStep(i);
    seqPattern.publish();
}

void AmbientProcessor::loadSeqPreset(int index)
{
    const auto info = seqModel.loadPresetPattern(index);
    publishPattern();
    auto set = [this](const juce::String& pid, float plain) { if (auto* p = apvts.getParameter(pid)) p->setValueNotifyingHost(p->convertTo0to1(plain)); };
    set("seq.length", float(info.length));
    set("seq.division", float(info.division));
    set("seq.swing", info.swing);
    set("seq.direction", float(int(info.direction)));
    if (info.scale >= 0) set("seq.scale", float(info.scale));
}

void AmbientProcessor::randomiseSeq(float amount)
{
    seqModel.randomise(juce::Random::getSystemRandom().nextInt() | 1u, amount);
    publishPattern();
}

juce::ValueTree AmbientProcessor::patternToTree() const
{
    juce::ValueTree t("SEQPATTERN");
    for (int i = 0; i < ambient::dsp::Sequencer::kMaxSteps; ++i)
    {
        const auto& st = seqModel.getStep(i);
        juce::ValueTree c("STEP");
        c.setProperty("i", i, nullptr);
        c.setProperty("on", st.active, nullptr);   c.setProperty("note", st.note, nullptr);
        c.setProperty("vel", st.velocity, nullptr); c.setProperty("gate", st.gate, nullptr);
        c.setProperty("prob", st.probability, nullptr); c.setProperty("rat", st.ratchet, nullptr);
        c.setProperty("skip", st.skip, nullptr);
        for (int l = 0; l < 4; ++l)
            if (! std::isnan(st.locks[size_t(l)])) c.setProperty("l" + juce::String(l), st.locks[size_t(l)], nullptr);
        t.appendChild(c, nullptr);
    }
    return t;
}

void AmbientProcessor::patternFromTree(const juce::ValueTree& t)
{
    if (! t.isValid()) return;
    for (const auto& c : t)
    {
        const int i = c.getProperty("i", -1);
        if (i < 0 || i >= ambient::dsp::Sequencer::kMaxSteps) continue;
        ambient::dsp::Sequencer::Step st = seqModel.getStep(i);
        st.active = c.getProperty("on", st.active);       st.note = c.getProperty("note", st.note);
        st.velocity = c.getProperty("vel", st.velocity);  st.gate = c.getProperty("gate", st.gate);
        st.probability = c.getProperty("prob", st.probability); st.ratchet = c.getProperty("rat", st.ratchet);
        st.skip = c.getProperty("skip", st.skip);
        for (int l = 0; l < 4; ++l)
        {
            const auto key = "l" + juce::String(l);
            st.locks[size_t(l)] = c.hasProperty(key) ? float(c.getProperty(key)) : std::numeric_limits<float>::quiet_NaN();
        }
        seqModel.setStep(i, st);
    }
    publishPattern();
}

void AmbientProcessor::readSequencerParams() noexcept
{
    using S = ambient::dsp::Sequencer;
    S::Params p;
    auto v = [this](size_t i) { return seqRaw[i].load(); };
    auto rawOf = [this](size_t i) { return seqRaw[i].raw->load(std::memory_order_relaxed); };
    p.length = juce::jlimit(1, 32, juce::roundToInt(v(3)));
    p.division = juce::roundToInt(v(4));
    p.swing = v(5);
    p.direction = S::Direction(juce::jlimit(0, 3, juce::roundToInt(v(6))));
    // root / scale: the raw value is the base, matrix modulation becomes the (step-latched) offset
    p.root = juce::roundToInt(rawOf(7));   p.rootMod = v(7) - rawOf(7);
    p.scale = juce::roundToInt(rawOf(8));  p.scaleMod = v(8) - rawOf(8);
    p.baseNote = 24 + 12 * juce::roundToInt(v(9));
    p.transpose = juce::roundToInt(v(10));
    p.gateScale = v(11);
    p.changeOnPatternStartOnly = v(12) > 0.5f;
    sequencer.setParams(p);
}

void AmbientProcessor::updateModulation(int startSample, int numSamples) noexcept
{
    using namespace ambient;
    // 1. LFOs (their own rate/shape may be modulated: read through PRef = previous sub-block's value)
    const double sr = getSampleRate() > 0 ? getSampleRate() : 48000.0;
    dsp::Lfo::Transport t;
    t.playing = transportPlaying; t.bpm = hostBpm;
    t.ppq = transportPpq + double(startSample) / sr * hostBpm / 60.0;
    for (size_t l = 0; l < 4; ++l)
    {
        const auto& r = lfoRaw[l];
        dsp::Lfo::Params p;
        p.wave = dsp::Lfo::Wave(juce::jlimit(0, dsp::Lfo::kNumWaves - 1, juce::roundToInt(r[0].load())));
        p.rateHz = r[1].load(); p.sync = r[2].load() > 0.5f; p.division = juce::roundToInt(r[3].load());
        p.phase = r[4].load(); p.shape = r[5].load(); p.smooth = r[6].load();
        p.unipolar = r[7].load() > 0.5f; p.retrigger = r[8].load() > 0.5f;
        for (size_t k = 0; k < 8; ++k) p.custom[k] = r[9 + k].load();
        lfos[l].setParams(p);
        srcValue[size_t(modsrc::Lfo1) + l] = lfos[l].advance(numSamples, t);
    }
    // 2. Other sources
    for (size_t i = 0; i < 8; ++i) srcValue[size_t(modsrc::Macro1) + i] = macroRaw[i]->load(std::memory_order_relaxed);
    srcValue[modsrc::Velocity] = midiVelocity;  srcValue[modsrc::ModWheel] = midiWheel;
    srcValue[modsrc::Aftertouch] = midiAftertouch; srcValue[modsrc::PitchBend] = midiBend;
    srcValue[modsrc::NoteKey] = midiKey;        srcValue[modsrc::NoteRandom] = midiRandom;
    for (int i = 0; i < 7; ++i) srcValue[size_t(modsrc::ScEnvelope + i)] = ducker.getModOutput(i);   // previous sub-block
    srcValue[modsrc::SeqGate] = seqNotesOn > 0 ? 1.0f : 0.0f;
    srcValue[modsrc::SeqVelocity] = seqLastVelocity;
    srcValue[modsrc::SeqStep] = sequencer.currentStep() >= 0 ? float(sequencer.currentStep()) / float(juce::jmax(1, sequencer.getParams().length - 1)) : 0.0f;
    srcValue[modsrc::None] = 0.0f;

    // 3. Matrix
    bool any = false;
    for (int m = 0; m < dsp::ModMatrix::kSlots; ++m)
    {
        auto& sl = slots[size_t(m)];
        sl.source = juce::roundToInt(mmRaw[size_t(m) * 3]->load(std::memory_order_relaxed));
        sl.destination = juce::roundToInt(mmRaw[size_t(m) * 3 + 1]->load(std::memory_order_relaxed));
        sl.amount = mmRaw[size_t(m) * 3 + 2]->load(std::memory_order_relaxed);
        any = any || (sl.source > 0 && sl.destination > 0 && sl.amount != 0.0f);
    }
    if (any) dsp::ModMatrix::accumulate(slots, srcValue.data(), modsrc::kCount, modOffset.data(), moddest::kNumDestinations);
    else modOffset.fill(0.0f);

    // 4. Modulated destination values (normalised offset, clamped to the parameter range)
    for (size_t k = 0; k < size_t(moddest::kNumDestinations); ++k)
    {
        const float v = destRaw[k]->load(std::memory_order_relaxed);
        const float off = modOffset[k + 1];
        if (off == 0.0f) { modValue[k] = v; continue; }
        auto* prm = destParam[k];
        modValue[k] = prm->convertFrom0to1(juce::jlimit(0.0f, 1.0f, prm->convertTo0to1(v) + off));
    }
    // 5. Sequencer parameter locks: a locked step SETS its lane's target (normalised 0..1)
    if (seqRaw[0].load() > 0.5f)
        for (int l = 0; l < 4; ++l)
        {
            const int d = juce::roundToInt(seqLockDst[size_t(l)]->load(std::memory_order_relaxed));
            const float lv = sequencer.lockValue(l);
            if (d > 0 && d <= moddest::kNumDestinations && ! std::isnan(lv))
                modValue[size_t(d - 1)] = destParam[size_t(d - 1)]->convertFrom0to1(juce::jlimit(0.0f, 1.0f, lv));
        }
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
    const bool scEnabled = getBusCount(true) > kScBus && getBus(true, kScBus)->isEnabled();
    int scChannels = 0;
    float scPk = 0.0f; double scSq = 0.0;
    if (scEnabled)
    {
        const int offset = getChannelIndexInProcessBlockBuffer(true, kScBus, 0);
        scChannels = juce::jlimit(0, juce::jmax(0, buffer.getNumChannels() - offset), getBus(true, kScBus)->getNumberOfChannels());
        if (scChannels >= 1)
        {
            auto sc = getBusBuffer(buffer, true, kScBus);
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
    if (kMainBus >= 0 && getBusCount(true) > kMainBus && getBus(true, kMainBus)->isEnabled())
    {
        auto in = getBusBuffer(buffer, true, kMainBus);
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

    // newest step pattern from the editor (lock-free triple buffer)
    if (seqPattern.update())
        for (int i = 0; i < ambient::dsp::Sequencer::kMaxSteps; ++i) sequencer.setStep(i, seqPattern.front()[size_t(i)]);

    // ---- 2. Transport (tempo, position) -----------------------------------------
    transportPlaying = false;
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
        {
            if (const auto bpm = pos->getBpm(); bpm.hasValue() && *bpm > 1.0) hostBpm = *bpm;   // fallback 120 BPM
            transportPlaying = pos->getIsPlaying();
            if (const auto ppq = pos->getPpqPosition(); ppq.hasValue()) transportPpq = *ppq;
        }

    // ---- 3. Control-rate loop: modulation -> synth (sample-accurate MIDI) -> chain ----
    // Chain order (fixed until the Stage 9 router): synth/track -> granular -> delay -> reverb
    //                                              -> frequency sidechain -> stereo safety.
    float* s = synthBuffer.getWritePointer(0);
    float* sR = synthBuffer.getWritePointer(1);
    juce::FloatVectorOperations::clear(s, n);
    juce::FloatVectorOperations::clear(sR, n);
    if (const int nm = midi.getNumEvents(); nm > 0)
        midiEventCount.fetch_add(uint32_t(nm), std::memory_order_relaxed);
    const bool synthOn = pBypassSynth->load() < 0.5f && AMBIENT_IS_INSTRUMENT;
    auto ev = midi.cbegin();
    for (int start = 0; start < n; start += kControlBlock)
    {
        const int len = juce::jmin(kControlBlock, n - start), end = start + len;
        updateModulation(start, len);

        // sequencer: events for this sub-block (offsets relative to `start`)
        readSequencerParams();
        int seqN = 0;
        {
            ambient::dsp::Sequencer::Transport st;
            const bool on = seqRaw[0].load() > 0.5f, hostClock = seqRaw[1].load() < 0.5f;
            st.hostSync = hostClock;
            st.playing = on && (hostClock ? transportPlaying : true);
            st.bpm = hostClock ? hostBpm : double(seqRaw[2].load());
            st.ppqAtBlockStart = transportPpq + double(start) / getSampleRate() * hostBpm / 60.0;
            sequencer.process(len, st);
            seqN = sequencer.numEvents();
            seqCurrentStep.store(sequencer.currentStep(), std::memory_order_relaxed);
        }
        readSynthParams();
        int pos = start, si = 0;
        auto seqAt = [&](int i) { return start + sequencer.event(i).sampleOffset; };
        while ((ev != midi.cend() && (*ev).samplePosition < end) || si < seqN)
        {
            const bool takeMidi = ev != midi.cend() && (*ev).samplePosition < end && (si >= seqN || (*ev).samplePosition <= seqAt(si));
            const int at = juce::jlimit(pos, end, takeMidi ? (*ev).samplePosition : seqAt(si));
            synth.render(s, sR, pos, at);
            pos = at;
            if (takeMidi) { handleMidi((*ev).getMessage()); ++ev; }
            else
            {
                const auto& e = sequencer.event(si++);
                if (e.noteOn) { handleMidi(juce::MidiMessage::noteOn(1, e.note, e.velocity)); ++seqNotesOn; seqLastVelocity = e.velocity; seqNoteOnCount.fetch_add(1, std::memory_order_relaxed); }
                else { handleMidi(juce::MidiMessage::noteOff(1, e.note)); seqNotesOn = juce::jmax(0, seqNotesOn - 1); }
            }
        }
        synth.render(s, sR, pos, end);
        if (synthOn)
        {
            juce::FloatVectorOperations::add(intL + start, s + start, len);
            juce::FloatVectorOperations::add(intR + start, sR + start, len);
        }

        readGranularParams();  granular.process(intL + start, intR + start, len);
        readDelayParams();     delay.process(intL + start, intR + start, len);
        readReverbParams();    reverb.process(intL + start, intR + start, len);
        readSidechainParams();
        ducker.process(intL + start, intR + start, hasRef ? refL + start : nullptr, hasRef ? refR + start : nullptr, len);
        readStereoParams();    stereoSafety.process(intL + start, intR + start, len);
    }
    for (; ev != midi.cend(); ++ev) handleMidi((*ev).getMessage());   // events past the block end

    // UI telemetry (lock-free)
    for (size_t l = 0; l < 4; ++l) { lfoOut[l].store(lfos[l].value(), std::memory_order_relaxed); lfoPhaseOut[l].store(lfos[l].currentPhase(), std::memory_order_relaxed); }
    for (size_t i = 0; i < size_t(ambient::modsrc::kCount); ++i) sourceOut[i].store(srcValue[i], std::memory_order_relaxed);
    for (size_t m = 0; m < slots.size(); ++m)
        slotOut[m].store(slots[m].source > 0 && slots[m].destination > 0 ? slots[m].amount * srcValue[size_t(juce::jlimit(0, ambient::modsrc::kCount - 1, slots[m].source))] : 0.0f,
                         std::memory_order_relaxed);

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
    state.removeChild(state.getChildWithName("SEQPATTERN"), nullptr);
    state.appendChild(patternToTree(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void AmbientProcessor::setStateInformation(const void* data, int size)
{
    auto xml = getXmlFromBinary(data, size);
    if (! xml || ! xml->hasTagName(apvts.state.getType())) return;
    auto tree = juce::ValueTree::fromXml(*xml);
    migrateState(tree); // newer-version states still load known params
    patternFromTree(tree.getChildWithName("SEQPATTERN"));
    apvts.replaceState(tree);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AmbientProcessor(); }
