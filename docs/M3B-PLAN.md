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
| R+ | **Age:** a small per-note drift of pitch and cutoff, a MODS parameter | dsp | W4 (acid_voice id 16, after the reserved 8..15) |
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
- `Pattern` gains `LaneRef lanes[kMotionLanes]`, `LaneRef {target, id}`: target 0 none, 1 the voice (acid_voice id), 2 the distortion,
  3 the delay, 4 slot 0's mixer (id = `dsp::MixField`). The default lanes are the SOUND page: acid_voice ids 0..6 and the mixer's Level.
  A motion point stays 0..254 of the target's range (the mixer's ranges are fixed in `pattern.h`).
- `Pattern` is 4-byte aligned and a multiple of 4 bytes (the Bank copies words); about 2,260 bytes.

**Made 2026-10-25** (`pattern.h`, `tests/pattern_test.cpp`): `Pattern` is 2,260 bytes (was 2,180), `alignas(4)`, with the asserts. The pure
helpers are `ratchet_of`, `ratchet_hits`, `with_ratchet`; `cond_probability`, `cond_ratio`, `decode_cond`, `encode_cond`, `cond_valid`
(a reserved value decodes as `Always`; an invalid request encodes as `kCondAlways`); `kLane*` targets, `kMixer*` ids (the values of
`dsp::MixField`, checked in the test), `kMixerRange` (level and sends 0..1.5 linear gain, pan -1..1, mute 0..1) with
`mixer_point_to_value` / `mixer_value_to_point`, and `kDefaultLanes`. `clear()` sets cond 0 and the default lanes. (When this was made the
sequencer did not read `cond`, the ratchet or `lanes`; it does now, see "Made (seq slice)". The default's lane 7 is the mixer's level; in M3
lane 7 was the voice's id 7, Volume, which no page could write, so no pattern has a point there.) Operations that rewrite a whole step (`write_advance`, `random_fill`, a deleted
step, `clear_pattern`) reset its ratchet and condition; the ones that move a step (insert, delete, rotate, reverse, double, the step
clipboard) carry them; `set_flag` and `mutate` leave them alone.

**o3-sdk (made in o3-sdk under its own rules):** `BlockContext` lets a hook write a slot's mixer field for the current block
(ramped like a main-loop write), so a motion lane can drive the send A level or the slot level.

**The slew (seq):** the sequencer's hook owns a small set of slewing targets (unit, id or a mixer field), set from the main loop, and
emits `Param` events (mixer writes for the mixer) every 16 frames toward them over a given time: the pad ladders (30 ms) and the morph
use it. A p-lock or a motion point on the same target overrides the slew for its step.

