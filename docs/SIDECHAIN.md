# Frequency Sidechain

Spectral separation from an external Kick/Bass signal. AmbientLab's own audio is
attenuated **only** in the frequency regions where the reference genuinely
conflicts with it. This is not a full-band sidechain compressor (that mode exists
as an option, off by default).

## Signal flow

```
                    ┌──────────────────────── (Listen only) ────────────────────────┐
sidechain bus 1 ──► SC input gain ──► band detectors (4 x band-pass + level)        │
 (mono / stereo)                  ├─► transient detector ──► events (mascot, mods)  │
                                  └─► FFT peak finder ──► Auto band frequencies     │
                                                         │                         ▼
internal ──────────► band detectors ──► conflict ──► gain ──► dynamic cuts ──► mix / delta ──► master ──► out
 Instrument: synth                                   smoothing   y = x - c·BP(x)
 FX: track input (bus 0)
```

The reference is never mixed into the output, except through **Listen**.

## Buses

| Target | Input bus 0 | Input bus 1 | Output |
|---|---|---|---|
| AmbientLab (Instrument) | "Input" - disabled, unused | **"Sidechain"** - mono or stereo, optional | stereo |
| AmbientLab FX | "Input" - the track (stereo) | **"Sidechain"** - mono or stereo, optional | stereo |

Bus 1 is a VST3 *aux* bus, which is how hosts expose sidechains. Before v2 the
instrument's only input was bus 0 (a VST3 *main* input) and was mixed into the
output; that behaviour was removed (`global.sourceMix` is now a legacy parameter).

## Detection

* **Band levels** - per band, a band-pass (TPT state-variable filter, Q >= 0.8) on
  each channel of the reference and of the internal signal; peak level detector with
  1 ms attack and a release of max(20 ms, 2.5 periods of the band frequency) so low
  bands do not ripple.
* **Conflict rule** (per band, dB):
  * `over = refLevel - (bandThreshold + Threshold)` - nothing happens below threshold.
  * `masking = clamp((intLevel - (refLevel - W)) / W, 0, 1)`, `W = 12..36 dB` from
    Sensitivity - an overlap where AmbientLab is far quieter than the kick is *not* a
    conflict, so ambient detail is left alone.
  * `target = min(MaxCut, over x slope) x masking x Depth`, `slope = 0.3..2` from Sensitivity.
* **Transients** - reference low-passed at 180 Hz; fast (0.5/8 ms) vs slow (25/250 ms)
  envelopes; a transient is > 6 dB fast-over-slow above -50 dBFS, 70 ms hold-off.
  Strength 0..1 from the ratio and level. Sustained bass notes do not trigger.
* **Auto mode** - Hann-windowed FFT of the reference (~170 ms window: 8192 points at
  48 kHz, 6 Hz bins; scaled with sample rate), 1/4 hop, peak-hold spectrum (350 ms).
  Up to one peak per enabled band inside *Range lo..hi*, strongest first, at least
  1/2 octave apart; band centres glide in log frequency (120 ms). The FFT **only**
  steers band frequencies - it is not in the audio path.

## Attenuation

