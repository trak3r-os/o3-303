# Changelog

What changes for a player, newest first, in English. Each release's notes on GitHub link here. A change to a saved file or to what you
hear says so in plain words.

## Unreleased

## v0.5.0 (2026-11-06), a pre-release

The changes since v0.1.0 that a player hears, sees or saves.

### What plays or sounds different

- **New defaults:** the growl's CMB TUNE starts at +7 st, not 0, and the wobble's RESET starts off, not on. A value a saved project or
  snapshot already holds is not changed.
- **MIDI accent:** a note from a MIDI keyboard at velocity 96 or more is accented, as the sequencer's accent does. The pads and keys now
  send velocity 90 when unaccented, not 100.
- **Growl on and off:** GROWL's F8 turns the growl on and off. Off keeps the Mix you set; on brings it back, at 50 % the first time.
- **MODS:** the STOCK switch is now MODS, lit while the mods are heard. Each mod with a condition has a line that says when it is heard,
  such as SOFT ATK heard without accent or slide.

### Screens and controls

- **OBSIDIAN is the default look.** A unit that has already saved its settings keeps the look it had; Config, page 303, SKIN switches it.
- In OBSIDIAN, with Shift held on HOME and PATTERN SETUP, the ENC window also gives the knob's value, so a coarse turn can be read.
- **Held Left and Right** repeat on HOME, INSTRUMENT, MASTER and PERFORM while an F key is held. Left and Right with an F key held move
  its knob 1/20 of its range, one step on an enum. Config, page DISPLAY, sets the encoder's curve.
- **Ladder rows** on HOME (FILT rows 3 and 4) and PERFORM (row 3) can hold any F key's knob: hold the F key and tap the row. Tap it again
  for its default. Shift + FILT restores both HOME rows.
- **INSTRUMENT's knobs** sit beside the F keys on VOICE, FX, MODDED, GROWL and WOBBLE.
- **Steps:** on HOME and INSTRUMENT, Insert makes a note a tie or a rest, and a tie or rest a note; Shift + Insert pushes right. Up or Down
  on a rest or a tie makes it a note.
- **ROOT:** Shift + F5 on PATTERN SETUP sets the scale's root, from C to B. With a SCALE on, the scale's keys light mid and the root full.
- **RECORD** takes the master only. SOURCE, GAIN and MONITOR are gone.
- **FILE LIBRARY:** Shift + a pad loads slots 16 to 63.
- **LAST:** a load that replaces the sound keeps it. Shift + F3 on FILE LIBRARY swaps it back.
- **NO AUTOSAVE** shows as a header chip while autosave is off.
- A row latched with a colour button ends when you leave the screen.
- INFO shows the version, 0.5.0.

### MIDI

- A learned controller records motion when REC is on and the line plays.
- Accent from a keyboard is as above.

### Saved files

- **Projects:** a project saved with v0.1.0 loads on this version. The ladder rows start on their defaults and the wobble's latch is off.
- **Snapshots:** a slot now holds more of the sound. An older slot takes the new values from the sound when it loads.
- **Settings:** a settings file that saved SKIN keeps its look. A file with no SKIN entry gives OBSIDIAN.
- **Not checked:** a project saved by this image and opened on v0.1.0.

### What a unit has shown

On a unit, over the USB console, on this code or, for the older changes, on the test images that led to it:

- **Sound and load:** no audio dropout in two-minute runs on HOME, INSTRUMENT VOICE and WOBBLE, playing. The render peaked at 58.5 % of a block on HOME, and
  once at 64.8 % across a whole test round, with two blocks over budget and still no dropout.
- **Worked:** a project from an earlier image loads; the look stays across a restart; the ENC window's value with Shift; INSTRUMENT's layout on VOICE, FX, NOTES,
  GROWL and WOBBLE; GROWL's and WOBBLE's F8; MODS; TARGET VOWEL refused at Mix 0; the ladder rows on HOME; the ROOT and the key LEDs; FILE LIBRARY's slots; the held
  repeat; the encoder's curve; a learned controller moving CUTOFF; MIDI accent at velocity 96.
