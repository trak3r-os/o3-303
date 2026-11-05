# o3-303 backlog: the maintainer's requests, not yet planned

What the maintainer asked for while using o3-303 on the unit. Nothing in this file is decided, specified or built. Each item gives:
- the request, in the maintainer's own words;
- what the code does today, checked when the item was written;
- what is left to decide.

A request moves out of this file when a plan takes it (UX-PLAN, a slice brief); the plan then says where it went.

## Status on 2026-11-03

None of the live items is built (14 added 2026-11-05 from the maintainer's words, 15 to 19 the same day from a Fable review of the
backlog and the code; 15 to 19 were not asked for by the maintainer; 18 was dropped the same day). The code they describe has not changed since they were written: o3-303 is at 19a467d and o3-sdk
at bbe7b83. The proposed order for the next round, cheapest first:

| Order | Item | Size | Waits on |
|---|---|---|---|
| 1 | 11 VERSION on INFO (built, B1) | trivial | nothing; before the next release |
| 2 | 4 RECORD takes the master only (built, B1) | small | nothing |
| 3 | 13 (with 3) Delete, Insert, Up and Down on a step | small | nothing (proposal 2026-11-03) |
| 4 | 1 ROOT control | small | the place: Shift + F5 on PATTERN SETUP, proposed |
| 4b | 12 FILE LIBRARY: Shift + pad for slots 16 to 63 (built, B1) | small | nothing |
| 4c | 15 a MIDI velocity threshold gives the accent | small | o3-sdk's router, or the app routes notes itself |
| 5 | 7 Left and Right move the held F key's value, 1/20 of its range | small | nothing; the held repeat waits on 9 in o3-sdk |
| 5b | 16 a library load keeps the sound in a LAST slot, 17 a NO AUTOSAVE chip (17 built, B1) | small each | nothing; before item 8 |
| 6 | 8 one ladder model: HOME's FILT rows and PERFORM's row 3 take any F key's knob | medium | nothing (decided 2026-11-03) |
| 7 | 2 INSTRUMENT's knobs beside the F keys | medium (five screens share the drawing) | nothing |
| 7 (with 2) | 14 GROWL, WOBBLE and mods: on/off keys, audible defaults, "heard on" lines | medium | item 2's redraw; the new defaults are decided (2026-11-05), so they can go first, alone |
| 7a | 19 the row latch ends with the screen | small | with item 2 |
| 7b | 6 the key LEDs: scale at mid, root highlighted | small | item 1, the root |
| 8 | 5 OBSIDIAN the default | small once A9 is settled | A9 on the unit |
| 9 | 9 encoder speed and button auto-repeat, 10 MIDI automation | o3-sdk first | an o3-sdk change under its own rules |

## 2026-11-02: the first session with v0.1.0 on the unit

The image: o3-303 v0.1.0 (2fbb6de) on o3-sdk bbe7b83, installed over the USB console, with the OBSIDIAN look switched on.

### Questions answered on the day

- **The root key.** "how do I set the actual root key?"
  - No control sets the root. The project stores one: `Wiring::gen.root`, in the GEN chunk (`app/src/project.cpp:293`).
  - The keyboard's scale lock (`app/src/home_map.cpp:312`, `in_scale` in `perform.h`) and the generator use that root.
  - But nothing on the unit changes it, so it is always C. See item 1.
- **Writing notes.** "when introducing keys, i can only use them if I insert a rest 'in the pattern view' [and can't] input an actual note".
  - The manual (section 4, "Write notes") gives two ways to write a note:
    - with REC on, each key writes its note on the cursor step and moves to the next step;
    - with REC off, hold a step pad and press a key: that step gets the key's note.
  - With REC off and no step pad held, the keys only play the voice.
  - If the unit did something else, reproduce it next time: the screen, REC on or off, the cursor's column.
