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

// Oscillator engine (Stage 2). Additive: older states load with defaults.
namespace osc
{
// Per oscillator: "osc.a.<field>" / "osc.b.<field>"
inline constexpr std::array<std::string_view, 12> oscFields {
    "on", "wave", "octave", "semi", "fine", "pw", "phase", "free", "level", "unison", "detune", "spread" };
enum OscField { On = 0, WaveF, Octave, Semi, Fine, Pw, Phase, Free, Level, Unison, Detune, Spread };
inline std::string oscId(int oscIndex, OscField f)
{
    return std::string("osc.") + (oscIndex == 0 ? "a." : "b.") + std::string(oscFields[size_t(f)]);
}
inline constexpr std::string_view fm = "osc.fm", pm = "osc.pm";
inline constexpr std::string_view subOn = "osc.sub.on", subWave = "osc.sub.wave", subOctave = "osc.sub.octave", subLevel = "osc.sub.level";
inline constexpr std::string_view noiseOn = "osc.noise.on", noiseType = "osc.noise.type", noiseLevel = "osc.noise.level";
inline constexpr std::string_view drift = "osc.drift";
inline constexpr std::string_view satMode = "osc.sat.mode", satDrive = "osc.sat.drive", satMix = "osc.sat.mix";
inline constexpr std::string_view velocity = "osc.velocity";
} // namespace osc

// Filter + envelopes (Stage 3)
namespace flt
{
inline constexpr std::string_view on = "flt.on", mode = "flt.mode", cutoff = "flt.cutoff", reso = "flt.reso",
                                  drive = "flt.drive", env = "flt.env", key = "flt.key", vel = "flt.vel";
}
namespace env
{
inline constexpr std::string_view ampA = "env.amp.a", ampD = "env.amp.d", ampS = "env.amp.s", ampR = "env.amp.r";
inline constexpr std::string_view modA = "env.mod.a", modD = "env.mod.d", modS = "env.mod.s", modR = "env.mod.r";
inline constexpr std::string_view modPitch = "env.mod.pitch";
}

// Granular engine (Stage 4)
namespace gr
{
inline constexpr std::string_view on = "gr.on", size = "gr.size", density = "gr.density", position = "gr.position",
    scatter = "gr.scatter", pitch = "gr.pitch", pitchRandom = "gr.pitchRandom", reverse = "gr.reverse",
    window = "gr.window", spread = "gr.spread", feedback = "gr.feedback", mix = "gr.mix", freeze = "gr.freeze";
}

namespace rv
{
inline constexpr std::string_view on = "rv.on", algo = "rv.algo", decay = "rv.decay", predelay = "rv.predelay",
    size = "rv.size", diffusion = "rv.diffusion", damping = "rv.damping", early = "rv.early", mod = "rv.mod",
    feedback = "rv.feedback", shimmer = "rv.shimmer", mix = "rv.mix", freeze = "rv.freeze";
}

inline constexpr int kMaxVoices = 8;
} // namespace ambient::ids
