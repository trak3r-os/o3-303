# M3c: the bass-music round (plan, contracts and ownership)

Status: planned, 2026-10-26. **Cut the same day** (the maintainer, short of weekly tokens: "recortamos hasta trance_gate y pump (que
se quedan fuera)"): this round builds **W, G and R** only. T (gate and pump), C (crush), P (dive and riser) and O (OTT) are out; they stay
below as the backlog. The slices are now one dsp+seq slice (the growl, the freeze modes, the wobble and its lane targets), then one ui slice
(the WOBBLE, ROLL, STOP and REV actions, a GROWL page, the wiring). The maintainer asked for Skrillex-style effects ("alguna idea para añadir efectos estilo skrillex?"); the
orchestrator proposed seven, and the maintainer said "vamos con todo" (all of them). "Skrillex" names a style only: no sample, preset or code from anyone's
records or plug-ins. All DSP is written here, or ported from a licence-compatible source after the licence gate (skill `dsp-port`).
It starts when M3b's ui-2 is under way; its dsp slices may run alongside M3b's ui because they own different files.

The aim: the 303 becomes a performance bass for dubstep and riddim. A wobble that changes rate under the player's fingers, a
talking growl, build-up rolls and tape stops, gated and pumped bass, dives and risers, and the "everything loud" multiband squash. All of it is played from the
pads and the four performance buttons (M3b C4).

## Items

| Id | Item | Owner | Cost [est] |
|---|---|---|---|
| W | **Wobble.** A tempo-synced LFO in the sequencer's hook that drives any lane target (default: the voice's Cutoff; the growl's Vowel is the classic). Rates 1/1, 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32, dotted 1/8 and 1/16. Shapes sine, triangle, saw down, saw up, square, sample & hold. Depth, and a phase reset on each note (on by default: the wobble restarts with the note). Played: hold the WOBBLE action and a pad of row 4 picks the rate live (the LEDs show it); its release goes back to the pattern's rate. A per-step rate and depth through a motion lane | seq, ui | about 0.1 % (Param events every 16 frames, like the slew) |
| G | **Growl**, a new unit `growl` on slot 0's insert 1 (after the distortion). A formant filter of three band-passes that morphs through the vowels A E I O U (Vowel 0..1, continuous), with Q and Mix, then a comb with feedback tuned to the playing note (key tracked from a note parameter the sequencer sets), with Tune offset, Feedback (±) and Mix. The wobble on Vowel gives the talking bass | dsp | 2-3 % |
| C | **Crush**, a new unit `crush` on slot 0's insert 2. Bit depth (16..1, continuous), sample-rate reduction (44.1 kHz down to about 500 Hz, without interpolation, aliasing on purpose), Mix, and a pre-gain | dsp | under 0.5 % |
| R | **Rolls, tape stop, reverse**: the freeze unit gains modes, all appended as parameters (append, never insert; FREEZE stays bit for bit what was verified on the unit 2026-10-26). ROLL: a retriggered loop at 1/8, 1/16, 1/32, 1/64, with Accelerate (each bar, or each beat, halves the roll until 1/64: the build-up roll). STOP: tape stop, the read speed falling to 0 over Stop Time (1/8 bar .. 2 bars) with linear interpolation, and a spin-up back on release. REVERSE: the ring played backwards from the entry. Each is a performance action (ROLL, STOP, REV) | dsp, ui | about +1 % while active (an interpolated read) |
| T | **Trance gate and pump.** Both write slot 0's mixer from the hook (`BlockContext::set_mix`, ramped across a block, so click-free). GATE: a 16-step on / off pattern from a table of 16 built-in rhythms, at a rate (1/8, 1/16, 1/32), with depth and smoothing. PUMP: a sidechain-style duck on each beat (or each half bar), with depth and release, on the slot level and optionally send A. Actions GATE and PUMP (held or latched) | seq, ui | negligible |
| P | **Dive and riser.** Through the slew. DIVE: the voice's Tune falls N semitones (12 by default) over a time while the action is held, and comes back on release. RISE: the build-up, Cutoff and Tune up and the delay send up over 1, 2 or 4 bars, synced to the bar, reset at the next downbeat or on release. Actions DIVE and RISE | seq, ui | negligible |
| O | **OTT-style multiband squash**, a new unit `ott` on master insert 1 (after the freeze). Three bands (Linkwitz-Riley crossovers near 88 Hz and 2.5 kHz), each with downward and upward compression at fixed, steep ratios, Depth (the blend), Time (attack and release together), In and Out gain, Upward and Downward amounts. Gains computed every 16 frames and interpolated | dsp | 5-8 %, to measure on the unit before it is on by default |
| K | **Four inserts per slot and on the master** in o3-sdk: **done 2026-10-26**, o3-sdk 84618d9 | integrator | none |

