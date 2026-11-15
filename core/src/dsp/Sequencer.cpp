#include <ambient/dsp/Sequencer.h>
#include <ambient/dsp/Scales.h>
#include <ambient/dsp/Delay.h>
#include <algorithm>

namespace ambient::dsp
{
namespace
{
inline int64_t floorDiv(double x) noexcept { return int64_t(std::floor(x)); }
inline int posMod(int64_t a, int m) noexcept { const int64_t r = a % m; return int(r < 0 ? r + m : r); }
inline double unit(uint64_t h) noexcept { return double(h >> 11) * (1.0 / 9007199254740992.0); }
} // namespace

uint64_t Sequencer::hash(uint64_t a, uint64_t b, uint64_t c) noexcept
{
    uint64_t x = a * 0x9E3779B97F4A7C15ull ^ (b + 0x632BE59BD9B4E019ull) * 0xBF58476D1CE4E5B9ull ^ (c + 0x94D049BB133111EBull);
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27; x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x;
}

void Sequencer::prepare(double sampleRate) noexcept
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    pendingCount = 0; eventCount = 0;
    sounding.fill(0);
    wasPlaying = false; needResync = true; flushRequested = false;
    internalPpq = expectedPpq = 0.0;
    curStep = -1; latchValid = false;
}

void Sequencer::reset() noexcept { flushRequested = true; }

float Sequencer::lockValue(int lane) const noexcept
{
    if (curStep < 0 || lane < 0 || lane >= kLockLanes) return Step::nanv();
    return steps[size_t(curStep)].locks[size_t(lane)];
}

double Sequencer::slotStart(int64_t s, double stepBeats) const noexcept
{
    const double swing = std::clamp(double(params.swing), 0.0, 0.75);
    return double(s) * stepBeats + ((s & 1) ? swing * 0.5 * stepBeats : 0.0);
}

int Sequencer::buildOrder() noexcept
{
    const int len = std::clamp(params.length, 1, kMaxSteps);
    std::array<int, kMaxSteps> a {};
    int n = 0;
    for (int i = 0; i < len; ++i) if (! steps[size_t(i)].skip) a[size_t(n++)] = i;
    if (n == 0) return 0;
    switch (params.direction)
    {
        case Direction::Reverse:
            for (int i = 0; i < n; ++i) order[size_t(i)] = a[size_t(n - 1 - i)];
            return n;
        case Direction::PingPong:
        {
            for (int i = 0; i < n; ++i) order[size_t(i)] = a[size_t(i)];
            if (n < 2) return n;
            int c = n;
            for (int i = n - 2; i >= 1; --i) order[size_t(c++)] = a[size_t(i)];
            return c;
        }
        case Direction::Forward:
        case Direction::Random:
        default:
            for (int i = 0; i < n; ++i) order[size_t(i)] = a[size_t(i)];
            return n;
    }
}

int Sequencer::stepForSlot(int64_t s, int cycle) const noexcept
{
    if (params.direction == Direction::Random)
        return order[size_t(hash(params.seed, uint64_t(s), 0x51u) % uint64_t(cycle))];
    return order[size_t(posMod(s, cycle))];
}

void Sequencer::push(const Pending& p) noexcept
{
    if (pendingCount < int(pending.size())) pending[size_t(pendingCount++)] = p;
}

void Sequencer::emit(int offset, int note, float vel, bool on) noexcept
{
    if (eventCount < kMaxEvents) events[size_t(eventCount++)] = Event { offset, note, vel, on };
}

void Sequencer::flushAll(int offset) noexcept
{
    for (int nn = 0; nn < 128; ++nn)
        if (sounding[size_t(nn)] > 0) { emit(offset, nn, 0.0f, false); sounding[size_t(nn)] = 0; }
    pendingCount = 0;
}

