# M3: o3-303 on o3sdk (plan, contracts and ownership)

Status: built and verified on the host, 2026-10-25; waiting for the unit test (Done when item 4). The working brief for the parallel slices of milestone M3, the first playable o3-303. The design
is `docs/DESIGN-303.md`; the framework is `../o3-sdk` (its `CLAUDE.md` and `docs/FRAMEWORK.md` rules apply unchanged, and it is
never edited from here: a framework change is asked of the integrator, who makes it in o3sdk by o3sdk's own process).

## The maintainer's decisions (2026-10-25)

- Two sibling directories: `../o3-sdk` is the framework, this repository the 303.
- **Based on Replicant 303** (DanimanX, MIT, `third_party/replicant303/`, pinned at `d5369be`): its DSP is ported (oscillator,
  diode-ladder filter, envelopes, the five-model distortion), optimised for the M4F; its eight knobs with their ranges and defaults
  are the SOUND page; its preset generator and vault (8 banks x 64, seeded) are ported. DESIGN-303's extras (Devil Fish-style mods,
  delay, reverb, ties, p-locks, motion, song, HOOKS) go on top.
- The ACC / SLIDE / TIE / REST pad block on the right, as drawn (DESIGN-303 section 2).
- Pattern length 16 by default, configurable to 8, 16, 32 or 64.

## Goal and Done when (this round)

The first playable o3-303 in the simulator and as an image: Replicant's voice and distortion, the delay on send A, a sequencer
that plays patterns of 8 to 64 steps with accent, slide, tie, rest, swing, gate and motion, the GEN vault, and the 303 screen with
every control of DESIGN-303 sections 2 to 5 that does not need the card or MIDI.

1. Host: `cmake -S . -B build-host && cmake --build build-host -j4 && ctest --test-dir build-host` builds with 0 warnings and passes,
   including the oracle tests against the pinned Replicant sources and the scenario ctests.
