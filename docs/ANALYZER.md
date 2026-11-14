# AmbientLab analyzer

## Architecture

```
audio thread                         message thread (UI timer: 60 Hz HQ / 30 Hz Standard)
------------                         ----------------------------------------------------
processBlock end:                    VisualizerHub::update()
  if editorOpen: ring.write(L, R)      - reads only the newest frames it needs (lock-free)
  (copy only - no FFT, no drawing,     - stereo statistics (always, ~50 ms window)
   no allocation, no locks)            - ONLY the visible mode is analysed:
                                           Scope: trigger search + window copy
                                           Stereo: frames received since last update
                                           Spectrum: FFT(s) of the selected channel
                                     Views draw from the hub's results.
                                     Freeze: the hub stops analysing; views keep their image.
```

* Editor closed: the audio thread does not capture at all.
* `StereoCaptureRing` (2^18 frames) is single-producer / single-consumer; the reader
  validates every read, so a torn read is discarded, never displayed.

## Level conventions

| Display | Unit | Calibration |
|---|---|---|
| Spectrum | dBFS | full-scale sine = 0 dB (Blackman-Harris window, amplitude-corrected) |
| Spectrum - Left / Right | dBFS of that channel | |
| Spectrum - Stereo | two curves, L and R, each calibrated separately | a one-channel signal is never reduced |
| Spectrum - Mid | dBFS of (L+R)/2 | identical L/R at FS = 0 dB; one channel only = -6 dB (by definition) |
| Spectrum - Side | dBFS of (L-R)/2 | inverted L/R at FS = 0 dB |
| Scope | linear amplitude, +-1 = full scale / gain | |
| Vectorscope | M/S: up = mid, left/right = side; L/R: x = L, y = R | full-scale = Auto (peak) or fixed 0..-24 dB |
| Correlation | -1..+1 | +1 mono, 0 uncorrelated, -1 inverted |
| Width | 0..100 % | rms(S) / (rms(M) + rms(S)); 0 mono, 50 % uncorrelated, 100 % inverted |

## Graphics quality

| | Standard | High quality |
|---|---|---|
| UI / analyzer refresh | 30 Hz | 60 Hz |
| Analyzer curve / column resolution | logical pixels (spectrum: 1 point / 2 px) | physical pixels (HiDPI) |
| Persistence & phosphor images | 1x | physical pixel density |
| Vectorscope dots | single pixel | bilinear beam + halo, interpolated sub-samples |
| Glow passes (knob arcs, traces, curves) | off | on |
| Knob detail (knurling, sheen) | off | on |
| Background artwork | 1x, fewer contours | physical density, full detail |

Audio processing is not affected by this setting.

## Benchmark (Linux, Xvfb software rasteriser, 1280x800, 48 kHz / 512)

Whole-process CPU of one core (includes audio processing, analysis, painting and
software blitting to the virtual display). Windows uses JUCE 8's Direct2D renderer,
so these numbers are a conservative upper bound.

| Configuration | Analyzer paint (ms) | Analyzer fps | Process CPU |
|---|---|---|---|
| Scope docked, Standard | 1.1 | 31 | 12 % |
| Scope docked, HQ | 1.0 | 53 | 20 % |
| Scope expanded, HQ | 2.3 | 52 | 32 % |
| Stereo docked, Standard | 1.7 | 31 | 11 % |
| Stereo docked, HQ | 1.4 | 52 | 17 % |
| Stereo expanded, HQ | 2.9 | 53 | 24 % |
| Spectrum docked, Standard | 2.1 | 31 | 13 % |
| Spectrum docked, HQ | 2.4 | 50 | 21 % |
| Spectrum expanded, HQ | 4.1 | 50 | 32 % |
| Spectrum expanded, HQ, frozen | 0.5 | 0 | 5 % |

Audio thread: capture adds ~0.8 us per 512-frame block (editor open vs closed).

Optimisations made from profiling: scope uses one filled interpolated min/max
envelope per channel (was: long stroked polylines, 69 % -> 20 % CPU); spectrum
smoothing uses prefix sums (O(1) per point) and bevel-joined strokes (50 % -> 32 %).

**OpenGL**: not enabled. Paint times are 0.5-4 ms per frame on a software
rasteriser and Windows already renders through Direct2D; OpenGL would add a
dependency and context-management risk in hosts without a measured need.
Re-evaluate if Windows measurements (Settings > Graphics quality > Show render
statistics) show paint times above ~8 ms.

Reproduce: build with `-DAMBIENT_BUILD_SNAPSHOT=ON`, then
`xvfb-run AmbientSnapshot --bench` (Linux) or run `AmbientSnapshot --bench` (Windows).