- **Recording automation.** "can we record automations? via midi too?"
  - From the unit, yes. With REC on and the line playing, a knob you turn is recorded as motion in the pattern's lanes: HOME's F row and PERFORM's ladders (`record_motion`). A pattern holds lanes for up to eight knobs.
  - From MIDI, no. A learned CC goes through o3-sdk's MIDI learn, which writes the parameter itself: the app never sees the move, so it cannot record it. MIDI notes play the voice and do not write steps either.
  - See item 10.

### Items

1. **A ROOT control.**
   - Today: the root is always C (above).
   - To decide: where it goes. PATTERN SETUP's F row is full (LENGTH, SWING, GATE, TRANSP, SCALE, DENSITY, ACC %, SLD %). The candidates:
     - Shift + F5;
     - SCALE's value carrying the root;
     - one of MASTER's free keys (F2, F3, F6 to F8).
   - The keyboard's lights (item 6) and the generator use the root.
2. **INSTRUMENT's knobs beside the F keys.** "On instrument page, can you put the Cutoff etc controls down, next to the f-keys."
   - Today: on INSTRUMENT VOICE the eight knobs are at the top, under the header. The DISTORTION, DELAY and MODDED boxes come next, then the 16 steps at the bottom (manual picture `instrument-voice.png`).
   - Asked: the knob row at the bottom, next to the F keys, as HOME's F row is. The same question applies to FX, MODDED, GROWL and WOBBLE, which use the same drawing.
3. **Up and Down on an empty step.** "in instrument view let me change empty notes with the key up down, as if it were a note."
   - Today: INSTRUMENT's step row shows "--" for a step with no note.
   - Asked: Up and Down on such a step give it a note and step its pitch, as on a note.
   - To decide: the first note it gets. The last note before it, or the keys' C?
4. **RECORD takes the master only.** "on sample recorder I only want to record master, logically."
   - Today: F2 SOURCE picks MASTER or an input the unit has: LINE, MIC, RADIO or USB (MANUAL, RECORD).
   - Asked: MASTER always. SOURCE goes, which frees F2.
