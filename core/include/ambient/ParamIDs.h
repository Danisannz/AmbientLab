#pragma once
// Stable parameter identifiers.
// RULES:
//   * Never rename or reuse an ID once released - hosts store automation by ID.
//   * New parameters are appended; removed ones are kept in kRetiredIDs.
//   * Bump kStateVersion and add a migration step when meaning/range changes.

#include <array>
#include <string>
#include <string_view>

namespace ambient::ids
{
inline constexpr int kStateVersion = 2; // v2: frequency sidechain (sc.*), sourceMix retired

// Global / IO
inline constexpr std::string_view masterGain  = "global.masterGain";   // dB
inline constexpr std::string_view inputGain   = "global.inputGain";    // dB - FX: track input gain (instrument: unused since v2)
inline constexpr std::string_view sourceMix   = "global.sourceMix";    // LEGACY (v1). Kept for automation/state
                                                                       // compatibility; since v2 the external input
                                                                       // is a sidechain reference and is never mixed in.
inline constexpr std::string_view bypassSynth = "global.bypassSynth";  // bool

// Macros 1..8
inline constexpr std::array<std::string_view, 8> macros {
    "macro.1", "macro.2", "macro.3", "macro.4",
    "macro.5", "macro.6", "macro.7", "macro.8"
};

// Frequency sidechain (v2)
namespace sc
{
inline constexpr std::string_view enable      = "sc.enable";
inline constexpr std::string_view inputGain   = "sc.inputGain";    // dB, reference gain
inline constexpr std::string_view sensitivity = "sc.sensitivity";  // 0..1
inline constexpr std::string_view threshold   = "sc.threshold";    // dB offset for all bands
inline constexpr std::string_view depth       = "sc.depth";        // 0..1
inline constexpr std::string_view mix         = "sc.mix";          // 0..1
inline constexpr std::string_view automatic   = "sc.auto";         // bool
inline constexpr std::string_view fullBand    = "sc.fullBand";     // bool
inline constexpr std::string_view rangeLow    = "sc.rangeLow";     // Hz
inline constexpr std::string_view rangeHigh   = "sc.rangeHigh";    // Hz
inline constexpr std::string_view listen      = "sc.listen";       // bool
inline constexpr std::string_view delta       = "sc.delta";        // bool

inline constexpr int kBands = 4;
// Per band: "sc.band<N>.<field>", N = 1..4
inline constexpr std::array<std::string_view, 7> bandFields { "enable", "freq", "q", "threshold", "reduction", "attack", "release" };
enum BandField { BEnable = 0, BFreq, BQ, BThreshold, BReduction, BAttack, BRelease };

inline std::string bandId(int band, BandField f)
{
    return "sc.band" + std::to_string(band + 1) + "." + std::string(bandFields[size_t(f)]);
}
} // namespace sc

inline constexpr int kMaxVoices = 8;
} // namespace ambient::ids