void Sequencer::triggerSlot(int64_t s, double stepBeats, int cycle, double tOn, double dur, double bpm) noexcept
{
    if (cycle <= 0) { curStep = -1; return; }
    const int idx = stepForSlot(s, cycle);
    curStep = idx;

    const bool patternStart = posMod(s, cycle) == 0;
    if (! latchValid || ! params.changeOnPatternStartOnly || patternStart)
    {
        latchedScale = scales::clampScale(params.scale + int(std::lround(params.scaleMod)));
        latchedRoot = scales::wrap12(params.root + int(std::lround(params.rootMod)));
        latchValid = true;
    }

    // sequencer LFO value at this step (song position, or position in the cycle when retriggered)
    const auto& L = params.lfo;
    const int64_t cycleIndex = int64_t(std::floor(double(s) / double(cycle)));
    float lfoV = 0.0f;
    if (L.depth != 0.0f)
    {
        const double beats = L.retrigger ? double(posMod(s, cycle)) * stepBeats : tOn;
        lfoV = seqLfoAt(beats, bpm, L.retrigger ? 0 : cycleIndex);
    }
    const bool lfoNotes = L.depth != 0.0f && (L.dest == Params::SeqLfo::Dest::Notes || L.dest == Params::SeqLfo::Dest::NotesAndEvolution);
    const bool lfoEvo = L.depth != 0.0f && (L.dest == Params::SeqLfo::Dest::Evolution || L.dest == Params::SeqLfo::Dest::NotesAndEvolution);

    // live variation (random mode and / or LFO evolution): deterministic per (seed, cycle, step)
    double amount = params.random.on ? double(std::clamp(params.random.amount, 0.0f, 1.0f)) : 0.0;
    if (lfoEvo) amount += double(std::fabs(L.depth)) * (0.5 + 0.5 * double(lfoV));
    amount = std::clamp(amount, 0.0, 1.0);
    const Step st = amount > 0.0 ? varied(steps[size_t(idx)], idx, cycleIndex, amount, latchedScale) : steps[size_t(idx)];

    if (! st.active) return;
    if (unit(hash(params.seed, uint64_t(s), 0x9E37u)) >= double(std::clamp(st.probability, 0.0f, 1.0f))) return;

    const int r = std::clamp(st.ratchet, 1, 4);
    const double sub = dur / double(r);
    const double g = std::clamp(double(st.gate) * double(std::clamp(params.gateScale, 0.0f, 2.0f)), 0.01, 1.0);
    const bool chromatic = params.random.on && params.random.chromatic;
    int semis = std::clamp(st.note, -24, 24);
    if (lfoNotes)
    {
        const int shift = int(std::lround(double(L.depth) * double(lfoV) * 7.0));   // up to +-7 degrees
        semis = chromatic ? semis + shift : scales::shiftDegrees(semis, latchedScale, shift);
        while (semis > 36) semis -= 12;                                              // keep the configured range
        while (semis < -24) semis += 12;
    }
    int note = params.baseNote + latchedRoot + params.transpose + semis;
    if (! chromatic) note = scales::quantize(note, latchedScale, latchedRoot);
    note = std::clamp(note, 0, 127);
    const float vel = std::clamp(st.velocity, 0.0f, 1.0f);
    for (int k = 0; k < r; ++k)
    {
        const double on = tOn + double(k) * sub;
        const double off = std::min(on + sub * g, on + sub - 1.0e-9);
        push({ on, note, vel, true });
        push({ off, note, 0.0f, false });
    }
}