5. **OBSIDIAN the default look.** "Make obsidian default theme."
   - Why CLASSIC is the default today: OBSIDIAN's draw time went over its budget on the unit (SU1p, A9 `peak_us`).
   - On the unit on 2026-11-02, HOME in OBSIDIAN while playing, after `skin on`: draw 3,214 µs, peak 3,819 µs, render at most 48.3 % of a block, 0 underruns.
   - To do: settle A9 (the budget and every screen's peak on the unit), then make OBSIDIAN Config's SKIN default. A unit with a saved settings file keeps the look its owner chose.
6. **Keys on the two last pad rows, with the scale lit.** "In pattern mode can we have 2 last pad rows with notes, so that i can input notes? with the scale notes (and the set root) mid highlighted?"
   - Today: on HOME, rows 3 and 4 are the keys, C2 to B3, unless FILT is on. With a SCALE, the keys outside it go dark.
   - Asked, as the item reads:
     - the same two key rows in pattern mode;
     - the LEDs: the scale's notes at mid brightness, the root highlighted.
   - To check with the maintainer: which screen "pattern mode" is. Holding Pattern turns the pads into the 48 patterns.
   - 2026-11-03, the maintainer: "On pad note input mode, i think we already have this." Read as: HOME with FILT off already has the two
     key rows. What is left is the LEDs: the scale's notes at mid, the root highlighted (today the keys outside the scale go dark). That
     part waits on item 1, the root.
7. **Left and Right.** "any more instance where lft/right keys could be useful? to change selected f-key function like faster?"
   - Decided 2026-11-03: "left/right move the value (bigger steps like 20 for full cycle) while the f-key is pressed."
   - The behaviour, on every screen whose F keys own knobs:
     - with an F key held, Left and Right move that F key's knob down or up by 1/20 of its range: 20 presses take it from minimum to maximum;
     - a knob with few values (an enum, a switch, fewer than 20 steps) moves one value per press;
     - with no F key held, Left and Right keep what they do today (the cursor on HOME, the focus or the slot elsewhere);
     - a press is a turn: with REC on and the line playing it is recorded as motion, the same as the encoder.
   - Also asked on 2026-11-03: "also allow left/right on value change to acelerate". Proposed:
     - a press moves one step (1/20 of the range);
     - held for 400 ms, it repeats, at 8 steps a second at first, rising to 25 a second over the next second;
     - it stops when Left, Right or the F key is let go.
   - Today: the kit tells a screen whether a button is held (`Panel::held`, o3-sdk `kit/app/include/o3sdk/app/panel.h:50`). It has no button
     auto-repeat.
   - Decided 2026-11-03: "put it in o3sdk". The repeat and its acceleration go into o3-sdk's input, with the encoder acceleration of item 9.
     The 1/20 step on a single press can be built in the app now; the held repeat comes when o3-sdk has it.
8. **The ladder rows take any F key's knob.** "for the selected f-key in instrument or performance, can we use last pad row to change it?", then
   on 2026-11-03: "think a way to set the pad mode where we have cutoff/resonance to be able to change those, so that I can assign those pad
   buttons to my desired f-key encoder values".
   - Today:
     - with FILT on, rows 3 and 4 are fixed ladders: row 3 CUTOFF, row 4 RESO, 12 places from MIN to MAX (`HomeMap::ladder_press`,
       `app/src/home_map.cpp:448`). HOME and the INSTRUMENT pages share these pads.
     - PERFORM's row 3 is a ladder of the latched knob, whatever it is (`PerformScreen::ladder_press`, `app/src/perform_screen.cpp:277`). That
       path already handles any knob: a lane slew, an enum set directly, motion recorded with REC on.
   - The proposal:
     - **Assign:** with FILT on, hold an F key and tap any pad of row 3 or row 4. That row becomes the ladder of that F key's knob. The tap
       only assigns: it does not move the value. While an F key is held, both ladder rows blink, to show they take an assignment.
     - **What is stored:** the knob itself (its unit and parameter), not the F key's number. The row keeps its knob when the page changes.
     - **Reset:** Shift + FILT puts the rows back to CUTOFF and RESO.
     - **Where:** in the project, as two fields appended to its chunk. An old project loads with CUTOFF and RESO.
     - **The screen:** the strip names each row's knob, for example "ROW 3 CUTOFF . ROW 4 DECAY". The help text says so too.
     - **The code:** FILT's ladders use PERFORM's generic path for any knob, not the fixed CUTOFF and RESO ids.
   - The assignment gesture also works on MODDED, GROWL and WOBBLE (they share the home map, `instrument_screen.cpp:480`), so a mod or a
     growl knob can be put on PERFORM's row 3. Today PERFORM's ladder never targets a mod (MODS-PLAN 5.7). Fable, 2026-11-05.
   - The original question, row 4 as the latched F key's value on INSTRUMENT and PERFORM, is covered: on INSTRUMENT, FILT plus an
     assignment gives it; PERFORM's row 3 already follows the latched knob.
   - PERFORM's row 3, decided 2026-11-03: "make it seamless". The same model on both screens:
     - one kind of ladder row, used three times: HOME's FILT row 3 and row 4, and PERFORM's row 3. The same code, gesture, LEDs and caption;
     - a row either follows the latched knob or holds a knob of its own. The defaults: HOME's rows hold CUTOFF and RESO, PERFORM's row follows
       the latched knob, as each does today;
     - hold an F key and tap a pad of a ladder row: the row holds that F key's knob. Doing it again with the knob the row already holds puts the
       row back to its default. Shift + FILT puts both HOME rows back;
     - the row's caption always names what it drives: "CUTOFF", or "LATCHED: DECAY" when it follows;
     - all three rows are stored in the project.
9. **The encoder's speed.** "can we set the encoder knob velocity?"
   - Today: there is no setting. o3-sdk has no encoder acceleration: its console says track3r-os's `enc accel` was cut (o3-sdk `platform/mk66/console/src/inject_commands.cpp:90`).
   - Needs o3-sdk first: acceleration in the kit's input, and a system Config row for it. Then o3-303 inherits it.
   - The same o3-sdk change brings button auto-repeat with acceleration (item 7, decided 2026-11-03): a screen asks for it on the buttons it
     wants (Left and Right here). Every app on o3-sdk gets it.
10. **Automation from MIDI.** (from "via midi too?" above)
    - Needs o3-sdk first: a hook that tells the app a learned control moved, or hands the app the CC.
    - Then the app records the move as motion, as it does for a turned knob.
11. **The version number on INFO.**
    - Today: Config > INFO says VERSION 0.0.1 while BUILD says v0.1.0. The number is set in `CMakeLists.txt:23` (`project(... VERSION 0.0.1`) and in `app/src/o3_303_app.cpp:94` (`AppInfo`).
    - To do: bump both before the next release, or take INFO's version from the build. Found during the install, not asked by the maintainer.

12. **FILE LIBRARY: every slot on a pad.** "In FILE when loading a pattern, we only have 48 pads, not the 64" (2026-11-03).
    - Today: a library bank has 64 slots, but the pads load only slots 0 to 47 (`app/src/vault_screen.cpp:168`). Slots 48 to 63 need the
      encoder or the arrows, then Insert (MANUAL, FILE LIBRARY). UX-303 planned Shift + pad for slots 16 to 63 (`docs/UX-303.md:906`), but
      that was not built.
    - Proposed: Shift + pad loads slot pad + 16, so Shift reaches 16 to 63. While Shift is held, the LEDs show slots 16 to 63, with the slot
      shown lit. The PADS line says "PADS 0-47 . SHIFT 16-63". Shift + turn keeps picking the seed.
    - No gesture is lost. Today Shift + pad loads the same slot as the pad alone (the pad press ignores Shift). Shift's F row (NEW SEED,
      LOAD what) stays as it is.

