# o3-303 review: how the knobs are played

Written 2026-11-11 against o3-303 `61a8258` (local main: R8 and U1 to U8 built; the TRACT page, A2, is on `lane-a2` at `6a3e8b4`)
and o3-sdk as it stands beside it. Read-only: no code was changed, nothing was built and no unit was touched.

It answers three questions of the maintainer:

1. Choosing an F key and then turning the encoder is tedious. Is there a better way (the encoder as a switch, the arrows, something else)?
2. Why is there a ladder for ACCENT on PERFORM?
3. Can PERFORM's pads take the GROWL, WOBBLE and MODDED controls, with the F keys of KNOBS as four slots to save and four keys that
   choose each row's knob (four knob templates)?

**What it rests on.** REVIEW-303 (F1, S1 to S3, the X items), TRACT-KNOBS-PLAN sections 5 and 10 to 12, the manual's PERFORM, KNOBS
and FILT parts, DESIGN-303 section 3, and the code named in each item. **What it does not rest on:** no simulator run, no CPU or
display-list measurement of any proposal; every cost is an estimate.

**Flags.** Cost S (a day or less), M (a slice), L (several slices or an o3-sdk change). **P** = changes the project format.
**H** = changes a habit the player has.

## Summary

| Question | Short answer | Proposal | Cost | P | H |
|---|---|---|---|---|---|
| 1. F key, then encoder | The encoder has no push in o3-sdk (no pin is configured). The better way is pads for the big moves and the encoder for the fine one, with no F key in between | **Q1-A** the encoder follows the row tapped last on KNOBS (part of Q3-A). **Q1-B** on PERFORM the arrows drive the F row: Left and Right move the latch, Up and Down move the value | S each | no | no (new gestures) |
| 2. ACCENT ladder on PERFORM | Nobody chose ACCENT. PERFORM's row 3 follows the F key tapped last on HOME, PERFORM or INSTRUMENT VOICE; F5 ACCENT was the last one | **Q2-A** build S2 of REVIEW-303: the row stops following and is CUTOFF until told otherwise | S to M | benign | yes |
| 3. Four templates | Yes to the idea, with two changes to the sketch: the slots are live sets that come filled (no save step), and the four row keys show and fine-tune the rows' knobs | **Q3-A** four knob sets on KNOBS' F row. **Q3-B** the WOBBLE settings and the on/off switches as row targets. **Q3-0** TRACT's knobs in the list (a gap today) | M, M, S | appended | yes (KNOBS, unreleased) |

Nothing here adds a parameter (the 64-parameter limit is not touched), moves a parameter id, or changes a snapshot (they stay
full at 48 after A2: a set is which knobs are on the pads, never a value of the sound).

---

## 1. "Choosing an F key and then turning the encoder is tedious"

### What the code does today

- **The convention** is the kit's: tap an F key and the encoder drives it from then on; hold an F key and turn for a moment's
  change; hold two and both move; Shift + turn is coarse (`app/include/o3_303/pages.h:8-13`, `PageKeys::input`,
  `app/src/pages.cpp:819-848`; the latch is set at the press, `:831`).
- **Three shortcuts already exist.** Left and Right with an F key held move its knob by 1/20 of the range and repeat while held
  (`PageKeys::arrow_turn`, `pages.cpp:856`; `repeat_arrows()` in `pages.h`). The encoder accelerates on a fast spin
  (`Context::enc_scale`, o3-sdk `kit/app/include/o3sdk/app/context.h:88`). And the ladder rows: twelve pads are one knob, on
  HOME (FILT, rows 3 and 4), PERFORM (row 3) and KNOBS (four rows).
- **The encoder has no push switch in o3-sdk.** `platform/mk66/input/include/o3sdk/platform/mk66/input/pins.h:20-21`: "There is
  no encoder push: the encoder switch is unused (docs/design/product-ux.md section 3, maintainer), so no pin of it is
  configured." Only the two quadrature pins exist (`kEncoderA`, `kEncoderB`, `:22-23`), and `platform/mk66/input/src/device.cpp:8-9`
  says the encoder push is "not touched". The kit has no event for it: `kit/app/include/o3sdk/app/panel.h:17-26` lists 33
  buttons and one encoder id. DESIGN-303 section 1 says "one encoder with no push". The source of that decision, track3r-os
  `docs/design/product-ux.md:48`, reads "Encoder push: not a control in this design", and its line 249 still holds the open
  question "Does your unit's encoder have a push switch? (a) Yes (b) No (c) Unsure: design without it, recommended". So the code
  does not say whether the part on the board has a switch; it says none is wired into the software.