// ---------------------------------------------------------------- random mode
Sequencer::Step Sequencer::makeStep(const Step& base, int i, int prevSemis, uint64_t h, const Params::Random& r, int scale, int length) noexcept
{
    Step st = base;
    auto u = [h](uint64_t k) { return unit(hash(h, k, 0x3C6EF372u)); };
    const bool strong = (i % 4) == 0, down = (i % 8) == 0;
    (void) length;
    if (r.steps && ! r.lockSteps)
    {
        const double p = double(std::clamp(r.density, 0.0f, 1.0f)) * (strong ? 1.35 : ((i % 2) == 0 ? 1.0 : 0.8));
        st.active = u(1) < std::clamp(p, 0.0, 1.0);
    }
    if (! r.lockNotes)
    {
        const int range = std::clamp(r.range, 2, 21);
        if (r.chromatic)
        {
            static constexpr int iv[] = { 0, 0, 1, -1, 2, -2, 3, -3, 5, -5, 7, -7, 12, -12 };
            int n = prevSemis + iv[size_t(hash(h, 2, 7) % 14u)];
            const int hi = range * 12 / 7, lo = -hi / 2;
            while (n > hi) n -= 12;
            while (n < lo) n += 12;
            st.note = std::clamp(n, -24, 24);
        }
        else
        {
            const int size = scales::kScales[size_t(scales::clampScale(scale))].size;
            int deg = scales::semisToDegree(prevSemis, scale);
            const double a = u(2);
            const int sign = u(3) < 0.5 ? -1 : 1;
            if (a < 0.22) {}                                   // repeat
            else if (a < 0.62) deg += sign;                    // step
            else if (a < 0.84) deg += 2 * sign;                // skip
            else if (a < 0.95) deg += (3 + int(u(8) * 2.0)) * sign;
            else deg += size * sign;                           // octave
            if (down && u(4) < 0.5)                            // strong beats lean on the root / fifth
            {
                const int target = u(5) < 0.6 ? 0 : scales::semisToDegree(7, scale);
                const int oct = int(std::lround(double(deg - target) / double(size)));
                deg = target + oct * size;
            }
            const int hi = range, lo = -range / 2;
            while (deg > hi) deg -= size;
            while (deg < lo) deg += size;
            st.note = std::clamp(scales::degreeToSemis(deg, scale), -24, 24);
        }
    }
    if (! r.lockVelocity)
        st.velocity = float(std::clamp(0.72 + (strong ? 0.15 : 0.0) + double(r.velocity) * 0.35 * (2.0 * u(6) - 1.0), 0.15, 1.0));
    if (! r.lockGate)
        st.gate = float(std::clamp(0.5 + double(r.gate) * 0.45 * (2.0 * u(7) - 1.0), 0.08, 1.0));
    return st;
}

Sequencer::Step Sequencer::varied(const Step& base, int idx, int64_t cycleIndex, double amount, int scale) const noexcept
{
    const uint64_t seed = uint64_t(params.random.seed) * 0x9E3779B97F4A7C15ull;
    if (unit(hash(seed, uint64_t(cycleIndex), uint64_t(idx) * 31u + 0x77u)) >= amount) return base;
    const int len = std::clamp(params.length, 1, kMaxSteps);
    const int prev = steps[size_t((idx + len - 1) % len)].note;
    return makeStep(base, idx, prev, hash(seed, uint64_t(cycleIndex) + 0x1000u, uint64_t(idx)), params.random, scale, len);
}

void Sequencer::generate(uint32_t seed) noexcept
{
    const int len = std::clamp(params.length, 1, kMaxSteps);
    const int scale = scales::clampScale(params.scale);
    int prev = 0;
    for (int i = 0; i < len; ++i)
    {
        steps[size_t(i)] = makeStep(steps[size_t(i)], i, prev, hash(seed, uint64_t(i), 0x99u), params.random, scale, len);
        prev = steps[size_t(i)].note;
    }
}

// ---------------------------------------------------------------- sequencer LFO
float Sequencer::seqLfoAt(double beats, double bpm, int64_t) const noexcept
{
    const auto& L = params.lfo;
    double ph;
    if (L.sync) ph = beats / std::max(1.0e-3, double(Delay::divisionBeats(L.division)));
    else ph = beats * 60.0 / std::max(20.0, bpm) * std::clamp(double(L.rateHz), 0.01, 20.0);
    ph += double(L.phase);
    const double k = std::floor(ph), f = ph - k;
    const double sm = std::clamp(double(L.smooth), 0.0, 1.0);
    constexpr double pi = 3.14159265358979323846;
    double y = 0.0;
    switch (L.wave)
    {
        case Params::SeqLfo::Wave::Sine: y = std::sin(2.0 * pi * f); break;
        case Params::SeqLfo::Wave::Triangle: y = f < 0.25 ? 4.0 * f : (f < 0.75 ? 2.0 - 4.0 * f : 4.0 * f - 4.0); break;
        case Params::SeqLfo::Wave::SawUp:
        case Params::SeqLfo::Wave::SawDown:
        {
            const double w = 0.5 * sm;
            y = 2.0 * f - 1.0;
            if (w > 0.0 && f > 1.0 - w) { const double t = (f - (1.0 - w)) / w; y = 1.0 - 2.0 * (t * t * (3.0 - 2.0 * t)); }
            if (L.wave == Params::SeqLfo::Wave::SawDown) y = -y;
            break;
        }
        case Params::SeqLfo::Wave::Square:
        {
            const double kk = 1.0 + 40.0 * (1.0 - sm) * (1.0 - sm);
            y = std::tanh(kk * std::sin(2.0 * pi * f)) / std::tanh(kk);
            break;
        }
        case Params::SeqLfo::Wave::SampleHold:
        {
            const double a = unit(hash(params.random.seed, uint64_t(int64_t(k)), 0x5A5Au)) * 2.0 - 1.0;
            const double b = unit(hash(params.random.seed, uint64_t(int64_t(k) + 1), 0x5A5Au)) * 2.0 - 1.0;
            y = a + sm * (b - a) * (0.5 - 0.5 * std::cos(pi * f));
            break;
        }
    }
    return float(std::clamp(y, -1.0, 1.0));
}

