# Delay engine (Stage 6)

Chain position: synth / track input -> granular -> **delay** -> reverb -> sidechain ducker -> stereo safety -> master.
Zero latency. 8 s stereo buffer allocated in prepareToPlay only.

| Type | Character |
|---|---|
| Tape | varispeed glide on time changes, wow (0.45 Hz) + flutter (6.3 Hz + noise), asymmetric tape saturation, 2-pole LP (max 11 kHz) |
| BBD | loop LP falls with delay time (clock darkening, 1.8-12 kHz), soft-knee compander clip, quadrature chorus modulation |
| Digital | clean; time changes crossfade two read heads (40 ms): no clicks, no pitch swoop; High cut at max = bypassed |
| Ping-pong | mono-summed input into L, feedback crosses L <-> R |
| Multi-tap | taps at 1/4, 2/4, 3/4, 1 x time, alternately panned; feedback from the last tap |
| Reverse | time-reversed segments of length Time (two overlapping sin^2 heads), reversed repeats |
| Pitch | pitch shifter after the read: every repeat moves by Pitch semitones (-24..+24) |
| Granular | the line is read as grains (~25/s, 40-120 ms, scatter + pitch jitter by Mod, random pan by Width) |

Controls (`dl.*`): on, mode, time (1-4000 ms), sync + division (1/64 .. 2/1, triplets and dotted; host
BPM from the playhead, 120 BPM fallback), feedback (0-100 %, loop soft-limited), low cut, high cut,
drive, mod depth, mod rate, width, pitch, mix (dry stays full up to 50 %, wet full from 50 %).

Every echo, not only the repeats, carries the type's tone and saturation.

Tests (tests/test_delay.cpp): bit-exact at mix 0 / off, echo timing within 2 samples, repeat ratio
follows feedback, sync math, ping-pong alternation, four multi-taps, reverse produces falling sweeps
from rising ones, pitch +12 st moves energy an octave up, tape modulation detunes, BBD darker than
digital, granular active and stereo, no clicks on time changes, all types stable at max
feedback/drive/mod at 44.1-192 kHz and blocks 1/512, correlation > 0.2 for a mono source.

CPU (Linux, whole processBlock, block 512): 1.3-1.8 % of one core at 48 kHz, 6-7 % at 192 kHz.
