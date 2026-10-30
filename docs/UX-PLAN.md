# UX: the new way o3-303 is used (plan, waves and ownership)

Status: planned, 2026-10-30. The specification is `docs/UX-303.md` (the maintainer's approved concept of 2026-10-29, questions answered
1a, 2a, 3a, 4a, 5a). This plan cuts it into slices for firmware engineers, in waves that give the tester something playable on the unit
early. The obsidian skin is not in it: it waits for the FT812 fill-rate test and the kit's skin items (section 7), and gets its own plan.

## Goal

Every button opens the screen printed on it; HOME is the pattern playing with the eight sound knobs under it; the pads are always 16
steps, eight modifiers and a two-octave keyboard, except on PERFORM where they are 48 live controls; nothing is lost (UNDO, snapshots,
autosave). Built on today's kit rendering (header strip, soft-key row, the app's 800 x 426), with the view logic kept apart from the
drawing so the skin can replace the drawing later.

## 1. Scope

In: UX-303 sections 2 to 13: HOME, PATTERN SETUP, PERFORM, PERFORM SETUP, INSTRUMENT, MASTER, SONG, FILE PROJECTS, FILE LIBRARY,
RECORD, the HOOKS placeholder, Config "303" and "DEEP", MIDI learn on INSTRUMENT, the project with autosave, the rename of the voice.

Out (later, each said in UX-303): the obsidian skin; SEQ MIDI OUT; EXPORT TXT; HOOKS itself (M4); an INSTRUMENT INPUT page; the voice's
MODS parameters; folder dates in FILE.

## 2. Rules

M2C-PLAN section 10's rules apply, as for every o3-303 round:
- Stay in your files (the "Owns" column). An additive one-line change elsewhere is listed in the slice's report.
- Build only in `build-ux-<slice>-host` (and `-mk66` when asked), under `nice -n 10`. Never flash. Do not commit.
- At most **two firmware engineers at a time, sharing no file**: column A and column B of section 3. **One firmware-reviewer reviews
  each slice** before the next slice of that column starts.
- A screen or sound change has a scenario in `app/scenarios/`; pure logic has a host test in `tests/` (or `app/seq/tests/`).
- A slice that removes a gesture rewrites the scenarios and tests that use it, and only those it owns. Scenarios it does not own must give
  byte-identical PNGs and WAVs.
- Every frame stays under 1,800 display-list words; 0 warnings under `-Werror`; no heap, no `double` (`arm-none-eabi-nm -u` on the cross
  build), big buffers `O3SDK_BIG`.
- `verify_gate.py` holds: ctest passes before an engineer stops.
- An image goes to the tester only at the end of a wave, after `fw-gate-check`, and **every wave end keeps every one of today's
  functions reachable** (UX-303 section 10 says where it is at that point).
- o3-sdk is never edited from here: its needs are section 7, done in o3-sdk under its own rules.

Sizes: **S** about an hour of engineer work plus a review; **M** two to three hours plus a review. Nothing here is larger.

## 3. Waves

| Wave | Column A | Column B | Image for the tester | When |
|---|---|---|---|---|
| 1 | **P1** PERFORM screen | **S1** sequencer operations | **U1**: PERFORM on the Perform button; HOME as today | **today** |
| 2 | **RN** rename + test split (alone, first), then **H1** the home map | **F1** project codec, then **F2** project service | U2: the new HOME pads, autosave and the project back after a reboot | next session |
| 3 | **H2** PATTERN SETUP | **I1** INSTRUMENT and the Params button | U3: SETUP, INSTRUMENT | later |
| 4 | **H3** the drawer rows | **M1** MASTER, then **F3** FILE and LIBRARY | U4: rows, MASTER, FILE | later |
| 5 | **H4** Shift row and clean-up, then **P2** PERFORM SETUP | **R1** RECORD and HOOKS | U5: the whole model but SONG | later |
| 6 | **C1** Config pages and MIDI learn | **SG1** the song in the sequencer | none | later |
| 7 | (doc-keeper: DESIGN-303, README) | **SG2** SONG screen | U6: everything | later |
| S | skin, separate plan | | after N2 to N7 | later |

