#pragma once
// Musical scales for the sequencer (Stage 8). Indices are STABLE (presets, automation):
// never reorder; append only.
#include <array>
#include <cstdint>

namespace ambient::dsp::scales
{
inline constexpr int kNumScales = 16;

struct Scale
{
    const char* name;
    uint8_t size;                 // number of degrees per octave
    std::array<uint8_t, 12> iv;   // semitone offsets from the root, ascending
};

inline constexpr std::array<Scale, kNumScales> kScales { {
    { "Major",             7, { 0, 2, 4, 5, 7, 9, 11 } },
    { "Natural minor",     7, { 0, 2, 3, 5, 7, 8, 10 } },
    { "Harmonic minor",    7, { 0, 2, 3, 5, 7, 8, 11 } },
    { "Melodic minor",     7, { 0, 2, 3, 5, 7, 9, 11 } },
    { "Dorian",            7, { 0, 2, 3, 5, 7, 9, 10 } },
    { "Phrygian",          7, { 0, 1, 3, 5, 7, 8, 10 } },
    { "Lydian",            7, { 0, 2, 4, 6, 7, 9, 11 } },
    { "Mixolydian",        7, { 0, 2, 4, 5, 7, 9, 10 } },
    { "Locrian",           7, { 0, 1, 3, 5, 6, 8, 10 } },
    { "Major pentatonic",  5, { 0, 2, 4, 7, 9 } },
    { "Minor pentatonic",  5, { 0, 3, 5, 7, 10 } },
    { "Blues",             6, { 0, 3, 5, 6, 7, 10 } },
    { "Whole tone",        6, { 0, 2, 4, 6, 8, 10 } },
    { "Hirajoshi",         5, { 0, 2, 3, 7, 8 } },
    { "Phrygian dominant", 7, { 0, 1, 4, 5, 7, 8, 10 } },
    { "Chromatic",        12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
} };

inline constexpr int clampScale(int s) noexcept { return s < 0 ? 0 : (s >= kNumScales ? kNumScales - 1 : s); }
inline constexpr int wrap12(int x) noexcept { return ((x % 12) + 12) % 12; }

inline const char* name(int scale) noexcept { return kScales[size_t(clampScale(scale))].name; }

/** True if the MIDI note belongs to the scale with the given root (0 = C .. 11 = B). */
inline bool contains(int note, int scale, int root) noexcept
{
    const auto& s = kScales[size_t(clampScale(scale))];
    const int pc = wrap12(note - root);
    for (int i = 0; i < s.size; ++i) if (s.iv[size_t(i)] == pc) return true;
    return false;
}

/** Nearest in-scale note; ties resolve downwards. */
inline int quantize(int note, int scale, int root) noexcept
{
    for (int d = 0; d <= 6; ++d)
    {
        if (contains(note - d, scale, root)) return note - d;
        if (contains(note + d, scale, root)) return note + d;
    }
    return note; // unreachable: every scale contains the root
}
} // namespace ambient::dsp::scales