2. Simulator: the scenarios of `app/scenarios/` show every screen and gesture (PNGs looked at) and write WAVs of the voice.
3. mk66: `o3_303.ptf` builds with 0 warnings, its image ctests pass (flash configuration field exact), no `__aeabi_d*`.
4. On the unit (the maintainer's, later): the 303 sounds, every control works, the render's CPU % stays under the budget.

Not in this round: the MODS page's new mods (W4), the reverb (W5), the song mode, MIDI, saving to the card (needs o3sdk M2b),
HOOKS (M4).

## Contracts (the slices build against these; a change goes through the integrator)

**The pattern** is `app/include/o3_303/pattern.h` (exists, owned by the integrator): `Step {note, flags}` with `kStepAccent`,
`kStepSlide`, `kStepTie`, `kStepRest`; `Pattern {steps[64], length (8/16/32/64), swing 0..66 %, transpose, gate 5..100 %,
motion[8][256]}`; trivial (it lives in an O3SDK_BIG array), `clear()` makes the fresh pattern: 16 steps, each a rest with C2 under
it, so a new project has no pattern that sounds (integrator's decision, 2026-10-25). Step semantics are in its header comment
(Replicant's: slide = held into the next step, which glides; rest = gate closed). M3b adds `Step::cond`, the ratchet bits of `flags` and
`Pattern::lanes` (`docs/M3B-PLAN.md`, "Contracts").

**The voice unit `acid_voice`** (`units/acid_voice`, class `o3_303::AcidVoice`, `UnitKind` instrument, monophonic). Parameter ids are table
indices: append, never insert.

| id | Name | Range | Default | Notes |
|---|---|---|---|---|
| 0 | Cutoff | 25..4500 Hz | 350 Hz | Replicant's skew (0.35) as the descriptor's curve |
| 1 | Reso | 0..100 % | 65 % | |
| 2 | Env Mod | 0..100 % | 60 % | |
| 3 | Decay | 200..2500 ms | 750 ms | |
| 4 | Accent | 0..100 % | 70 % | |
| 5 | Wave | SAW / SQR | SAW | a choice, as Replicant |
| 6 | Tune | -12.0..+12.0 st, step 0.1 | 0 | |
| 7 | Volume | -60..+6 dB | -3 dB | at or below -59.5 dB silence; the app holds it at 0 dB (see the topology) |
| 8..15 | (W4, reserved in this order) Acc Decay, Filter Track, Slide Time, Soft Attack, Sub Osc, Filter FM, Sweep, Muffler | | | each default reproduces Replicant (acc decay 180 ms, slide 60 ms, the rest off) |

Events: note-on with `dsp::kNoteAccent` = accent (Replicant: velocity 100 or more); `dsp::kNoteSlide` = legato glide (60 ms one-pole,
no envelope retrigger); a note-on while a note is held also glides (Replicant's legato); note-off releases (25 ms).

**The distortion unit `distortion`** (`units/distortion`, class `o3_303::Distortion`, an insert effect), Replicant's
`DistortionUnit`; model names are ours:

| id | Name | Range | Default |
|---|---|---|---|
| 0 | Active | OFF / ON | ON |
| 1 | Type | GRIT / TUBE / HARD / FUZZ / FOLD (Replicant: PocoRat, TubeDrive, HardClip, GermaniumFuzz, Wavefolder) | GRIT |
| 2 | Drive | 0..100 % | 50 % |
| 3 | Tone | 0..100 % (800 Hz..16 kHz, exponential) | 60 % |
| 4 | Mix | 0..100 % | 85 % |

Every model's output goes through a DC blocker (one pole, about 5 Hz): Replicant's FUZZ leaves up to +0.1 of DC at idle, which would
reach the delay and the outputs and click when the unit is switched. A wet trim that follows Drive (`Distortion::trim(model, drive)`,
a 21-point table interpolated once per control period) levels the five models to GRIT's loudness at the same Drive, and holds GRIT's
peak at its default level above the default Drive (Replicant's spread is about 12 dB, and its FUZZ presets clipped the master by up
to 11 dB). FUZZ's negative half is bounded: Replicant's `fastTanh` is a Pade form that saturates only to |x| = 3 and then rises
again, which made FUZZ's negative half an unbounded ramp; the port floors that argument at -3 (identical to Replicant below it, a
deliberate deviation, the integrator's decision of 2026-10-25: a real tanh saturates). The oracle compares before the blocker and
the trim, FUZZ against the pinned original with that one line changed (generated at configure time; the build stops if the line
changes upstream).

**The engine's topology** (set by the app): slot 0 = `acid_voice`, its insert 0 = `distortion`; send A = o3sdk's `delay`; send B
reserved for the reverb (W5). The sequencer is a `dsp::Clocked` hook on the engine. The SOUND page's Volume is slot 0's level, after
the distortion as in Replicant (-60..+3.5 dB, the engine's 1.5 cap; default -6 dB for headroom with the delay return, the ui
review's finding), so the distortion sees the voice at Replicant's
level. At first boot the app loads the vault's factory preset (bank 0, slot 0) into pattern 0, so Play sounds at once.

**The sequencer** (`app/seq`, library `o3_303_seq`, namespace `o3_303::seq`): owns the project's 48 patterns (an O3SDK_BIG array,
cleared at setup), the current and the queued pattern, the playhead, and the editing operations (pure functions on a `Pattern`:
set note, toggle a flag, insert, delete, copy / paste, rotate, transpose, reverse, invert, double, clear, random / mutate) with an
undo / redo history. Its `Clocked` hook turns the playing pattern into voice events: Replicant's timing (swing lengthens even steps
and shortens odd ones by half the swing of a step), the gate, a slide as the next note-on with `kNoteSlide` before the held note's
note-off at the same frame, ties, rests (release), the transpose and the live transpose, motion and p-locks as `Param` events at
their frames (four points a step). Main-thread commands reach it as the kit's engine commands do (no locks, no allocation).
The render reads a shadow of the playing pattern through a per-pattern seqlock; the queue is a chain of up to 16 entries with a
cursor, which the main thread reads as a `ChainView`. Transpose moves every step's note (rests and ties included) and refuses if
one would leave 0..127. A slide onto the same note is a note-on with `kNoteSlide` and no note-off.
**GEN and the vault** (`app/seq`, `o3_303::seq::gen`): Replicant's `PresetBank` generator, deterministic from (bank 0..7, slot
0..63, seed), giving a 16-step pattern plus a patch (the voice's ids 0..7, the distortion's 0..4, tempo, swing); slot 0 of each bank
is the factory pattern of `Pattern.h`'s default lines; the result is mapped into `Pattern` and parameter values. `to_values()` gives
each parameter in its unit's native value (fractions 0..1 for the % parameters, Hz, ms, dB, choice index), applied with `set_param`
as is.

**The screens** (`app/src`): an app-level page type for pages that span units and engine values (the FX page: distortion Drive and
Tone, the delay's time and feedback, the send A level, master; the SEQ page: tempo, swing, length, gate, transpose, scale, density,
acc/slide %), since the kit's `ParamPage` is one unit a page. Promoting it to the kit is a later o3sdk change, not this round.

## Slices and ownership

| Slice | Owns | Build dirs |
|---|---|---|
| dsp | `units/acid_voice/`, `units/distortion/` (sources, headers, CMake, and their tests in `units/<unit>/tests/` with `o3sdk_test_suite`) | `build-m3-dsp-host`, `build-m3-dsp-mk66` |
| seq | `app/seq/` (library, headers under `app/seq/include/o3_303/seq/`, tests in `app/seq/tests/`) | `build-m3-seq-host`, `build-m3-seq-mk66` |
| ui | `app/src/`, `app/CMakeLists.txt`, `app/scenarios/`, `app/include/o3_303/` except `pattern.h`, `tests/` | `build-m3-ui-host`, `build-m3-ui-mk66` |
| integrator | `docs/`, the root `CMakeLists.txt`, `units/CMakeLists.txt`, `pattern.h`, `THIRD_PARTY.md`, `third_party/`, o3sdk | `build-host`, `build-mk66` |

`third_party/replicant303/` is read-only for everyone: the oracle tests compile its JUCE-free files (`Common303.h`, `Oscillator303`,
`DiodeLadderFilter`, `Envelope303.h`, `DistortionUnit`) on the host; the JUCE ones (`Pattern.h`, `PatternSequencer`, `SynthEngine`,
`PresetBank`) are read, or compiled against a minimal shim of the few JUCE names they use, inside the slice's own test directory.
A ported file says where it comes from (file and commit) and keeps the MIT notice in its header.

Until the dsp slice lands, the ui slice may wire the framework's `synth` as a stand-in voice; it switches to `acid_voice` and
`distortion` once they build (it reads their headers; the ids above are the contract). Until the seq slice lands, the ui slice
draws and edits `Pattern` objects of its own; it switches to `o3_303_seq` when that builds.

## Budgets [est]

CPU (the render, 60 % of a block = 156,672 cycles is the governor's limit): the voice about 11 % (the dsp slice's count of the port,
about 445-465 cycles a sample; Replicant as written is about 27 %), the distortion about 2 % with its DC blocker and trims, the delay
as o3sdk measures it, the sequencer small;
the engine runs from flash in M2's image (no SRAM_L placement yet): the unit's CPU % is the real figure. Memory: 48 patterns about
106 KB of SDRAM (108 KB since M3b), the undo history in SDRAM, the voice and the sequencer's state in SRAM_U (o3sdk's image has about 140 KB free).

## Rules (o3sdk's CLAUDE.md applies; these bite here)

- Real-time paths (`process`, `block`): no allocation, no locks, no `double`, no `std::pow` / `std::tan` per sample, bounded loops.
- Warning-free with o3sdk's strict flags; no heap, RTTI, exceptions; `-fsingle-precision-constant`; check the cross build for
  `__aeabi_d*` (the oracle code on the host may use double; the ports may not).
- Stay in your files; build in your directories; another slice's half-done work may break the tree for a while: do not fix it.
- Do not commit; never flash; never edit `../o3-sdk` (ask the integrator).

## Result (2026-10-25)

All three slices delivered, were reviewed and folded their reviews. Host: 0 warnings, ctest 15/15 (the oracle tests against the
pinned Replicant sources, the sequencer and generator tests, the app's 44 cases, the nine scenarios). The display list peaks at 1,441
of 2,048 words (the vault). mk66: `o3_303.ptf` 0 warnings, image ctests 3/3, no `__aeabi_d*`, two fresh builds byte-identical; flash
114,312 B, SRAM about 58 KB, SDRAM 884,928 B. The CPU figures above are still estimates: the unit's header gives the real ones.

Closed after the round (2026-10-25, M3B-PLAN item L1): the vault's FUZZ presets peaked at 1.45 before the master clipper at the
default -6 dB. With the bounded FUZZ and the Drive-following trim above, the worst of all 512 vault slots is 0.912 (it was 1.877; 71
presets were over 1.0, none are now), and the strict headroom test passes without `may_fail`. How the new FUZZ sounds, and its cost
(estimated under 0.15 % of a block), are for the unit.
