#pragma once
// Stage 1 smoke-test synth: 8-voice sine with a linear attack/release.
// It exists only to verify MIDI -> audio in a DAW and will be replaced
// by the real OscillatorEngine in Stage 2.

#include "ParamIDs.h"
#include <array>

namespace ambient
{
class TestToneSynth
{
public:
    void prepare(double sampleRate) noexcept;
    void noteOn(int note, float velocity) noexcept;
    void noteOff(int note) noexcept;
    void allNotesOff() noexcept;

    /** Adds (mixes) into the mono buffer, sample range [start, end). */
    void render(float* out, int start, int end) noexcept;

    int activeVoices() const noexcept;

private:
    struct Voice
    {
        int note = -1;
        double phase = 0.0, inc = 0.0;
        float gain = 0.0f, level = 0.0f, target = 0.0f;
        unsigned age = 0;
    };

    std::array<Voice, ids::kMaxVoices> voices {};
    double sr = 48000.0;
    float rampStep = 0.0f;
    unsigned ageCounter = 0;
};
} // namespace ambient
