#pragma once
// Step sequencer (Stage 8) - core engine, no plugin dependency.
//
// Timing is a pure function of the musical position: slot s starts at
//   t(s) = s * stepBeats + (s odd ? swing * 0.5 * stepBeats : 0)    [beats]
// and plays the step order[s % cycle] (Random: a seeded hash of s). So host sync is
// sample-accurate across blocks, survives tempo changes, and a transport jump/loop
// simply re-derives the slot. Probability and Random are hashed from (seed, slot):
// deterministic for a given seed and position.
//
// Real-time safe: process() never allocates or locks; all storage is fixed-size.
// Every note-on is matched by a note-off (gate end, stop, reset, transport jump).
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace ambient::dsp
{
class Sequencer
{
public:
    static constexpr int kMaxSteps = 32;
    static constexpr int kLockLanes = 4;
    static constexpr int kMaxEvents = 256;
    static constexpr int kNumPresets = 8;

    enum class Direction { Forward = 0, Reverse, PingPong, Random };

    struct Step
    {
        bool  active = true;
        int   note = 0;            // semitones -24..+24 relative to base note + root
        float velocity = 0.8f;     // 0..1
        float gate = 0.5f;         // 0.05..1 of the step (of each ratchet)
        float probability = 1.0f;  // 0..1
        int   ratchet = 1;         // 1..4
        bool  skip = false;        // skipped steps consume no time
        std::array<float, kLockLanes> locks { nanv(), nanv(), nanv(), nanv() };
        static constexpr float nanv() noexcept { return std::numeric_limits<float>::quiet_NaN(); }
    };

    struct Params
    {
        int   length = 16;         // 1..32
        int   division = 3;        // Delay::divisionBeats index (3 = 1/16)
        float swing = 0.0f;        // 0..0.75
        Direction direction = Direction::Forward;
        int   root = 0;            // 0..11 (C..B)
        int   scale = 0;           // 0..15 (see Scales.h)
        int   baseNote = 48;       // C3
        int   transpose = 0;       // semitones
        float gateScale = 1.0f;    // 0..2, multiplies every step gate (capped at the step end)
        float scaleMod = 0.0f;     // modulation offset in scale-index units (rounded)
        float rootMod = 0.0f;      // modulation offset in semitones (rounded)
        bool  changeOnPatternStartOnly = false; // scale/root changes wait for the pattern start
        uint32_t seed = 1;

        /** Random mode: Generate() builds new base patterns; when `on`, every pattern cycle also
            plays a deterministic variation of `amount` of the steps (the stored pattern is kept). */
        struct Random
        {
            bool  on = false;
            float amount = 0.3f;        // share of steps changed (Generate and live variation)
            float density = 0.7f;       // share of active steps (when randomising steps)
            int   range = 7;            // note range in scale degrees above the root (2..21), half of it below
            bool  chromatic = false;    // notes may leave the scale (playback quantisation off)
            float velocity = 0.3f;      // velocity randomisation 0..1
            float gate = 0.3f;          // gate-length randomisation 0..1
            bool  steps = true;         // randomise which steps are active
            bool  lockNotes = false, lockVelocity = false, lockGate = false, lockSteps = false;
            uint32_t seed = 1;          // variation / generation seed
        } random;

        /** Dedicated sequencer LFO, evaluated at musical positions (never at audio rate):
            per step for notes / evolution, per bar for speed. */
        struct SeqLfo
        {
            enum class Wave { Sine = 0, Triangle, SawUp, SawDown, Square, SampleHold };
            enum class Dest { Notes = 0, Speed, Evolution, NotesAndEvolution };
            Wave  wave = Wave::Sine;
            Dest  dest = Dest::Notes;
            float depth = 0.0f;         // -1..1 (0 = off)
            bool  sync = true;
            int   division = 13;        // Delay::divisionBeats index (13 = 1/1 = one bar)
            float rateHz = 0.25f;       // free-running rate
            float phase = 0.0f;         // 0..1
            bool  retrigger = false;    // restart at every pattern cycle
            float smooth = 0.0f;        // softens square / saw edges, interpolates S&H
            bool  smoothSpeed = false;  // internal clock only: continuous speed instead of x1/2, x1, x2 per bar
        } lfo;
    };

    struct Transport
    {
        bool playing = false;
        bool hostSync = true;          // true: use ppqAtBlockStart; false: internal clock
        double ppqAtBlockStart = 0.0;
        double bpm = 120.0;
    };

    struct Event { int sampleOffset; int note; float velocity; bool noteOn; };

    /** Suggested globals of a preset pattern; scale = -1 means 'keep the current scale'. */
    struct PresetInfo { const char* name; int length; int division; float swing; Direction direction; int scale; };

    void prepare(double sampleRate) noexcept;
    /** Stops all notes on the next process() call (note-offs at offset 0) and resets the clock. */
    void reset() noexcept;

    void setParams(const Params& p) noexcept { params = p; }
    const Params& getParams() const noexcept { return params; }
    void setStep(int index, const Step& s) noexcept { if (index >= 0 && index < kMaxSteps) steps[size_t(index)] = s; }
    const Step& getStep(int index) const noexcept { return steps[size_t(index < 0 ? 0 : (index >= kMaxSteps ? kMaxSteps - 1 : index))]; }

    /** Generates the events of the next numSamples samples (sorted by offset; note-off first on ties). */
    void process(int numSamples, const Transport& t) noexcept;
    int numEvents() const noexcept { return eventCount; }
    const Event& event(int i) const noexcept { return events[size_t(i)]; }

    /** Step of the slot currently playing (-1 when stopped / none). */
    int currentStep() const noexcept { return curStep; }
    /** Parameter lock of the current step for a lane, NaN if none / stopped. */
    float lockValue(int lane) const noexcept;
    /** Scale and root currently latched (after modulation). */
    int currentScale() const noexcept { return latchedScale; }
    int currentRoot() const noexcept { return latchedRoot; }

    /** Deterministic randomisation of the pattern; amount 0..1 = share of steps changed. */
    void randomise(uint32_t seed, float amount) noexcept;
    /** Random mode: builds a new musical base pattern from params.random (scale / root aware,
        respects the locks, changes `amount` of the steps). Message thread (edit model). */
    void generate(uint32_t seed) noexcept;
    /** Sequencer LFO value (-1..1) at a song position in beats (exposed for tests / display). */
    float seqLfoAt(double beats, double bpm, int64_t cycleIndex) const noexcept;
    /** Current speed multiplier from the sequencer LFO (1 = none). */
    double currentSpeed() const noexcept { return lastSpeed; }
    /** Loads a preset pattern into the steps; returns the suggested global settings. */
    PresetInfo loadPresetPattern(int index) noexcept;
    static const char* presetName(int index) noexcept;

    static constexpr int getLatencySamples() noexcept { return 0; }

private:
    struct Pending { double time; int note; float velocity; bool on; };

    double slotStart(int64_t s, double stepBeats) const noexcept;
    Step varied(const Step& base, int index, int64_t cycleIndex, double amount, int scale) const noexcept;
    static Step makeStep(const Step& base, int index, int prevSemis, uint64_t h, const Params::Random& r, int scale, int length) noexcept;
    double speedForBar(int64_t bar, double bpm) const noexcept;
    double virtualAt(double ppq, double bpm) noexcept;      // host clock: song position -> pattern position (bar-mapped speed)
    int buildOrder() noexcept;                 // fills order[], returns cycle length (0 = nothing playable)
    int stepForSlot(int64_t s, int cycle) const noexcept;
    void triggerSlot(int64_t s, double stepBeats, int cycle, double tOn, double dur, double bpm) noexcept;
    void flushAll(int offset) noexcept;
    void push(const Pending& p) noexcept;
    void emit(int offset, int note, float vel, bool on) noexcept;
    static uint64_t hash(uint64_t a, uint64_t b, uint64_t c) noexcept;

    Params params;
    std::array<Step, kMaxSteps> steps {};
    std::array<int, 2 * kMaxSteps> order {};
    std::array<Pending, 64> pending {};
    int pendingCount = 0;
    std::array<Event, kMaxEvents> events {};
    int eventCount = 0;
    std::array<uint8_t, 128> sounding {};

    double sr = 48000.0;
    bool wasPlaying = false, needResync = true, flushRequested = false;
    double expectedPpq = 0.0, internalPpq = 0.0;
    int64_t lastSlot = std::numeric_limits<int64_t>::min();
    int curStep = -1;
    int latchedScale = 0, latchedRoot = 0;
    double virtualPos = 0.0;                 // pattern position (beats at base speed)
    int64_t vCacheBar = 0; double vCacheV = 0.0; bool vCacheValid = false;
    double lastSpeed = 1.0;
    uint64_t speedSig = 0;
    bool latchValid = false;
};
} // namespace ambient::dsp
