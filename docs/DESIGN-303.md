# o3-303: design (draft)

Status: draft, 2026-10-25, from the design conversation with the maintainer. The plan for milestone M3, now built
(`docs/M3-PLAN.md`) and continued by M3b (`docs/M3B-PLAN.md`). Open decisions are in section 8.

An improved TB-303 clone as an o3sdk app: a 303 voice with Devil Fish-style extras, drive and delay, a 303-style step sequencer
without the original's pitch/time-mode split, motion recording and parameter locks, and a second screen (HOOKS) that loads or
samples phrases and launches them in time.

## 1. Hardware facts the design rests on

48 pads in 4 rows of 12, no velocity, no pressure, one monochrome LED each; 33 buttons with no LED; one encoder with no push; an
800×480 display. In track3r-os a sounding TB-303 voice costs 15.5-22 % of a block on the unit.

## 2. The pads: 12 columns are one chromatic octave

```
 col →   0    1    2    3    4    5    6    7  │  8     9     10    11
row 0   [ 1] [ 2] [ 3] [ 4] [ 5] [ 6] [ 7] [ 8] │ ACC   SLIDE OCT-  OCT+
row 1   [ 9] [10] [11] [12] [13] [14] [15] [16] │ TIE   REST  KEYS  MOTION
row 2    C    C#   D    D#   E    F    F#   G    G#    A     A#    B    (upper octave)
row 3    C    C#   D    D#   E    F    F#   G    G#    A     A#    B    (lower octave)
```

- Step LEDs: off = rest, dim = note, bright = playhead, blinking = the selected step.
- Keyboard LEDs: naturals dim, sharps off (a piano drawn with one colour); the current step's note at full level.
- With no velocity, accent cannot come from how hard a pad is hit: ACC and SLIDE are modifier pads. Their block goes on the side
  away from the hand on the encoder (decision 8.1).

