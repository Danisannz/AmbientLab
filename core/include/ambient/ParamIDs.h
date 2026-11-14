#pragma once
// Stable parameter identifiers.
// RULES:
//   * Never rename or reuse an ID once released - hosts store automation by ID.
//   * New parameters are appended; removed ones are kept in kRetiredIDs.
//   * Bump kStateVersion and add a migration step when meaning/range changes.

#include <array>
#include <string_view>

namespace ambient::ids
{
inline constexpr int kStateVersion = 1;

// Global / IO
inline constexpr std::string_view masterGain  = "global.masterGain";   // dB
inline constexpr std::string_view inputGain   = "global.inputGain";    // dB
inline constexpr std::string_view sourceMix   = "global.sourceMix";    // 0 = synth, 1 = external input
inline constexpr std::string_view bypassSynth = "global.bypassSynth";  // bool

// Macros 1..8
inline constexpr std::array<std::string_view, 8> macros {
    "macro.1", "macro.2", "macro.3", "macro.4",
    "macro.5", "macro.6", "macro.7", "macro.8"
};

inline constexpr int kMaxVoices = 8;
} // namespace ambient::ids