- **The arrows are free on PERFORM and KNOBS.** Those two pages use Left and Right only with an F key held
  (`perform_screen.cpp:429-433`) and Up, Down, Copy, Insert and Delete not at all (`help_texts.cpp:82-87, 102-107`). On HOME and the
  INSTRUMENT pages the arrows are the step cursor and the note, so they are not free there.

### Why it feels tedious

Two hands and two places for one move: a finger goes to the F row, then the hand goes to the encoder, and the eyes go to the
screen to check which key is lit. With eight knobs and one encoder that is the cost of every change of knob. The pads remove
the choice step for the knobs that are on them, but today a pad gives twelve places and nothing in between, and the encoder
does not follow the pad: to fine-tune the knob just tapped on a row, the F key must still be found (and a GROWL or MODDED knob
on a row has no F key on that screen at all).

### Proposals

**Q1-A. The encoder follows the row tapped last (KNOBS).** A tap on a ladder row moves the knob to that place, as today, and
also gives the encoder to that row's knob: the pad is the coarse move, the encoder the fine one, and no F key is pressed. The
ENC window names the knob. It lasts until an F key is tapped, the rule the snapshot morph already has (`morph_`,
`perform_screen.cpp:396-402`). With REC on the turns are recorded on the lane the tap claimed.
- Gestures: tap a pad of row 2, then turn. Shift + turn is coarse. Tap an F key: the encoder is that key's again.
- Cost S alone (one "row focus" field in `PerformScreen`, the path `walk_row_` already has); it comes free inside Q3-A, where
  the row has an F key of its own. P no. H mild: after a pad tap the encoder no longer moves the F key latched before.
