# AmbientLab

Hybrid modular ambient instrument and effects processor (VST3, Windows).

Two plugins share one DSP core:

| Plugin | Type | Use |
|---|---|---|
| **AmbientLab** | Instrument | MIDI → synth (+ optional stereo sidechain input) |
| **AmbientLab FX** | Audio effect | Process any track; MIDI drives sequencer/triggers |

## Status

**Stage 1 – foundation** (current): CMake/JUCE project, both VST3 targets, stable parameter IDs,
versioned state with migration, parameter smoothing, denormal/NaN protection, zero-latency output
safety (DC blocker + soft clip), 8 macros, 8-voice test tone synth (temporary), minimal test UI, unit tests, CI.

## Interface

Cyberpunk-psychedelic identity (Space Grotesk + IBM Plex Sans, ultraviolet /
lime / magenta on graphite), four tabs (Sound, Process, Modulation, Sequencer),
always-visible macros, and a docked analyzer (oscilloscope, stereo vectorscope,
spectrum) that can expand over the work area. Engines that are not built yet
appear as labelled placeholders with their stage number - never as fake controls.
Settings > Graphics quality switches Standard / High quality (see docs/ANALYZER.md).

## Getting the plugin

Every push builds on GitHub Actions. Open the **Actions** tab → latest run → download
`AmbientLab-VST3-Windows`, then copy the `.vst3` folders to `C:\Program Files\Common Files\VST3`.

## Building locally (Windows)

```
cmake -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

## Layout

```
core/     DSP library, no JUCE dependency (unit-tested headless)
plugin/   JUCE processor, parameters, state migration, editor
tests/    Catch2 unit tests
tools/    snapshot: headless editor renderer + rendering benchmark (optional)
docs/     ROADMAP.md, ANALYZER.md (architecture, level conventions, benchmark)
```

## Design rules

- No allocation, locks or blocking in `processBlock`.
- Parameter IDs in `core/include/ambient/ParamIDs.h` are permanent. Append only.
- Any change in parameter meaning bumps `kStateVersion` and adds a migration step.
- Zero latency unless a feature strictly requires it.