* Dynamic cut per band: `y = x - c·BP(x)` with a unity-peak band-pass, giving exactly
  `-reduction dB` at the band centre and unity far away. `c` is interpolated **per
  sample**; filter coefficients change only when frequency/Q move. Result: no zipper
  noise (unit test: ducking adds no sample-to-sample steps beyond the signal's own slope).
* Gain smoothing: per-band attack / release, never faster than half a period of the
  band frequency (a 50 Hz band cannot be chopped inside one cycle).
* Bands are cascaded; stereo-linked (same gain on L and R preserves the image).
* Full-band option: the whole signal follows the strongest band's reduction.
* Enable / Listen / Delta / Mix switch with 10 ms crossfades.

Gentle defaults: 2 bands (50 / 90 Hz start, Auto), 6 dB max cut, depth 70 %,
sensitivity 50 %, threshold -30 dBFS per band, attack 5 ms, release 120 ms.

## Latency

**0 samples** (reported to the host). The audio path is IIR only; detection is
causal; the FFT runs beside the path. Verified by the unit tests and reported by the
plugin self test.

## CPU (whole plugin, sidechain active, 4 bands, Auto; Linux CI-class machine)

| Sample rate | block 64 | block 512 |
|---|---|---|
| 44.1 kHz | 0.7 % | 0.7 % |
| 48 kHz | 0.8 % | 0.7 % |
| 96 kHz | 1.7 % | 1.7 % |
| 192 kHz | 3.2 % | 3.2 % |

(% of one core in real time. The FFT cost is periodic - one transform per hop - so
individual blocks containing a transform cost more than the average.)

## Modulation sources (for the Stage 6 matrix)

`ambient::mod::ModulationSource` is implemented by the sidechain; IDs are stable:

| ID | key | value |
|---|---|---|
| 100 | sc.env | reference envelope, -60..0 dBFS -> 0..1 |
| 101 | sc.transient | decaying pulse on each detected kick |
| 102 | sc.low | reference energy < 150 Hz |
| 103 | sc.mid | 150 Hz .. 2 kHz |
| 104 | sc.high | > 2 kHz |
| 105 | sc.centroid | spectral centroid, log 20 Hz..20 kHz -> 0..1 |
| 106 | sc.gr | current max reduction, 0..24 dB -> 0..1 |

No modulation destinations are exposed until the matrix exists.

## DAW routing (VST3)

| DAW | How to feed the Kick/Bass |
|---|---|
| Ableton Live 11/12 | Device title bar > Sidechain toggle (FX); for the instrument, Live's VST3 sidechain support for instruments is limited - use **AmbientLab FX** on an audio track after AmbientLab |
| Bitwig | Device > Sidechain input selector |
| Cubase / Nuendo | Activate Side-Chain on the plugin, route the kick/bass track's send or output to it |
| FL Studio | Mixer: route the kick channel to the AmbientLab insert as sidechain, then pick it in the plugin's wrapper processing tab |
| Reaper | Plugin I/O: map track channels 3/4 to the "Sidechain" input pins, send kick/bass to channels 3/4 |
| Studio One | Sidechain button in the plugin header |

**Not yet verified in a real DAW.** All routing above is the standard VST3 aux-bus
mechanism and is exercised by the self test with real bus layouts, but the actual
hosts have not been tested yet.

## Tests

* Unit (`tests/test_sidechain.cpp`, 15 cases): no input; kick vs drone (low band
  reduced, 880 Hz unchanged); band isolation; overlap != conflict; threshold;
  Auto fundamentals 42-98 Hz; changing bass notes (2 bands); mono vs stereo
  reference identical; transient count and strength ordering, sustained bass does
  not trigger; bypass; delta + processed == dry; listen; no leakage (silent internal
  -> silent output); no clicks; stability at 6 sample rates x 5 block sizes; full-band
  mode; latency 0 and display magnitude = measured magnitude; modulation outputs.
* Plugin self test (`AmbientSnapshot --selftest`, both targets, runs in CI): mono /
  stereo sidechain layouts accepted; latency 0; no leakage through the real buses;
  Listen; ducking within the default cap; every kick detected once; mascot reacts
  exactly once per kick and never while idle; v1 -> v2 state; CPU table above.


## Detection fix (2026-11-14): sidechain bus inactive by default

Hosting the built .vst3 through JUCE's VST3 host showed the aux "Sidechain" bus was
declared inactive. Hosts that route audio without calling `activateBus()` delivered
0 channels, so the detector never saw samples. The bus is now active by default in both
targets (an unrouted sidechain receives silence). `tools/vst3host` (AmbientVst3HostTest)
checks this end to end in CI: default activation, a host that never negotiates, stereo and
mono sidechains, SC Listen arrival, no leakage, ducking, and a disabled sidechain.

Diagnostics in the sidechain graph: bus on/off, channels read (and host buffer channels),
raw RMS/peak, whether non-zero samples are arriving, detector gain reduction.

Host notes: in Ableton Live, choose the source in the plugin's Sidechain section; in Reaper,
route the send to channels 3/4; in FL Studio, select the source in the mixer's sidechain
routing. Whether a host offers sidechain inputs to *instrument* plugins depends on the host.


## Instrument in Ableton Live (2026-11-15)

The FX worked in Live but the Instrument did not. JUCE declares the first input bus as the
VST3 *main* input; the Instrument's first bus was an unused, disabled "Input", with "Sidechain"
second, and Live kept the sidechain off. The Instrument now has a single input bus, "Sidechain",
declared kAux via `VST3ClientExtensions::getPluginHasMainInput() == false` (the JUCE-recommended
layout for synths with a sidechain). The FX keeps main input + sidechain aux. The VST3 host test
reads each bus's VST3 type and default-active flag exactly as a DAW does.

After updating: in Live, rescan plug-ins and re-insert AmbientLab (the bus layout changed), then
pick the kick track in the device's Sidechain section.
