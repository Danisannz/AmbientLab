# AmbientLab: build, install and test (Windows)

## Builds

Every push to `feature/ui-refinement` builds on GitHub Actions (windows-latest, MSVC, Release):

| Plugin | VST3 bundle (CI build tree) | Artifact |
|---|---|---|
| AmbientLab (Instrument) | `build/plugin/AmbientLabInstrument_artefacts/Release/VST3/AmbientLab.vst3` | `AmbientLab-VST3-Windows` |
| AmbientLab FX | `build/plugin/AmbientLabFX_artefacts/Release/VST3/AmbientLab FX.vst3` | `AmbientLab-VST3-Windows` |

The run fails unless all of these pass: unit tests (ctest), the self tests of both targets, the VST3
host test (loads the built bundles like a DAW, checks bus types, the sidechain path, leakage and ducking)
and pluginval at strictness 10 for both bundles (`pluginval-logs` artifact).

Local build: `cmake -B build -A x64 -DAMBIENT_BUILD_SNAPSHOT=ON` then `cmake --build build --config Release`.

## Install

1. GitHub -> Actions -> latest green run on `feature/ui-refinement` -> download `AmbientLab-VST3-Windows`.
2. Unzip. Copy both `AmbientLab.vst3` and `AmbientLab FX.vst3` (the whole `.vst3` folders) to
   `C:\Program Files\Common Files\VST3\`, replacing older copies.
3. Restart the DAW and rescan plug-ins. Re-insert instances saved with old builds if their
   inputs look wrong (the Instrument bus layout changed on 2026-11-15).

User presets are stored in `%APPDATA%\AmbientLab\Presets\` (`*.ambientpreset`).

## Test checklist (real DAW)

Sound / Instrument
- Play notes: oscillators A/B, unison, sub, noise, filter + envelopes respond; 8-voice polyphony.
- Header -> Presets -> Factory presets: each loads and sounds (sequenced ones need the transport running
  when Seq Clock = Host).

Process
- Granular, Delay (8 types; Sync follows the DAW tempo), Reverb (7 types), Stereo safety, Routing
  (Serial order / Parallel / Loop feedback).
- Sidechain (Instrument and FX): Ableton -> device Sidechain section -> pick the kick track. Diagnostics
  line in Process > Sidechain must read `SC BUS ON | 2 ch | SAMPLES YES`.

Modulation
- LFO 1-4: waveforms, Sync to host, Retrig, Custom curve (drag points).
- Matrix: e.g. LFO 1 -> Filter Cutoff 30 %, Macro 1 -> Reverb Mix, SC Transient -> Filter Cutoff -20 %.

Sequencer
- Seq On, Clock Host + play the DAW (or Clock Internal). Edit lanes, ratchets, probability, skip.
- Parameter locks: choose Lock 1 target (e.g. Filter Cutoff), select LOCK 1 lane, drag values on steps.
- Scale / root: pick a scale; route LFO -> Seq Scale or Seq Root in the matrix.

Persistence / automation
- Save the DAW project, reopen: all parameters, sequencer pattern and preset name restored.
- Save a user preset, load another, load it back.
- Automate a few parameters (filter cutoff, reverb mix, delay feedback) in the DAW lanes.

Performance
- Header CPU readout; typical patches 2-5 % of one core at 48 kHz; worst-case synth patch ~10 %.
