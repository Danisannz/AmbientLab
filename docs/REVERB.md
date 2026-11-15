# Reverb engine (Stage 5)

Signal position: synth / track input -> granular -> **reverb** -> frequency sidechain -> master.
Zero latency (pre-delay is a musical control and defaults to 20 ms; set 0 for none).

## Algorithms

| Type | Topology | Character |
|---|---|---|
| Hall | 4 input allpass diffusers -> 8-line FDN (Householder), per-line gains from RT60, in-loop damping, slow per-line modulation | Smooth, wide, medium-dense build-up |
| Room | Same FDN, short prime-ish line set, strong early reflections | Fast build-up, small and close |
| Cathedral | Long line set, late sparse early reflections | Slow bloom, very large space |
| Plate | Dattorro figure-eight tank (input bandwidth filter, 4 input diffusers, modulated tank allpasses) | Dense, bright, instant diffusion |
| Spring | Two dispersive allpass-chain spring models in a loop, L/R different lengths | "Boing" chirp, metallic flutter |
| Shimmer | Hall FDN with an octave-up pitch shifter in the feedback (Shimmer knob) | Rising octave halo |
| Infinite | FDN with ~600 s RT60 and a tanh limiter inside the loop | Endless sustained wash, bounded |

## Controls

`rv.on, rv.algo, rv.decay (0.2-30 s RT60), rv.predelay (0-500 ms), rv.size (0.5-2x), rv.diffusion,
rv.damping, rv.early, rv.mod, rv.feedback (regenerative wet->input, DC-blocked, tanh-limited, 0-90 %),
rv.shimmer (Shimmer type only - knob disabled otherwise), rv.mix, rv.freeze`.

Freeze mutes the input (smoothed) and makes the loop lossless; the tail holds indefinitely.
Changing type or size restarts the tank from silence (the old tail is cut).

## Safety

FDN lines have a soft knee above +/-2 (linear below), the infinite loop uses tanh, the
regenerative path is tanh-limited. Tested stable at 44.1/96/192 kHz, blocks of 1 and 512,
with 30 s decay, 90 % feedback, max modulation/size/diffusion and continuous loud noise.

## Measured

Unit tests (tests/test_reverb.cpp): RT60 within 30 % for FDN types (1 s and 4 s), plate/spring
decay scales with Decay, pre-delay onset, mix 0 bit-exact, freeze within 6 dB over 4 s,
infinite still ringing 5 s after input stops, damping darkens the tail, pairwise impulse-response
correlation < 0.5 between types, L/R correlation < 0.6, shimmer adds > 15 dB octave energy vs Hall.

CPU added by the reverb (processBlock delta, block 512, one core):

| Build | 48 kHz | 192 kHz |
|---|---|---|
| Windows / MSVC Release (CI runner) | 1.0 % (Plate, Spring) - 1.7 % (Shimmer, Infinite) | 4-8 % |
| Linux / clang Release | 0.5-1.6 % | 2-7 % |
