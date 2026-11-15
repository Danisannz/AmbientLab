# Stereo engine and mono compatibility

## Where negative correlation came from (measured, 48 kHz, mono pad in)

| Stage | Correlation | Below 200 Hz | 50 ms windows < 0 |
|---|---|---|---|
| Synth (unison, constant-power pan) | +1.00 | +1.00 | 0 % |
| Granular (spread 50 %) | +0.88 | +0.89 | 0 % |
| Reverb Hall, wet (before) | -0.10 | -0.24 | 71 % |
| Reverb Room, wet (before) | -0.15 | +0.29 | 97 % |
| Reverb Plate, wet (before) | -0.26 | **-0.77** | 84 % |
| Reverb Shimmer, wet (before) | -0.03 | -0.18 | 61 % |

Synth and granular pan with positive gains on both sides and cannot create sustained
anti-phase. The reverb was the cause: each output tapped independent delay lines
(side energy >= mid), the Dattorro plate output taps subtract heavily in the bass, and
nothing kept the wet low end mono. Run `ambient_tests "[report]" -s` for the current table.

## Reverb stereo stage

Wet L/R -> M/S -> side high-passed at 180 Hz (24 dB/oct: the reverb bass is mono) ->
per-algorithm side calibration -> **Width** (`rv.width`, 0-100 %, default 70 %).
Width 100 % is the safety limit: side ~1.5 dB below mid (correlation ~ +0.15), never
systematically anti-phase. Width 0 = mono wet.

Wet only, after the fix: default width corr +0.48..+0.72, bass +0.72..+0.95,
negative windows 0-2 %; width 100 % corr +0.17..+0.51.

## Output stereo safety (last stage before master gain)

`out.bassMono` (default on) / `out.bassMonoHz` (120 Hz): side removed below the frequency.
`out.width` (0-150 %, default 100 %). `out.safety` (default on) + `out.floor` (default 0.00):
feed-forward side gain, keeps the ~100 ms correlation >= floor; attack 30 ms, release 400 ms.
It reduces side only when needed (normal material: no correction) and never touches the mid.
`out.monoCheck`: monitor in mono. Zero latency; exact passthrough with bass mono off and width 100 %.

Monitor (Process > Stereo): correlation in/out, mid/side level, side correction, anti-phase warning.

## Whole plugin (self test, reverb mix 50 %, defaults)

Instrument and FX, all seven algorithms: correlation +0.51..+0.89, bass +0.91..+0.98,
<= 4 % negative windows, mono-sum loss <= 1.23 dB.

Note: sessions saved before this change load `rv.width` = 70 % and bass mono on, so old
reverb settings sound slightly narrower. Set Reverb Width 100 % and Bass Mono off to get closer.