double Sequencer::speedForBar(int64_t bar, double bpm) const noexcept
{
    const auto& L = params.lfo;
    if (L.depth == 0.0f || L.dest != Params::SeqLfo::Dest::Speed) return 1.0;
    const double x = double(L.depth) * double(seqLfoAt(double(bar) * 4.0 + 2.0, bpm, 0));   // sampled mid-bar
    return x > 0.5 ? 2.0 : (x < -0.5 ? 0.5 : 1.0);     // rhythmic divisions only: x1/2, x1, x2
}

/** Pattern position for a song position: speed changes only at bar starts, so the mapping is a pure
    function of the song position (host loops / jumps stay exact). Cached incrementally per bar. */
double Sequencer::virtualAt(double ppq, double bpm) noexcept
{
    if (params.lfo.depth == 0.0f || params.lfo.dest != Params::SeqLfo::Dest::Speed || ppq < 0.0) return ppq;
    const int64_t bar = int64_t(std::floor(ppq / 4.0));
    if (! vCacheValid || bar < vCacheBar) { vCacheBar = 0; vCacheV = 0.0; vCacheValid = true; }
    while (vCacheBar < bar) { vCacheV += 4.0 * speedForBar(vCacheBar, bpm); ++vCacheBar; }
    return vCacheV + (ppq - double(bar) * 4.0) * speedForBar(bar, bpm);
}

