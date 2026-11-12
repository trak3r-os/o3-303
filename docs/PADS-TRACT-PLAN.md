# PADS-TRACT-PLAN: TRACT on the pads, GAIN, WOBBLE learn, one key per sound, the PERFORM effects surface (docs/PADS-TRACT-PLAN.md)

Status: plan, 2026-11-12, for the next release. This is a read-only design and nothing in it is built yet. Every slice is one commit, made after its own checks (CLAUDE.md): host build and tests under `nice -n 10`, the simulator scenario, then the unit check named in the slice. Files are added by name. Nothing in ../o3-sdk changes. Each slice carries its own MANUAL.md, CHANGELOG "Unreleased" and help_texts.cpp lines. The last slice regenerates the manual images and amends UX-303.

The maintainer's decisions folded in:
- (E) one key per sound on every screen;
- (H) the PERFORM pads become an effects surface, with no pads for the sounds;
- (P) "NO LOAD OF OLD PROJECTS": one format bump for the whole batch.

## 0. Order

| # | Slice | Sec | Needs | Size |
|---|---|---|---|---|
| 1 | B1 GAIN stage, level measurement, mix law | B | none | M |
| 2 | D1 WOBBLE MIDI learn through a proxy unit | D | none | S |
| 3 | G1 HOME and SETUP Shift rows | G | none | S |
| 4 | P1 Project format 2.0, older files refused | P | none | M |
| 5 | P2 Cleanup of the old-file paths | P | P1 | S |
| 6 | G2 CHANCE on SETUP F5 | G | P1 | S |
| 7 | F1 TRACT set on PERFORM KNOBS | F | P1 | S |
| 8 | E1a Screen buttons: GROWL, TRACT, WOBBLE, RECORD | E | none | S |
| 9 | E1b Colour keys = sounds, Shift + colour = actions | E | E1a | M |
| 10 | E1c Header chips MOD GRL TRC WOB | E | E1b | S |
| 11 | B2 GAIN on F8 of TRACT and GROWL | B | B1, E1b | S |
| 12 | E2a TRACT STEPS layer (A) | E | E1a | M |
| 13 | E2b TRACT strip in both skins (A) | E | E2a | M |
| 14 | H1 PERFORM effects surface from existing features (done: cc5d473) | H | E1b | M |
| 15 | E2c WOBBLE screen rows (rates on row 3) (done: b50e046) | E | H1 | S |
| 16 | H2 Punches: DIST, CUT LO, CUT HI, RESO (done: 3a9e0e5) | H | H1 | M |
| 17 | H3 GATE, PUMP, DIVE (M3C T and P), measured (not built: needs CPU measured on the unit) | H | H2 | M, optional |
| 18 | Docs sweep: UX-303 amendments, manual images (done, images owed: see slice 18) | all | all | S |

## P. Project format 2.0

Decision: "NO LOAD OF OLD PROJECTS". A file of format 1.x is refused whole. It is never half-loaded, it never crashes the app, and the file on the card is not touched. The format is bumped once for the batch.

**Where the check lives.**
- `app/src/project.h`:
  - `kVersion` becomes 0x0200 and every chunk uses it. `kHeadVersion` and `kPerfVersion` collapse into it, so all minors are 0.
  - `enum class Status` gains `FromOlder`, shown as "PROJECT FROM AN OLDER O3-303".
- `app/src/project.cpp` `decode()`, in the HEAD branch (about line 762):
  - major < kMajor returns `FromOlder` before any other chunk is read;
  - major > kMajor stays `FromNewer`;
  - no HEAD stays `Damaged`.
- `app/src/project_service.cpp`:
  - The boot pick (about line 411) treats `FromOlder` exactly like `FromNewer`: `blocked_ = true`, the notice, `boot_end(ctx, false)`. The session starts as a fresh project with autosave off.
  - `load_settle` (about line 1114) calls `abort_request(ctx, "PROJECT FROM AN OLDER O3-303")`, and the current session stays as it was.
  - Requirement, to be checked in project_service_test: after a refused boot, nothing is ever written into the old folder unless the player picks that name in SAVE AS and confirms, and last.txt is not rewritten until a save.

**FILE screen.**
- The list shows names only (file_screen.cpp reads no headers), so an old project is listed like any other.
- LOAD of an old project shows the outlined refusal notice (`good_notice` is false) and stays on PROJECTS.
- An "OLD" tag would need a header read per entry. It is not worth it.

**Strict 2.0 reading.**
- HEAD, GEN, PATS, SND, SNAP, PERF and SONG must each be present once, with this build's record sizes. Otherwise the file is `Damaged` and the .bak is tried, as today.
- Unknown chunks and newer minors are still skipped. That rule exists only for later builds.
- `Result::patterns_ok` and its autosave guard go: there is no half-load left.

**Layout 2.0.**
- HEAD and GEN are as today.
- PATS: count 48, then `sizeof(Pattern)` bytes per pattern, written whole. This includes the TRACT line and G2's `chance` bytes. The TRCT chunk is removed.
- SND: (target, id, value) entries, as today. A missing entry takes `d.def`, which is robustness, not compatibility.
- SNAP is as today. Snapshots move to PERFORM SETUP in H1.
- PERF:
  - the four colour-key action assignments;
  - the four ladder rows (the retired slot is gone);
  - all five knob sets written whole, set 1 included, plus the set in use.
  - If `ladder_rows.cpp` shows the ladder rows are only a copy of the set in use, store them once. Check `knob_set_use`.
