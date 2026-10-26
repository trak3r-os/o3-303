# M3c: the bass-music round (plan, contracts and ownership)

Status: planned, 2026-10-26. The maintainer asked for Skrillex-style effects ("alguna idea para añadir efectos estilo skrillex?"); the
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
slot 0: tb303 ─ distortion ─ growl ─ crush ─ level/pan (gate, pump) ─┬──────────▶ master ─ freeze (+ roll, stop, rev) ─ ott ─ level ─ out
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