void Sequencer::process(int numSamples, const Transport& t) noexcept
{
    eventCount = 0;
    if (numSamples <= 0) return;
    if (flushRequested)
    {
        flushAll(0);
        flushRequested = false; needResync = true; internalPpq = 0.0; curStep = -1; latchValid = false;
    }
    if (! t.playing)
    {
        flushAll(0);
        wasPlaying = false; needResync = true; internalPpq = 0.0; curStep = -1; latchValid = false;
        return;
    }

    const double bpm = std::clamp(t.bpm, 20.0, 999.0);
    const double spb = sr * 60.0 / bpm;                      // samples per beat
    if (! t.hostSync && ! wasPlaying) internalPpq = 0.0;
    const double ppq0 = t.hostSync ? t.ppqAtBlockStart : internalPpq;
    const double ppq1 = ppq0 + double(numSamples) / spb;

    // transport jump / loop: close notes, re-derive the slot from the new position
    if (t.hostSync && wasPlaying && ! needResync && std::fabs(ppq0 - expectedPpq) > 4.0 / spb)
    {
        flushAll(0);
        needResync = true;
    }

    const double stepBeats = double(Delay::divisionBeats(params.division));
    const int cycle = buildOrder();

    // Speed modulation (sequencer LFO): slots live in "pattern position" v.
    //  - quantised (default, both clocks): x1/2 / x1 / x2 per bar, v = pure function of the song position
    //  - smooth (internal clock only): v integrates a continuous speed
    const auto& L = params.lfo;
    const bool speedOn = L.depth != 0.0f && L.dest == Params::SeqLfo::Dest::Speed;
    const bool smooth = speedOn && L.smoothSpeed && ! t.hostSync;
    const uint64_t sig = hash(uint64_t(speedOn) | (uint64_t(smooth) << 1) | (uint64_t(L.sync) << 2) | (uint64_t(L.wave) << 3),
                              uint64_t(int64_t(L.depth * 1.0e4f)) ^ (uint64_t(L.division) << 20) ^ (uint64_t(int64_t(L.rateHz * 1.0e4f)) << 28),
                              uint64_t(int64_t(L.phase * 1.0e4f)) ^ (uint64_t(params.random.seed) << 16));
    if (sig != speedSig) { speedSig = sig; vCacheValid = false; needResync = true; if (smooth) virtualPos = ppq0; }

    double v0;
    if (smooth) v0 = needResync && ! wasPlaying ? ppq0 : virtualPos;
    else v0 = virtualAt(ppq0, bpm);

    if (needResync)
    {
        // last slot that started strictly before v0 (a slot starting exactly at v0 still triggers)
        int64_t s = floorDiv(v0 / stepBeats) + 1;
        while (slotStart(s, stepBeats) >= v0) --s;
        lastSlot = s;
        needResync = false;
        latchValid = false;
    }

    // pieces of constant speed inside this block (split at bar starts when quantised)
    double pa = ppq0, va = v0;
    lastSpeed = 1.0;
    for (int piece = 0; piece < 64 && pa < ppq1; ++piece)
    {
        double pb = ppq1, m = 1.0;
        if (smooth)
            m = std::pow(2.0, std::clamp(double(L.depth) * double(seqLfoAt(ppq0, bpm, 0)), -1.0, 1.0));
        else if (speedOn)
        {
            const int64_t bar = int64_t(std::floor(pa / 4.0));
            pb = std::min(ppq1, double(bar + 1) * 4.0);
            m = speedForBar(bar, bpm);
            if (pb <= pa) { pa = pb; continue; }
        }
        lastSpeed = m;
        const double vb = va + (pb - pa) * m;
        for (int guard = 0; guard < 4096; ++guard)
        {
            const int64_t s = lastSlot + 1;
            const double vs = slotStart(s, stepBeats);
            if (vs >= vb) break;
            const double tOn = pa + (vs - va) / m;
            const double dur = (slotStart(s + 1, stepBeats) - vs) / m;
            triggerSlot(s, stepBeats, cycle, tOn, dur, bpm);
            lastSlot = s;
        }
        pa = pb; va = vb;
    }
    if (smooth) virtualPos = va;

    // due events -> sorted by time (note-off first on ties) -> sample offsets
    std::array<Pending, 64> due {};
    int nd = 0, keep = 0;
    for (int i = 0; i < pendingCount; ++i)
    {
        if (pending[size_t(i)].time < ppq1) due[size_t(nd++)] = pending[size_t(i)];
        else pending[size_t(keep++)] = pending[size_t(i)];
    }
    pendingCount = keep;
    std::sort(due.begin(), due.begin() + nd, [](const Pending& a, const Pending& b)
              { return a.time < b.time || (a.time == b.time && ! a.on && b.on); });
    for (int i = 0; i < nd; ++i)
    {
        const auto& e = due[size_t(i)];
        const int off = std::clamp(int(std::ceil((e.time - ppq0) * spb - 1.0e-6)), 0, numSamples - 1);
        if (e.on)
        {
            emit(off, e.note, e.velocity, true);
            if (sounding[size_t(e.note)] < 255) ++sounding[size_t(e.note)];
        }
        else if (sounding[size_t(e.note)] > 0)
        {
            emit(off, e.note, 0.0f, false);
            --sounding[size_t(e.note)];
        }
    }

    expectedPpq = ppq1;
    if (! t.hostSync) internalPpq = ppq1;
    wasPlaying = true;
}

void Sequencer::randomise(uint32_t seed, float amount) noexcept
{
    const double a = std::clamp(double(amount), 0.0, 1.0);
    for (int i = 0; i < kMaxSteps; ++i)
    {
        auto u = [&](uint64_t salt) { return unit(hash(seed, uint64_t(i), salt)); };
        if (u(1) >= a) continue;
        auto& st = steps[size_t(i)];
        st.active = u(2) < 0.75;
        st.note = int(hash(seed, uint64_t(i), 3) % 25u) - 12;
        st.velocity = float(0.5 + 0.5 * u(4));
        st.gate = float(0.2 + 0.8 * u(5));
        st.probability = u(6) < 0.7 ? 1.0f : float(0.5 + 0.5 * u(7));
        st.ratchet = u(8) < 0.85 ? 1 : 2 + int(hash(seed, uint64_t(i), 9) % 3u);
    }
}