- SONG is as today.

**Factory and library content.**
- FILE LIBRARY is generated in firmware (vault_screen, the generator), and no project files ship. So nothing on the card needs converting.
- The NEW / RESET PROJECT defaults are code tables. These are the 8 factory snapshots, the factory knob sets (set 5 TRACT, F1) and the GAIN defaults (B1). Update them in their slices.
- CHANGELOG states plainly that projects saved by earlier versions are refused and that there is no converter.

**system.o3s keeps its compatibility.** It holds the kit's settings and the MIDI learn bindings (LRNB), and it is not a project. The rule "a unit's parameter id is its index" therefore still holds:
- GAIN is appended (ids 7 and 8);
- the WOBBLE proxy unit (D1) keeps id 6 and its param order for good.

**Within the batch** the 2.0 layout may still change between slices while the major stays 2. A file made by a mid-batch build may be refused as DAMAGED by a later one, but it is never half-loaded. Tell the maintainer before each unit check.

**P2 cleanup list.** Each item exists only to read old files. Confirm with `grep -n "older\|KN[0-9]\|A1b\|SG1\|minor" app/src/project.cpp`.
1. project.cpp:
   - `read_trct`, `kTrctPayload`, `kSeenTrct`, the TRCT writer, and apply's "line stays empty unless TRCT" branch (about line 849);
   - the PERF minor 0/1/2 readers and the conditional `kKnobSetBytes` (line 45, `read_perf` about 443-460);
   - the retired-slot read-and-ignore (456-458);
   - the LINE rule for a missing `kAppTractLine` entry (887);
   - the tolerance for a file without SONG (SG1), the HEAD minor-0 branch, and the static_asserts tying PATS to 2,260 B and TRCT to "the last 68 bytes" (32-35). These become one assert on `sizeof(Pattern)`.
2. project.h: the "an older file lacks it" notes on `kAppWobbleOn`, `kAppModsOn`, `kAppGrowlLastMix`, `kAppTractLastMix` and `kAppTractLine` (72-76), and the chunk history in the header comment. It is rewritten as format 2.0.
3. pages.h and ladder_rows.cpp:
   - `kLadderRetired` goes, and `LadderRowId` becomes {Home3 0, Home4 1, Knobs1 2, Knobs2 3} with `kLadderRows` 4;
   - the five `row == kLadderRetired` guards (ladder_rows.cpp 113, 149, 297, 342, 349) go;
   - so does the "an older build refuses row-only targets" note (pages.h 151 and project.cpp 440). Validation stays.
4. tests/project_codec_test.cpp: delete the old-image cases and add one "major-1 image is FromOlder, state untouched" case.
   - Cases to delete: SG1 without SONG (about 481), RANGE/SUSTAIN missing (1069), app-value-missing helper (1351), A1b without TRCT (1437, 1471), PERF minor 0 (2168, 2368-2382), KN3 without sets (2185), retired slot (2210, 2315-2332).
   - Do the same in tests/project_service_test.cpp, with the refusal notice and no write.
5. `tract_sanitize`, `lane_ref_ok` and the SND finiteness checks stay. They guard against damage, not old versions.

## B. GAIN for TRACT and GROWL

**Parameter.**
- "GAIN", unit "dB", `ParamUnit::Fixed1`, range -12..+18, 60 steps (0.5 dB), coarse 6 (3 dB).
- It is appended as TractFx id 7 (`kParamCount` 7 to 8) and Growl id 8 (8 to 9).
- At a parameter change, g is computed with an explicit branch: g = 1.0f exactly at 0 dB, else 10^(dB/20).

**DSP.**
- The gain is applied to the wet signal only, before the mix crossfade, with the same 5 ms ramp as Mix. In tract.cpp the ramp runs per frame (fields `gain_`, `gain_step_`, `gain_end_`); in growl.cpp it runs per control period.
- The multiply is skipped while g == 1 and the ramp is idle. Mix 0 stays the exact bypass, so it is bit-exact even with FMA contraction.
- In TRACT the gain goes after the extras processor's output, which is after its level trim of about 60 ms (-30..+24 dB, kMakeup 1.2). Before the trim, the AGC would cancel it.

**Default by sound** (format 2.0 frees it). B1 adds `tests/fx_level_test.cpp`:
- Source: AcidVoice saw at C2 through the unit.
- RMS of the output at Mix 0, 50 and 100.
- TRACT: the five vowels at RES 0, 0.5 and 1. GROWL: the factory settings and GRL MIX at its maximum.
- The table is printed and pasted into the file's header comment.

`kTractGainDefaultDb` and `kGrowlGainDefaultDb` (unit headers) are set to the level match at Mix 100 under the factory settings, rounded to 0.5 dB, and 0 when the match is within 0.5 dB. The checks:
- GAIN +6 measures +6.02 ±0.1 dB;
- Mix 0 is bit-exact dry at any GAIN;
- a golden FNV-1a hash of the output, captured on the current tree before the edit, is reproduced at GAIN 0 dB. This proves the stage is transparent.

**Mix law.** If Mix 50 dips 1.5 dB or more below the mean of the two ends (dry and wet decorrelated by the formants), switch the crossfade to equal power, dry·cos(πm/2) + wet·sin(πm/2). Mix 0 keeps its exact-bypass branch. This is likely the real cause of "the mix lowers its volume". Under 1.5 dB, linear stays. The decision is recorded in the test comment.

