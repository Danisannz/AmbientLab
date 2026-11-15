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

void Sequencer::triggerSlot(int64_t s, double stepBeats, int cycle) noexcept
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

    const Step& st = steps[size_t(idx)];
    if (! st.active) return;
    if (unit(hash(params.seed, uint64_t(s), 0x9E37u)) >= double(std::clamp(st.probability, 0.0f, 1.0f))) return;

    const double t0 = slotStart(s, stepBeats), t1 = slotStart(s + 1, stepBeats);
    const int r = std::clamp(st.ratchet, 1, 4);
    const double sub = (t1 - t0) / double(r);
    const double g = std::clamp(double(st.gate) * double(std::clamp(params.gateScale, 0.0f, 2.0f)), 0.01, 1.0);
    int note = params.baseNote + latchedRoot + params.transpose + std::clamp(st.note, -24, 24);
    note = std::clamp(scales::quantize(note, latchedScale, latchedRoot), 0, 127);
    const float vel = std::clamp(st.velocity, 0.0f, 1.0f);
    for (int k = 0; k < r; ++k)
    {
        const double on = t0 + double(k) * sub;
        const double off = std::min(on + sub * g, on + sub - 1.0e-9);
        push({ on, note, vel, true });
        push({ off, note, 0.0f, false });
    }
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

    if (needResync)
    {
        // last slot that started strictly before ppq0 (a slot starting exactly at ppq0 still triggers)
        int64_t s = floorDiv(ppq0 / stepBeats) + 1;
        while (slotStart(s, stepBeats) >= ppq0) --s;
        lastSlot = s;
        needResync = false;
        latchValid = false;
    }

    for (int guard = 0; guard < 4096; ++guard)
    {
        const int64_t s = lastSlot + 1;
        if (slotStart(s, stepBeats) >= ppq1) break;
        triggerSlot(s, stepBeats, cycle);
        lastSlot = s;
    }

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
