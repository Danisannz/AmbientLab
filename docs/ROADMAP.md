# AmbientLab roadmap

Development stages (DSP) and the requirements captured for each. Interface
placeholders show the stage number; no control appears before its engine exists.

| Stage | Scope | Status |
|---|---|---|
| 1 | Project foundation, VST3 Instrument + FX, state versioning | Done |
| UI | Interface redesign, analyzer (scope, vectorscope, spectrum) | Done (branch) |
| SC | Frequency Sidechain (intelligent spectral ducking) + mascot | Done (branch) - see SIDECHAIN.md |
| 2 | Oscillator engine: A/B (5 band-limited waves, unison 7, detune, spread, phase/free), FM/PM B->A, sub, white/pink noise, analog drift, ADAA saturation | Done (branch). Worst case (8 voices, A+B unison 7, FM, noise, drive): ~10 % of one core at 48 kHz |
| 3 | Multimode filter (LP24/LP12/BP/HP12/HP24/Notch, resonance, drive, env/key/velocity) + amp and mod ADSR (mod -> cutoff, pitch) | Done (branch) |
| 4 | Granular engine: 10 s buffer, freeze, 64-grain pool, size/density/position/scatter, pitch +-24 st + random, reverse probability, 5 windows, stereo spread, stable feedback, mix; processes synth (instrument) or track input (FX) | Done (branch) |
| 5 | Reverb: 7 independent algorithms (Hall/Room/Cathedral 8-line FDN with per-line RT60 gains, Dattorro plate, dispersive spring, octave-up shimmer, limited infinite), decay, pre-delay, size, diffusion, damping, early reflections, modulation, regenerative feedback, freeze, mix. Zero latency | Done (branch). 0.5-1.6 % of one core at 48 kHz - see REVERB.md |
| 6 | Delay algorithms | Next |
| 6 | Four LFOs + 32-slot modulation matrix | Planned |
| 7 | Step sequencer | Planned |
| 8 | Flexible routing + stereo engine | Planned |
| 10 | Presets, optimization, validation | Planned |

## Stage 6 - modulation: sources already prepared

The Frequency Sidechain implements `ambient::mod::ModulationSource` with stable
IDs 100-106 (envelope, transient, low/mid/high energy, centroid, gain reduction).
The matrix should consume sources through that interface; see docs/SIDECHAIN.md.

## Stage 7 - sequencer: additional requirements (requested 2026-11-14)

Full list: 16/32 steps, gates, probability, ratchets, swing, parameter locks,
pattern randomisation, plus the scale features below.

**Scale mode (pitch quantisation)**
- Pitch steps are quantised to a selectable scale and root note.
- At least 12 scales. Planned set (16): Chromatic, Major (Ionian), Natural minor
  (Aeolian), Harmonic minor, Melodic minor, Dorian, Phrygian, Lydian, Mixolydian,
  Locrian, Major pentatonic, Minor pentatonic, Blues, Whole tone, Hirajoshi,
  Phrygian dominant.
- Root: C .. B (12 values).
- Quantisation applied to the final pitch (step value + transpose + modulation),
  so modulated pitch stays in key.

**Modulatable scale and root**
- `seq.scale` and `seq.root` are exposed as modulation destinations, so any LFO
  (or other source) can be mapped to them in the matrix (Stage 6).
- Discrete destinations: the modulated value is quantised to a scale index /
  semitone and only changes on step boundaries (no mid-note retuning), with an
  option to change only at pattern start.
- Both are also host-automatable parameters with stable IDs.