**Automatic compensation is rejected.**
- TRACT already has an AGC.
- An automatic growl level match pumps on transients.
- It hides the control the maintainer asked for.

**Placement (B2).**
- F8 of kTractPage and kGrowlPage. Their on/off keys leave those pages in E1b.
- The hints "TRACT OFF: F8" and "GROWL OFF: F8" become "TRACT OFF: FX1" and "GROWL OFF: INSTR".
- These come free with a page place: the lane lock (`Controls::lane_ref`, id < `seq::kKnobs`), the ladder walk, and MIDI learn (`learn_target` covers unit params).
- GAIN is not a snapshot target (`kSnapTargetCount` is 48; growing it is optional now).
- Fallback if E1b is refused: Shift + F8 latches GAIN.

Format: SND carries the new ids automatically. SRAM: about 24 B.

## D. WOBBLE knobs MIDI-learnable, app-only

The kit binds MIDI only to (unit, param) (`Midi::learn`), while WOBBLE's values are app values (WobbleSettings, [D15]). No kit change is needed.

- `app/src/wobble_learn.{h,cpp}`: `class WobbleLearn final` is a kit Unit with 6 parameters and an empty `process()`.
  - Params 0..4 are RATE, SHAPE, DEPTH, RESET, TARGET (`WobbleField` order); param 5 is WOB.
  - `desc(id)` returns `wobble_learn_desc(id)`, a contiguous table exported from pages.cpp (kWobbleDescs plus kWobOn).
- `O3_303App::setup` attaches it after tract (id 6, before `start()`) and never routes it, so its events are dropped at block end. It is stored as `Wiring::wob_learn`.
- In instrument_screen.cpp, `learn_target` maps `Item::Wobble` to (wob_learn, field) and `Item::WobOn` to (wob_learn, 5). `page_learnable` becomes true, so "NOT LEARNABLE" goes.
- `O3_303App::midi_learned_moved`:
  - for unit == wob_learn and param < 5, it calls `Controls::set(Entry{Item::Wobble, param}, value)`;
  - for param 5, `live_->toggle_latch(Wobble)` runs when (value ≥ 0.5) differs from `wobble_on()`.
- `WobbleLearn::mirror(dsp::Engine&, const WobbleSettings&, bool wob_on)` runs from `update()` only when a cached field differs. That way Pickup sees the real value, and an edit from the panel must be caught again.
- UX-303 D15 is amended. SRAM: about 40 B (the object plus the cache).

## G. HOME and SETUP Shift rows, SETUP F5

| Key | HOME Shift today | after | SETUP Shift today | after | SETUP plain today | after |
|---|---|---|---|---|---|---|
| F1 | NEW | CLEAR | COPY PAT | same | LENGTH | same |
| F5 | REVERSE | NOTE (root) | ROOT | REVERSE | SCALE | CHANCE |
| F6 | CLEAR | SCALE | (free) | RANDOMIZE | DENSITY | same |
| F2-F4, F7-F8 | VARY RHYTHM NOTES REDO UNDO | same | PASTE DUP>NEXT CLR LOCK REDO UNDO | same | | |

**Encoder owner.**
- `bool root_enc_` becomes `enum class EncOwner : std::uint8_t { None, Root, Scale } enc_owner_` (main_screen.h).
- `root_owns()` becomes `enc_owns()`, true on HOME when no drawer row is latched and no step is held.
- `turn_input`: Root uses `root_step` as today. Scale calls `Controls::step(Entry{Item::Scale}, …)`, which is clamped with no wrap, as the SETUP knob was.
- The owner is released by any F key, a drawer-row latch, `setup_toggle` or leaving the screen.
- `enc_text` shows "ROOT C" or "SCALE PENTA MIN" (`scale_value_text`).
- `root_tap` (main_screen.cpp 186) works on HOME while Shift + F5 is held (`tool_mask_` bit 4), with keys 36..47. The SETUP-only ROOT label code (about 560-571) goes.
- `tool_dim` on SETUP dims only PASTE, UNDO and REDO.
- The key label is "RANDOMIZE" if skin_frow_test shows it fits the box, else "RANDOM". The toast says "RANDOMIZED".

**What the generators do with NOTE and SCALE** (app/seq/src/ops.cpp). `scale_notes(g)` (line 64) is the range [low, high] filtered by the scale over the root, or the lowest note when nothing is left.
- RANDOMIZE (`random_fill`, 747) draws a new rhythm and ties. Every sounding step in the length gets a weighted in-scale pitch (`draw_notes`). Ties take the pitch before, rests keep whatever pitch they had, and conditions and ratchets are cleared.
- NOTES (`regen_notes`, 876) gives every playing step a new in-scale pitch. Rests and ties keep their stored pitch (a tie continues the note before), and flags, ratchets and conditions stay.
- VARY (`mutate` at kVaryPct 25 %, 807) picks about 25 % of the steps for one of four changes. Only "new note" draws from the scale, so a press re-pitches about 6 % of the steps.
- RHYTHM (`regen_rhythm`, 839) rewrites the flags only (rests, accents, slides, ties; the ratchet stays). Every pitch stays.

When a pattern has notes outside a newly chosen scale:
- after RANDOMIZE none of the sounding notes are out of scale (silent rests may still hold old pitches);
- NOTES replaces all the sounding ones;
- VARY replaces only the few it draws;
- RHYTHM keeps them all, and a rest it wakes plays its old pitch;
- REVERSE and CLEAR draw nothing;
- the key LEDs dim out-of-scale notes (home_map.cpp 1471).