namespace
{
const char* const kPresetNames[Sequencer::kNumPresets] = {
    "Pulse 8ths", "Arp up", "Arp up-down", "Sparse bells", "Octave drone pulse", "Pentatonic random", "Ratchet rolls", "Off-beat swing"
};
}

const char* Sequencer::presetName(int index) noexcept { return kPresetNames[std::clamp(index, 0, kNumPresets - 1)]; }

Sequencer::PresetInfo Sequencer::loadPresetPattern(int index) noexcept
{
    index = std::clamp(index, 0, kNumPresets - 1);
    for (auto& s : steps) { s = Step {}; s.active = false; }
    auto set = [this](int i, int note, float vel, float gate, float prob = 1.0f, int ratchet = 1)
    {
        auto& s = steps[size_t(i)];
        s.active = true; s.note = note; s.velocity = vel; s.gate = gate; s.probability = prob; s.ratchet = ratchet;
    };
    using D = Direction;
    switch (index)
    {
        case 0: // Pulse 8ths
            for (int i = 0; i < 16; ++i) set(i, 0, (i % 2) ? 0.6f : 0.9f, 0.5f);
            return { kPresetNames[0], 16, 6, 0.0f, D::Forward, -1 };
        case 1: { // Arp up
            const int n[8] = { 0, 2, 4, 7, 9, 12, 14, 16 };
            for (int i = 0; i < 8; ++i) set(i, n[i], 0.8f, 0.6f);
            return { kPresetNames[1], 8, 3, 0.0f, D::Forward, -1 }; }
        case 2: { // Arp up-down
            const int n[8] = { 0, 3, 7, 10, 12, 15, 19, 24 };
            for (int i = 0; i < 8; ++i) set(i, n[i], 0.75f, 0.55f);
            return { kPresetNames[2], 8, 3, 0.0f, D::PingPong, -1 }; }
        case 3: // Sparse bells
            set(0, 12, 0.9f, 1.0f, 0.95f); set(5, 19, 0.7f, 1.0f, 0.6f); set(9, 16, 0.8f, 1.0f, 0.8f); set(12, 24, 0.6f, 1.0f, 0.5f);
            return { kPresetNames[3], 16, 6, 0.0f, D::Forward, -1 };
        case 4: { // Octave drone pulse
            const int n[8] = { 0, 12, 0, -12, 0, 12, 7, -12 };
            for (int i = 0; i < 8; ++i) set(i, n[i], 0.7f, 0.9f);
            return { kPresetNames[4], 8, 9, 0.0f, D::Forward, -1 }; }
        case 5: // Pentatonic random
            for (int i = 0; i < 16; ++i) set(i, (i * 3) % 25, 0.55f + 0.03f * float(i % 8), 0.5f, 0.7f);
            return { kPresetNames[5], 16, 3, 0.0f, D::Random, 10 };
        case 6: { // Ratchet rolls
            const int r[16] = { 1, 1, 2, 1, 3, 1, 1, 4, 1, 2, 1, 1, 3, 1, 2, 4 };
            const int n[16] = { 0, 7, 12, 7, 0, 5, 7, 12, 0, 7, 12, 14, 0, 7, 12, 19 };
            for (int i = 0; i < 16; ++i) set(i, n[i], 0.75f, 0.6f, 1.0f, r[i]);
            return { kPresetNames[6], 16, 6, 0.0f, D::Forward, -1 }; }
        case 7: default: // Off-beat swing
            for (int i = 1; i < 16; i += 2) set(i, (i % 4 == 1) ? 0 : 7, 0.8f, 0.4f);
            set(0, -12, 0.6f, 0.3f, 0.5f);
            return { kPresetNames[7], 16, 3, 0.35f, D::Forward, -1 };
    }
}
} // namespace ambient::dsp
