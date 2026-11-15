# Sequencer: Random mode and dedicated Sequencer LFO

Both live in the Sequencer tab, row 2 (view buttons MAIN / RANDOM / SEQ LFO). The step grid is unchanged.

## Random mode (RANDOM view)

- **Generate**: builds a new base pattern from the settings below. Scale- and root-aware: notes are scale
  degrees from a musical random walk (repeat 22 %, step 40 %, skip 22 %, larger leaps 11 %, octave 5 %),
  strong beats lean on the root / fifth, accents on strong beats. Playback continues: the new pattern is
  swapped in at the next audio block through the lock-free pattern buffer. Also picks a new variation seed.
- **Random (on/off)**: every pattern cycle plays a variation of *Amount* of the steps. Variations are a pure
  function of (seed, cycle, step): the stored pattern is never changed, and a looped section replays identically.
- **Amount** (share of steps varied per cycle), **Density** (active steps; strong beats favoured),
  **Range** (note range in scale degrees: up to Range above the root, half of it below), **Velocity** and
  **Gate** (how much they are randomised), **Steps** (randomise which steps are active).
- **Chromatic**: notes may leave the scale; playback quantisation is switched off for random mode.
  Off (default): every generated or varied note is in the selected scale and root.
- **Locked fields**: Notes / Velocity / Gate / Steps are kept unchanged by Generate and by the variations.

## Sequencer LFO (SEQ LFO view)

Independent from LFO 1-4. Evaluated at musical positions, never at audio rate: per step for notes and
evolution, per bar for speed. Positions come from the song position, so host loops and jumps reproduce
the same result.

- Waveforms: Sine, Triangle, Saw Up, Saw Down, Square, Random S&H. **Smooth** softens square / saw edges
  and interpolates S&H. **Phase**, **Retrig** (restart every pattern cycle = repeating melodic figure).
- **Sync** (division, default 1 bar) or free **Rate** (Hz). **Depth** -1..+1 (0 = off).
- **Destination**
  - *Notes*: shifts each step by up to +-7 scale degrees (depth x LFO); stays in the scale (or semitones in
    chromatic mode) and within -24..+36 semitones of the base.
  - *Speed*: x1/2, x1 or x2 per bar (LFO sampled mid-bar). Rhythmic divisions only, so the pattern
    position stays a pure function of the song position: no drift, exact host sync, loops reproduce.
    *Smooth speed* (internal clock only): continuous 0.5x..2x.
  - *Evolution*: drives the random-mode variation amount (adds |depth| x LFO), so variations swell and
    recede; works with Random mode on or off.
  - *Notes + Evolution*: both.

## Limits

- Speed modulation assumes 4/4 bars (4 beats) for its bar grid.
- Smooth speed is ignored with the host clock (by design: host sync must stay exact).
- Generate is not undoable (save a preset first if you want to keep a pattern).