13. **Delete, Insert, Up and Down on a step.** "when editing a pattern, delete, removes the note or the rest or tie, insert, inserts a tie or
    rest, up and down sets the note, makes sense?" (2026-11-03). It takes in item 3.
    - Today on HOME:
      - Delete clears the step to a rest: no accent, slide, ratchet, condition or locks;
      - Insert inserts a rest at the cursor and pushes the steps after it right; Shift + Insert takes the cursor to step 1;
      - Up and Down change the step's pitch (`HomeMap::arrow`). On a rest or a tie, the step stays silent: `seq::set_note` leaves the flags alone
        (`app/seq/src/ops.cpp:157`), so only a hidden pitch changes.
    - Proposed, on HOME and INSTRUMENT:
      - **Delete:** as today. An empty step is a rest ("--"); there is no third state.
      - **Insert:** on a note or a rest, the step becomes a tie; on a tie, a rest. A tie after a rest holds nothing and shows "--".
      - **Shift + Insert:** the push-right insert, the partner of Shift + Delete (remove and pull left). The cursor-to-step-1 gesture goes;
        step pad 1 does it.
      - **Up and Down:** on a note, a semitone, Shift an octave, as today. On a rest or a tie, the first press makes it a note, without moving
        the pitch: a tie becomes the pitch it carried, a rest the last note before it, else the keys' C. The next presses move it.
      - With held steps, each held step takes the edit, as today.
    - The manual's HOME table (MANUAL "Insert a rest at the cursor") and the help texts change with it.