- Conflicts: none with S2, U5 or A2. The same rule on HOME's FILT rows is possible later; HOME's encoder already has several
  owners (a held step's lock, the colour-button rows), so it is not in this proposal.

**Q1-B. On PERFORM the arrows drive the F row.** Left and Right move the latch one key along F1 to F8 (blank keys skipped, no
wrap, the strip shows it at once). Up and Down move the latched knob by 1/20 of its range and repeat while held, the step Left
and Right already give with an F key held; a switch flips. So the right hand stays on the arrow cluster and the encoder.
- Gestures: Right, Right (the latch goes from CUTOFF to ENV MOD), then turn or hold Up. With an F key held, Left and Right
  keep meaning minus and plus 1/20 (unchanged).
- Cost S (`perform_screen.cpp` button path, `PageKeys` gets a `move_latch()`; HELP's buttons table; one test). P no. H no.
- Limit: only PERFORM (and KNOBS, if Q3-A is not built). On HOME and INSTRUMENT the arrows are taken, so this does not become a
  rule of every screen; HELP must say where it holds.
- Conflicts: none. If Q3-A is built, on KNOBS Up and Down with a row key held walk that row's list instead (below).

**Not proposed: the encoder as a switch.** It needs a switch that o3-sdk does not read. If the maintainer confirms the part has
one and which pin it is on, it is an o3-sdk change (pins, scanner, a kit event, the simulator, the HIL injector), cost L, agreed
first with the sessions that share the kit, and it parts from track3r-os, whose platform code this tree keeps identical. Its
best use would be "push = next F key", which Q1-B gives with a button that already exists.

**Recommendation for 1:** Q3-A with Q1-A inside it (the pads become the main way and the encoder trims), and Q1-B for PERFORM.

---

## 2. "Why is there a ladder for ACCENT in PERFORM?"

### What the code does

Nobody put ACCENT there. PERFORM's row 3 is the one ladder row that holds no knob of its own by default:

- `app/include/o3_303/pages.h:137`: `kDefaultLadder[kLadderPerform]` is `{kLaneNone, 0}`; the comment at `:131-132` says "HOME's
  FILT rows hold CUTOFF and RESO by default, PERFORM's row 3 follows".
- `app/src/ladder_rows.cpp:18-20`: `follows()` is "the row's target is `kLaneNone`"; `ladder_entry()` (`:105-110`) then returns
  the latched key's entry.
- `app/src/perform_screen.cpp:303` and `:590`: the latched key is `rig_->sound_keys.focused()`, the one latch of the SOUND page,
  which HOME, PERFORM and INSTRUMENT VOICE share (UX-303 section 9, decision 3, `docs/UX-303.md:1034`).

So the row is whatever F key was tapped last on any of those three screens. A boot latches F1, and the row reads CUTOFF (the
manual's picture). Tap F5 anywhere and the row is ACCENT, captioned `LATCHED: ACCENT` (`> ACCENT` in the skin's narrow caption).
There is a second way to get it: F5 held while a pad of the row was tapped assigns ACCENT to the row for good, saved in the
project (`ladder_assign`, `ladder_rows.cpp:112-123`); then the caption reads plain `ACCENT`. The same gesture again gives the
row back to following.

### Who decided it, and why

- **2026-10-30, `ca3062b` (UX P1, the PERFORM screen).** The UX specification: "Row 3, ladder: twelve positions of the latched
  knob ... tap F2 and the row is RESO" (`docs/UX-303.md:292`). The reason was one row serving all eight knobs: PERFORM had a
  single ladder row and no KNOBS page.
- **2026-11-03, the maintainer on backlog item 8:** "make it seamless" (`docs/BACKLOG.md:133-140`). The answer was one row
  model where "a row either follows the latched knob or holds a knob of its own", with the default "PERFORM's row follows the
  latched knob, as each does today". Built in `e70d7c9` (2026-11-06, B4), which added the `LATCHED:` caption.
- **2026-11-11, REVIEW-303 S2** proposed removing the follow mode. It is not among the review's fixes that were built
  (TRACT-KNOBS-PLAN 12.6, R1 to R10), so the rule stands on main.

The rule made sense with one ladder row. With KNOBS beside it, it is the odd one out: four rows that are what they say, and a
fifth that changes when a key is tapped on another screen. A twelve-place ladder for ACCENT is a correct result of the rule and
a surprise to the player, which is the case S2 described.

### Proposals

**Q2-A. Build S2: the row stops following.** PERFORM's row 3 is the same row as KNOBS' row 3 and HOME's FILT row 3: CUTOFF until
told otherwise. Hold an F key and tap the row still re-points it, on the three screens at once. `follows()`, the `LATCHED:`
captions and the fifth saved row go.
- Gestures: none new. PERFORM row 3 is CUTOFF; F5 held + a tap on the row makes it ACCENT if that is wanted.
- Cost S to M. P benign (the fifth row's two bytes stay in the PERF chunk and are ignored at load; an older build reads them as
  before). H yes: the row no longer changes by itself.
- Conflicts: none with U5 or A2. With Q3-A the rule becomes "row 3 of the set in use".

**Q2-B. Keep the follow, say it louder.** The caption `FOLLOWS F5: ACCENT`, and a toast the first time the row changes under the
player. Cost S, P no, H no. It explains the surprise without removing it; I would not choose it.

**Recommendation for 2:** Q2-A. Until it is built: if the caption says `LATCHED: ACCENT`, tap F1 and the row is CUTOFF again;
if it says `ACCENT`, hold F5 and tap a pad of the row.

---

## 3. "Can the pads take GROWL, WOBBLE and MODDED, with four templates on KNOBS' F keys?"

### What the code does today

**Which knobs a row can hold.** A row stores a lane target and a parameter id (`LaneRef`, `Wiring::ladder`, `pages.h:161`), so
it can hold every knob a motion lane can drive, of any unit, not only the voice's (`Controls::lane_ref`, `pages.cpp:631-655`):

| Family | On a row today | Notes |
|---|---|---|
| SOUND (8) | yes | VOLUME is the mixer's level |
| FX | DRIVE, TONE, MIX, DLY TIME, DLY FB, DLY SEND, DLY TONE | MODEL (`Item::DistMode`) has no lane: no |
| MODDED NOTES and TONE | all the mods | they are voice parameters; a tap on a mod with MODS off switches MODS on (`ladder_rows.cpp:254-256`). The MODS switch itself: no |
| GROWL | its seven knobs | the GROWL on/off key: no |
| TRACT | the storage and the project accept it (`kLaneTract`, `project.cpp:427`) | but nothing on KNOBS can reach it: see below |
| WOBBLE | **no** | RATE, SHAPE, DEPTH, TARGET, RESET are `Item::Wobble`, which `lane_ref` refuses: the toast is THAT KNOB HAS NO LADDER. The only pads the wobble has are PERFORM's row 3 as the rate picker while WOB is on (`perform_screen.cpp:240-241`) |

A list or a switch gets one pad a value (`ladder_is_list`, `ladder_rows.cpp:32-41`), so WAVE, ACC SWEEP or SUB OSC already work
as pads.

**How a row gets its knob on KNOBS.** Two ways, both slow for anything outside SOUND:
- Hold an F key and tap the row. On KNOBS the F row is the SOUND page only (`knobs_press` reads `rig_->sound_keys`,
  `perform_screen.cpp:312-328`), so this gives the eight SOUND knobs and nothing else. (From the MODDED, GROWL and FX pages the
  same gesture with FILT on reaches rows 3 and 4, because they are HOME's rows.)
- Shift + F1 to F4, then turn: the encoder walks one flat list of every bindable knob (`ladder_walk`, `ladder_rows.cpp:192-212`).
  The list is SOUND, FX, NOTES, TONE, GROWL (`walk_page`, `:138-148`), about 35 knobs, one detent each, the name in a toast.

**Gap: TRACT is not in that list.** `kWalkPageCount` is 5 on main and still 5 on `lane-a2` (`ladder_rows.cpp:138` there), though
plan 5.2 names TRACT among the knobs KNOBS reaches. On main no gesture at all can put a TRACT knob on a row.

**Where the eight F keys of KNOBS are used.** All eight, twice:

| | F1 | F2 | F3 | F4 | F5 | F6 | F7 | F8 |
|---|---|---|---|---|---|---|---|---|
| Plain | CUTOFF | RESO | ENV MOD | DECAY | ACCENT | WAVE | TUNE | VOLUME |
| Shift | ROW 1 list | ROW 2 list | ROW 3 list | ROW 4 list | - | - | - | DEFAULTS |

(`soft_keys`, `perform_screen.cpp:485-495`; `knobs_shift_key`, `:350-360`.) Note that Shift + F8 is DEFAULTS here, while
sentence 3 of plan 10.4 (U4) lists KNOBS among the screens where Shift + F7 and F8 are REDO and UNDO. KNOBS records motion with
REC, so it does edit the pattern, and it has no UNDO.

**What is saved.** The five rows, two bytes each, appended to the project's PERF chunk and read by length (`project.cpp:40,
407-437, 541-547`). Snapshots do not take the rows.

### The maintainer's sketch, judged

The idea is right. Four rows cannot cover SOUND, MODDED, GROWL, WOBBLE and TRACT at once, and re-pointing rows one at a time
through a list of 35 is exactly the tedium of question 1 moved to another place. Sets of four knobs, switched with one key, are
the missing piece.

Three things in the sketch I would change:

1. **"Four slots to save" should be four sets that are live and come filled.** A save step means a slot is empty until the
   player has built and stored it, and a wrong press overwrites one. A set that is simply in use, like a pattern, needs no save:
   choose set 3, re-point a row, and set 3 has it. A new project starts with four useful sets, so GROWL on the pads is one key
   away on the first day. "Save" and "record" also collide with words already taken on this screen: REC records taps, and
   Shift + a pad stores a snapshot.
2. **"Four keys choose which knob each row changes" is what Shift + F1 to F4 already does**, and it is the slow part. Those four
   keys earn their place better as the four rows' own knob keys: each shows its row's knob and value, a tap gives it the
   encoder (the fine control of question 1), and the binding hangs off the same key.
3. **It takes the whole plain F row**, so the eight SOUND knobs leave KNOBS. I think that is right (the screen then describes
   its own pads, and PERFORM, one press away, keeps the SOUND row), but it is a real loss and it is the maintainer's call. Q3-A2
   below keeps the SOUND row for those who want it.

### Q3-A. Four knob sets, the F row of KNOBS as the sketch draws it (recommended)

**The F row.**

| | F1 | F2 | F3 | F4 | F5 | F6 | F7 | F8 |
|---|---|---|---|---|---|---|---|---|
| Plain | SET 1 | SET 2 | SET 3 | SET 4 | row 1's knob | row 2's knob | row 3's knob | row 4's knob |
| Shift | FACTORY 1 | FACTORY 2 | FACTORY 3 | FACTORY 4 | - | - | REDO | UNDO |

- **F1 to F4** are action keys (U6 draws them framed, the set in use lit): a tap switches the four rows to that set at once. No
  value moves; only what the pads mean changes.
- **F5 to F8** are knob keys: the name and value of the knob its row holds (for example `VOWEL  A-E`). A tap latches the
  encoder on it; hold + turn, two held, Shift + turn and Left and Right at 1/20 work as on any knob key. A tap on a pad of a row
  also moves the latch to that row's key (Q1-A), so tap-then-trim needs no F key.
- **Re-pointing a row:** hold its key (F5 to F8) and press **Up or Down**: the row takes the previous or next knob of the list,
  named in a toast with its family (`ROW 2: GROWL / FMT Q`). **Shift + Up or Down** jumps to the first knob of the previous or
  next family. The change goes into the set in use; there is nothing to save. The built gesture from the sound pages stays:
  with FILT on, hold an F key of MODDED, GROWL, TRACT or FX and tap row 3 or 4.
- **Shift + F1 to F4** puts that set back to its factory four, with a toast. **Shift + F7 and F8** are REDO and UNDO, which
  closes the gap with sentence 3.

**The pads.** Unchanged: four ladder rows, a bar up to the place and the place at full level. The set in use is not shown on the
pads (one colour, and all 48 are taken); the lit F1 to F4 key and the four row captions on screen say it. The colour buttons stay
PERFORM's four actions.

**The four sets of a new project** (the contents need the ear on the unit; the rows are a starting point):

| Set | Row 1 | Row 2 | Row 3 | Row 4 |
|---|---|---|---|---|
| 1 FILTER | ENV MOD | DECAY | CUTOFF | RESO |
| 2 MODDED | ACC DECAY | SLIDE | OVERDRIVE | FILTER FM |
| 3 GROWL | VOWEL | FMT Q | CMB TUNE | GRL MIX |
| 4 FX (WOBBLE with Q3-B) | DRIVE | DLY FB | DLY SEND | DLY TIME; with Q3-B: WOB RATE, WOB DEPTH, DRIVE, DLY SEND |

After A2 lands, set 3 can open as VOWEL (TRACT), TONGUE, GROWL's VOWEL and GRL MIX.

**One rule for the other screens.** HOME's FILT rows are rows 3 and 4 of the set in use, and (with Q2-A) PERFORM's row 3 is its
row 3. The plan's sentence "the same physical rows hold the same knobs on both screens" stays true.

**Data and format.**
- `Wiring` gains four sets of four `LaneRef` and the set in use: 33 bytes. `Wiring` sits in `Rig` (`rig.h:42`); the bytes go
  where the rig lives, and `ProjectState` (already in the SDRAM since `61a8258`) gets the same 33. SRAM_U's 6,072 B above its
  floor is enough either way, but the link map decides, not this estimate.
- PERF chunk, appended after the five rows: the set in use (1 byte) and sets 2 to 4 (24 bytes). Set 1 is the four rows the
  chunk already has, so v0.6.0 and today's main read a newer project as "set 1 only", and an older project loads with factory
  sets 2 to 4. Minor version up, no migration, no parameter id involved. The proof is the test K1 already has for PERF (a
  shorter and a longer chunk).
- Snapshots are not involved.

**Budget.** The F row draws four action boxes and four knob boxes where it drew eight knob boxes, so the display list should
not grow, and the pads' lamps are the same; but KNOBS has no measured figure in the docs I read, so `skin stats` on KNOBS in
both looks is part of the slice (1,800 words, 6,000 µs).

- **Cost** M (`perform_screen.cpp/.h`, `perform_view.h`, the skin's perform files, `ladder_rows.*`, `pages.h`, `project.cpp`,
  `help_texts.*`, the manual's KNOBS section, `knobs` tests, `skin_knobs.txt`).
- **P** yes, appended only. **H** yes: on KNOBS the plain F row is no longer the SOUND knobs, "hold a SOUND key + tap a row" is
  gone there, Shift + F1 to F4 changes meaning and DEFAULTS becomes four FACTORY keys. KNOBS was built after v0.6.0 and has not
  been released or read on the unit (plan 12.4 still owes it), so the habit is days old.
- **Conflicts.** S2: compatible, and better built first (one row model before it is multiplied by four). U5: the tabs stay
  `PERFORM | KNOBS`; the set is not a tab. A2: `pages.h` and `project.cpp` are touched by both (A2's snapshot count and
  `kTractPage`), so this slice starts after `lane-a2` merges. Snapshots at 48: untouched. Plan 5.2 kept the colour buttons as
  actions on KNOBS: respected.

### Q3-A2. The same sets, on the Shift row (the fallback)

The plain F row stays the eight SOUND knobs. Shift row: F1 to F4 SET 1 to 4, F5 to F8 ROW 1 to 4 list (today's walk). Shift +
the key of the set in use puts it back to factory.
- Cost S to M. P the same as Q3-A. H small (Shift + F1 to F4 and F8 change meaning).
- Why it is second: switching sets, the gesture made most often, needs Shift every time; a GROWL or MODDED knob on a row still
  has no key for the encoder unless Q1-A is built separately; and REDO and UNDO still have no place.

### Q3-B. WOBBLE's settings and the on/off switches as row targets

A row can only store a lane target today, and the sequencer's slew refuses the wobble's values (`sequencer.cpp:401`). Add row
targets that are not lanes: RATE (11 values, one pad each), SHAPE, DEPTH (12 steps), TARGET, and the switches MODS, GROWL, TRACT
and WOB (two pads: OFF, ON). `ladder_tap` already has the path for a knob with no lane: it sets the value directly and records
nothing (`ladder_rows.cpp:259-263`).
- Stored in the PERF rows as target values above the lane targets. An older build finds an unknown target and loads that row's
  default (`project.cpp:413-436`): benign.
- REC does not record these taps (no lane). The toast must say so once, or the caption carries a mark.
- With WOB RATE on a KNOBS row, PERFORM's "row 3 is the rate while WOB is on" (`perform_screen.cpp:240`) can stay as it is.
- Cost M. P appended and benign. H no.

### Q3-0. TRACT's seven knobs in the list

`walk_page()` gains `kTractPage`. One line and a test, on `lane-a2` where the page exists. Cost S. It is owed whatever is
decided above: plan 5.2 promised it.

### What I would not do

- **A row of pads to choose the set.** It costs a quarter of the knobs to save one F key.
- **The colour buttons as the four sets.** It is the quickest gesture (one finger, no Shift), and issue #2 asked for something
  like it, but plan 5.2 refused a fourth meaning for those buttons and the four actions are worth having under the left hand
  while the right plays the rows. If set switching on F1 to F4 proves too slow on the unit, this is the next thing to try.
- **Sets inside snapshots.** A snapshot is the sound and is full at 48; a set is the surface. Kept apart, recalling a snapshot
  never changes what the pads mean.

---

## Recommended order of slices

| Order | Slice | What | Cost | P | H | Waits for |
|---|---|---|---|---|---|---|
| 1 | **KN0** (= Q3-0) | TRACT's knobs in KNOBS' list | S | no | no | built on `lane-a2`, inside A2 |
| 2 | **KN1** (= Q2-A, S2) | PERFORM's row 3 stops following; the ACCENT surprise goes | S to M | benign | yes | nothing; it is in `perform_screen.cpp` and `ladder_rows.cpp`, which A2 does not hold |
| 3 | **KN2** (= Q1-B) | PERFORM: Left and Right move the latch, Up and Down the value | S | no | no | nothing |
| 4 | **KN3** (= Q3-A with Q1-A) | Four knob sets, KNOBS' F row is SET 1 to 4 and the four rows' knobs, the encoder follows the row tapped | M | appended | yes | A2 merged (shared `pages.h`, `project.cpp`); the maintainer's yes to losing the SOUND row on KNOBS, else Q3-A2 |
| 5 | **KN4** (= Q3-B) | WOBBLE's settings and the on/off switches on rows; set 4 becomes WOBBLE | M | appended, benign | no | KN3 |
| 6 | U5 | Tabs: unchanged by all of the above (`PERFORM | KNOBS`) | | | | A2, as planned |

KN1 and KN2 can go in the current batch. KN3 and KN4 are one release together, so the manual's KNOBS section changes once.

**For the unit and the maintainer only.** KNOBS by hand is still owed from K2 and K3 (plan 12.4): do it once, on KN3, not twice.
Whether a set switch is fast enough on F1 to F4; whether twelve places and an encoder trim feel like a knob; the contents of the
four factory sets by ear; whether the lit SET key is read at arm's length (U6's drawings are owed the same look); the display
list and frame time of the new KNOBS page from `skin stats`. And one question of fact: whether the board's encoder has a push
switch at all, which no file in either repository answers.

## Decision (the maintainer, 2026-11-11)

"lo que diga fable a todo": every recommendation of this review is accepted as written. That is Q1-A and Q1-B, Q2 by S2 of
REVIEW-303, Q3-A (the four sets on the plain F row; the SOUND row leaves KNOBS and stays on PERFORM) and Q3-B, in the order
above: KN0, KN1, KN2, KN3, KN4. The encoder push is not pursued.
