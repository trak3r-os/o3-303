# M3b: Fable's round (plan, contracts and ownership)

Status: planned, 2026-10-25; starts when M3 (`docs/M3-PLAN.md`) is reviewed, folded and verified. The maintainer asked Fable what it
would add to the 303 and then said: "haz todo lo que diga fable, oki?" (do everything Fable says). This round turns Fable's answer
into work. Its frame: a 303 is played with two hands on cutoff, reso, env mod and accent while the pattern runs, and this unit has one
encoder, so most items give the player "a second hand".

## Fable's items

Ranked additions (R), screen critiques (S), changes to the design (C), and what M3 left open (L). Owner = the slice that builds it (M3-PLAN's slices).

| Id | Item | Owner | Needs |
|---|---|---|---|
| R1 | **Pad filter ladders.** Hold KEYS: row 2 is 12 positions of Cutoff, row 3 of Reso (Shift: Env Mod); a tap jumps there with a ~30 ms slew; the LEDs draw the position as a bar. A tap of KEYS still toggles live play | ui, seq (slew) | nothing |
| R2 | **Snapshots and morph.** 8 snapshots of SOUND + MODS + FX (+ slot level and send A) on the F keys of a SNAP page (in the PARAMS cycle and on Shift + Params); tap recalls, Shift + F stores; the encoder morphs from the previous snapshot to the last one recalled, through the slew | ui, seq (slew) | nothing |
| R3 | **Momentary FILL.** Hold the FILL performance button: the pattern plays a derived variant (MUTATE, OCT UP, RATCHET or ACC ROTATE, chosen on the SEQ page) and reverts on release; Shift latches. **Layer rotate:** hold ACC (or SLIDE) + ← → rotates only that layer | seq, ui | nothing |
| R4 | **Ratchets per step:** ×1..×4. Hold a step + OCT+ / OCT- (the step's octave moves to hold a step + ↑ ↓), or hold a step + the encoder on SEQ's RATCHET | seq, ui | nothing |
| R5 | **Trig conditions:** always, a probability, A:B (play on loop A of every B), FILL, NOT FILL, FIRST, NOT FIRST. Hold a step + the encoder on SEQ's COND | seq, ui | nothing |
| R6 | **Beat freeze / stutter**, a new unit `freeze` on master insert 0: loops the last 1/4, 1/8 or 1/16 of a bar of the master, pitch kept, click-free; on the Fx performance button | dsp, ui | nothing |
| R7 | **Tempo nudge:** ±3 % while held, snaps back. **Tempo and swing get permanent places** (see "Gestures") | ui | nothing |
| R8 | **Scale-locked keyboard:** with a scale set, out-of-scale keyboard pads go dark and are skipped | ui | nothing |
| R9 | **Ducking from line in:** an envelope follower on the input ducks slot 0 and the delay return (depth, release) | dsp, ui | o3-sdk M2c (audio in) |
| R10 | **External audio through the 303 filter and distortion** (the Devil Fish's audio in), the accent envelope keyed by the sequencer | dsp, ui | o3-sdk M2c (audio in) |
| R+ | **Age:** a small per-note drift of pitch and cutoff, a MODS parameter | dsp | W4 (tb303 id 16, after the reserved 8..15) |
| S1 | **Step view:** draw the pattern's real pitch range ±3 semitones; octave lines bright; note names only on non-C notes; beat columns 1/5/9/13 on a visibly different background; the playhead column is the one saturated thing | ui | nothing |
| S2 | **Knob strip:** a small F1..F8 inside each box and no second label row; larger values readable standing; the value as a background fill of the box, not a 2 px line; labels dim and values bright everywhere (the header's LEN / SW / GATE row too) | ui | nothing |
| S3 | **Piano roll and vault:** dim the white keys (they outshine the notes); off states drawn as outlines ("MOTION REC OFF" in a filled box reads as on); the motion lane gets a faint curve and min / max ticks; the vault's help becomes a one-line footer; keep the preset card's density | ui | nothing |
| S4 | **The 303 panel skin** (the maintainer chose "mockup A obsidian", 2026-10-25; the design spike's NOTES and mockups): the main screen becomes a smoked red-black acrylic panel: a header with red 7-segment PATTERN and TEMPO displays and LEDs, the step view in a dark glass drawer, a row of eight spun-metal knobs F1..F8 (static shaded caps, the pointer a LINE) with a latch LED and a value under each (it is S2's knob strip). One skin (obsidian), L4/L8 alpha masks tinted at draw time, about 116 KB of RAM_G (115,632 bytes; 177,072 with a second palette), in o3-sdk's app region `kRamGApp`, about 400 words a frame of the screen's 856-870. Its busiest line, the knob row, asks the FT812 for 2,034 bitmap and 2,174 filled pixels (the design spike's count). Later: the full-screen PANEL page (variant B) on Shift + Pattern, its MIX knob being the distortion's Mix. Needs o3-sdk: an app RAM_G region and asset upload, a bitmap handle, a full-screen screen; first the unit's fill-rate check (apps/fillrate) | ui, o3-sdk | the fill-rate check on the unit; the kit changes |
| C1 | **One voice.** The second voice is cut (DESIGN-303 decision 8.3 is closed); the CPU goes to freeze, ducking and audio in, and "two 303s" is MIDI out to an external one | integrator | nothing |
| C2 | **Motion lanes are a per-pattern table of parameter references** (unit, id), SOUND as the default, so the lanes can be the delay send (throws), Drive or Acc Decay | integrator (contract), seq, ui, o3-sdk | o3-sdk: mixer writes from a hook |
| C3 | **SEQ page:** transport and generator separated. Tempo and Swing keep places; Scale, Density and Acc / Slide % move to a RANDOM popup on Shift + F1 | ui | nothing |
| C4 | **The four performance buttons are assignable** from MUTE, ACC ALL, SLIDE ALL, OCT UP, FILL, THROW, FREEZE (an o3-303 page in the kit's system Config, o3-sdk FRAMEWORK.md section 5: the kit owns the Config button; until that lands, a page of o3-303's own); defaults Note = MUTE, Instr = ACC ALL, Vol = FILL, Fx = FREEZE (SLIDE ALL leaves the defaults, as Fable says) | ui | nothing |
| L1 | **Vault headroom** (left open by M3): **done 2026-10-25.** FUZZ's negative half bounded (Replicant's Pade `fastTanh` rose again past |x| = 3) and a wet trim that follows Drive; all 512 slots peak at 0.912 at most (was 1.877); the headroom test is strict. Open: GRIT overshoots its nominal 0.85 for the same Pade reason (wet peak 1.2 at default Drive), left so the default sound is unchanged | dsp | nothing |

Kept, as Fable says: the keyboard transposes when REC is off, pattern taps build a chain, the pattern header.

R9 and R10 wait for o3-sdk M2c; R+ goes with W4. Everything else is this round.

## Contracts (changes; the integrator makes them at the start of the round)

**`pattern.h`:**
- `Step {note, flags, cond}`: flags bits 4-5 are the ratchet (0 = one hit .. 3 = four hits, `kStepRatchetShift`,
  `kStepRatchetMask`); `cond` is the trig condition: 0 always; 1..99 a probability in percent; `0x80 | (a - 1) << 3 | (b - 1)` for
  A:B with 1 <= a <= b <= 8; `0xC0` FILL, `0xC1` NOT FILL, `0xC2` FIRST, `0xC3` NOT FIRST; other values are reserved and play as
  always.
- `Pattern` gains `LaneRef lanes[kMotionLanes]`, `LaneRef {target, id}`: target 0 none, 1 the voice (tb303 id), 2 the distortion,
  3 the delay, 4 slot 0's mixer (id = `dsp::MixField`). The default lanes are the SOUND page: tb303 ids 0..6 and the mixer's Level.
  A motion point stays 0..254 of the target's range (the mixer's ranges are fixed in `pattern.h`).
- `Pattern` is 4-byte aligned and a multiple of 4 bytes (the Bank copies words); about 2,260 bytes.

**o3-sdk (made in o3-sdk under its own rules):** `BlockContext` lets a hook write a slot's mixer field for the current block
(ramped like a main-loop write), so a motion lane can drive the send A level or the slot level.

**The slew (seq):** the sequencer's hook owns a small set of slewing targets (unit, id or a mixer field), set from the main loop, and
emits `Param` events (mixer writes for the mixer) every 16 frames toward them over a given time: the pad ladders (30 ms) and the morph
use it. A p-lock or a motion point on the same target overrides the slew for its step.

**The `freeze` unit (dsp):** `units/freeze`, an effect on master insert 0. Parameters: 0 Active (OFF / ON), 1 Length (1/4, 1/8,
1/16 of a bar, from the transport's tempo), 2 Mix. It records the master into an SDRAM ring all the time (at most 1 MB) and, when
active, loops the last Length with short crossfades at the loop point and at entry and exit.

## Gestures this round adds (where Fable's gesture collided, the nearest free one)

| Gesture | Effect |
|---|---|
| hold KEYS + row 2 / row 3 pad | cutoff / reso ladder (Shift: env mod on row 3) |
| tap Master | the encoder latches TEMPO; Shift + tap Master latches SWING |
| hold Master + turn | tempo nudge ±3 %, back on release (Fable said Shift + turn, but that is the kit's coarse adjust on every page) |
| hold a step + OCT+ / OCT- | the step's ratchet up / down |
| hold a step + ↑ ↓ | the step's octave (↑ ↓ alone stay ±1 semitone on the cursor step) |
| hold ACC / SLIDE + ← → | rotate that layer only |
| Shift + Params | the SNAP page (it was the previous page) |
| Shift + F1 | the RANDOM popup (scale, density, acc / slide %, then generate) |
| Note / Instr / Vol / Fx | the assigned performance actions (C4), Shift latches |

## Slices

Same slices and ownership as M3 (dsp, seq, ui; the integrator owns `pattern.h`, the docs and the o3-sdk change). Build dirs as in
M3. Done when: host build 0 warnings and ctest passes (new tests for ratchets, conditions, fill variants, layer rotate, lane targets,
the slew, freeze); every new gesture and screen change has a scenario with its PNGs looked at; `o3_303.ptf` builds with its image
ctests and no `__aeabi_d*`; the display list stays under 1,800 words in every scenario; the render's estimated CPU stays under the
budget with freeze active.
