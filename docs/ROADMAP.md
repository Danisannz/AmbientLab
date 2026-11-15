# AmbientLab roadmap

Development stages (DSP) and the requirements captured for each. Interface
placeholders show the stage number; no control appears before its engine exists.

| Stage | Scope | Status |
|---|---|---|
| 1 | Project foundation, VST3 Instrument + FX, state versioning | Done |
| UI | Interface redesign, analyzer (scope, vectorscope, spectrum) | Done (branch) |
| SC | Frequency Sidechain (intelligent spectral ducking) + mascot | Done - verified by the user in Ableton Live (Instrument + FX), 2026-11-15 |
| 2 | Oscillator engine: A/B (5 band-limited waves, unison 7, detune, spread, phase/free), FM/PM B->A, sub, white/pink noise, analog drift, ADAA saturation | Done (branch). Worst case (8 voices, A+B unison 7, FM, noise, drive): ~10 % of one core at 48 kHz |
| 3 | Multimode filter (LP24/LP12/BP/HP12/HP24/Notch, resonance, drive, env/key/velocity) + amp and mod ADSR (mod -> cutoff, pitch) | Done (branch) |
| 4 | Granular engine: 10 s buffer, freeze, 64-grain pool, size/density/position/scatter, pitch +-24 st + random, reverse probability, 5 windows, stereo spread, stable feedback, mix; processes synth (instrument) or track input (FX) | Done (branch) |
| 5 | Reverb: 7 independent algorithms (Hall/Room/Cathedral 8-line FDN with per-line RT60 gains, Dattorro plate, dispersive spring, octave-up shimmer, limited infinite), decay, pre-delay, size, diffusion, damping, early reflections, modulation, regenerative feedback, freeze, mix. Zero latency | Done (branch). 0.5-1.6 % of one core at 48 kHz - see REVERB.md |
| 6 | Delay: tape, BBD, digital, ping-pong, multi-tap, reverse, pitch, granular; host sync (15 divisions), feedback, low/high cut, drive, modulation, width, mix. Zero latency | Done (branch) - see DELAY.md |
| ST | Stereo safety: reverb width + mono bass, output bass mono / width / correlation floor / mono check, monitor | Done (branch) - see STEREO.md |
| 7 | 4 LFOs (14 waveforms incl. smooth/stepped random, S&H, slewed S&H, chaotic, 8-point custom curve; free or host-synced phase-locked; phase, shape, smooth, unipolar, retrigger) + 32-slot matrix: 26 sources (LFOs, 8 macros, velocity, mod wheel, aftertouch, pitch bend, key, note random, 7 sidechain sources) -> 85 destinations, control rate 32 samples | Done (branch) |
| 8 | Step sequencer: 1-32 steps, host sync or internal clock, 15 rates, swing, 4 directions, per-step gate/note/velocity/gate length/probability/ratchet 1-4/skip, 4 parameter-lock lanes (any of 90 destinations), 16 scales + root (both modulation destinations, latched at step / pattern start), octave, transpose, 8 preset patterns, randomise; drives the synth (Instrument), locks + Seq Gate/Velocity/Step mod sources work in both | Done (branch) |
| 9 | Routing: serial (6 orders) / parallel (sum of each processor's contribution), safe global loop feedback (HP, DC block, tanh, loop limiter ~-12 dBFS), chain gain. Presets: 12 factory presets (also host programs), user presets save / load / delete (%APPDATA%/AmbientLab/Presets), name persists in the session | Done (branch) |
| 11 | Macros: default assignments (slots 25-32, audible with the default patch), targets shown under each macro, right-click to assign / edit / remove. Right-click modulation on every modulatable knob (same matrix slots): live amount sliders, invert, remove, add source, open matrix; lime ring shows the modulation range. Glim v2 mascot wired, reacting only to real telemetry (output level, kicks, gain reduction, routed LFOs, sequencer steps) | Done (branch) |
| 10 | Validation: Windows CI (unit tests, self tests, VST3 host test, pluginval strictness 10), real-DAW testing by the user | In progress |

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