- **OBSIDIAN's draw time:** at most 5,685 µs a frame, under the budget of 6,000.
- **Known problem:** once, after a `reset` from the USB console, the unit stayed off USB until it was switched off and on. It left no crash record and the card was
  untouched, and the cause is not known. Ten more resets came back in about 4.5 s. If it happens, switch the unit off and on.
- **Not shown on a unit yet:** snapshots and the morph with the wobble; one undo over a held lock; the first repeat's 400 ms; a learned controller recording motion;
  the ladder rows on MODDED, GROWL, WOBBLE and PERFORM; the new defaults on a new sound; INSTRUMENT's layout on TONE (it is the same drawing as NOTES, and was
  seen in the simulator); anything by eye or by ear, including how smooth OBSIDIAN looks on VOICE while playing.

## v0.1.0 (2026-11-02), the first release, a pre-release

o3-303 as a whole: an improved TB-303 as a firmware image for the Tracker hardware, built on the o3-sdk framework.
- **Pre-release:** the newest pages, MODDED, GROWL and WOBBLE on the Sample buttons, are built and tested on the host but have not run on
  a unit yet (`docs/MODS-PLAN.md`, section 11).
- **Not affiliated:** unofficial, and not affiliated with or endorsed by Polyend.

### What is in it

- **The voice:** the 303 voice and the distortion, ported from Replicant 303, and a delay (MONO or PING on MASTER F5).
- **MODDED** (Sample Load): eleven Devil Fish-style circuit mods on two pages.
  - NOTES: SOFT ATK, ACC DECAY, AMP DECAY, SLIDE, ACC SWEEP and SWP SPEED.
  - TONE: TRACKING, FILTER FM, OVERDRIVE, MUFFLER and SUB OSC.

  At their stock values the voice is the plain one, to the bit. Shift + Sample Load turns STOCK on and off from any screen: an A/B
  against the plain 303 that keeps your settings.
- **GROWL** (Sample Play): the formant and comb growl on one page.
- **WOBBLE** (Sample Edit): an LFO on the cutoff or the growl's vowel, run from F8 WOB or PERFORM.
- **The sequencer:**
  - 8 to 64 steps, with accent, slide, tie, swing and gate;
  - motion recording and parameter locks;
  - chains and undo;
  - the GEN preset generator;
  - 48 patterns on the pads.
- **Performance:**
  - PERFORM with pad filter ladders, snapshots and morph, momentary FILL, ratchets and trig conditions;
  - beat freeze, rolls, tape stop and reverse;
  - tempo nudge, a scale-locked keyboard and assignable performance buttons.
- **SONG:** rows of patterns with repeat, transpose and mute, saved with the project.
- **Projects:** saved on the card with autosave; FILE PROJECTS and LIBRARY.
- **RECORD:** writes audio takes to the card.
- **USB:**
  - audio out;
  - MIDI: notes, clock in and out, and MIDI learn;
  - the USB console, for updates without hands on the unit.
- **HELP** on every screen. Two looks: CLASSIC (the default) and OBSIDIAN.

### What a unit has shown, and what it has not

The sessions are in `docs/hw/`. They ran over USB, by the console and screenshots. Nothing was judged by ear or by hand unless a session
page says so.

- **Shown on a unit, with earlier images:**
  - the beat freeze;
  - the growl, the wobble and the rolls;
  - the snapshot morph;
  - autosave and the project back after a reboot;
  - recording to the card;
  - every main screen skinned;
  - USB MIDI from a computer: notes, the clock followed from 60 to 300 BPM, clock out, learn, and a 20-minute soak.
- **Render CPU:** at most 49.9 % of a block in those sessions.
- **Not yet shown on a unit:**
  - MODDED, GROWL and WOBBLE as pages, and every mod;
  - the pads, the knobs and the LEDs by hand;
  - tap tempo, the name keyboard, LOAD and REVERT;
  - DIN MIDI, and learn with a hardware controller;
  - SONG by ear;
  - HELP by eye;
  - the OBSIDIAN look by eye. Its draw time is over its budget on the unit, which is why CLASSIC is the default.