Why this order (judged from the code): PERFORM goes first because the Perform button does nothing today, so a new screen is additive:
it reuses logic that exists (the live actions, snapshots, ladders and tools in `main_screen_perf.cpp` / `main_screen_snap.cpp`) and
leaves HOME and its 23 scenarios untouched. HOME is the bigger rewrite (`main_screen.cpp` is 1,661 lines; most scenarios walk its KEYS,
MOTION, colour buttons or the Params cycle), so it is cut into H1..H4 across four waves, each keeping the tree green. The project (F1,
F2) runs beside H1 because it touches no screen code, and it is the change that stops jams being lost.

**Today** (about three hours): P1 and S1 start together; S1 is reviewed when done; P1 is reviewed, fixed and folded; the integrator runs
the full host ctest, the MK66 build and `fw-gate-check`, and writes the U1 hw-session page. RN runs today only if the gate is done with
time to spare (it needs the tree to itself); otherwise it opens the next session.

## 4. Slices

### P1: PERFORM screen (M, wave 1, column A, today)

- **Goal:** UX-303 5.3. Perform opens PERFORM, again PERFORM SETUP (today's PERF page as it is until P2); the 48 pads live; HOME
  behaves exactly as today (its colour buttons still trigger live actions until H3).
- **Owns:** new `app/src/perform_screen.{h,cpp}`, `live_actions.{h,cpp}`, `frame.{h,cpp}`; `app/src/main_screen.{h,cpp}`,
  `main_screen_perf.cpp`, `main_screen_snap.cpp` (the live actions, `FreezeOwner`, wobble and snapshot logic move into `LiveActions`;
  MainScreen calls it); `app/src/rig.{h,cpp}` (`LiveActions* live`; `PageKeys sound_keys`, the SOUND latch PERFORM and INSTRUMENT
  VOICE share, HOME from H4); `app/src/o3_303_app.cpp` (the Perform button; `LiveActions::update()` every pass);
  `app/include/o3_303/pages.h`, `app/src/pages.cpp` (only: `PageKeys` can run over any page table, `set_table(const Page*, unsigned)`,
  the global one by default, so later screens keep their pages local); `app/include/o3_303/perform.h` (the pure PERFORM pad map, the
  wobble rate on row 3); `app/CMakeLists.txt` (SOURCES become `file(GLOB CONFIGURE_DEPENDS src/*.cpp)`); tests `perform_test.cpp`, new
  `perform_screen_test.cpp`, `hands.h` (adds `to_perform()`), `o3_303_scenarios_test.cpp` (required list); new
  `app/scenarios/perform_pads.txt`.
- **Acceptance:** all of 5.3 (rows, LEDs, PADS line, colour buttons, encoder and MORPH, momentary actions end at `leave()`, latched stay
  with their chips on HOME, the PERFORM Shift row REDO / UNDO); every existing scenario's PNG and WAV byte-identical to HEAD; every frame
  under 1,800 words; ctest green with 0 warnings; the MK66 build links with no new `__aeabi_d*`.
- **Scenario:** `perform_pads.txt`. **Host tests:** `perform_test.cpp` (pure map), `perform_screen_test.cpp`.
- **Depends:** none. **o3-sdk:** no. **Unit:** yes (U1).

### S1: sequencer operations (S, wave 1, column B, today)

- **Goal:** the pure pattern logic H2 and H3 need, so they only wire it: `seq::regen_rhythm` (keeps every pitch, redraws rests,
  accents, slides), `seq::regen_notes` (keeps the rhythm and flags, redraws pitches in scale and range), the PLAYS list (UX-303 2.3 and
  D8: index to {ratchet, condition}, setting one clears the other, stray values shown and stepped to the nearest), the GATE list (REST,
  NOTE, ACC, SLD, ACC+SLD, TIE to and from the flags), `seq::claim_lane` (the lane already driving a target, else the first lane with no
  points, re-pointed; -1 when all eight are used: D2), `seq::resize` (a step up copies the steps and their motion into the new part, a
  step down truncates). `Bank` wrappers, one undoable edit each; a lock with its lane claim is one edit.
- **Owns:** `app/seq/**` (`ops.h/.cpp`, `bank.h/.cpp`, new `tests/seq_ux_test.cpp`).
- **Acceptance:** deterministic from the seed; only the steps below the length change; motion untouched except by `resize`; no
  history entry when nothing changed; the existing seq tests unchanged; no file outside `app/seq`.
- **Host test:** `seq_ux_test.cpp`. **Depends:** none. **o3-sdk:** no. **Unit:** no.

### RN: rename the voice, split the app test (S, wave 2 opener, alone)

- **Goal:** UX-303 section 12 (`units/tb303` and `Tb303` become `units/acid_voice` and `AcidVoice`, pending decision 1), and
  `tests/o3_303_app_test.cpp` (44 cases touching everything) split by area so later slices own whole files: `app_boot_test`,
  `app_steps_test`, `app_patterns_test`, `app_pages_test`, `app_tools_test`, `app_perf_test`, `app_master_test`, `app_vault_test`.
  No behaviour change.
- **Owns:** everything section 12 lists, and the test split. No other engineer runs meanwhile.
- **Acceptance:** `grep -rIi tb303 app units tests README.md` finds only the third-party path; every scenario's PNG and WAV
  byte-identical; the same 44 cases, none lost; the image differs only by the unit's name string; gate passes. The `CLAUDE.md` line is
  reported to the maintainer, not edited.
- **Depends:** P1 and S1 folded. **o3-sdk:** no. **Unit:** no (bit-identical).

### H1: the home map (M, wave 2, column A)

- **Goal:** UX-303 6.2 and 2.2. `HomeMap` extracted from `MainScreen` with this API, which every home-map screen forwards to:
  `input(ctx, in)` (pads, Note / Instr / Vol / Fx, arrows, Copy / Insert / Delete, Rec; true when taken), `turn(ctx, detents, coarse,
  const Entry& knob)` (taken while a step is held or a row owns the encoder), `knob_latched(const Entry&)`, `update()`, `leds()`, and
  read accessors for the drawer, the STEP box, the status line and the PADS line. The new right block (ACC SLIDE TIE REST / OCT- OCT+
  TRNSP FILT); TRNSP and FILT toggles; the keyboard plays the voice by default; MOTION gone (Rec while playing records turns, a lock
  claims its lane with S1's `claim_lane`); Shift + Up / Down octave, Shift + Insert to step 1, Shift + Delete backspace; hold step + OCT±
  ratchet kept until H3; the status and PADS lines on HOME (`frame.h`).
- **Owns:** new `app/src/home_map.{h,cpp}`; `main_screen.{h,cpp}`, `main_screen_perf.cpp` (the ladders move to `HomeMap`);
  `app/include/o3_303/pad_map.h`; tests `pad_map_test`, `ladders_test`, `lanes_test`, `step_edit_test`, `scale_lock_test`, `pattern_test`,
  `pages_test` (the role references), `app_steps_test`, new `home_map_test`, `hands.h`, `o3_303_scenarios_test.cpp`; scenarios
  `write`, `edit`, `ladders`, `lanes`, `step_edit`, `scale`, `m3b_seq` (pad parts), `long`, `boot`, new `home.txt`.
- **Acceptance:** 6.2 entirely (roles, states, LEDs, PADS lines); the scenarios that used KEYS or MOTION rewritten with the same musical
  result; ctest green; frames under 1,800.
- **Depends:** RN, S1. **o3-sdk:** no. **Unit:** yes (U2: TRNSP and FILT by hand, the ladder slew by ear).

### F1: project codec (M, wave 2, column B)

- **Goal:** UX-303 8.1 and 8.3: `project::encode/decode` over a `ProjectState` gathered from and applied to `Rig` and the engine, in one
  128 KiB `O3SDK_BIG` buffer, with `formats/chunk_file.h`; `Bank::restore` (no history).
- **Owns:** new `app/src/project.{h,cpp}`; `app/seq/include/o3_303/seq/bank.h`, `app/seq/src/bank.cpp`, `app/seq/tests/seq_bank_test.cpp`;
  new `tests/project_codec_test.cpp`.
- **Acceptance:** a round trip of every field of 8.1; a flipped byte refused; `HEAD` of a newer major refused; another chunk's newer
  major and an unknown chunk skipped; a newer minor read; a missing parameter keeps its default; a `SNAP` with fewer values filled from the
  sound; a static_assert that the image fits; the encode's host time in the report.
- **Depends:** S1 folded (app/seq passes to column B's next slice). **o3-sdk:** no. **Unit:** no (F2's checks cover it).

### F2: project service (M, wave 2, column B, after F1)

- **Goal:** UX-303 8.4 to 8.6: `ProjectService` (autosave with `AutosaveClock`, change detection by Bank versions and the CRC of the small
  chunks, the 60 s cap, quiet strip, failure shown once), boot load through `last.txt`, SAVE on Shift + File, the no-card and card-less
  session rules.
- **Owns:** new `app/src/project_service.{h,cpp}`, `app/include/o3_303/autosave.h`; `app/src/o3_303_app.cpp`; new tests
  `autosave_test.cpp`, `project_service_test.cpp`; new `app/scenarios/project.txt`.
- **Acceptance:** 8.4 to 8.6; the scenario's `reboot` brings the project back; no file call without a card; ctest green.
- **Depends:** F1. **o3-sdk:** optional N1 (the power-off notice); without it the loss bound of 8.4 is documented. **Unit:** yes (U2:
  the first real save on a card, 0 underruns while autosaving during play, the encode's main-loop time, a reboot, a card pulled during a
  save, a boot with no card).

### H2: PATTERN SETUP (M, wave 3, column A)

- **Goal:** UX-303 5.2: Pattern on HOME toggles SETUP; the ROLL view goes; `Item::AccPct` and `Item::SldPct` appended (`AccSlide` stays
  for the popup until H4); LENGTH through `seq::resize`; SETUP's Shift row.
- **Owns:** `main_screen.{h,cpp}`, `home_map.{h,cpp}`, `pages.{h,cpp}`; tests `pages_test`, `app_patterns_test`, new
  `pattern_setup_test`; scenarios `patterns`, `roll` (retired), new `pattern_setup.txt`. Not `hands.h` (I1's this wave): its helpers stay in
  its own test file.
- **Acceptance:** 5.2; LENGTH up and down one UNDO each; SCALE locks the keyboard as today's R8; ctest green.
- **Depends:** H1, S1. **o3-sdk:** no. **Unit:** no (U3 looks at it).

### I1: INSTRUMENT and the Params button (M, wave 3, column B)

- **Goal:** UX-303 5.5: `InstrumentScreen` with VOICE (on `Rig::sound_keys`), FX, MODS and the temporary MORE page (its pages local, over
  `PageKeys::set_table`), the home map forwarded; the app routes Params to it (on the vault, Params stays the vault's until F3). Every
  test and scenario that walks the Params cycle moves to the new screens: `hands.h` gets `to_instrument(page)` and an `assign_perf()` over
  PERFORM SETUP. The Shift row stays empty until C1.
- **Owns:** new `app/src/instrument_screen.{h,cpp}`; `o3_303_app.cpp`; `hands.h`, `o3_303_scenarios_test.cpp`; tests `app_pages_test`,
  `m3c_ui_test`, `freeze_app_test`, `snapshots_app_test`, `seq_m3b_app_test`, `lanes_test`, `step_edit_test`, new
  `instrument_screen_test`; scenarios `freeze_modes`, `freeze`, `growl`, `lanes`, `m3b_seq`, `perf_assign`, `perform`, `random`,
  `snapshots`, `step_edit`, `tempo` (their Params walks), `pages` replaced by new `instrument.txt`.
- **Acceptance:** 5.5 without MIDI learn; every function of the old cycle reachable (FX, GROWL, WOBBLE on INSTRUMENT; SEQ and MASTER on
  MORE; PERF and SNAP on PERFORM); rewritten scenarios keep their musical content; ctest green.
- **Depends:** H1 (the `HomeMap` API), P1 (`sound_keys`, `set_table`). **o3-sdk:** no. **Unit:** yes (U3: legibility).

### H3: the drawer rows (M, wave 4, column A)

- **Goal:** UX-303 2.3 and 5.1: Note / Instr / Vol / Fx latch the NOTE, GATE, PLAYS and LOCK rows on HOME, SETUP and (through
  `HomeMap`) INSTRUMENT; the PLAYS and GATE lists (S1); the LOCK row with lane claims and "--"; the ENC text; the STEP box; the drawer's
  GATE / PLAYS / LOCK rows; the OCT± ratchet gesture cut; the colour buttons' live actions leave HOME (their tests and scenarios move to
  PERFORM).
- **Owns:** `home_map.{h,cpp}`, `main_screen.{h,cpp}`, `main_screen_perf.cpp`, `step_view.{h,cpp}`, `step_model.cpp`,
  `app/include/o3_303/step_model.h`; tests `perf_buttons_test`, `freeze_app_test`, `m3c_ui_test`, `seq_m3b_app_test`, `step_model_test`,
  `step_edit_test`, `lanes_test`, `app_perf_test`, new `home_rows_test`, `hands.h`, `o3_303_scenarios_test.cpp`; scenarios `perform`,
  `freeze`, `freeze_modes`, `perf_assign`, `growl`, `m3b_seq`, `step_edit`, `lanes`, new `home_rows.txt`.
- **Acceptance:** 2.3 for HOME, SETUP, INSTRUMENT; a lock on a ninth parameter says "8 LANES USED ..."; the old OCT± gesture gone;
  ctest green.
- **Depends:** H2, I1, S1. **o3-sdk:** no. **Unit:** yes (U4: the rows with the encoder by hand).

### M1: MASTER and the global keys (S, wave 4, column B)

- **Goal:** UX-303 5.6 and 2.6: `MasterScreen`; Master opens it at the release of a tap, hold + turn nudges from anywhere, Shift + Master
  taps the tempo, Shift + Play restarts; `TapTempo` pure.
- **Owns:** new `app/src/master_screen.{h,cpp}`, `app/include/o3_303/tap_tempo.h`; `o3_303_app.cpp`; tests `tempo_test`,
  `app_master_test`, new `master_screen_test`, `tap_tempo_test`; scenarios `tempo`, new `master.txt`.
- **Acceptance:** 5.6; the nudge never changes screen; ctest green. (MainScreen's Master code is now dead; H4 deletes it.)
- **Depends:** I1 (same file, earlier wave), H1. **o3-sdk:** no. **Unit:** yes (U4: tap tempo by hand).

### F3: FILE PROJECTS and LIBRARY (M, wave 4, column B, after M1)

- **Goal:** UX-303 5.8 and 5.9: `FileScreen` over `ProjectService` (LOAD, SAVE, SAVE AS and NEW on `NameEntry`, REVERT and DELETE twice);
  the File button: PROJECTS, again LIBRARY, again PROJECTS; the vault relabelled FILE LIBRARY (a pad loads, Shift + F1 NEW SEED, Shift + F2
  LOAD what, slots 48-63 by encoder and arrows, Params no longer special there).
- **Owns:** new `app/src/file_screen.{h,cpp}`; `vault_screen.{h,cpp}`, `project_service.{h,cpp}`, `o3_303_app.cpp`; tests
  `app_vault_test` (becomes `library_test`), new `file_screen_test`; scenarios `vault` (becomes `library.txt`), new `file.txt`.
- **Acceptance:** 5.8 and 5.9; DELETE refused for the current project; NO CARD state; ctest green.
- **Depends:** F2, M1. **o3-sdk:** no. **Unit:** yes (U4: NameEntry on the pads, LOAD and REVERT on a real card).

### H4: the Shift row and the clean-up (M, wave 5, column A)

- **Goal:** the Shift row at once on HOME and SETUP (5.1, 5.2: NEW VARY RHYTHM NOTES REVERSE CLEAR REDO UNDO; the 600 ms wait goes);
  the RANDOM popup, INVERT, DOUBLE, `Item::AccSlide`, the Params cycle, the SNAP page and the dead Master code removed;
  `main_screen_snap.cpp` deleted; HOME moves onto `Rig::sound_keys` (D3); the knob row's tab line goes.
- **Owns:** `main_screen.{h,cpp}`, `main_screen_perf.cpp`, `main_screen_snap.cpp` (deleted), `pages.{h,cpp}`, `knob_strip.{h,cpp}`;
  tests `app_tools_test`, `pages_test`, new `home_tools_test`, `hands.h`, `o3_303_scenarios_test.cpp`; scenarios `tools`, `random`
  (retired), new `home_tools.txt`.
- **Acceptance:** no state where an F key does something its label does not say; HOME's latch is PERFORM's; ctest green.
- **Depends:** H3, M1, I1. **o3-sdk:** no. **Unit:** yes (U5: the Shift row by hand).

### P2: PERFORM SETUP and the MORE page (S, wave 5, column A, after H4)

- **Goal:** UX-303 5.4 (FILL on F5, ROLL as one list with `Item::RollMode`, STOP on F8); the snapshot targets gain VOWEL, FMT Q, GRL MIX,
  CMB MIX (D29); INSTRUMENT's MORE page removed.
- **Owns:** `perform_screen.{h,cpp}`, `live_actions.{h,cpp}`, `pages.{h,cpp}`, `instrument_screen.cpp`; tests `perform_screen_test`,
  `snapshot_test`, `snapshots_app_test`, `instrument_screen_test`; scenarios `perf_assign`, `snapshots`, new `perform_setup.txt`.
- **Acceptance:** 5.4; a project saved before P2 loads (its `SNAP` has fewer values: filled from the sound); ctest green.
- **Depends:** H4, M1, H3. **o3-sdk:** no. **Unit:** U5.

### R1: RECORD and HOOKS (M, wave 5, column B)

- **Goal:** UX-303 5.10 and 5.11: `RecordScreen` (bar-aligned takes from the master or the input, 30 s stereo buffer, save to
  `Samples/`), Shift + Rec from any screen, `App::update_blocker` while recording; `HooksScreen` on the three Sample buttons.
- **Owns:** new `app/src/record_screen.{h,cpp}`, `hooks_screen.{h,cpp}`; `o3_303_app.cpp`; new test `record_screen_test`; new scenarios
  `record.txt`, `hooks.txt`.
- **Acceptance:** 5.10 and 5.11; a one-bar take starts on the bar's sample in the simulator; the WAV lands in `Samples/`; ctest green.
- **Depends:** F3 (same file, earlier), H1. **o3-sdk:** no. **Unit:** yes (U5: the cost while recording, a long WAV on the card).

### C1: Config pages and MIDI learn (M, wave 6, column A)

- **Goal:** UX-303 5.12 (pages "303" and "DEEP", keys fixed there) and INSTRUMENT's Shift row: MIDI learn of each unit-parameter knob;
  KEYS AT BOOT, LADDER SLEW, PAD LEGEND, AUTOSAVE and RESET PROJECT wired.
- **Owns:** new `app/src/config_pages.cpp`, `app/include/o3_303/config_keys.h`; `o3_303_app.cpp`, `instrument_screen.{h,cpp}`,
  `home_map.cpp`, `live_actions.cpp`, `project_service.cpp`, `frame.cpp`; new test `config_pages_test`; new scenario `config_303.txt`.
- **Acceptance:** 5.12; values survive the simulator's `reboot`; learn arms only unit parameters; ctest green.
- **Depends:** P2, R1, F3. **o3-sdk:** optional N8. **Unit:** yes (U6: values after a reboot, learn with a controller).

### SG1: the song in the sequencer (M, wave 6, column B)

- **Goal:** `seq::Song` (64 rows of pattern, repeat, transpose, mute), `Sequencer::set_song()` and the play mode (rows advance at the
  pattern end, the row's transpose on top of the live one, muted rows skipped, loop), FROM CHAIN conversion, a `History` entry kind so song
  edits share the undo stack (D9).
- **Owns:** `app/seq/**`; new `app/seq/tests/seq_song_test.cpp`.
- **Acceptance:** the render plays a song to the sample of the same chain made by hand; the pattern mode is bit-identical to before; undo
  restores a song edit; ctest green.
- **Depends:** F1 (same files, earlier). **o3-sdk:** no. **Unit:** no (SG2's).

### SG2: SONG screen (M, wave 7, column B)

- **Goal:** UX-303 5.7 and the project's `SONG` chunk.
- **Owns:** new `app/src/song_screen.{h,cpp}`; `o3_303_app.cpp`, `project.{h,cpp}`; tests `project_codec_test`, new `song_screen_test`;
  new scenario `song.txt`.
- **Acceptance:** 5.7; a project with a song round-trips, an older project loads with an empty song; ctest green.
- **Depends:** SG1, C1. **o3-sdk:** no. **Unit:** yes (U6: a song with repeats by ear).

## 5. Who owns the shared files in each wave

| File | W1 | W2 | W3 | W4 | W5 | W6 | W7 |
|---|---|---|---|---|---|---|---|
| `app/src/o3_303_app.cpp` | P1 | RN, F2 | I1 | M1, F3 | R1 | C1 | SG2 |
| `app/src/main_screen*.cpp/.h` | P1 | RN, H1 | H2 | H3 | H4 | — | — |
| `app/src/home_map.*` | — | H1 | H2 | H3 | — | C1 | — |
| `app/include/o3_303/pages.h`, `app/src/pages.cpp` | P1 | RN | H2 | — | H4, P2 | — | — |
| `app/src/rig.*` | P1 | RN | — | — | — | — | — |
| `app/src/instrument_screen.*` | — | — | I1 | — | P2 | C1 | — |
| `app/src/project_service.*` | — | F2 | — | F3 | — | C1 | — |
| `app/src/project.*` | — | F1 | — | — | — | — | SG2 |
| `app/seq/**` | S1 | RN, F1 | — | — | — | SG1 | — |
| `tests/hands.h`, `tests/o3_303_scenarios_test.cpp` | P1 | RN, H1 | I1 | H3 | H4 | — | — |
| `app/CMakeLists.txt` | P1 | RN | — | — | — | — | — |

Column B's new scenarios run in the scenarios test at once; they join its required list when the next owner of that file touches it.

## 6. Images and unit sessions

Each image: the integrator builds `build-mk66`, runs `fw-gate-check`, and writes a hw-session page (`docs/hw/`) with the checklist below and
a regression pass of the previous image's list. The unit is shared with track3r-os sessions: start from the state found and restore exactly
it, card files included. Firmware reaches the unit only by the documented `.ptf` path.

| Image | Waves | What only the unit shows |
|---|---|---|
| U1 | 1 | Perform opens PERFORM; every action pad audible, latch and ALL OFF; snapshots store, recall, MORPH on the encoder without clicks; the ladder on CUTOFF then RESO (F2) by ear; WOBBLE + row 3 rates; the key row; LEDs mirror the screen; CPU with FREEZE, WOBBLE and FILL latched against the 49.9 % max measured after M3c G (`docs/hw/`); HOME unchanged (regression) |
| U2 | 2 | TRNSP and FILT by hand; the first real project save on a card; autosave while playing: 0 underruns, the encode's main-loop time; power off and on: the project back; a card pulled during a save; a boot with no card |
| U3 | 3 | SETUP by hand; INSTRUMENT's large knobs and read-outs legible; nothing of the old cycle missing |
| U4 | 4 | the rows with the encoder; tap tempo and nudge by hand; FILE: NameEntry on the pads, LOAD, REVERT |
| U5 | 5 | the Shift row at once; PERFORM SETUP; a 30 s take written to `Samples/`, the CPU while recording |
| U6 | 6, 7 | Config values after a reboot; MIDI learn with a real controller; a song with repeats and a transpose |

## 7. o3-sdk needs (done in o3-sdk under its own rules; none blocks waves 1 to 7)

| Id | Need | For | Blocks |
|---|---|---|---|
| N1 | An app power-off notice (for example `App::power_off(Context&)` at the power-off hold where the kit writes `system.o3s`), and the hold waiting for `Files::busy()` up to a bound | the last autosave (UX-303 8.4) | nothing: without it at most one AUTOSAVE period is lost |
| N2 | The FT812 fill-rate test (`apps/fillrate`, skin NOTES section 6, unit check 1) | deciding if the knob and key rows can be sprite-composed | the skin |
| N3 | An app region in RAM_G (`kRamGApp`, about 204 KiB, `ram_g_map.h`) | skin textures | the skin |
| N4 | An app asset upload (`App::assets()`, uploaded by `Runtime::start()` in 4 KB chunks) | skin textures | the skin |
| N5 | Bitmap handle 0 declared the app's (`kHandleApp`) | skin sprites | the skin |
| N6 | `Renderer::bind_bitmap`, `Renderer::sprite` with RGB and alpha, `point` / `line` with RGB | skin drawing without raw sections | the skin (recommended, not strictly needed) |
| N7 | A `Screen` chrome flag (Full / None) so a screen owns all 800 x 480 | the skin's panel header and knob row | the skin |
| N8 | MIDI learn of an app value (a key and a callback, beside `Midi::learn(unit, param)`) | learning VOLUME, DLY SEND, MODEL, the wobble knobs (UX-303 D15) | nothing |

## 8. Risks

1. **Test and scenario churn.** Most of today's 23 scenarios and 18 test files walk KEYS, MOTION, the colour buttons or the Params cycle.
   Mitigation: section 5's ownership, RN's split of `o3_303_app_test.cpp`, `hands.h` helpers, and P1 changing no existing output.
2. **Extracting from `main_screen.cpp`** (P1, H1) can break HOME silently. Mitigation: byte-identical scenarios for what a slice does not
   own; the reviewer diffs behaviour, not only code.
3. **The autosave encode** (about 110 KB copied and a CRC in one main-loop pass) may cost a UI frame on the unit. Mitigation: F1 reports the
   host time, U2 measures it; if it is over a frame, F2 encodes across passes.
4. **The app's first card writes on a unit:** the kit's file API is host-only so far (o3-sdk M2c). U2 is its first real run; a failure there
   is an o3-sdk finding, reported, not patched here.
5. **PERFORM's display list** (48 lamps with labels plus the knob row): the scenarios test bounds it on the host; U1 confirms.
6. **Tester time:** images only at wave ends, each with a short checklist.

## 9. Decisions for the maintainer

1. **The voice's new name** (RN). (a) `acid_voice` / `AcidVoice`, unit name "ACID" **[pick]**; (b) `voice` / `Voice` (reads like the
   existing `Item::Voice` and `kLaneVoice`); (c) `bassline` / `Bassline`.
2. **What lands first.** (a) PERFORM today: additive on an unused button, HOME untouched, no regression risk **[pick]**; (b) HOME first:
   90 % of the use, but it rewrites the 1,661-line screen and about 15 scenarios, so it cannot land and be gated today.
3. **The generator's settings on PATTERN SETUP** (SCALE, DENSITY, ACC %, SLD %). (a) One set for the whole project, the keyboard's scale
   lock stable when the pattern changes **[pick]**; (b) per pattern (4 bytes each, `Pattern` 2,260 to 2,264 bytes, the lock follows the
   pattern).