| Gesture | Effect |
|---|---|
| REC (stopped) + key | writes the note on the cursor step and advances; with ACC or SLIDE held, the note has accent or slide; REST and TIE also advance |
| hold a step + key | sets that step's note without moving the cursor |
| hold a step + ACC / SLIDE / TIE / OCT | toggles that attribute on the step |
| hold a step + encoder | locks the focused parameter on that step (a p-lock) |
| hold ACC (or SLIDE, TIE…) alone | the step pads show which steps have it; tap steps to toggle (TR style) |
| playing, REC off | the keyboard transposes the pattern live (the 303's track-mode transpose); KEYS switches to playing the voice live |
| hold MOTION | steps with automation light up; Delete clears it |

## 3. F1..F8 and the encoder

The kit's convention (FRAMEWORK.md, "The controls convention"): tap an F key to latch the encoder on its parameter; hold and turn to
change it for a moment; hold two and turn to move both; Shift + turn for coarse. PARAMS cycles the pages.

| Page | F1 | F2 | F3 | F4 | F5 | F6 | F7 | F8 |
|---|---|---|---|---|---|---|---|---|
| SOUND | Cutoff | Reso | Env Mod | Decay | Accent | Wave | Tune | Volume |
| MODS | Acc Decay | Filter Track | Slide time | Soft Attack* | Sub Osc* | Filter FM* | Sweep* | Muffler* |
| FX | Drive* | Drive Tone* | Delay time (sync) | Delay FB | Delay send | Reverb send* | — | Master |
| SEQ | Tempo | Swing | Length | Gate | Transpose | Scale | Density (generator) | Acc/Slide % |

\* not in track3r-os's `modules/tb303` (Drive, Soft Attack and the sub-oscillator were left out there for CPU); the delay exists as
`units/delay`.

With Shift held the soft-key row becomes the pattern tools: F1 RANDOM, F2 MUTATE, F3 REVERSE, F4 INVERT, F5 DOUBLE (length × 2,
copied), F6 CLEAR, F7 REDO, F8 UNDO.

## 4. The other buttons

| Button | 303 screen | + Shift |
|---|---|---|
| Play | play / stop, at the press | tap tempo |
| Rec | write mode; with Play, live recording of notes and encoder moves | — |
| ← → | step cursor (across pages) | rotate the pattern |
| ↑ ↓ | the step's note ±1 semitone | transpose the pattern |
| Note / Instr / Vol / Fx | momentary performance effects: MUTE, ACC ALL, SLIDE ALL, DELAY THROW | latch them |
| Pattern | tap: the 303 screen (again: step view / piano roll); hold: the 48 pads are 48 patterns, a tap queues one, several taps make a chain (as the original firmware does) | — |
| Copy / Insert / Delete | step or pattern: copy, insert, clear | paste / duplicate / clear the whole pattern |
| Params | the next page of F keys | the previous page |
| Song | the chain, with repeats and a transpose per entry | |
| Performance | the HOOKS screen | the split mode (section 7) |
| SampleLoad / Play / Edit / Rec | the HOOKS sub-pages | |
| Master / File / Config | mixer and master FX / projects, WAV and MIDI export / MIDI, clock, pads | |

The printed legends are the original firmware's; the screen always shows what the keys do (and a printed overlay is an option).

## 5. The screen

```
+--------------------------------------------------------------------------+
| A07 · LEN 16 · 132.0 BPM · SW 56% · ▶ PLAY · REC · NEXT A08 · KEYS TRNSP  |
+--------------------------------------------------------------------------+
|       1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16                     |
|  C3                  ▬                       ▬                           |
|  C2   ▬  ▬──▬     ▬     ▬  ▬  ▬▬▬▬▬▬     ▬          (a ramp is a slide)  |
|  acc  ▲        ▲           ▲        ▲                                    |
+--------------------------------------------------------------------------+
| motion CUTOFF  ▁▂▃▄▅▆▇█▇▆▅▄▃▂▁▁                                          |
+--------------------------------------------------------------------------+
|  CUTOFF   RESO   ENVMOD   DECAY   ACCENT   WAVE    TUNE    VOL           |  (the kit's knob strip)
|  F1       F2     F3       F4      F5       F6      F7      F8            |
+--------------------------------------------------------------------------+
```

## 6. The sequencer

Per step: note, octave, accent, slide, gate (note / tie / rest), and optional parameter locks. Pattern length 8, 16 (the default, a
pure 303), 32 or 64 steps, set per pattern (decision 8.2); the step pads show 16 steps a page. Swing, a per-pattern transpose. 48
patterns a project. Improvements over the original:

- note entry without the pitch-mode / time-mode split;
- motion recording (Rec + Play and turn) and p-locks per step, at 4 points a step;
- a random acid generator in a scale, and MUTATE;
- chains on the fly and a song mode;
- Devil Fish-style mods; drive and delay built in;
- MIDI clock in and out, the voice playable from MIDI, the sequence sendable to an external 303 clone;
- USB audio out: the master as a class-compliant audio interface on the computer (the kit's, o3-sdk M2b; the maintainer, 2026-10-25);
- undo; patterns as readable text on the card, to share.

```cpp
struct Step { std::uint8_t note, flags, cond; };  // flags: ACC | SLIDE | TIE | REST, the ratchet in bits 4-5; cond: the trig condition
struct Pattern {
  Step steps[64];
  std::uint8_t length, swing; std::int8_t transpose; std::uint8_t gate;  // length 8, 16, 32 or 64
  std::uint8_t motion[8][64 * 4];  // 8 lanes (the SOUND page), 4 points a step, 0xFF = no lock
  LaneRef lanes[8];                // {target, id}: the parameter each lane drives (M3b, docs/M3B-PLAN.md)
};                                 // 2,260 bytes a pattern; 48 patterns about 108 KB, in the SDRAM
```

The sequencer is a clocked hook (FRAMEWORK.md, "The engine"): steps from `for_each_step()` with swing, accent as `kNoteAccent`, slide as
`kNoteSlide`, motion as `Param` events at their frames.

## 7. HOOKS (milestone M4)

- 48 slots in 4 rows of 12; a row is a lane (one voice, one hook at a time, like a track in a clip launcher), e.g. VOX / LOOPS /
  CHOPS / ONE-SHOTS. LEDs: off empty, dim loaded, bright playing, slow blink queued, fast blink recording.
- Tap: launch or stop at the next quantise point (off / beat / bar / 2 / 4 bars). Shift + pad: select without launching. Hold + encoder:
  that hook's level. Delete or Copy + pad: clear or copy. Note / Instr / Vol / Fx: stop lane 1 / 2 / 3 / 4 (Shift: all).
- Sampling: Rec + an empty pad starts at the next bar and records exactly N bars, so the result is a loop at the tempo. Sources: line
  in, microphone, the FM radio, and a resample of the 303.
- In time with the tempo, cheapest first: REPITCH (varispeed; the default), SLICE (re-triggered slices, pitch kept; the chop code),
  STRETCH (granular; later, if the CPU allows). A hook's BPM is exact when recorded, or follows from the bars declared.
- F keys on the selected hook: VOL, PITCH, START, BARS, MODE (loop / one-shot / gate), SYNC, FILTER, SEND; Shift: NORMALIZE, REVERSE,
  FADE, CROP, SLICE, UNDO.
- Split mode (Shift + Performance): rows 0-1 are 24 hooks, rows 2-3 the 303's transpose keyboard, the F keys the SOUND page.

Both screens share the engine's transport: launches are scheduled to the sample, and the 303 keeps playing on the HOOKS screen.

## 8. Open decisions (the maintainer's)

1. ~~The ACC / SLIDE block: right (as drawn) or left.~~ Decided 2026-10-25: right, as drawn.
2. ~~Maximum pattern length.~~ Decided 2026-10-25: "16 steps, but can be configured to 8, 16, 32, or 64".
3. ~~One 303 voice or two.~~ Decided 2026-10-25: one voice. Fable advised cutting the second voice (its CPU goes to freeze,
   ducking and audio in; "two 303s" is MIDI out to an external one) and the maintainer said "haz todo lo que diga fable"
   (docs/M3B-PLAN.md, item C1).
4. ~~The reference the voice and its controls follow.~~ Decided 2026-10-25: "based on replicant 303" (Replicant 303 by DanimanX,
   MIT): its DSP (voice and distortion) is ported, and its preset generator and vault too (docs/M3-PLAN.md).
