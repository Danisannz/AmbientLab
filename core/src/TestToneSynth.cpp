#include "ambient/TestToneSynth.h"
#include <algorithm>
#include <cmath>

namespace ambient
{
static constexpr double kTwoPi = 6.283185307179586;

void TestToneSynth::prepare(double sampleRate) noexcept
{
    sr = sampleRate;
    rampStep = static_cast<float>(1.0 / (0.010 * sr)); // 10 ms ramps -> click-free
    allNotesOff();
    for (auto& v : voices) { v.level = 0.0f; v.note = -1; }
}

void TestToneSynth::noteOn(int note, float velocity) noexcept
{
    // Re-use voice already playing this note, else a free voice, else steal oldest.
    Voice* chosen = nullptr;
    for (auto& v : voices) if (v.note == note) { chosen = &v; break; }
    if (! chosen) for (auto& v : voices) if (v.note < 0 && v.level <= 0.0f) { chosen = &v; break; }
    if (! chosen) chosen = &*std::min_element(voices.begin(), voices.end(),
                            [](const Voice& a, const Voice& b) { return a.age < b.age; });

    const double freq = 440.0 * std::pow(2.0, (note - 69) / 12.0);
    chosen->note = note;
    chosen->inc = kTwoPi * freq / sr;
    chosen->gain = 0.2f * std::clamp(velocity, 0.0f, 1.0f);
    chosen->target = 1.0f;
    chosen->age = ++ageCounter;
}

void TestToneSynth::noteOff(int note) noexcept
{
    for (auto& v : voices) if (v.note == note) { v.target = 0.0f; v.note = -1; }
}

void TestToneSynth::allNotesOff() noexcept
{
    for (auto& v : voices) { v.target = 0.0f; v.note = -1; }
}

void TestToneSynth::render(float* out, int start, int end) noexcept
{
    for (auto& v : voices)
    {
        if (v.level <= 0.0f && v.target <= 0.0f) continue;
        for (int i = start; i < end; ++i)
        {
            if (v.level < v.target)      v.level = std::min(v.target, v.level + rampStep);
            else if (v.level > v.target) v.level = std::max(v.target, v.level - rampStep);
            out[i] += static_cast<float>(std::sin(v.phase)) * v.gain * v.level;
            v.phase += v.inc;
            if (v.phase >= kTwoPi) v.phase -= kTwoPi;
        }
    }
}

int TestToneSynth::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& v : voices) if (v.level > 0.0f || v.target > 0.0f) ++n;
    return n;
}
} // namespace ambient
