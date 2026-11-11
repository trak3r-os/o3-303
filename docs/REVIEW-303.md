# o3-303 review: what to fix, simplify and improve

Written 2026-11-11 against o3-303 `fa04783` (local main, KNOBS built) and o3-sdk as it stands beside it. Read-only: no code was
changed and nothing was built.

**What this rests on.** The manual, DESIGN-303, the headings of UX-303, TRACT-KNOBS-PLAN sections 3 and 10 to 12, the BACKLOG, the
unit notes of 2026-11-06 and 2026-11-07, the CHANGELOG, the two open issues (#1 TRACT, #2 pads as knobs; neither has comments), and
the code named in each item. Screens: the manual's pictures in `docs/manual/img/` and one simulator run of `skin_knobs.txt` on
`build-knobs-host` (PNG paths are given where used).

**What it does not rest on.** I heard nothing and touched no unit. UX-303 was read by its headings only, not in full. Of the code, I
read `home_map.cpp` (pads, arrows, copy keys, LEDs), `o3_303_app.cpp` (global routing), `perform_screen.cpp` (input), `ops.cpp`
(generators), the cutoff path of `acid_voice.cpp`, and grepped the rest. Song, file, record, master and vault screens were checked
only for the specific points cited. Where an item is an inference and not something I read, it says so.

**Flags.** Cost S (a day or less), M (a slice), L (several slices or an o3-sdk change). **P** = changes the project format.
**H** = changes a habit the player has. **PLAN** = touches a slice already planned in TRACT-KNOBS-PLAN 10 to 12.

Sim PNGs: `/tmp/claude-1000/-home-anortes-Code-o3-303/9cded33d-714c-4bd1-a280-d1e21e31535c/scratchpad/fable-sim/knobs/`

---

## 1. Fix

### F1. More than half of the CUTOFF knob does nothing on a stock 303

- **What.** With RANGE on 303 (the default, and always with MODS off) the voice clamps the knob to 200 to 2,394 Hz. The knob, its
  read-out, the ladder and the KNOBS row still run 25 to 4,500 Hz.
- **Evidence.**
  - `units/acid_voice/src/acid_voice.cpp:107` (the parameter: 25 to 4,500 Hz, table curve), `:190` (`kLog2KnobLo`, 200 Hz),
    `:925` (`cutoff_base()` clamps the knob between `clamp_lo_` and `clamp_hi_`).
  - From the knot table (`:97-101`): 200 Hz sits at about 32 % of the travel and 2,394 Hz at about 80 %. So roughly the bottom
    third and the top fifth are dead: about 52 % of the travel.
  - CHANGELOG v0.6.0 says it in words ("the bottom third ... and the top fifth ... now sound the same").
  - `knobs/04_walk_row_2.png`: the CUTOFF row's twelve pads read 25, 30, 60, 137, 277, 501, 821, 1.26k, 1.84k, 2.56k, 3.44k,
    4.50k. On RANGE 303 the first four all play 200 Hz and the last three all play 2,394 Hz. Five pads of twelve do something.
  - The screen shows "25 Hz" while the filter is at 200 Hz. The v0.5.0 unit note has an unexplained "CUTOFF 25 Hz"
    (`docs/hw/2026-11-06-v050.md`, rows 4 and R5): a ladder tap on the first pad would give exactly that, though I cannot confirm it.
- **Why it matters.** CUTOFF is the control a 303 player's hand lives on. Dead travel at both ends reads as a broken knob, and it
  halves the resolution of the ladder that issue #2 was about.
- **Proposed change.** The stored value stays in Hz over 25 to 4,500 (no format change, id unchanged). The app, not the voice,
  bounds it while RANGE is 303: the encoder stops at 200 and 2,394, the read-out shows the bounded value, and a ladder row spreads
  its twelve pads over 200 to 2,394 (`ladder_rows.cpp` takes a min and max from `Controls`, not from the descriptor). On FISH the
  whole range comes back. A project with a value outside loads as it is and shows the nearest end.
- **Cost** M. **P** no. **H** mild (the knob reaches its end sooner, and the ladder pads change values).
- **Only the ear decides** whether 200 Hz is the right floor: it was "fitted roughly to one recording of one unit"
  (`acid_voice.cpp:190-192`).

### F2. PERFORM cannot change the pattern

- **What.** On PERFORM, KNOBS and every other screen but HOME, pressing Pattern leaves the screen for HOME at the press. The 48
  patterns are only on HOME's Pattern-held overlay.
- **Evidence.** `app/src/o3_303_app.cpp:248-251` (`in.press(Button::Pattern) && ctx.screen() != &main_` shows HOME and takes the
  press). `perform_screen.cpp:440-480` handles F keys, colour buttons and Rec only: no arrows, no pattern gesture. The manual's
  PERFORM section (366-409) has no way to pick a pattern.
- **Why it matters.** PERFORM is specified as "twenty minutes of play without a menu" (UX-303 5.3). Changing pattern is the most
  common live move on a 303, and it costs: Pattern, hold Pattern, pad, Performance. Latched actions survive the trip, but the hands
  leave the action row and the snapshot morph.
- **Proposed change.** Pattern held is the 48-pattern overlay on every screen, in place. A tap of Pattern (released under the hold
  time with no pad touched) still goes to HOME, at the release. The overlay code is already in the shared home map
  (`home_map.cpp:568-583`, `:1160-1178`); PERFORM needs to route the pads to it while Pattern is down and draw the grid.
- **Cost** M. **P** no. **H** mild (from other screens HOME opens at the release of Pattern, not the press).

### F3. A chain never loops

- **What.** Patterns chained with Pattern held play once in order, then the last one repeats for ever.
- **Evidence.** `o3_303_app.cpp:179` sets `set_chain_loop(false)` once; nothing else calls it (grep over `app/src`). The sequencer
  supports the loop (`sequencer.h:242-254`). To loop A, B, A, C today: build the chain, open SONG, F3 CHAIN, F1 PLAYS to SONG.
- **Proposed change.** A chain of two or more patterns loops until a single tap replaces it. That is how the original's pattern
  chaining behaves and what a player expects of "hold and tap several". A one-entry queue behaves as today.
- **Cost** S. **P** no. **H** yes (a chain used to end on its last pattern). If both are wanted, Shift held while chaining could
  mean "once".

### F4. In a long pattern the step pads change page under the fingers while it plays

- **What.** While playing, the step pads follow the playhead's page, every bar, whatever the player is doing.
- **Evidence.** `home_map.cpp:368-371`: `view_page_` is set to the playing page each time it changes; nothing pins it. The manual
  says so (lines 337-338).
- **Why it matters.** On a 32 or 64 step pattern, editing bar 3 while the line loops means catching the page in the bar it is
  shown. A held step keeps its own index (`held_step_` stores the absolute step), but the next tap lands on another bar.
- **Proposed change.** The page follows the playhead until the player picks a page: Left or Right across a page edge, or a step
  pad tap, pins it. The PAGE window says "PAGE 3/4 HELD"; Shift + Left or Right at the edge, or Stop, gives it back. One bool.
- **Cost** S. **P** no. **H** mild.

### F5. Only the playing pattern can be seen or edited

- **What.** While the transport runs, the screen is forced to the pattern that plays.
- **Evidence.** `home_map.cpp:363-366`: `bank->select(ph.pattern)` on every update while playing. A pad tap with Pattern held
  queues (`:578`), it never selects.
- **Why it matters.** Writing the next pattern while the current one loops is the normal way to build a set live. Today the player
  must queue the empty pattern and write it while the audience hears it.
- **Proposed change.** Shift + pad in the Pattern overlay selects that pattern for editing without queueing it. The header's
  PATTERN digits show the edited one and a chip says "PLAYS A01". The follow comes back at the next plain queue tap or at Stop.
  REC while playing keeps writing at the playhead of the *playing* pattern (as `key_press` does, `:434-438`), so REC must be
  refused or redirected while detached: that is the design question of this item.
- **Cost** M. **P** no. **H** no (a new gesture).

### F6. The manual's "Recording a take" still has the SOURCE key

- **What.** Section 11, step 2: "**F2** picks the source: MASTER is what o3-303 plays. **F3** picks the length" (`docs/MANUAL.md:880`).
  Backlog item 4 removed SOURCE; the RECORD table of section 5 (line 685-690) has no F2. `record_screen.cpp:115-116` still draws a
  SOURCE label and value on the classic card.
- **Proposed change.** Drop the F2 sentence in section 11. Drop the SOURCE label from the card, or keep it as a fixed "MASTER"
  caption. U7 moves these keys anyway (ARM, BARS, SAVE, DISCARD on F1 to F4): fix the text in the same commit. **PLAN** (U7).
- **Cost** S. **P** no. **H** no.

### F7. Pattern held + Copy answers with a redirection

- **What.** With Pattern held, Copy, Insert and Delete only toast "COPY PAT, DUP>NEXT, CLEAR LOCKS: PATTERN SETUP".
- **Evidence.** `home_map.cpp:1001-1005`. The copy itself is PATTERN SETUP's Shift + F1, then go to the target, then Shift + F2.
  DUP>NEXT only reaches the next slot.
- **Why it matters.** The player is already looking at the 48 patterns with a finger on Copy. Being told to go elsewhere is a dead
  end at the exact moment of intent.
- **Proposed change.** In the overlay: Copy + a pad copies the current pattern onto that pad (one undo, a toast "A01 > B03"),
  Delete + a pad clears that pattern (press twice, or undo covers it). PATTERN SETUP's COPY PAT and PASTE can then go (see S6).
- **Cost** S. **P** no. **H** no for the gesture; yes if the Shift keys are removed.

### F8. The 56 % gate for TRACT rests on CPU figures that do not repeat

- **What.** The plan lets A1 start only if the render stays under 56 % on the unit. The readings of the same project move by ten
  points between runs.
- **Evidence.** `docs/hw/2026-11-07-cf682d4.md`: `stock_only` reads 46.4 % against 35.7 % in the round before, "not looked into
  further". `docs/hw/2026-11-07-de6704f.md`: three rows 4 to 5.5 points lower, "the cause was not looked for". v0.5.0 read 52.0,
  57.3 and 58.5 % on HOME in three windows, and 64.8 % with two over-budget blocks since boot (`2026-11-06-v050.md`, R6). The
  notes themselves say "read every row as one run of one project".
- **Why it matters.** TRACT measured at 7.6 to 9.5 % of a block (plan 11.7). Added to 49.5 % it is at the governor; added to 58.5 %
  it is over. A gate on a max-of-60-seconds figure with a 10-point spread decides nothing.
- **Proposed change.** Before A1, one fixed CPU scenario for the unit: a committed project (not the maintainer's `UNTITLED`), a
  scripted key sequence, `cpu reset` after settling, a fixed window, each row run three times, and the max and the mean both
  written down. Explain the `stock_only` jump first: a voice path that costs 10 points more with everything off is either a
  measurement artefact or a real slow path (a denormal, a slew that never settles). **PLAN** (X3, A1 gate).
- **Cost** S to M, plus a unit turn. **P** no. **H** no.

### F9. Unit rows still open that hide possible bugs

These are not new findings; they are rows that ended "not run" or "inconclusive" and have since been built upon.

| Row | State | Why it matters now |
|---|---|---|
| FILT ladder LEDs on the unit (`2026-11-06-v050.md`, rows 4, R5) | Inconclusive: `leds` showed the keyboard pattern whether FILT was toggled or not, and taps on pads 35 and 47 changed nothing | KNOBS is four such rows. If the ladder LEDs or taps misbehave on the unit, KNOBS inherits it. The plan's hw-session lists "KNOBS by hand with REC and its LED bars": put the FILT row first |
| The hang after a plain `reset` (same note, Second pass) | Unexplained; 10 resets passed, the maintainer stopped the loop | It was once in about fifteen resets. It is probably the kit's or the bootloader's, but no release note can say it is gone |
| Ladder rows on MODDED, GROWL, WOBBLE; snapshots with the wobble; one undo for a held lock; MIDI learn recorded as motion (rows 5 to 11) | Not run, or partly | All shipped in v0.5.0 and v0.6.x unverified on hardware |
| `leds` reads the same with GROWL on and off (R4: "lit=31 in both") | A fact, not a bug | No pad light anywhere says an effect is on. See X6 |
| The tester could not find the snapshot pads (R4, row 7) | "Shift + Params gave the FX page" | The tester followed an old page name. HELP is per screen, so nothing answers "where are snapshots" from HOME. See X8 |

- **Cost** one unit turn. Nothing to build until a row fails.

### F10. Playing before the boot load ends turns autosave off

- **What.** If the player presses Play or edits before the project has loaded at start-up, the load is abandoned and autosave is
  suspended ("AUTOSAVE OFF: PLAYING BEFORE THE LOAD").
- **Evidence.** `docs/MANUAL.md:779-781` ("Give the unit a moment after switching on before you press Play"), and the two messages
  in section 14. The NO AUTOSAVE chip (backlog 17) makes it visible, which is good, but the trap is still there.
- **Why it matters.** The first thing anyone does after switching on a groovebox is press Play. The cost is an unsaved session,
  discovered later.
- **Proposed change.** Hold Play and the edit keys until the boot load settles (the boot screen stays, or the PAGE window says
  LOADING), with a short timeout after which today's rule applies. I did not measure how long the boot load takes on the unit; if
  it is under half a second the hold is invisible.
- **Cost** S to M. **P** no. **H** no.

### F11. DELETE keeps the files and burns the name

- **What.** DELETE hides a project with a `.deleted` marker. The space is not freed and the name can never be used again ("NAME
  USED BEFORE: PICK ANOTHER").
- **Evidence.** `docs/MANUAL.md:639-640`, `:959`; o3-sdk `kit/app/include/o3sdk/app/files.h:134-135` ("decision D4").
- **This challenges a recorded decision (D4).** I understand the caution (never remove a player's files). But a player who deletes
  "test" and later wants a project called "test" hits a wall that only a computer removes. A middle way that keeps D4's safety:
  SAVE AS or NEW onto a hidden name asks "REPLACE DELETED PROJECT? F7" and then overwrites it; the marker goes. Nothing is removed
  that the player did not ask twice to replace.
- **Cost** M (o3-sdk's files service, under its rules). **P** no. **H** no.

### F12. FILE LIBRARY draws its slots eight wide over pads that are twelve wide

- **What.** The screen shows the bank's 64 slots as an 8 x 8 grid. The pads load slots 0 to 47 as 4 rows of 12.
- **Evidence.** `docs/manual/img/library.png`; `app/src/vault_screen.cpp:339` (`s % 8`, `s / 8`). The LEDs dim-mark every eighth
  slot (`:280`, `pad % 8u == 0u`), which on rows of twelve lights pads 1 and 9 of row 1, pad 5 of row 2, pads 1 and 9 of row 3:
  a pattern that means nothing to the eye.
- **Why it matters.** It is the one screen where the picture and the pads disagree. Slot 12 is the first pad of row 2 on the pads
  and the fifth cell of row 2 on the screen.
- **Proposed change.** Draw the slots as the pads are: 4 rows of 12 for 0 to 47, and with Shift held the grid shows 16 to 63 as
  the pads then do. Mark the beats of twelve, or nothing. This is also the moment to give LIBRARY its OBSIDIAN form, since "too
  full to be redrawn" (manual, section 13) is mostly the 8 x 8 grid.
- **Cost** M. **P** no. **H** no.

### F13. Small inconsistencies

| # | What | Evidence | Change |
|---|---|---|---|
| a | Three on/off switches, three rules of memory: MODS is not saved, GROWL's "last mix" is not saved (50 % after a restart), WOB's latch is saved | MANUAL 519, 539, 564 | Save all three with the project (appended fields). **P** (appended, old projects load) |
| b | MIDI: "A fix is being made" for the 25 ms late first beat has stood since 2026-11-01 | MANUAL 870-871; `docs/hw/2026-11-01-midi.md:131` | Either land it or say "known, not scheduled" |
| c | UNDO on RESET PROJECT brings back the patterns and the song but not the sound, snapshots or tempo | MANUAL 808-811 | RESET first stores the sound in the library's LAST slot (it exists: backlog 16) and says "LAST: SHIFT F3" |
| d | HELP for Sample Play and Sample Edit says "again does nothing" | `help_texts.cpp:54` | Goes with A2 and U5. **PLAN** |
| e | The PLAYS row prints "x1" under all sixteen steps | `docs/manual/img/home.png` | See X3 |


---

## 2. Simplify

Each item says what goes and what a player loses.

### S1. PERFORM SETUP out of the Performance cycle (challenges K2 and sentence 2 of plan 10.4)

- **What.** Performance now cycles PERFORM, KNOBS, SETUP (`o3_303_app.cpp:252-260`). Going from KNOBS back to PERFORM, the two
  pages a hand alternates between while playing, passes through a settings page every time.
- **I am challenging a built decision.** The plan's sentence 2 ("the same button again goes to its next page, and wraps") is a good
  rule for pages of the same kind. SETUP is not a page you play; it is the configuration of the other two. DESIGN-303 section 4
  had it as Shift + Performance, and that was right.
- **Proposed change.** Performance flips PERFORM and KNOBS. Shift + Performance opens SETUP from anywhere; Performance leaves it.
  The tab strip of U5 reads `PERFORM | KNOBS`.
- **Goes:** the third press. **The player loses:** nothing; SETUP is one chord away instead of one or two presses.
- **Cost** S. **P** no. **H** yes (but KNOBS is unreleased, so the three-page habit is days old). **PLAN** (K2 built, U5).

### S2. One ladder table: PERFORM's row 3 becomes KNOBS' row 3

- **What.** There are now five ladder rows with two rules. HOME's FILT rows 3 and 4 are KNOBS' rows 3 and 4 (shared, "holds a
  knob"). KNOBS' rows 1 and 2 hold a knob. PERFORM's row 3 is a fifth row that "follows the latched knob" unless given one, with
  its own caption grammar ("LATCHED: DECAY", "> DECAY") and its own saved field (`ladder_rows.cpp:18` `follows()`, MANUAL 329, 378).
- **Proposed change.** PERFORM's row 3 is the same row as KNOBS' row 3 and HOME's FILT row 3: CUTOFF until told otherwise. The
  follow mode and its captions go. "Hold an F key and tap the row" still re-points it, on all three screens at once.
- **Goes:** the follow mode, one saved row, two caption forms, a paragraph of the manual. **The player loses:** the row changing
  by itself when another F key is tapped. They gain a row that is always what it says and matches the page next to it.
- **Cost** S to M. **P** yes, benign (the fifth row's field is ignored at load; older builds use their default). **H** yes.
  **PLAN** (K1 built).

### S3. The keys never go dead; the scale only lights them

- **What.** Three states hide behind SCALE: "not set" (the info line says "--", the keys are chromatic, yet NEW uses the scale
  shown on F5), "set" (keys outside it are dark *and do nothing*), and "No scale / Chromatic" (unlocked again)
  (`home_map.cpp:321-328`, `project.h:86` `scale_set`, MANUAL 359, and section 14's "The keys do nothing, or some are dark").
- **Why.** Acid lines live on passing notes outside the scale (the flat second, the chromatic walk-up). A locked keyboard refuses
  exactly those, and the hidden "set by turning" flag is in the troubleshooting list because people trip on it.
- **Proposed change.** The scale shown on F5 always lights the keys (scale at mid, root full, others off, as backlog 6 built), and
  every key always plays and writes. `scale_set` goes. NEW, NOTES and VARY keep to the scale as today.
- **Goes:** one hidden flag, one troubleshooting entry, the "--" state. **The player loses:** the guarantee of no wrong note. If
  that guarantee is wanted, make it an explicit Config row (KEYS: ALL / IN SCALE), default ALL.
- **Cost** S. **P** benign (the flag is ignored). **H** yes.

### S4. INSTRUMENT opens on what HOME does not have (challenges "not taken" in plan 10.4)

- **What.** Params opens VOICE, whose F row is HOME's and PERFORM's F row again (plan 10.3, point 3;
  `docs/manual/img/instrument-voice.png`). The plan keeps VOICE because it is "the only screen that shows the whole sound's
  read-outs at once".
- **Proposed change, smaller than removing it.** Keep both pages and the read-outs, but Params opens **FX** first when it is
  pressed from a screen that already has the voice on its F row (HOME, PERFORM, KNOBS), and VOICE first from the others. Or
  simply: FX is page 1. The first press then always shows eight new knobs.
- **Goes:** nothing. **The player loses:** nothing; one press saved every time they reach for DRIVE or the delay.
- **Cost** S. **P** no. **H** yes (the order of two pages).

### S5. One meaning for Play

- **What.** SONG's F1 PLAYS switches what the Play button does on every screen (SONG or PATTERN). The only sign elsewhere is the
  TRANSPORT lamp reading SONG instead of PLAY (MANUAL 146, 608).
- **Why.** A global mode set on one screen and felt on all the others is the kind of state a player forgets on stage: Play on
  HOME starts the song from the cursor row instead of the pattern in front of them.
- **Proposed change.** Play pressed on the SONG screen plays the song from the cursor row; Play pressed anywhere else plays the
  pattern shown. Once started, either keeps running across screens. F1 PLAYS goes (and frees a key for U7's row).
- **Goes:** a mode and a key. **The player loses:** starting the song without opening SONG (one extra button press: Song, Play).
- **Cost** S. **P** benign (the play-mode field is ignored). **H** yes. **PLAN** (U7 lays out SONG's F row).

### S6. PATTERN SETUP's Shift row shrinks

- With F7 (Copy + pad in the overlay), COPY PAT and PASTE on Shift + F1 and F2 can go. DUP>NEXT stays (it is the one-press
  "variation of this"). CLR LOCK and ROOT stay. That leaves room to put ROOT where it is seen: see X4.
- **The player loses:** two Shift keys that the overlay replaces. **Cost** S. **P** no. **H** yes.

### S7. A library load never moves the tempo while the line plays

- **What.** A pad tap on FILE LIBRARY loads the line, the sound *and the tempo* at once, and LOAD LINE "still sets the tempo"
  (MANUAL 660-667). UNDO takes back the line only.
- **Why.** Auditioning lines while playing is the library's best use, and each tap can jump the BPM. With a MIDI clock led set
  this is worse.
- **Proposed change.** While the transport runs, a load keeps the tempo and the swing of the pattern; stopped, it loads as today.
  The toast says "TEMPO KEPT". Then make **LOAD LINE the default** of Shift + F2: the sound is the player's, the line is what they
  came for, and LAST (Shift + F3) becomes a rarely needed safety net.
- **Goes:** nothing. **The player loses:** the preset's own tempo when browsing live (they can stop and load).
- **Cost** S. **P** no. **H** yes.

### S8. Config's 303 page

- KEYS AT BOOT (PLAY / TRANSPOSE) exists because TRNSP is not remembered. Save TRNSP with the project like FILT's rows and the
  row goes. PAD LEGEND OFF frees one text line but the drawing does not reflow to use it (inferred from the fixed layouts; not
  checked in every screen): if so, the row can go too.
- **The player loses:** two settings almost nobody changes. **Cost** S. **P** appended field. **H** no.

Not proposed: merging the two MODDED pages (thirteen mods do not fit eight keys), dropping CLASSIC (it is the fallback when the
skin's draw budget is at risk), and anything on the colour buttons' three meanings (the plan weighed it in 10.3, point 6).

---

## 3. Improve: function

### I1. Pattern lengths of any number of steps

- **What.** A pattern is 8, 16, 32 or 64 steps (`app/include/o3_303/pattern.h:265`, `valid_length`).
- **Evidence of the cost.** The library's own generator makes lines of 7 and 12 steps; they are padded to 8 and 16 with rests,
  "the one deviation from Replicant ... so the loop period differs" (`app/seq/include/o3_303/seq/gen.h`, header comment). So some
  of the 512 library lines (I did not count how many) do not loop as they were composed. And the planned TRACT line gets lengths 4 to 64 of its own
  to run polymetric (plan 11.3), while the notes cannot.
- **Why it matters.** Lines of 12 (triplet feel, 3/4), 6, 7 or 14 against a 4/4 drum machine are core acid vocabulary; the original
  had a last-step setting for exactly this.
- **Proposed change.** LENGTH takes 1 to 64. The encoder walks every value; Shift + turn (coarse) jumps 8, 16, 32, 64, so the
  common case stays one move. The step data is already 64 steps and a length byte. `double_length` clamps. The library maps 7 and
  12 as they are.
- **Cost** M. **P** benign with a caveat: the byte is the same, but v0.6.x reads a length it does not know as 16 (`ops.cpp:15`,
  `length_of`), so a newer project plays wrong lengths on an older build; the CHANGELOG says so. **H** no.

### I2. NEW that sounds like a 303 line (extends U3)

- **What.** NEW picks each note uniformly from every scale note between C1 and C3, with a 12 % chance of repeating the last one
  (`app/seq/src/ops.cpp:608-640`). The range is fixed (`GenParams.low/high` = 24, 48; saved in the project, `project.cpp:302`, but
  no key sets it). Accents and slides are independent coin flips per step.
- **Why it matters.** A uniform walk over two octaves of a pentatonic is eleven equally likely notes: lines that wander. Acid
  lines are the opposite: the root most of the time, the octave as the jump, one or two colour notes, a figure that comes back.
  U3 adds ties; it does not change which notes are drawn.
- **Proposed change,** all inside `random_fill` and `regen_notes`, no new key:
  1. Weights: root about 45 %, root an octave up about 20 %, fifth and minor third or seventh about 25 %, the rest of the scale 10 %.
  2. A motif: draw the first half (or quarter for 32 and 64), copy it to the second, then re-draw about a quarter of the copy.
  3. Slides prefer steps whose next step sounds and has a different note (a slide into a rest or onto the same pitch is wasted).
  4. Accents lean to off-beats and to the octave jumps.
  DENSITY, ACC % and SLD % keep their meaning. The seed-stability promise for SLD 0 that U3 makes cannot hold; say so once.
- **Cost** M. **P** no. **H** mild (NEW sounds different for the same seed). **PLAN** (U3: build them as one change to `gen`/`ops`
  so the maintainer hears NEW once, not twice).
- **Only the ear decides** the weights. Make them four constants.

### I3. Switch pattern now, not at the end

- **What.** A queued pattern starts when the playing one ends; on a 64-step pattern that is up to four bars. The sequencer has an
  immediate switch (`sequencer.h:255-258`, `jump()`: "starts at the next step, from its step 0") that no screen calls (grep).
- **Proposed change.** In the overlay, Shift + pad is taken by F5 (edit without playing), so: a **double tap** on a pad, or the
  pad tapped while Play is held, jumps at the next step. Better musically: jump at the next *bar* and keep the step position
  within the bar, so a fill pattern drops in on the beat. That needs a small change in the hook.
- **Cost** S (as `jump()` is) to M (bar-aligned). **P** no. **H** no.

### I4. Factory snapshots

- **What.** A new project's eight snapshot pads are empty; a tap says "SNAP n IS EMPTY" (`live_actions.cpp:276`).
- **Why.** Row 2 of PERFORM is a third of the screen's live surface, and in the first minutes it does nothing. The morph, one of
  the best things in the app, is found only after reading the manual.
- **Proposed change.** A new project starts with eight sounds in the snapshots (for example: the default, a closed dubby one, a
  screaming high-reso one, a hollow square, an overdriven one, a long-decay one, a FISH one, the growl on). Tap 1, tap 3, turn the
  encoder: the player has found the morph. Existing projects are untouched.
- **Cost** S (a constant table; the sounds need the ear). **P** no. **H** no.

### I5. Write notes from a MIDI keyboard

- **What.** Incoming notes go from the kit straight to the voice (`o3_303_app.cpp:168`, `ctx.midi().route(voice)`); the app never
  sees them, so REC ignores a MIDI keyboard. Backlog item 10 solved the same thing for controllers with a hook
  (`o3_303_app.cpp:130`, `midi_learned_moved`).
- **Proposed change.** After U1 lands (hold = length, nearest step), feed MIDI note-on and note-off through the same path as the
  pad keys: stopped + REC writes and advances, playing + REC writes at the nearest step with ties while held, velocity over the
  threshold writes the accent, overlapping notes write the slide. A full-size keyboard is how most people would rather play a
  line in.
- **Cost** M, with an o3-sdk change (a note hook), to be agreed with the other session. **P** no. **H** no. **PLAN** (after U1).

### I6. TRNSP: latch or momentary, and when it lands

- **What.** A transpose key takes effect on the next step and stays until another key (`home_map.cpp:440-445`,
  `sequencer.cpp:1050`).
- **Proposed change.** Two small things. (a) Momentary while held with Shift, so a two-step lift returns by itself. (b) An option
  to land at the pattern's start (the original's behaviour in track play). I would not add a Config row for (b) before the
  maintainer has asked for it; (a) is free.
- **Cost** S. **P** no. **H** no.

### I7. A take longer than 30 seconds

- **What.** RECORD holds 30 s (MANUAL 889). At 125 BPM that is 15 bars; "8 BARS TOO LONG AT 60.0".
- **Why.** The screen's purpose was "record the jam". A jam is minutes. USB audio out is the workaround, with a computer.
- **Proposed change.** FREE takes stream to the card while recording (a ring in the SDRAM drained by the files service), up to a
  cap such as 10 minutes; bar-length takes stay in memory as today. This is a real piece of work in the kit's recorder and in the
  card's write timing against autosave.
- **Cost** L (o3-sdk). **P** no. **H** no. Rank it low unless the maintainer misses it.

### I8. What I looked at and would leave alone

Per-step gate length (the plan's N4 is right: TIE is the 303's way). Note output over MIDI (dropped by the maintainer, backlog
18). A sound per pattern (breaks every project; snapshots plus a song row could carry a snapshot number later, appended, if it is
ever wanted). More lanes than eight (SRAM and the manual's limit are both honest about it).

---

## 4. Improve: interface

### X1. The header says which pattern plays and which is next

- **What.** The red PATTERN digits show the pattern on screen. A queued pattern is visible only while Pattern is held (a blinking
  pad, `home_map.cpp:1166-1171`). On PERFORM nothing shows it at all.
- **Proposed change.** While a pattern is queued the PATTERN window alternates or splits: "A01 > B03", and a chain shows its
  position ("2/4"). It uses the header every screen already draws, so PERFORM and KNOBS get it for free. Goes with F2 and F3.
- **Cost** S. **P** no. **H** no.

### X2. Step pads that show accent and slide at a glance

- **What.** A note is `kLedDim` (24 of 255), an accent `kLedLow` (70), a tie is dim like a note, a slide has no light of its own
  (`home_map.cpp:1189-1195`; levels in o3-sdk `pad_leds.h:16-20`). Holding ACC or SLIDE shows that layer alone, which is good.
- **Only the unit can say** whether 24 against 70 is told apart on the real LEDs in a lit room; on the simulator's picture
  (`docs/manual/img/home.png`) it is faint. If it is not, widen the gap (note 40, accent 140) before adding anything.
- **Proposed addition.** While the line is stopped and no modifier is held, a slid step pulses slowly to its neighbour's level
  (LEDs refresh every pass, independent of screen redraws: o3-sdk `runtime.cpp:461`, `update_leds()`). Skip it if it reads as noise.
- **Cost** S. **P** no. **H** no.

### X3. Less ink in the step rows

- **What.** The PLAYS row prints "x1" under every step and the LOCK row a dot, in the smallest face, on every pattern that uses
  neither (`docs/manual/img/home.png`). The GATE row's "-", ".", "A", "S" carry the real information in the same size.
- **Proposed change.** PLAYS and LOCK print only what is not the default; an all-default row shows just its name, dim. When
  neither row holds anything, the piano roll takes their height (two bands more of pitch, or taller bars). The colour button
  still latches the row, and the row appears as soon as it has content or is latched.
- **Cost** S to M (the skin's draw budget must be re-read: `skin stats` on HOME). **P** no. **H** no.

### X4. The root where the scale is

- **What.** ROOT is under Shift + F5 on PATTERN SETUP, as an encoder mode that lasts "until you press an F key" (MANUAL 362).
  The first unit session's first question was "how do I set the actual root key?" (BACKLOG, 2026-11-02).
- **Proposed change.** With SCALE latched (F5), the keys of row 4 pick the root directly: hold F5 and tap a key. The value
  already reads "D MAJOR". Shift + F5 can stay as the encoder way. It mirrors "hold an F key and tap a ladder row", a gesture the
  player already has.
- **Cost** S. **P** no. **H** no.

### X5. The wobble seen on a pad

- **What.** Backlog 14.4 (the wobble shown moving) was left out because the screen redraws only on events.
- **Proposed change.** The LEDs do not have that limit (`runtime.cpp:461`). On PERFORM the WOB pad, and on WOBBLE the F8 lamp's
  pad equivalent, follows the wave: brightness from the sequencer's published phase (the plan for 14.4 already named an atomic).
  With WOB latched, row 3 on PERFORM (the rate picker) can run the same pulse on the selected rate's pad.
- **Cost** S. **P** no. **H** no. **Unit:** whether LED PWM at these rates looks smooth.

### X6. One place that says what is on

- **What.** The header's PERFORM window shows four action lamps. Nothing outside their own pages says that GROWL is mixed in,
  that TRACT is on, or (in OBSIDIAN) more than a line of text for MODS off. The unit tester's `leds` read the same with GROWL on
  and off (`2026-11-06-v050.md`, R4).
- **Why it matters more now.** With TRACT there are four sound layers that can each be silently off or on (MODS, GROWL, TRACT,
  WOB), and the "no sound / odd sound" hunt of v0.6.0 (OVERDRIVE at the bottom) was this kind of problem.
- **Proposed change.** A fixed row of four small lamps in the header: MODS, GRL, TRC, WOB, lit when heard. Budget: four lamps and
  four labels in a header that peaks near 5,650 µs on VOICE against 6,000 (`2026-11-06-v050.md`, row 13), so it must replace
  something (the tagline "ACID BASS VOICE" is the candidate) and be measured.
- **Cost** S to M. **P** no. **H** no. **PLAN** (A2 adds TRACT's state; U5 redraws the header for tabs: do the lamps there).

### X7. The first five minutes

Things a new player meets, in order, read from the manual's tour and the screens:

1. HOME opens with CUTOFF latched and a line ready: good. The first turn may do nothing, because 350 Hz down to 200 is short and
   then the knob is dead (F1).
2. "Pick an empty pattern" then "press Rec": the EMPTY PATTERN hint is good (U8d updates it).
3. Nothing on HOME says that Shift is where NEW lives. The fastest route to a second line is Shift + F1, and it is found only by
   holding Shift. **Proposed:** the EMPTY PATTERN hint gets a second line: "OR SHIFT + F1 = NEW LINE".
4. PERFORM's snapshots are empty (I4).
5. HELP is Shift + Config and is per screen. **Proposed:** see X8.

- **Cost** S. **P** no. **H** no. **PLAN** (U8d).

### X8. HELP answers "where is it"

- **What.** HELP explains the screen in view. It cannot answer "where are the snapshots", "where is the delay", "how do I copy a
  pattern": the question the unit tester could not answer (`2026-11-06-v050.md`, R4 row 7) and the first-session questions in the
  BACKLOG.
- **Proposed change.** A third HELP page, F3 WHERE: one static screen, the same from everywhere, a table of about twenty lines
  ("SNAPSHOTS: PERFORMANCE, ROW 2", "DELAY: PARAMS, FX", "COPY A PATTERN: HOLD PATTERN, COPY + PAD", "ROOT: PATTERN, PATTERN,
  HOLD F5 + KEY", "UNDO: SHIFT + F8"). A constexpr table beside `help_texts.cpp`; `help_texts_test` checks it against the routing.
- **Cost** S (app) if the kit's overlay allows a third page; M if o3-sdk's help overlay must change. I did not read
  `help_overlay.h`. **P** no. **H** no.

### X9. Text at arm's length

- **What.** The PADS line, the hint line under the knobs and the MOTION lane's label use the micro face; in the 800 x 480
  pictures they are about 7 px tall (`home.png`, `perform.png`, `instrument-voice.png`).
- **Only the unit decides** whether they are read at 60 cm. If not: the PADS line can drop to three short tokens when the map is
  the home map ("STEPS . MODS . KEYS") and grow to the full sentence only for two seconds after a screen change or a mode pad
  (TRNSP, FILT) changes, in the next face up.
- **Cost** S. **P** no. **H** no.

---

## 5. TOP 10, by value to the player over cost

| # | Item | Why it ranks here | Cost |
|---|---|---|---|
| 1 | **F3** A chain loops | One line of code turns chaining into the live tool it reads as | S |
| 2 | **F1** CUTOFF's dead travel on RANGE 303 | The main knob and five of twelve ladder pads; affects every minute of play | M |
| 3 | **F2 + X1** Patterns from PERFORM, and the header shows what is next | PERFORM without pattern changes is half a performance screen | M |
| 4 | **I4** Factory snapshots | A table of constants; makes row 2 and the morph discoverable | S |
| 5 | **S1** SETUP out of the Performance cycle | PERFORM and KNOBS flip with one press each way; cheapest now, before KNOBS is released | S |
| 6 | **F4** Pin the step page in long patterns | One bool; makes 32 and 64 step patterns editable while they play | S |
| 7 | **I2** A NEW that favours the root, the octave and a motif | With U3 it is what makes the generator worth pressing; the ear tunes four constants | M |
| 8 | **S7** A library load keeps the tempo while playing, LOAD LINE by default | Makes the 512 lines safe to browse live | S |
| 9 | **I1** Any pattern length | Polymetric and 3/4 lines; the library's 7 and 12 play as written | M |
| 10 | **F7 + S6** Copy + pad in the patterns overlay | The gesture the player already tries; removes two Shift keys | S |

Close behind: **F8** (a repeatable CPU reading; it is first in *risk* terms because TRACT's gate depends on it, but it is not a
player feature), **S3** (keys never dead), **F5** (edit a pattern that is not playing), **X6** (the four lamps), **F10** (the boot
load trap).

**Order against the plan.** S1 and S2 touch built KNOBS code and are cheapest before a release carries it. F6, S5 and X6 ride on
U7 and U5. I2 belongs in the same change as U3. I5 follows U1. Nothing here needs TRACT, and nothing blocks it except F8.

---

## 6. What only the unit or the maintainer's ear can decide

**The ear**

- Whether the voice is "a Roland 303 + Devil Fish". Nothing in this review claims it, and the CHANGELOG's own open list stands:
  the slide, the filter, HI RES, FOLD's level, SWP SPEED, FILTER FM, the level (`docs/hw/2026-11-07-cf682d4.md`).
- F1: whether 200 Hz and 2,394 Hz are the right ends for the stock range (one recording of one unit set the floor).
- I2: the note weights and whether a repeated half sounds like a line or like a loop.
- I4: the eight factory sounds.
- U3 (planned): ties at half of SLD %.
- S3: whether a keyboard that never refuses a note is wanted, or the lock is.

**The hands and eyes on the unit**

- F9's rows: the FILT and KNOBS ladders by LED and tap, snapshots with the wobble, the held lock's undo, learn recorded as motion.
- F8: a CPU figure that repeats, and the cause of `stock_only` at 46.4 %.
- X2 and X5: whether LED levels 24, 70, 140 are told apart, and whether a pulsing pad looks smooth.
- X9 and U6 (planned): what is legible at arm's length.
- F4: whether a pinned page should release by itself after a few bars.
- F10: how long the boot load takes, which decides whether holding Play back is invisible.
- The hang after `reset` on v0.5.0: unexplained, and only repeats on hardware will explain it (not without the maintainer's word).

**The maintainer's decisions**

- F3: loop by default, or loop with a modifier.
- F11: whether D4 (DELETE removes nothing) may be softened to "replace a deleted project on request".
- S1, S2: changing KNOBS and the ladder rules that were decided this week.
- S5: Play's meaning tied to the screen it is pressed on.
- I1: accepting that a v0.6.x build reads a new length as 16.
- I5, I7, F11, X8: each needs an o3-sdk change, to be agreed with the other session first.
