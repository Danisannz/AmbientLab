# High Quality mode - evaluation and decision

Measured with `ambient_tests "[.quality]" -s` (AMBIENT_OS=2 for the 2x column): aliasing = energy in
20 Hz-20 kHz that is not on the harmonic series, relative to the signal, at 48 kHz versus the same
DSP object run oversampled with a ~-100 dB up/down filter.

| Stage | 48 kHz | 2x | 4x |
|---|---|---|---|
| Oscillator saw, 440 Hz (PolyBLEP) | -40.6 dB | -61.7 | -80.4 |
| Oscillator saw, 3136 Hz | -29.8 dB | -54.1 | -71.3 |
| Oscillator square, 3136 Hz | -30.2 dB | -54.6 | -71.8 |
| Saturator soft / asym, drive 12 dB | -74 .. -88 dB | - | no gain (0-13 dB) |
| Saturator soft / asym, drive 36 dB, 2.8 kHz | -27.3 dB | -45 | -66 |
| Saturator fold, drive 36 dB, 2.8 kHz | -8.5 dB | -16.6 | -85.2 |
| Filter drive 24 dB, LP24 8 kHz, 2.5 kHz | -53.6 dB | -81.9 | -92.9 |
| Delay loop saturation (drive 100 %) | -139.7 dB | - | no gain |

Not affected by oversampling (linear stages, nothing to gain): reverb (all 7), delay and granular
interpolation (cubic Hermite), stereo safety, sidechain bells. Internal precision: float32 has ~140 dB of
dynamic range, far below any audible artefact here; double precision would only cost CPU.

**Decision:** a High Quality mode is justified for the Instrument's synth voices only (oscillators,
saturator, filter drive). It renders the voices at 4x and decimates with two polyphase IIR half-band
stages (4x->2x: 6 coefficients, 2x->1x: 12 coefficients; passband deviation < 0.15 dB to 20 kHz,
> 90 dB rejection of everything that would alias into 0-20 kHz; measured in `test_highquality.cpp`).
2x was rejected: it leaves -45..-55 dB on hot material and -17 dB on hard fold.

Whole-synth result (one held note): high note C7 saw -34.2 -> -74.9 dB; G5 saw + 30 dB soft drive
-35.8 -> -50.1 dB; C5 saw + 30 dB fold -24.7 -> -41.4 dB (a heavily driven saw is close to a square whose
harmonics extend beyond even 4x - the remaining aliasing is inherent to that material).

- Selector: Settings (gear) > Audio quality > Standard (default) / High Quality. Instrument only; the FX
  has no stage that benefits. Stored in presets and sessions, not host-automatable.
- Switching is glitch-free: the synth output ramps down 64 samples, the voice engine changes its internal
  rate live (no reset, held notes keep sounding), ramps back up. Same level within 0.06 dB.
- Latency: none reported. The IIR half-bands add a few samples of frequency-dependent group delay at low
  frequencies, inaudible and not compensatable by a fixed latency.
- CPU (Linux, 8 voices): default patch 2.9 % -> 9.5 %; worst case (A+B unison 7) 9.1 % -> 33.9 % of one core.
