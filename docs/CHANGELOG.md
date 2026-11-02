# Changelog

What changes for a player, newest first, in English. Each release's notes on GitHub link here. A change to a saved file or to what you
hear says so in plain words.

## Unreleased

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
