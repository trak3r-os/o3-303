# o3-303 backlog: the maintainer's requests, not yet planned

What the maintainer asked for while using o3-303 on the unit. Nothing in this file is decided, specified or built. Each item gives:
- the request, in the maintainer's own words;
- what the code does today, checked when the item was written;
- what is left to decide.

A request moves out of this file when a plan takes it (UX-PLAN, a slice brief); the plan then says where it went.

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
7. **Left and Right.** "any more instance where lft/right keys could be useful? to change selected f-key function like faster?"
   - Idea: Left and Right move the latched F key to the previous or next one, without tapping F keys.
   - To do: list the screens where Left and Right do nothing today.
8. **The last pad row as the selected F key's value.** "for the selected f-key in instrument or performance, can we use last pad row to change it?"
   - Asked: on INSTRUMENT and PERFORM, row 4's twelve pads set the latched F key's value, as a ladder from minimum to maximum.
   - Today: PERFORM's row 3 is already a ladder (MIN to MAX) for the filter. The pad ladders are R1 in M3B-PLAN.
   - To decide: what happens to the keys on row 4 of those screens.
9. **The encoder's speed.** "can we set the encoder knob velocity?"
   - Today: there is no setting. o3-sdk has no encoder acceleration: its console says track3r-os's `enc accel` was cut (o3-sdk `platform/mk66/console/src/inject_commands.cpp:90`).
   - Needs o3-sdk first: acceleration in the kit's input, and a system Config row for it. Then o3-303 inherits it.
10. **Automation from MIDI.** (from "via midi too?" above)
    - Needs o3-sdk first: a hook that tells the app a learned control moved, or hands the app the CC.
    - Then the app records the move as motion, as it does for a turned knob.
11. **The version number on INFO.**
    - Today: Config > INFO says VERSION 0.0.1 while BUILD says v0.1.0. The number is set in `CMakeLists.txt:23` (`project(... VERSION 0.0.1`) and in `app/src/o3_303_app.cpp:94` (`AppInfo`).
    - To do: bump both before the next release, or take INFO's version from the build. Found during the install, not asked by the maintainer.

Items 9 and 10 need an o3-sdk change first, made in o3-sdk under its own rules. The rest are o3-303's.
