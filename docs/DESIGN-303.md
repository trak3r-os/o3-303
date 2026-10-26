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
| hold a step + ACC / SLIDE / TIE / REST | toggles that attribute on the step |
| hold a step + OCT+ / OCT- | the step's ratchet one hit up or down (x1 to x4) |
| hold a step + up / down | the step's octave |
| hold a step + encoder | locks the focused parameter on that step (a p-lock), on the lane that drives it; on the SEQ page's RATCHET and COND the step's own ratchet and trig condition |
| hold ACC (or SLIDE) + left / right | rotates that layer of the pattern alone, one step |
| hold KEYS + a pad of rows 2 and 3 | the filter ladders: row 2 is 12 places of Cutoff, row 3 of Reso (Shift: Env Mod), through a 30 ms slew; the LEDs draw the position as a bar. A tap of KEYS toggles live play as before |
| a scale chosen in the RANDOM popup | the keyboard pads out of it go dark and do nothing; lit means playable. The generator's default scale is not a choice: the keyboard is chromatic until the popup's Scale key is turned, and "No scale" or "Chromatic" unlocks it |
| hold ACC (or SLIDE, TIE…) alone | the step pads show which steps have it; tap steps to toggle (TR style) |
| playing, REC off | the keyboard transposes the pattern live (the 303's track-mode transpose); KEYS switches to playing the voice live |
| hold MOTION | steps with automation light up; Delete clears it |
| hold MOTION + an F key | the motion lane shown under the step view now drives that key's parameter (Drive, the delay send, Acc Decay ...): the lanes are a per-pattern table, SOUND by default |

## 3. F1..F8 and the encoder

The kit's convention (FRAMEWORK.md, "The controls convention"): tap an F key to latch the encoder on its parameter; hold and turn to
change it for a moment; hold two and turn to move both; Shift + turn for coarse. PARAMS cycles the pages.

| Page | F1 | F2 | F3 | F4 | F5 | F6 | F7 | F8 |
|---|---|---|---|---|---|---|---|---|
| SOUND | Cutoff | Reso | Env Mod | Decay | Accent | Wave | Tune | Volume |
| MODS | Acc Decay | Filter Track | Slide time | Soft Attack* | Sub Osc* | Filter FM* | Sweep* | Muffler* |
| FX | Drive* | Drive Tone* | Delay time (sync) | Delay FB | Delay send | Reverb send* | — | Master |
| SEQ | Tempo | Swing | Length | Gate | Transpose | FILL's variant | Ratchet (of the step) | Trig condition (of the step) |
| PERF | Note's action | Instr's | Vol's | Fx's | Freeze length | — | — | — |
| SNAP | snapshot 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| RANDOM popup (Shift + F1) | Scale | Density (generator) | Acc/Slide % | — | — | — | Close | Generate |

\* not in track3r-os's `modules/tb303` (Drive, Soft Attack and the sub-oscillator were left out there for CPU); the delay exists as
`units/delay`.

PERF is o3-303's own page until the kit's system Config exists (o3-sdk FRAMEWORK.md section 5). SEQ is the transport and the step's settings;
the generator's settings are the RANDOM popup's (M3b C3): Shift + F1 opens it over the knob strip, F1..F3 latch the encoder on Scale, Density and
Acc / Slide % (the accent chance, with the slide chance at four fifths of it; the Scale goes through the same control as ever, so the keyboard's
scale lock engages), F8 makes a new line (again and again, the settings stay), F7 or Shift + F1 or Params closes it. The encoder can also be
latched on the tempo (tap Master) or the swing (Shift + tap Master), whatever page the F keys are on.

SNAP (M3b R2) is the page of eight snapshots of the sound: SOUND, the distortion, the delay, the level and the delay send (MODS joins when it
exists), in RAM, not saved. A tap on its F key recalls (the values glide in 30 ms), Shift + an F key stores what the sound is now. The encoder
morphs from the snapshot recalled before (the sound as it was, for the first recall) to the one recalled last: 32 detents, Shift + turn four
at a time; a switch (the wave, the distortion's model, the delay's time and mode) changes at the middle. It runs on the sequencer's slew, and
the engine stores the blend when the glide is over. SNAP is the last page of the PARAMS cycle and what Shift + Params gives; Shift + Params
again goes back to the page it came from. Shift + F stores here, so there is no tools row on this page.

With Shift held (and the encoder still for 600 ms) the knob strip becomes the pattern tools: F1 RANDOM (the popup), F2 MUTATE, F3 REVERSE,
F4 INVERT, F5 DOUBLE (length × 2, copied), F6 CLEAR, F7 REDO, F8 UNDO; they work from the press of Shift.

## 4. The other buttons

| Button | 303 screen | + Shift |
|---|---|---|
| Play | play / stop, at the press | tap tempo |
| Rec | write mode; with Play, live recording of notes and encoder moves | — |
| ← → | step cursor (across pages); with ACC or SLIDE held, rotate that layer | rotate the pattern |
| ↑ ↓ | the cursor step's note ±1 semitone; with a step held, its octave | transpose the pattern |
| Note / Instr / Vol / Fx | the four performance buttons, each one of MUTE, ACC ALL, SLIDE ALL, OCT UP, FILL, THROW (the delay send to unity) or FREEZE (the beat freeze), assigned on the PERF page; momentary; the defaults are MUTE, ACC ALL, FILL, FREEZE | latch them |
| Pattern | tap: the 303 screen (again: step view / piano roll); hold: the 48 pads are 48 patterns, a tap queues one, several taps make a chain (as the original firmware does) | — |
| Copy / Insert / Delete | step or pattern: copy, insert, clear | paste / duplicate / clear the whole pattern |
| Params | the next page of F keys (SOUND, FX, GROWL, WOBBLE, SEQ, PERF, SNAP) | the SNAP page, and back |
| Song | the chain, with repeats and a transpose per entry | |
| Performance | the HOOKS screen | the split mode (section 7) |
| SampleLoad / Play / Edit / Rec | the HOOKS sub-pages | |
| Master | tap: the encoder latches TEMPO (Shift: SWING); hold + turn: a tempo nudge of 1 % a detent, at most 3 %, back at the release (the mixer and master FX come later) | |
| File / Config | projects, WAV and MIDI export / MIDI, clock, pads | |

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
|  F1 CUTOFF  F2 RESO  F3 ENV MOD  F4 DECAY ...  (the knob strip: the F-number and the  |
|  350 Hz     65 %     60 %        750 ms        name small and dim, the value large     |
|  ▓▓▓▓░░░░░░ ...                                  and bright, as the fill of the box)    |
+--------------------------------------------------------------------------+
```

The step view (M3b S1) draws the pattern's real pitch range and three semitones each side, the octave lines (the Cs) bright and the Cs named in
the gutter, note names above the bars only for the notes that are not a C; the beat columns (1, 5, 9, 13) have a lighter ground and the playhead's
column is the one saturated thing. A ratchet is the bar split into its hits; under the ACC / SLD / TIE rows an R/C row has the ratchet's number
and the trig condition of each step, and the step the RATCHET and COND keys edit (the held one, else the cursor's) is framed. The knob strip
(S2) has the F-number and the name in each box, no second label row (the kit's key row keeps its F-number caps and no names), larger values and
the value as the fill of the box; labels are dim and values bright, the header's LEN / SW / GATE / TR row too. S3: in the piano roll the white keys
are dim; a state that is off (MOTION REC OFF, the KEYS or TRNSP chip that is not the mode, a performance button at rest) is an outline, not a
filled box; the motion lane has a faint curve of what it plays and ticks at its lowest and highest lock; the vault's help is one footer line.

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