The budget: the unit measured o3-303 at about 37 % of a block playing, with a 43.6 % peak (`docs/hw/2026-10-26-m3ba.md`); the governor sheds at 60 %.
These estimates add about 9-12 % with everything on: tight but inside. Each new unit is measured on the unit (the render CPU with it active,
through the console) before it is relied on. OTT ships off by default until then.

## The signal chain after M3c

```
slot 0: acid_voice ─ distortion ─ growl ─ crush ─ level/pan (gate, pump) ─┬──────────▶ master ─ freeze (+ roll, stop, rev) ─ ott ─ level ─ out
                                                                      └─ send A ─ delay ─┘
```

The units' ids keep their order: voice 0, distortion 1, delay 2 and freeze 3 are the tests' contract. Growl, crush and ott attach after them.

## Contracts (the integrator makes them when the first slice that needs them starts)

**Unit parameter tables.** The id is the index: append, never insert. The engineer of each unit writes the table in its header and the
plan's "Made" paragraph. The first ids are fixed here:
- growl: 0 Vowel, 1 Formant Q, 2 Formant Mix, 3 Comb Tune (semitones), 4 Comb Feedback, 5 Comb Mix, 6 Note (set by the sequencer, MIDI
  note), 7 Mix (the whole unit's wet/dry).
- crush: 0 Bits, 1 Rate, 2 Pre-gain, 3 Mix.
- ott: 0 Depth, 1 Time, 2 In gain, 3 Out gain, 4 Upward, 5 Downward, 6 Active.
- freeze (appended after 2 Mix): 3 Mode (FREEZE, ROLL, STOP, REVERSE), 4 Roll Rate, 5 Accelerate (OFF, BEAT, BAR), 6 Stop Time.

**`pattern.h`, LaneRef targets** (added by the seq slice of M3c; the Pattern's size is unchanged): 5 the growl, 6 the crush, 7 the
sequencer's own performance values (id 0 wobble rate, 1 wobble depth, 2 gate depth, 3 pump depth). A motion point stays 0..254 of the
target's range.

**The sequencer** (seq slice): `set_wobble(...)`, `set_gate(...)`, `set_pump(...)`, `dive(...)`, `rise(...)` from the main loop, and the
wobble rate pad override. The hook emits the wobble as Param events every 16 frames toward the target (like the slew), and the gate and
the pump as mixer writes once a block. The growl's Note parameter follows the playing note (one Param event a note).

**Performance actions** (ui; the C4 list grows): WOBBLE, ROLL, STOP, REV, GATE, PUMP, DIVE, RISE, after M3b's MUTE, ACC ALL, SLIDE ALL, OCT
UP, FILL, THROW, FREEZE.

## Slices

| Slice | Owns | Content | Starts |
|---|---|---|---|
| D1 | `units/growl`, `units/crush`, their tests | G, C | when an agent slot is free (alongside M3b ui-2) |
| D2 | `units/freeze` | R (modes appended; FREEZE byte-identical) | after D1, or alongside it |
| D3 | `units/ott` | O | after D1 |
| S | `app/seq`, `pattern.h` targets | W, T, P, the growl's Note, the lane targets | after M3b ui-2 lands (no app/src edits) |
| U | `app/src` | the actions, the pages for GROWL, CRUSH, OTT, WOBBLE / GATE, the wiring of the new units | after S and M3b closes |

Every unit gets the o3-303 unit standards:
- a host oracle or a behavioural test;
- a click-free bound where a parameter jumps;
- block-size independence;
- the cost counted from the cross build's disassembly;
- no `double`, no heap;
- buffers in the SDRAM with `O3SDK_BIG`.

Done when:
- the host build has 0 warnings and ctest passes;
- each new sound has a scenario with its WAV listened to through a plot;
- the display list stays under 1,800 words;
- `o3_303.ptf` passes the gate;
- the unit measures the render CPU with everything on below the governor;
- the tester records each effect over USB, with no clicks where none are meant.


## Made (dsp+seq), 2026-10-26

Built: G (units/growl), R (the freeze modes) and W (the wobble in app/seq). Nothing is wired into the app (the ui slice does that), so no scenario was added and the existing ones are untouched.
- **growl** (Effect, slot 0 insert 1): 0 Vowel 0..1 (A E I O U), 1 Formant Q 1..20, 2 Formant Mix, 3 Comb Tune +-24 st, 4 Comb Feedback +-0.95, 5 Comb Mix, 6 Note (set by the sequencer), 7 Mix (default 0: bit-exact bypass). Three parallel TPT band-passes (Peterson-Barney male formants) then a peak-normalised feedback comb (linear-interpolated delay, 2048 floats in the unit, no SDRAM); the wet path is mono. Vowel, pitch, mixes smoothed at a 16-frame control rate. Cost [est], from the cross build: see the unit's header; to be measured on the unit.
- **freeze** (appended): 3 Mode FREEZE/ROLL/STOP/REVERSE, 4 Roll Rate 1/8..1/64, 5 Accelerate OFF/BEAT/BAR, 6 Stop Time 1/8..2 bars. FREEZE is bit for bit the old unit (checked on 1.5 M frames of random gesture against HEAD). ROLL = freeze with the rate as loop; a shorter rate takes the head of the old loop (no join), a longer one leaves and enters again, on a ring that holds the roll's own repeats (so the longer loop is copies of the short one plus about 5 ms of input; a fresh capture would need `len` frames of real input first: open). STOP and REVERSE leave the ring a true record of the input. REVERSE is limited to (ring - headroom) / 2 and then starts over.
- **sequencer**: `set_wobble(on, rate, shape, depth, LaneRef)`, `set_wobble_reset`, `wobble_rate_override(rate | kWobbleRateNone)`, `wobble()`, `wobble_rate_now()`, `set_unit(kLaneGrowl, id)` (after setup(), before start()). pattern.h: kLaneGrowl = 5, kLaneSeq = 6 (id 0 rate index, 1 depth), kLaneTargetCount = 7; Pattern unchanged. The hook sets the growl's Note (id 6) on every note start. Wobble depth d swings the target +-d/2 of its range around its base (lock, else slew, else knob; after a morph's slew, its goal), clamped to the range, so it flat-tops near the ends and depth 1 sweeps the whole range only from a centred base, 4 events a block + one per note restart (kWobbleEvents = 8).
- Not measured: CPU of any of it on the unit.

## Made (ui), 2026-10-26

Built: the wiring of G, W and R, the WOBBLE, ROLL, STOP and REV actions, and the GROWL and WOBBLE pages. Sound unchanged until a performer asks for it: the growl's Mix is 0 (a bit-exact bypass) and FREEZE alone is as it was.
- **Wiring** (`o3_303_app.cpp`): the growl is attached last (unit ids voice 0, distortion 1, delay 2, freeze 3, growl 4), on slot 0's insert 1 after the distortion, and given to the sequencer with `set_unit(kLaneGrowl, id)` after `setup()`; `Wiring::growl` and `Wiring::wobble` (rate, shape, depth, reset, target) hold the rest. A run starts with the wobble off, its pad override cleared and the reset on.
- **Pages**: the Params cycle is now SOUND, FX, GROWL, WOBBLE, SEQ, PERF, SNAP (`kPageCount` 7; two pages because the growl's seven keys and the wobble's five do not fit eight; SEQ, PERF and SNAP are two further Params from SOUND, so the tap counts of the scenarios moved by two). GROWL: F1 VOWEL, F2 FMT Q, F3 FMT MIX, F4 CMB TUNE, F5 CMB FB, F6 CMB MIX, F7 GRL MIX (the unit's Mix); the Note is the sequencer's. WOBBLE: F1 RATE (the eleven rates), F2 SHAPE, F3 DEPTH, F4 RESET (on note), F5 TARGET (CUTOFF or VOWEL). The growl's keys are lane targets (`kLaneGrowl`). PERF: F5 FRZ LEN, F6 ROLL RT, F7 ROLL ACC, F8 STP TIME.
- **Key map**: the C4 list is MUTE, ACC ALL, SLD ALL, OCT UP, FILL, THROW, FREEZE, then WOBBLE, ROLL, STOP, REV (appended: an assignment is a number; the mask is 16 bits). WOBBLE: held (or Shift-latched) the wobble runs at the WOBBLE page's settings; the bottom row of pads (row 4, columns 0..10) is then the rate picker, column c = rate c (1/1 1/2 1/4 1/4T 1/8 1/8T 1/16 1/16T 1/32 1/8D 1/16D), live through `wobble_rate_override`; a pad stays picked until the action lets go; the LEDs show `wobble_rate_now()` (full level, the others dim, column 11 dark; they follow at the next frame); the release switches the wobble off and gives the pattern's rate back (`kWobbleRateNone`); a change of a setting while it runs applies at once. ROLL, STOP and REV write the freeze's Mode (1, 2, 3), then Active; the release writes Active off only and **leaves the Mode** (nothing touches the unit while it leaves, so nothing can click; each entry writes its own Mode first, FREEZE too: Mode 0). One of FREEZE, ROLL, STOP and REV owns the unit at a time (`FreezeOwner`): the first to go on; a second pressed under it does nothing, and one still held when the owner lets go is not picked up, press it again.
- **Scenarios**: `growl.txt` (Vowel wobble at 1/8 then 1/16 over the factory line; A/B against the same run without the action: bit-exact until the press, different while held, converged after the release) and `freeze_modes.txt` (ROLL with Accelerate BEAT, STOP, REV; the output is bit-exact with the run without the actions in between them). The existing scenarios' Params counts moved by two; `pages.txt` shows the two new pages.
- Display list: the peak of the worst-case test is 1,412 words (before: about 1,400), the scenarios 781..910 (the vault 1,395).
- Not done: the new values are not in the snapshots (SNAP holds the SOUND, FX targets as before), the wobble's settings are not in a preset. Not measured: the CPU of the growl, the wobble and the freeze modes on the unit; the click-freedom of the exits on the unit.