14. **GROWL, WOBBLE and the MODDED mods: turn a knob and hear nothing** (2026-11-05). "the wobble and growl effects are somehow quite
    complicated to use", then "changing the knobs and dont feeling the effect. same with devil fish. how can we make this understandable?"
    Diagnosed by an opus session and a Fable second opinion (read-only, from the code; the inferred points are marked, none is measured on
    the unit). No control is dead code and no bug was found: every screen opens in a state where its F keys are inert or conditionally
    audible, and nothing on the screen says so.
    - Today:
      - **GROWL:** `Growl::Mix` (F7 GRL MIX) defaults to 0 and at 0 the audio passes through (`units/growl/src/growl.cpp:38`, `:472-482`), so
        F1 to F6 do nothing until F7, the last key, is raised. At CMB TUNE 0 on a steady note CMB FB and CMB MIX are close to inert (inferred
        from `growl.cpp:136-159`). The formants are 730 to 2440 Hz against a default cutoff of 350 Hz (inferred).
      - **WOBBLE:** F1 to F5 are settings only; nothing runs until F8 WOB latches the action (`pages.cpp:423-429`). WOB is off at boot
        (`o3_303_app.cpp:141`) and forced off at every project load (`project.cpp:749`). With the defaults RESET ON and 1/8 on a 16th-note
        line the wave restarts at phase 0 on every note (`sequencer.cpp:1507-1512`): SINE gives one upward bump per note, like the filter
        envelope, and SQUARE a constant offset, no wobble. TARGET VOWEL is inert while GRL MIX is 0. The read-outs never move, because the
        wobble's events go to the unit and the screen reads `Engine::values_` (`engine.cpp:206`, `:357`).
      - **MODDED:** every default is stock, by design (`acid_voice.cpp:84-95`). Each mod is heard only under a condition: SOFT ATK on
        non-accented, non-legato notes (0.3, 1 and 3 ms are near-inert, 3 of 6 positions); ACC DECAY, ACC SWEEP, SWP SPEED on accented steps
        (the last two scale by ACCENT, so inert at 0); SLIDE on slid steps; TRACKING away from C2; AMP DECAY only below the gate length (about
        two thirds of its log travel is inaudible at 125 BPM, inferred); MUFFLER on loud signal.
      - STOCK's polarity is the opposite of WOB's: STOCK lit means you hear nothing.
    - Proposed, in this order:
      1. **One on/off action key per screen, one polarity, lit while heard.** GROWL's empty F8 becomes GROWL (toggles Mix between 0 and the
         last non-zero value, kept in the rig). WOBBLE's F8 stays. STOCK becomes MODS, lit while the mods are heard. The status strip says
         "GROWL OFF: F8", "WOB OFF: F8" or "MODS OFF: F8" while off. A host test on the toggle, a scenario per screen.
      2. **Defaults that are heard on the first turn.** RESET OFF (or the sine restarted at phase 0.75, trough first); CMB TUNE +7 st; TARGET
         VOWEL refused with the toast "GROWL IS OFF" while GRL MIX is 0. This changes the sound of a project saved with the old defaults. The
         app is pre-release, so the format can take it. Decided 2026-11-05, the maintainer: "i dont care about old saved projects go ahead".
         No migration, no version flag: an old project simply loads with its stored values and the new defaults apply to new ones.
      3. **A "heard on" line per mod,** in the strip under the latched key: "HEARD ON ACCENTED STEPS", "HEARD ON SLIDES", "HEARD ON NOTES
         WITHOUT ACCENT", "HEARD AWAY FROM C2", "HEARD UNDER THE GATE LENGTH". A constexpr table beside the help texts. The drawer's A and S
         lights highlight for the latched mod.
      4. **The wobble shown moving:** the sequencer publishes its current wave value (an atomic float, as `wob_rate_now_`) and WOBBLE draws it
         as a marker on the DEPTH key. Lower priority; it must stay inside the A9 draw budget.
    - **5. The wobble is part of the sound; save it.** Decided 2026-11-05, the maintainer: "yeah save wobble setting".
      - Today WOB is not saved with the project (forced off at load, `project.cpp:749`), so a wobble sound opens silent, and snapshots keep
        the growl but not the wobble (`kSnapTargets` has no `Item::Wobble`, `app/src/pages.cpp:59-67`).
      - To build: the project keeps the five wobble settings and the WOB latch, and loads them as saved (the force-off goes); snapshots keep
        the wobble settings and the latch, so a recall brings the wobble back. The chunk grows by appended fields (old projects load with
        the new defaults, as decided above). The WOB toggle of 1 reads and writes the same latch.
      - Open detail for the spec, not for the maintainer: whether a snapshot morph interpolates the wobble's enum settings (it should not:
        they switch at the morph's midpoint or at its end, as the other enums do).
    - Also: ACCENT at 0 silently kills six controls (ACC steps, ACC ALL, ACC DECAY, ACC SWEEP, SWP SPEED; `acid_voice.cpp:297-298`,
      `:663-666`). The "heard on" lines of 3 say so for the accent mods.
    - Not proposed, and why: growl presets or ladders on the pads (MODS-PLAN MD4 holds); auto-raising GRL MIX on a turn (a hidden state
      change); widening the DSP ranges; merging the two MODDED pages; reading the unit's live cutoff back through the engine (a kit change
      for a cosmetic). A built-in audition phrase (TRY) was considered and dropped: 1 and 3 explain the same thing without a new feature.
    - Lands with item 2's redraw, not before it: both reshape the same five screens. The defaults of 2 are the exception and go first.

