#pragma once
// Interface for modulation sources, prepared for the Stage 6 modulation matrix.
// Sources publish values on the audio thread once per block; the matrix (future)
// reads them on the same thread. IDs are stable and must never be reused.

#include <cstdint>

namespace ambient::mod
{
enum class SourceId : uint16_t
{
    // 100-199: Frequency Sidechain
    SidechainEnvelope      = 100, // reference full-band envelope, 0..1 (-60..0 dBFS)
    SidechainTransient     = 101, // decaying pulse on each detected kick, 0..1
    SidechainLowEnergy     = 102, // reference energy  < 150 Hz, 0..1
    SidechainMidEnergy     = 103, // reference energy 150 Hz .. 2 kHz, 0..1
    SidechainHighEnergy    = 104, // reference energy  > 2 kHz, 0..1
    SidechainCentroid      = 105, // spectral centroid, log 20 Hz..20 kHz -> 0..1
    SidechainGainReduction = 106, // current maximum band reduction, 0..1 (0..24 dB)
};

struct SourceInfo
{
    SourceId id;
    const char* key;   // stable string key for presets, e.g. "sc.env"
    const char* name;  // display name
    bool bipolar;
};

class ModulationSource
{
public:
    virtual ~ModulationSource() = default;
    virtual int getNumModOutputs() const noexcept = 0;
    virtual SourceInfo getModOutputInfo(int index) const noexcept = 0;
    /** Current value of an output (audio thread, after the block was processed). */
    virtual float getModOutput(int index) const noexcept = 0;
};
} // namespace ambient::mod