No tool snaps notes to the scale, and none is proposed (no free key).

**SETUP F5: CHANCE is recommended.** It is a per-pattern 1..100 % chance that a step plays, applied at play time and reversible. It is not a duplicate:
- PLAYS is per step and stored in `Step.cond` (75/50/25 %, ratios, FILL, 1ST);
- FILL is a momentary variant;
- DENSITY is the generator's destructive rest share at edit time.

How it combines:
- an Always step gets CHANCE %;
- a step with p % gets p·CHANCE/100 (at least 1 %);
- ratio and named conditions are left alone, because they are deliberate.

The other two:
- OCTAVE is already there: Shift + turn on F4 TRANSP moves 12 st per detent (kTranspose coarse 12, pages.cpp 112; `Controls::step`, 625). The cost is a MANUAL line.
- A per-pattern ACCENT amount has no cheap place. The voice's accent is binary per note (acid_voice.cpp 889), so the sequencer would have to own ACCENT per pattern against locks, the knob, snapshots and MIDI learn. Also, after G, SETUP has no free key on either row. It is deferred, with today's routes: the ACCENT knob and per-step locks.

**Keys named in:**
- main_screen.cpp: `kHomeKeyLabels`/`kSetupKeyLabels` (49, 51), `kTool*` (55-68), `home_tool`/`setup_tool`, `tool_dim`, `enc_text`, `root_tap`;
- pages.cpp `kSetupPage` (63);
- help_texts.cpp: the Home and Setup texts, and `kVariationPads` (460) "NEW" becomes "RANDOM";
- tests: home_tools_test, pattern_setup_test, scale_lock_test, app_tools_test, help_texts_test, skin_frow_test, pages_test;
- scenarios: home_tools, pattern_setup, scale, tools, step_edit, manual_tour, help, help_skin;
- MANUAL: the HOME Shift row and PATTERN SETUP (about line 380).

## F. TRACT as a fifth knob set on PERFORM KNOBS

- `kKnobSets` goes from 4 to 5. Set index 4 is TRACT, with factory {kLaneTract 0 VOWEL, 1 TONGUE, 2 LIPS, 6 TRC MIX}, and `knob_set_name` returns "TRACT" (ladder_rows.cpp).
- How it is reached: a tap on F3 while F3's set is in use flips GROWL and TRACT. F3's label and lamp show whichever is up, and the toast says "SET 3 TRACT". Shift + F3 is FACTORY for the one in use. The alternative, if the maintainer prefers it: Shift + Up/Down cycles all five sets (free on KNOBS).
- Saved in PERF 2.0 as all five sets whole (P1). Rig is in SDRAM, so SRAM costs 0.

## E. One key per sound, own screens, header chips, the pads (A inside)

Ambiguity: "sample recorder" is named twice. The consistent reading is that WOBBLE takes the Sample Recorder button and RECORD moves to Song (press again).

| Button | Today | Again today | After | Again after |
|---|---|---|---|---|
| Sample Loader | MODDED | NOTES/TONE | same | same |
| Shift + Sample Loader | STOCK flip | | retired: it would double the Note key | |
| Sample Playback | TRACT/GROWL group | flips | GROWL | nothing |
| Sample Editor | WOBBLE | nothing | TRACT, STEPS layer | LINE layer and back |
| Sample Recorder | RECORD | nothing | WOBBLE | nothing |
| Song | SONG | nothing | SONG | RECORD |
| Shift + Rec | RECORD F1 | | same | |

**E1a.** `tract_screen_ = sdram_instance<InstrumentScreen, 4>()` gets a 1-page `kTractGroup`, and `kGrowlGroup` becomes 1 page (instrument_screen.cpp). The button code is in o3_303_app.cpp 298-341.

**E1b: the colour keys on every screen.**
- Plain Note toggles MODS, Instr GROWL, Vol (FX1) TRACT and Fx (FX2) WOBBLE (the WOB latch).
- They are intercepted in `O3_303App::input`, like the screen buttons. Check whether the NAMING screen uses them first.
- New `app/src/sound_buttons.{h,cpp}` with `enum class Sound : std::uint8_t {Mods, Growl, Tract, Wobble}`:
  - `void toggle_sound(Rig&, app::Context&, Sound)`;
  - `bool sound_on(const Rig&, const dsp::Engine&, Sound)`;
  - `std::uint8_t sound_bits(const Rig&, const dsp::Engine&)`.
  These are moved from `InstrumentScreen::run_action` and `action_on`.
- With a step held on a home-map screen, the plain key latches the drawer row as today (NOTE, GATE, PLAYS, LOCK) and moves the cursor to the held step.
- Shift + colour runs the PERFORM action assigned on PERFORM SETUP F1-F4, on every screen. A tap (< 300 ms) latches or unlatches; a hold is momentary.
  - New LiveActions methods: `action_button_press(ctx, button, now_ms)` and `action_button_release(ctx, button, now_ms)`.
  - The app keeps a down-mask to route the releases.
- The on/off F keys of the sound pages leave them: GROWL F8, TRACT F8, WOBBLE F8 and MODDED's MODS key. GAIN takes the first two in B2; the others stay blank.
- SONG's columns move to F5 PAT, F6 RPT, F7 TRSP and F8 MUTE.