**The `freeze` unit (dsp):** `units/freeze`, an effect on master insert 0. Parameters: 0 Active (OFF / ON), 1 Length (1/4, 1/8,
1/16 of a bar, from the transport's tempo), 2 Mix. It keeps the master in an SDRAM ring (at most 1 MB): off, it records the master all the
time; active, the ring holds the loop itself (the master is not recorded during a freeze, so the loop cannot be overwritten), and the unit loops
the last Length with short crossfades at the loop point and at entry and exit.

**Made 2026-10-25 (dsp slice)** (`units/freeze`, `tests/freeze_app_test.cpp`, `app/scenarios/freeze.txt`): class `o3_303::Freeze`, ids 0 Active
(default OFF), 1 Length (1/4, 1/8, 1/16 of a bar, default 1/8), 2 Mix (0..1, default 1); attached after the delay (unit id 3), master insert
0, ring 131,072 frames a side (1 MiB, `O3SDK_BIG`). Off, the master passes through bit for bit (every scenario's WAV is unchanged). The loop is
latched at entry (a tempo or Length change while frozen waits for the next entry); the loop point is a 5 ms crossfade baked into the ring at
entry (paced, at most 32 frames in a 64-frame block, through an exit if one comes first), so while frozen the ring holds the loop itself
(the master is not recorded during a freeze); an exit asked for within 5 ms of the entry waits for the entry crossfade, and an entry asked
for before the previous one's baking is over (a few frames after the shortest freeze) waits for it. Main loop (`app/src/rig.h`): `set_freeze(engine, rig, on)`, `freeze_on`, `set_freeze_length`,
`freeze_length`, or `Engine::set_param(rig.wiring.freeze, Freeze::Active, 1)`. The Fx performance button's FREEZE
action replaced the provisional Song key (ui-1, below). The cost is estimated at +0.6 % of a block off
(recording), +2.3 % held, +4.2 % in the worst block (an entry, or an exit that overlaps the end of the baking; `src/freeze.cpp`); measure it
on the unit.

**Made 2026-10-25 (seq slice)** (`app/seq`, `tests/seq_m3b_app_test.cpp`, `app/scenarios/m3b_seq.txt`): the hook plays what the contracts
describe; `sequencer.h` has every rule, these are the ones the ui slice needs. *Ratchet:* hit k of n at `start + k * len / n` ticks (swing
included), each with the gate of its part; the step's accent is on the first hit (ACC ALL accents all); a slide step holds its last hit into
the next step, a step after a slide glides on its first hit only; a ratchet on a rest or a tie is ignored. *Condition:* evaluated at the
step's start; a failed step acts as a rest, and a note before a tie step asks the tie's condition (the draw is kept for the tie's own start: one draw a step); pass counter 0 on the first pass since Play, a jump or the chain loading the pattern; A:B is
`pass % B == A - 1`; probabilities draw from the sequencer's own `Rng` (one draw per probability step; `set_seed()`, a fixed default). *FILL
(R3):* `kPerfFill` (`set_fill(bool)`), seen at each step start, so entry and exit are on step boundaries; the variant (`FillVariant` in
`ops.h`: MUTATE, OCT UP, RATCHET, ACC ROTATE; `set_fill_variant()`) is built from a copy of the playing pattern; FILL / NOT FILL follow the
same state; `Playhead::fill` says it is playing. *Lanes (C2):* lane i drives `Pattern::lanes[i]`; unit targets as Param events, the mixer
through `set_mix`; `setup(engine, voice, dist, delay)`; an unavailable target is silent. *Slew:* `slew(target, id, value, ms, store = true)` for
a voice, distortion or delay parameter or a slot 0 mixer field, in the target's own curve; at most 32 at once (a 33rd jumps to its goal, counted by
`slew_overflow()`), at most 16 events per unit per block; `store = false` for a many-parameter morph (see `sequencer.h` for why: the stored write costs the unit a second event in the first
block); a lock or motion point on the target wins while it holds. *Ops and Bank for the screens:* `set_ratchet` / `get_ratchet`, `set_cond` /
`get_cond`, `step_cond` (the encoder's walk over 49 conditions), `rotate_layer(Layer)`, `find_lane`, each also on `Bank` where it edits
(`rotate_layer`, `set_ratchet`, `set_cond`, undoable and joined per step). The ids of the lanes' coalescing keys: 0x08 ratchet, 0x09 condition.
The scenario ran on host-only stand-in controls (`O3_303_TEST_HOOKS`) until the screens had the real gestures; ui-1 removed them. Estimated cost (a block of 64 frames is about 261,000
cycles): no slew running, a scan of the 32 slots, about 150 cycles (0.06 %); a ratchet hit or a condition is a few dozen cycles at the step;
entering a fill copies the pattern (2,260 bytes, about 1,800 cycles) and rewrites up to 64 steps, once, about 5,000 cycles in all; a running
slew is a lerp, a curve inverse and an event for each tick, about 200 cycles (3 slews x 4 ticks, or a dozen at one tick each, about 2,400
cycles, 0.9 %; a full 32-slew morph, about 6,500, 2.5 %). Estimates, not measurements: measure on the unit (skill `rt-optimize`).

**Made 2026-10-26 (ui-1: interaction and performance)** (`app/src/main_screen_perf.cpp`, `app/include/o3_303/perform.h`, `main_screen.cpp`, `pages.cpp`;
tests `perform`, `perf_buttons`, `step_edit`, `ladders`, `tempo`, `scale_lock`, `lanes`; scenarios `perf_assign`, `step_edit`, `ladders`, `tempo`, `scale`,
`lanes`, with `m3b_seq`, `freeze`, `perform` and `edit` rewritten for the real gestures). All main loop: the render side sees only what the sequencer's
API already took (perf flags, slews, the live transpose) and the freeze's parameters. Decisions: *C4* the four buttons are `Wiring::perf` (a
`PerfAction` each, `perform.h`), edited on a PERF page, the fourth of the Params cycle (SOUND, FX, SEQ, PERF; Shift + Params from SOUND reaches it as the
previous page, which R2's SNAP takes from it): F1..F4 the buttons, F5 FILL's variant, F6 the freeze's Length, F7 RATCHET, F8 COND. The plan puts the
variant, RATCHET and COND on SEQ, whose eight keys are all in use until C3's RANDOM popup takes Scale, Density and ACC/SLD: they are `Item`s
with their whole behaviour already, so C3 moves them by editing three entries of `kPages`. OCT UP is the live transpose + 12 on top of the keyboard's; two
buttons with one action: on while either is; a button given another action loses its latch. The Song key, `O3_303_TEST_HOOKS` and their scenarios
are gone (the demo line is entered with REC, ACC / SLIDE held and the REST pad, the ratchets with hold a step + OCT+, the conditions with the encoder on COND). *R4, R5*
hold a step + OCT+ / OCT- is the ratchet, hold a step + up / down the octave (a step that would leave 0..127 stays), the encoder on RATCHET and COND
moves each held step from its own value (`Controls::step_at`, the condition through `seq::step_cond`; without a held step, the cursor's); a toast says
"STEP n RATCHET x2" until the step view draws them (ui-2). *R3* hold ACC and / or SLIDE + left / right (`Bank::rotate_layer`), the release is no accent
toggle. *R1* KEYS is tap or hold (300 ms, as the modifier pads): a tap toggles live play at the release; held, rows 2 and 3 are `slew(kLaneVoice, ..., 30 ms)` to
`param_denorm(ladder_norm(col))`, written as motion when REC or the MOTION latch is on and the pattern plays. *R7* Master: a tap latches TEMPO (Shift: SWING;
again, or an F key, gives the encoder back; a chip on the knob strip's tab line shows the latch with its value), a hold and a turn nudges 1 % a detent up to 3 % and gives the
base tempo back at the release or when the screen is left. *R8* the lock follows a scale the player chose: the generator's default Penta Min is not a choice, so
a boot is chromatic until the SEQ page's Scale is turned (`Wiring::scale_set`); with it, in-scale pads are dim (lit means playable, sharps too), the others dark and
skipped, and a chip with the scale's name (as the nudge's, on the tab line) says so. *Finding 4* `plock_turn`, `record_motion` and the lane caption go through the pattern's `lanes` table
(`Controls::lane_ref`, `lane_entry`, `to_point`, `from_point`): VOLUME and DLY SEND are lanes (the mixer's 0..1.5 gain), any unit key with a lane locks
there; a key no lane drives moves the knob and says "NO LANE". Hold MOTION + an F key points the lane shown at that key's parameter (clears its points;
one undo step; a parameter some lane already has is only shown). Existing scenarios: every picture now shows the PERF tab and FILL / FREEZE chips; `pages` shows
VOLUME's lane and the SCL chip; `perform` and `edit` assign / use the new gestures and are otherwise byte for byte what they were. Needs the unit: the tap / hold
feel of KEYS and Master, the ladder's 30 ms slew by ear, the freeze on the Fx button.

**Made 2026-10-26 (ui-2: pages, snapshots and the look)** (`app/src/main_screen_snap.cpp`, `knob_strip.{h,cpp}`, `snapshot.cpp`, `app/include/o3_303/snapshot.h`,
`pages.{h,cpp}`, `step_view.cpp`, `step_model.cpp`, `main_screen.cpp`, `vault_screen.cpp`; tests `snapshot`, `snapshots_app`, `pages`, `step_model`; scenarios
`random`, `snapshots`, `roll`, with `scale`, `pages`, `tools`, `freeze`, `perf_assign`, `perform`, `step_edit`, `m3b_seq`, `lanes` re-walked). *C3* SEQ is Tempo, Swing,
Length, Gate, Transpose, FILL's variant, Ratchet, Cond; PERF is the four buttons and the freeze's Length (F6..F8 blank); Scale, Density and ACC/SLD
are the RANDOM popup (`random_entry()`), Shift + F1: F1..F3 latch the encoder on them (`Controls::step`, so Scale sets `scale_set` and the lock engages), F8 generates, F7 /
Shift + F1 / Params close; Shift + F1 no longer generates by itself. *R2* the pages are SOUND, FX, SEQ, PERF, SNAP (`kPageCount` 5: Shift + Params is SNAP, again
back; PERF is three Params from SOUND now, so the older tests' and scenarios' route changed). `SnapBank` (pure) holds 8 snapshots of 24 numbers (18 in use: the voice's 7, the
distortion's 5, the delay's 4, Level, Send A); a tap recalls (30 ms slews), Shift + F stores, the encoder moves a 32-step morph (Shift: 4 a step) from the snapshot
recalled before (the live sound for the first recall) to the last, each detent a `slew(..., 50 ms, store = false)` per gliding target (about 13 slews; the 5 enums switch at
the middle), and the blend is stored in the engine 90 ms after the last detent, or at once when the page is left. Cost: a detent starts about 13 slews, in the estimate
above 0.9 % to 2.5 % of a block while they run; measure it. *S1* `range_window` (the real range +-3 semitones, 16 px rows down to 5), octave lines bright, names for non-C notes
only, the beat columns tinted, the cursor column a white tint and the playhead's the one saturated one, ratchets as the bar in its hits, an R/C row (`ratchet_hits`,
`cond_text`; the step of `Wiring::step_focus` framed while Ratchet or Cond is the focused key). *S2* `knob_strip`: F-number and name in the box, values in the Display
face when they fit (else Data), the value as a translucent fill of the box, no names on the kit's key row (its F-number caps stay: kit-owned); the header's values are
ChromeText always. *S3* dim white keys in the roll, off states as outlines (`outline_chip`: the off toasts, KEYS / TRNSP, the idle performance chips), the motion lane's
faint curve and its min / max ticks, the vault's help one footer line. Every scenario's peak list is under 900 words (the vault 1,395). Needs the unit: the look of the
translucent fills and the Display face values on the panel, the morph by ear (13 glides at once), the cost of a morph on a busy block.

**C1, one voice (made 2026-10-25):** nothing was left to remove: o3-303 never built a second voice (M3 wired one `acid_voice` on slot 0 and
slots 1..7 have no source), so the cut frees no CPU or memory; the 39.8 % the unit measured is the one-voice figure. `tests/freeze_app_test.cpp`
keeps it that way (four units, only slot 0 sounds).

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
| hold MOTION + an F key | the lane shown under the step view drives that key's parameter (finding 4, ui-1) |

## Slices

Same slices and ownership as M3 (dsp, seq, ui; the integrator owns `pattern.h`, the docs and the o3-sdk change). Build dirs as in
M3. Done when: host build 0 warnings and ctest passes (new tests for ratchets, conditions, fill variants, layer rotate, lane targets,
the slew, freeze); every new gesture and screen change has a scenario with its PNGs looked at; `o3_303.ptf` builds with its image
ctests and no `__aeabi_d*`; the display list stays under 1,800 words in every scenario; the render's estimated CPU stays under the
budget with freeze active.