15. **A MIDI keyboard cannot play an accent** (Fable's review, 2026-11-05; not asked by the maintainer).
    - Today: the kit sends every incoming note with flags 0 (`../o3-sdk/kit/app/src/runtime.cpp:637`); the voice reads the accent only
      from `kNoteAccent` (`units/acid_voice/src/acid_voice.cpp:626`), so velocity is ignored. Slides already come from overlapping notes
      (`acid_voice.cpp:549`).
    - Proposed: a velocity threshold gives the accent (the sequencer's own split is 80 and 110, `app/seq/src/sequencer.cpp:42-43`).
    - Needs o3-sdk (a threshold on the router), or the app routes notes itself instead of `ctx.midi().route(voice)` (`app/src/o3_303_app.cpp:132`).
      Small. After item 11, as a daily-use fix.

16. **A library load cannot be undone as a whole.** (Fable, 2026-11-05)
    - Today: UNDO takes the line back, not the sound or the tempo (`docs/MANUAL.md:629-631`, `:720-721`); LOAD LINE is a hidden mode under
      Shift + F2. Auditioning ten slots loses the sound you had.
    - Proposed: a load that replaces the sound first captures it, with the snapshot capture (`app/src/pages.cpp:596`), into a LAST slot,
      and the toast says so. Small to medium; no o3-sdk. Before item 8.

17. **AUTOSAVE OFF is a toast, then invisible.** (Fable, 2026-11-05)
    - Today: after a load while playing or edited, autosave is suspended (`app/src/project_service.cpp:453`, `:544-546`). Only FILE PROJECTS
      shows it (`app/src/file_screen.cpp:121`); the header draws only a failed autosave (`app/src/frame.cpp:183`; the suspended case is
      inferred). An hour of work can go.
    - Proposed: a persistent header chip, NO AUTOSAVE. Small; no o3-sdk. Before item 8.

18. **Dropped 2026-11-05, the maintainer: "18 i dont care, goes out".** It was MIDI note output from the sequencer (clock and transport go
    out today, `MANUAL.md:830-831`; no note-out path exists). Not planned. The number stays so the others keep theirs.

19. **One turn on HOME means three things.** (Fable, 2026-11-05)
    - Today: a turn edits the latched knob, or a held step's lock, or the colour-button row, and the row keeps the encoder until an F key
      is tapped (`MANUAL.md:277`). INSTRUMENT-class screens release it when they open (`app/src/instrument_screen.cpp:448`).
    - Proposed: the row latch ends with the screen everywhere; the ENC window stays the only owner display. No idle timeout (more hidden
      state). Small; no o3-sdk. With item 2, which redraws the same screens.

Not added, on Fable's advice: a mixer or master FX (DESIGN-303 says later), a sound or a scale per pattern (breaks every project), a third
step state (item 13's model holds), and anything more on the Sample buttons beyond 14 until the three screens have been heard on the unit.

Items 9 and 10 need an o3-sdk change first, made in o3-sdk under its own rules, and 15 may. The rest are o3-303's.
