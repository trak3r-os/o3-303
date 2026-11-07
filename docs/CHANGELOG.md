# Changelog

What changes for a player, newest first, in English. Each release's notes on GitHub link here. A change to a saved file or to what you
hear says so in plain words.

## v0.6.0 (2026-11-07), a pre-release

The voice's note start, its cutoff range, its square, its filter and its slide, brought closer to a TB-303 by measuring one reference recording of one unit (Din Sync's "TB-303 reference recordings for x0xb0x builders", 2010; approximate knob positions, licence unstated, so it was measured and nothing was copied). It needs more testing: the host tests pass and its CPU was measured on a unit (render at most 49.5 %, 0 underruns), but its sound still has to be judged by ear there.

### What plays or sounds different

- **Saved projects sound different.** The voice under the knobs has changed, so a project saved by v0.5.1 or earlier loads without an error and sounds different, on the same knob values. With MODS on, a project that has AMP DECAY at 1230 ms and ACC DECAY at 200 ms (the old defaults) keeps those two values, which are no longer the new defaults, so it plays a shorter amplifier decay (1.23 s against 1.43 s) and a longer accent decay (200 ms against 87 ms) than a new project does. With MODS off, or after setting the two to 1430 and 87, it plays the new stock sound.
- **The new defaults.** ACC DECAY starts at 87 ms (was 200) and AMP DECAY at 1430 ms (was 1230); both ranges are the same. DECAY keeps its range and its default and is still a time constant in milliseconds.
- **A note starts 4.4 ms late.** The sound of a note begins a few milliseconds after its gate while the filter's sweep has already started. It applies to every note that starts the voice from quiet, accented or not; a slide or a tied note does not have it. On a tail (the next step starts as the last one ends) the voice waits only as long as it takes to avoid a dip: 7 samples when the note ends and starts in the same frame, and none on an accent. An accent's loud part starts at once, without the delay. With SOFT ATK on a value, that value is the fade-in and there is no delay; RANGE FISH starts a note at once as before. All-notes-off during the wait stops the note with no blip.
- **The accent, the square and the cutoff law follow the measurement.** An accent adds `max(0, 2.6 x ACCENT - 0.6)` to the amplifier and its filter sweep decays in 87 ms; the SQR wave is at 0.54 of the saw's level; the sweep's offset against the knob has a steeper slope; the filter can open to 10.4 kHz (the knob's range is the same). The line is a little quieter at the default Accent, and an accent's brightness at the default is lower than before.
- **CUTOFF has a floor and a ceiling on RANGE 303.** The filter now plays 200 Hz to 2,394 Hz when RANGE is 303 (and with MODS off); RANGE FISH keeps the whole 25 Hz to 4,500 Hz. Roughly the bottom third of the knob (below about 200 Hz) and the top fifth (above about 2,400 Hz) now sound the same as the nearest end of that range. The knob still shows its own value in Hz. A saved project with a CUTOFF outside the range keeps its value, and plays the nearest end on RANGE 303; switch RANGE to FISH to hear it as before. The generated sounds of the vault are affected the same way: 54 of the 504 generated sounds (not the eight factory lines) sit below 200 Hz and 5 above 2,394 Hz, so they now play at the end of the range. When RANGE changes, the two ends of that range glide to their new places instead of jumping.
- **The slide follows the 303's RC.** The glide is now a one pole on the pitch in octaves with a time constant of 22 ms at SLIDE 60 (22/60 of the SLIDE value), so it is the same speed up or down and for any interval, and takes about 60 ms to arrive; before it ran in Hz, so a slide up was quicker than a slide down and a large leap took longer. The note still jumps at once with no slide, a slid note does not retrigger the envelopes, and the SLIDE range and default are the same. A slide up or down by an octave now has settled by 60 ms instead of 100 ms or more. It needs the maintainer's ear on a unit; the pitch is no longer updated per sample.
- **The SQR wave has the shape of Open303's square.** A little rounder at the falling edge, with a duty near 47 %, a touch more aliasing at the highest notes (2 to 3 dB at C7 and C8 in a host measurement) and slightly less at low notes.
- **Measured on a unit.** The render reads at most 49.5 % of a block with 0 underruns, on one project and ten rows (`docs/hw/2026-11-07-cf682d4.md`; 47.8 % before the filter and slide changes, `docs/hw/2026-11-07-de6704f.md`). This voice needs more testing: its sound still has to be judged by ear there.
- **The voice is quieter.** A note is about 2.4 dB lower than in v0.5.1, an accent about 3.5 dB and the SQR wave about 6.6 dB, at the same VOLUME. Raise VOLUME to compensate.
- **The distortion models are back at GRIT's level.** Their level table was fitted again on this voice: TUBE, HARD and FUZZ match GRIT at each Drive, and FOLD sits 1.9 dB under it so its peaks stay bounded.
- **The resonance is rolled off at a low cutoff, and the bass of the filter is fuller.** The feedback's high-pass is at 15 Hz (was 150 Hz) and the resonance falls toward a low cutoff (500 Hz corner), as a real TB-303's does. A held note keeps its low end; an accent's climb at a low cutoff with high RESO is smaller (about 1.8 x against 3 x before); FILTER FM is no longer brighter at high RESO. HI RES rings from a cutoff of about 350 Hz at full RESO and 375 Hz at 65 % (before: 350 Hz and 1 kHz; at full RESO it is faintly there from 325 Hz), stays bounded and keeps DC out. It needs the maintainer's ear on a unit.

## v0.5.1 (2026-11-06), a pre-release

A new voice. It needs more testing: on a unit its CPU was measured (render at most 49.8 % with everything on, 0 underruns) and a project saved by v0.5.0 loaded, but its sound still has to be
judged by ear there (`docs/VOICE-303.md`, section 6).

### What plays or sounds different

- **A new voice, built to sound like a Roland TB-303.** The filter, the way the envelope moves it, the amplifier and the bass are now
  those measured on a real TB-303 (Open303 by Robin Schmidt), and the accent's sweep follows the instrument's circuit (Robin Whittle's
  published description). The biggest differences you will hear:
  - The filter's sweep is narrower and goes below the knob as well as above it, so the sound sits lower and warmer.
  - Accents are much louder than before, about 9 dB at ACCENT 70 %, and at high RESO close accents climb.
  - A slide is about five times quicker: 60 ms is now the time to reach the note.
  - The filter no longer whistles by itself at normal settings. HI RES brings that back, as on a Devil Fish.
  - A note ends about 16 ms after the gate, and an accent rings a little longer.
- **It starts dry.** A new project and the eight factory lines have no distortion and no delay, as a TB-303 has neither. The other sounds of
  the vault keep their distortion. The default level is -3 dB, not -6 dB.
- **A saved project keeps its knobs, and the voice under them is new, so it sounds different.** Its level, distortion and delay stay as
  saved. A project saved at the old default level is 3 dB quieter than a new one, and it keeps ACC DECAY 180 ms and AMP DECAY 4000 ms: set them
  to 200 and 1230 ms, or press MODS off, for the new stock sound.
- **Devil Fish ranges:** DECAY now goes from 30 to 3000 ms, ACC DECAY starts at 200 ms and AMP DECAY at 1230 ms. OVERDRIVE at its bottom is
  no oscillator at all. New on MODDED TONE: **RANGE** (F6; FISH widens ENV MOD to zero and three times the top, fills out the bass and
  makes a note start and end at once) and **SUSTAIN** (F7). A motion lane recorded on DECAY plays back along the new range.

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