What breaks:
- D22 ("Shift + button = button") and UX-303 2.3 (MASTER and RECORD ignore the keys);
- tests: home_rows, perf_buttons, song_screen, master_screen, record_screen, perform_screen, help_texts;
- 15 scenarios: manual_tour, master, perform, skin_song, perf_assign, freeze_modes, perform_setup, growl, perform_pads, freeze, m3b_seq, skin_perform, home_rows, step_edit, song.

Note editing keeps Up/Down.

**E1c: header chips "MOD GRL TRC WOB".**
- OBSIDIAN (skin/header.cpp `fill_header`): the chips replace the tagline at (14,52) in Micro. That is 15 glyphs, as many as "ACID BASS VOICE". On = the key's colour, off = ChromeDim.
  - SAVE FAILED keeps priority and shows instead of the chips.
  - NO AUTOSAVE becomes a logo tint.
  - The "STOCK . MODS OFF" tagline goes, because the MOD chip says it.
- Classic (frame.cpp `draw_status`): the STOCK chip becomes four chips after SONG, filled = on and outline = off. The duplicate WOB action chip is skipped.
- `frame::StatusView` gains `std::uint8_t sounds`, a bit per Sound. Rig caches it in `update()` and `fill_take_status` (rig.h 94) copies it.

**The pads: two schemes.**
- Recommended, fixed rows: on every home-map screen, rows 1-2 are the pattern's steps and rows 3-4 belong to the screen.
  - TRACT: row 3 is the palette, row 4 the live vowels.
  - WOBBLE (E2c): row 3 is the wobble rates in H1's order, row 4 the keys.
  - GROWL, MODDED, INSTRUMENT and HOME are unchanged.
- Alternative, as a later superset: the screen's own button pressed again gives a full-pad sound layer on every screen.

**A: the TRACT STEPS layer (E2a).** The pattern is in view, and the pads edit VOWEL, TONGUE and LIPS instead of accent, slide and step.
- Data:
  - VOWEL is the existing TRACT line, so there is no new data. A pure `unsigned tract_line_step_of(unsigned s, const TractLine&, unsigned pat_len)` returns s for SAME, else s & (len-1). With a fixed length, an edit reaches every repeat, and the toast says so.
  - TONGUE and LIPS are kLaneTract lane locks (ids 1 and 2), claimed on the first write, so at most 2 of the 8 lanes.
  - Per-step arrays were rejected even with format freedom: they cost 384 B of SRAM in the sequencer's three Pattern copies.
- Pads:
  - rows 1-2, columns 1-8 go to the home map; columns 9-12 do nothing;
  - row 3 is today's palette (`palette_press`). It writes the held steps (new public `HomeMap::take_steps`, which wraps `row_steps`) or the cursor, then calls new public `HomeMap::advance_cursor`;
  - row 4 is the live vowels, as today.
- Knobs:
  - F1 VOWEL with a held step and a turn calls `Bank::set_tract_step`. It is intercepted in `InstrumentScreen::turn_input` before `home_->turn`.
  - The other knobs lock through the home path, and "--" unlocks.
  - Shift + F5 CLEAR also clears the TONGUE and LIPS lanes (`clear_lane_motion`).
- The LINE layer is today's 24-step editor, unchanged (the `line_*` code). Tabs read STEPS / LINE.

**The strip (E2b).**
- OBSIDIAN (skin/instrument.cpp):
  - the ACCENT LED row becomes a VOWEL glyph row, tinted for GLD and for a VOWEL lock;
  - the SLIDE row becomes the TONGUE LED (lit = lock), the STEP row the LIPS LED;
  - the NOTE row and the playhead stay (`note_ink`).
- Classic: the GATE row shows `cell.gate` as the vowel letter plus '~' for GLD or '*' for a lock.
- `InstrumentCell` gains `std::uint8_t vowel; bool glide, vowel_lock, tongue, lips;`. A pure `fill_tract_cells(const Pattern&, unsigned first, InstrumentView&)` is host-tested.

## H. PERFORM page 1 as an effects surface

PERFORM KNOBS (page 2) stays as it is, with F's TRACT set.

**Inventory and verdict.**
- These have no key and get pads:
  - MUTE, ACC ALL, SLD ALL, OCT UP, FILL (its variant set on PERFORM SETUP F5) and THROW: existing PerfActions;
  - the freeze unit (units/freeze, freeze.h 82-87), all by rate: FREEZE (Length 1/4, 1/8, 1/16), ROLL (Roll Rate 1/8..1/64, changed live at the loop boundary), BLD BEAT and BLD BAR (Accelerate), STOP (1/8..2 bars) and REV;
  - the wobble's 11 rates (seq/wobble.h);
  - OCT DOWN (the live transpose exists, new action).
- H2 brings the filter jumps (CUT LO, CUT HI, RESO) and DIST (distortion in).
- These get no pads:
  - the four sounds (the plain colour keys, per the maintainer);
  - ratchets (per-step PLAYS, plus FILL's RATCHET variant);
  - the tempo nudge, a sync correction that stays on Master + turn.
- Transpose stays as the keys row.

The M3C backlog:
- GATE and PUMP (T) are sequencer writes of slot 0's level through `BlockContext::set_mix`, which exists (engine.h 122). That is one write per block, negligible CPU, under 48 B of SRAM.
- DIVE (P) is a slew on Tune. All three go in H3 and are measured with `cpu` (TRACT on, 62 % peak) before the maintainer relies on them.
- RISE is dropped: BLD BAR with CUT HI does the build.
- CRUSH is a new DSP unit (estimated under 0.5 %, slot 0 insert 3 is free). It is a later round through dsp-port, with an oracle and a unit measurement.
- OTT (5-8 %) is out on a 62 % peak.

**Layout (recommended).** Columns 1-4 of rows 2 and 3 are both 1/4, 1/8, 1/16, 1/32.

| Row | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 LINE + TONE | MUTE | ACC | SLD | OCT- | OCT+ | FILL | THROW | DIST² | CUT LO² | CUT HI² | RESO² | ALL OFF |
| 2 REPEAT | FRZ 1/4 | RPT 1/8 | RPT 1/16 | RPT 1/32 | RPT 1/64 | BLD BEAT | BLD BAR | STOP 1/2 | STOP 2 | REV | GATE³ | PUMP³ |
| 3 WOBBLE | 1/4 | 1/8 | 1/16 | 1/32 | 1/4T | 1/8T | 1/16T | 1/8D | 1/16D | 1/2 | 1/1 | DIVE³ |
| 4 KEYS | C | … | | | | | | | | | | B |

² built in H2 (3a9e0e5); ³ comes in H3, which is not built: those pads are dark.

Reasons for the rows:
- Row 1 groups what the line plays (it takes effect at the next step) with the tone punches, and keeps ALL OFF where it is today.
- Row 2 is one unit with one owner.
- Row 3 puts a held rate under the finger.
- Keys stay because the live transpose (TRNSP) is the 303's main live gesture.
- The wobble order `kWobblePadRates[11]` (perform.h) is shared by all three pickers: here, the WOBBLE screen's row 3, and the home map's WOB row (`perform_wobble_rate`). `WobbleRate` values are unchanged.

**Gestures.**
- A press puts the effect in and the release takes it out. Shift + press latches it, and a tap or ALL OFF releases it. That is today's action gesture.
- Effects in different groups combine.
- Row 2:
  - `kRepeatPads[12]` = {PerfAction, freeze param, value}, for example {Roll, RollRate, kRoll16}. RPT pads also write Accelerate off.
  - A pad writes its parameter, then Mode, then Active, through FreezeOwner.
  - While a roll owns the unit, another RPT or BLD pad only rewrites the rate, live. The last pressed held pad wins, and releasing it returns to the one below it. A pure `struct PadStack` (press order of ≤ 12 columns) is host-tested.
  - FRZ, STOP and REV do nothing while another mode owns the unit, as today.
  - The pads leave their setting in the unit, so PERFORM SETUP F6-F8 show the rate last played and Shift + colour uses it. Restoring the setting would break STOP's release ramp.
- Row 3: a rate pad calls `wobble_rate(ctx, rate)` and holds Wobble; the stack works the same way.
- Quantisation is not new in H1:
  - the line acts at the next step;
  - repeat effects enter at once with the unit's 5 ms crossfade;
  - the wobble follows its RESET setting.
  An entry on the next 1/16 can be a later option, decided by ear.

**Lamps** (pad LED and screen lamp):
- off: no effect there, or blocked (a repeat pad while another mode owns the unit);
- dim: available;
- on: held or latched;
- latched also gets the screen frame, as today;
- the wobble row lights the rate that is running.

**Where the rest goes.**
- Snapshots 1-8 and RANDOM, VARY, UNDO, REDO keep their exact pad behaviour (store, recall, morph) on PERFORM SETUP's row 2. SETUP's map is page 1's map with row 2 swapped. After a recall the encoder morphs until an F key is pressed. UNDO and REDO are also Shift + F7/F8.
- The ladder leaves page 1. HOME's FILT row 3 and PERFORM KNOBS keep the ladders and their PERF data.
- No saved data is orphaned: SNAP and the four ladder rows are still used.

**Fit with E.**
- The plain colour keys are the sounds and have no pads.
- Shift + colour gives four assignable shortcuts to PerfActions on every screen. On PERFORM they are the same state bit as their pads, as today.
- The WOBBLE key toggles the wobble latch; the rate pads play the wobble momentarily at a rate. It is one state either way.

**REC.** Pad effects are not recorded. They are states of the performance, not lanes, and no toast is shown, to keep the surface quiet. REC on page 1 records the encoder's knob moves. H2's punches go through sequencer punch slots, never the lane recorder.

**Alternative layout.** Drop the keys and turn row 4 into more punches. Only worth it if the maintainer transposes from HOME.

## C. Slices

Every slice:
- builds clean, with all host tests and scenarios green and the scenario word gate under 1,800;
- updates MANUAL, CHANGELOG "Unreleased" and help_texts.cpp for what it changes;
- reports its SRAM delta from the size report (SRAM_U headroom about 5.8 KB; big data goes to SDRAM).

1. **B1.**
   - Files:
     - units/tract/src/tract.cpp and its header (table entry, `set_param` case, run loop, `kTractGainDefaultDb`);
     - units/growl/src/growl.cpp and its header (the same, per control period).
   - Tests:
     - tracts/tract_unit_test.cpp: count 8 and the golden hash;
     - units/growl/tests/growl_test.cpp: count 9 and the golden hash;
     - new tests/fx_level_test.cpp. Tests that assumed the old defaults set GAIN 0.
   - Accept: the golden hash holds at 0 dB, Mix 0 is bit-exact, +6 dB measures ±0.1, the table and the mix-law decision are in the comment.
   - Unit: A/B by ear at Mix 50 and 100, and `cpu` unchanged within noise. SRAM: about 24 B.
2. **D1.**
   - Files: new app/src/wobble_learn.{h,cpp}; pages.cpp (`wobble_learn_desc`); rig.h (Wiring); o3_303_app.cpp (attach, hook, mirror); instrument_screen.cpp (`learn_target`, `page_learnable`).
   - Tests: new tests/wobble_learn_test.cpp (the hook moves WobbleSettings, mirror runs only on change, the slot table is untouched, ids 0..5 still the contract); instrument_screen_test (WOBBLE learnable).
   - Scenario: new app/scenarios/wobble_learn.txt (Shift + F1 on WOBBLE, `midi usb b0 4a 40` moves RATE).
   - Unit: learn a CC on WOBBLE, power-cycle, the binding is still there (system.o3s), and Pickup catches.
3. **G1.**
   - Files: main_screen.{h,cpp}; help_texts.cpp.
   - Tests: home_tools_test (new row), pattern_setup_test (REVERSE, RANDOMIZE), scale_lock_test (root tap on HOME), skin_frow_test (label fit), help_texts_test.
   - Scenarios: home_tools, pattern_setup, scale, tools, step_edit, manual_tour.
   - Accept: each key does its tool, with one UNDO each, and the encoder owner is released by an F key.
   - Unit: the tools and NOTE/SCALE with the encoder. SRAM: 0.
4. **P1.**
   - Files: project.h, project.cpp, project_service.cpp, pattern.h (whole-Pattern PATS).
   - Tests: project_codec_test (2.0 round trip, a major-1 image is FromOlder with the state untouched, a missing required chunk is Damaged); project_service_test (boot and LOAD refusal, nothing written, the notice).
   - Scenario: project.txt (SAVE, LOAD round trip).
   - Accept: no write reaches an old folder, and `kMaxImageBytes` still fits 128 KiB.
   - Unit: put a v0.x project on the card. It is refused with the notice, and its files are byte-identical afterwards (compare sizes and dates on a PC).
5. **P2.**
   - Files: the cleanup list in P.
   - Accept: the greps in P find nothing old-format, and the tests are green.
   - Unit: none (code removal; P1's check is repeated).
6. **G2.**
   - Files:
     - pattern.h: `std::uint8_t chance; std::uint8_t reserved[3];` after `gate`, which keeps `motion` 4-aligned; `clear()` sets 100; `constexpr std::uint8_t effective_cond(std::uint8_t cond, unsigned chance)`; size assert 2,332;
     - bank.h/.cpp: `bool set_chance(unsigned index, unsigned pct)`, one UNDO per gesture;
     - pages.h/.cpp: `Item::Chance`, desc "CHANCE" % 1..100, default 100, coarse 10, in kSetupPage F5;
     - sequencer.cpp: the 1018 and 1037 call sites;
     - project.cpp: sanitise 1..100.
   - Tests: seq test (100 % is bit-identical, 50 % plays about half over 4,000 steps from a fixed seed, ratios untouched); pattern_setup_test; project_codec_test (round trip).
   - Scenario: pattern_setup.
   - Unit: play a line at 50 %.
   - SRAM: +12 B (the sequencer's three Pattern copies). SDRAM: about +330 B.
7. **F1.**
   - Files: pages.h (`kKnobSets` 5, kKnobFactory), ladder_rows.cpp, perform_screen.cpp (F3 flip, label), project.cpp (PERF 2.0 sets).
   - Tests: perform_knobs_test, ladder_rows_test, project_codec_test.
   - Scenario: knobs.txt.
   - Unit: play TRACT from KNOBS. SRAM: 0.
8. **E1a.**
   - Files: o3_303_app.cpp, instrument_screen.cpp, help_texts.cpp.
   - Tests: instrument_screen_test, tract_page_test, record_screen_test (Song again), help_texts_test.
   - Scenarios: growl, growl_wobble, tract_wobble, skin_tract, record, skin_record, manual_screens.
   - Unit: the five buttons. SRAM: 0 (SDRAM: one InstrumentScreen).
9. **E1b.**
   - Files: new app/src/sound_buttons.{h,cpp}; o3_303_app.cpp; live_actions.{h,cpp}; home_map.cpp (1323-1333); song_screen.cpp; instrument_screen.cpp; master_screen.cpp; record_screen.cpp; perform_screen.cpp.
   - Tests: new tests/sound_buttons_test.cpp, plus the E list.
   - Scenarios: the 15 in E.
   - Accept: a sound toggles from every screen, Shift tap latches, Shift hold is momentary, and a held step plus a key opens the row.
   - Unit: all four keys on HOME, PERFORM, MASTER and SONG. SRAM: about 8 B.
10. **E1c.**
    - Files: skin/header.cpp, frame.{h,cpp}, rig.h.
    - Tests: skin_core_test (chips and SAVE FAILED priority), skin_frow_test or the frame status test.
    - Scenario: skin_home.
    - Unit: `skin stats` peak unchanged ±50 µs, 0 dropped.
11. **B2.**
    - Files: pages.cpp (kTractPage and kGrowlPage F8), instrument_screen.cpp (hints).
    - Tests: tract_page_test, instrument_screen_test (GAIN learnable and lockable), pages_test.
    - Scenarios: skin_tract, growl.
    - Unit: GAIN by ear.
12. **E2a.**
    - Files: instrument_screen.{h,cpp} (`pad_input` 538, `turn_input`, layers), home_map.{h,cpp} (`take_steps`, `advance_cursor`), tract_line.h (`tract_line_step_of`).
    - Tests: tract_page_test (palette on held steps and the cursor, F1 turn writes the line, TONGUE/LIPS locks use at most 2 lanes, CLEAR clears them).
    - Scenario: new app/scenarios/tract_steps.txt.
    - Unit: write vowels on steps while playing.
13. **E2b.**
    - Files: skin/instrument.{h,cpp}, instrument_view.h, instrument_screen.cpp (`fill_view`).
    - Tests: skin_instrument_test (pure `fill_tract_cells`, `note_frame` words ≤ today's TRACT page).
    - Scenarios: skin_tract, tract_steps.
    - Unit: `skin stats` peak ≤ 6,000 µs with 0 dropped on TRACT (5,726 µs today).
14. **H1.**
    - Files: perform.h (`perform_pad`, PerformRole + Repeat/WobRate, `kRepeatPads`, `kWobblePadRates`, `PadStack`, `perform_setup_pad`, PerfAction OctDown appended); live_actions.{h,cpp}; perform_screen.cpp (`pad_input`, `fill_view`, `leds`); skin/perform.cpp (row titles); help_texts.cpp.
    - Tests: perform_test (map, stack), perform_screen_test, freeze_app_test (rate pads write the params), snapshots_app_test (on SETUP), skin_perform_test, perf_buttons_test.
    - Scenarios: perform_pads, perform, skin_perform, snapshots, freeze, freeze_modes, perform_setup, ladders, manual_tour.
    - Unit: a played session (rolls switching rate live, wobble rates, keys).
    - SRAM: about 28 B (the stacks).
15. **E2c.**
    - Files: instrument_screen.cpp (WOBBLE row 3 = rates), home_map.cpp (the WOB row's order, `perform_wobble_rate`).
    - Tests: perform_test, home_map_test.
    - Scenario: growl_wobble.
16. **H2.**
    - Files:
      - sequencer.{h,cpp}: `void set_punch(unsigned slot, std::uint8_t target, std::uint8_t id, float value)`, `void clear_punch(unsigned slot)`, `kPunchSlots` 4. A punch wins over the lock, the slew, the knob and the wobble; clearing it calls `restore_target`;
      - perform.h: Dist, CutLo, CutHi and Reso appended, count 15;
      - live_actions.cpp;
      - named constants `kPunchCutLo`, `kPunchCutHi`, `kPunchReso`, tuned by ear. DIST sets MODEL to the last model the session used if MODEL is OFF (GRIT if none) and MIX to 1.
    - Tests: seq punch test (it wins over a lock, is restored, and REC writes no point); perform_test.
    - Scenario: perform_pads.
    - Unit: by ear. SRAM: about 32 B.
    - As built (3a9e0e5): `PerfAction` count is 16 (not 15: Dist, CutLo, CutHi, Reso appended after OctDown), `kPerfActionCount` 16 and the mask is a uint16. DIST is the Distortion's `Active` = 1 and `Mix` = 1 in two punch slots (it does not touch MODEL). CUT LO and CUT HI are `kPunchCutLoHz` 200 and `kPunchCutHiHz` 2394 (`AcidVoice::kCutoffLiveLoHz` / `HiHz`, static_assert), one cutoff slot, the last pressed wins; RESO is `kPunchReso` 0.9. Tests: `app/seq/tests/seq_punch_test.cpp` (8 cases), perform_test, perform_screen_test, skin_perform_test, help_texts_test; scenario perform_pads (snaps 04c, 04d). Unit check owed: the values by ear.
17. **H3.** GATE and PUMP follow M3C-PLAN T and DIVE follows P. PerfAction masks widen to `std::uint32_t` (LiveActions, `perf_actions_on`, `live_actions_on`, `StatusView.actions`).
    - Tests: seq gate and pump tests (multiplier on the base level, released cleanly).
    - Unit: `cpu` with TRACT on, before and after, and 0 underruns.
    - **Not built:** it needs its CPU measured on the unit first.
18. **Docs.**
    - UX-303 amendments: D15, D22, 2.1, 2.3, 5.3 and the PERFORM map.
    - MANUAL pass.
    - `tools/manual_images.py` regenerated.
    - CHANGELOG: the old-projects refusal stated plainly.
    - Done: UX-303 (D15, D22, 2.1, 2.3, 5.3, the PERFORM map), MANUAL, README status row and CHANGELOG. **Owed:** `tools/manual_images.py` was not regenerated (it needs the simulator built locally, which the low-RAM rule forbids); the manual's PERFORM images still show the old pads.

## Riskiest assumptions not verified

1. The kit's MIDI learn and the LRNB store take a parameter of an attached but never-routed unit, save it and bring it back. Pickup also has to behave with the app writing the mirror through `set_param` (kit/app/src/midi.cpp was not read). If either fails, D needs a different proxy, and still no kit change.
2. The skin cost of a glyph is about that of an LED sprite. The four header chips cost no more than the one tagline. The TRACT strip with a vowel glyph row then stays inside the 274 µs margin (5,726 of 6,000 µs).
3. The cause of the volume drop is unproven. If it is the mid-Mix crossfade of decorrelated signals, the equal-power law fixes it and GAIN only trims the ends. If TRACT's level trim already matches the dry signal, its GAIN default stays 0. B1's measurement decides both.

Lesser ones:
- A boot refused as older never later saves into the old folder (the `blocked_` semantics were not traced to the end).
- "RANDOMIZE" may not fit a soft-key box.
- The NAMING screen may use the colour keys.
