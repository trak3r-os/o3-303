# Changelog

What changes for a player, newest first, in English. Each release's notes on GitHub link here. A change to a saved file or to what you
hear says so in plain words.

## Unreleased

### What you can do that you could not before

- **Project format 2.0: projects saved by an earlier version are refused (P1).** The project file now has one strict layout (HEAD, GEN, PATS, SND, SNAP, PERF and SONG, each once)
  and the format is 2.0. A project saved by any earlier o3-303 (v0.8.0 and before) does not load: the unit says PROJECT FROM AN OLDER O3-303, loads nothing of it and leaves the file on
  the card untouched (autosave stays off until you SAVE). **There is no converter**: start those projects again. A project of a newer o3-303 is refused the same way. A file with a
  chunk missing, cut or of another major is damaged (the unit then tries the saved copy). The settings of the unit (`system.o3s`) and the MIDI bindings are not affected.

- **KNOB SPEED, a factor on the encoder.** Config's page 303 has a new row, KNOB SPEED (X1, X2, X3, X4, X6, X8; X1 at first): one click of the encoder moves a continuous
  knob that many of its steps, so a whole knob takes fewer turns. It multiplies after DISPLAY's ENCODER acceleration. A list, a switch, Shift's coarse step and the arrows move as
  before. It is a setting of the unit, kept on the card.

- **WOBBLE learns MIDI (D1).** On the WOBBLE page, Shift + F1 to F5 and Shift + F8 arm RATE, SHAPE, DEPTH, TARGET, RESET and WOB for MIDI learn, like any other knob; the Shift row
  no longer says NOT LEARNABLE. MIDI Pickup applies (the controller acts once it has reached the value shown). The bindings are kept with the other MIDI bindings.

- **HOME and PATTERN SETUP Shift rows (G1).** HOME's Shift row is now CLEAR, VARY, RHYTHM, NOTES, NOTE, SCALE, REDO, UNDO: Shift + F5 NOTE and Shift + F6 SCALE give the
  encoder the generator's root and scale (the info line says ROOT or SCALE; a plain F key, a drawer row or leaving HOME gives it back), and the root tap on the bottom row of pads
  now works while Shift + F5 is held. NEW moved to PATTERN SETUP and is called RANDOM (Shift + F6), beside REVERSE (Shift + F5); CLEAR moved to HOME's Shift + F1. PATTERN SETUP's
  Shift + F5 ROOT is gone (HOME's NOTE does it). The Shift + F-key combos are the only change: no file or sound differs.

### What plays or sounds different

- **TRACT and GROWL have a GAIN stage, and GROWL's Mix crossfades at equal power (B1).** Both units gain a level control on the wet sound (-12 to +18 dB, before the Mix), set by a
  parameter that has no knob yet (the pages get it in a later slice). Its default is the level match of the factory sound at Mix 100 %: +0.5 dB on TRACT and +15 dB on GROWL (the
  growl, a narrow vowel filter and a comb, comes out some 15 dB under its input). A growl sound that was loud before is now louder; a saved project loads with those defaults. GROWL's
  Mix no longer dips about 3 dB at 50 %: it crossfades at equal power (TRACT's Mix stays linear, it dips 0.2 dB). Mix at 0 is still the input bit for bit.

## v0.8.0 (2026-11-12), a pre-release

The keys, the Params order, pattern lengths and the NEW line. It needs more testing: the host tests and the cross build pass (remote CI), but nothing after
v0.7.0 has been tried on a unit yet, so the key lamps, the LENGTH walk and the sound of NEW's four constants still have to be judged by eye and by ear there.

### What you can do that you could not before

- **Every key plays; the scale only lights them (S3).** The scale on PATTERN SETUP's F5 always lights the keys (the scale at mid, the root full, the others dark) and the info line
  always shows it with its root: the "--" state and the hidden "a scale was chosen" lock are gone. Every key plays and writes, so the flat second and the chromatic walk-up are
  yours. NEW, NOTES and VARY still keep to the scale. A new Config row, KEYS (ALL or IN SCALE, ALL at first), brings the old lock back: with IN SCALE the dark keys do nothing.
  A project saved with the old lock loads without it (the flag is read and ignored); KEYS is a setting of the unit, not of the project.
- **Params opens FX first (S4).** INSTRUMENT has the same two pages and read-outs, but FX is page 1 (the distortion and the delay, the effects you reach for most) and VOICE
  is page 2. Params from another screen opens FX; Params again goes to VOICE and back.
- **Patterns of any length from 1 to 64 steps (I1).** PATTERN SETUP's LENGTH moves one step a detent, and Shift + turn jumps over 8, 16, 32 and 64 (and down to 1). The library's
  7- and 12-step lines keep their own length instead of being padded to 8 or 16, so they loop as composed. A growing line repeats the bar before the new steps (16 to 32 still
  doubles it), a shrinking one cuts; the turns of one gesture are one UNDO step. DOUBLE clamps at 64 (40 doubles to 64 by repeating its first 24 steps). The last step of an odd
  length has no partner under swing and is not swung. A project with a length other than 8, 16, 32 or 64 saves and loads as it is; **an older version (v0.7.0 and before) reads
  a length it does not know as 16**, so open such a project in the new version.
- **NEW draws a line that sounds like a 303's (I2).** The pitches lean on the root (about 45 %) and its octave (20 %), with the fifth and the minor third or seventh for colour (25 %)
  and the rest of the scale for the remaining 10 %; the first half of the line (a quarter from 32 steps) comes back over the rest with about a quarter of it changed; a slide prefers
  a step that glides into a sounding step of another pitch, and an accent prefers the steps off the beat and the octave jumps. NOTES draws its pitches the same way. DENSITY, ACC %
  and SLD % keep their meaning (0 is none, 100 is every step). **The same seed draws a different line than in v0.7.0**, and the old promise that SLD 0 gives the pre-tie line is gone.
  The numbers are four constants at the top of NEW in `app/seq/src/ops.cpp`: the first guesses, to be tuned by ear on the unit.

## v0.7.0 (2026-11-12), a pre-release

TRACT, a talking mouth after the voice, with its page and its pattern line; PERFORM KNOBS with four sets of knobs; PERFORM's arrows; and a
few sequencer and HOME gestures. It needs more testing: the host tests pass and its CPU was measured on a unit with the tract off and on
(render 53.2 % running, 62.2 % at most with it on, 0 underruns, `docs/hw/2026-11-12-964cdbd.md`), but the tract's sound and the new knob sets
still have to be tried by ear and by hand there.

### What you can do that you could not before

- **PERFORM KNOBS has four sets of four knobs.** F1 to F4 are SET 1 to SET 4 (FILTER, MODDED, GROWL, WOBBLE, filled at the start with ENV MOD / DECAY / CUTOFF / RESO,
  ACC DECAY / SLIDE / OVERDRIVE / FILTER FM, VOWEL / FORMANT Q / COMB TUNE / MIX and WOB RATE / WOB DEPTH / DRIVE / DLY SEND); F5 to F8 are the rows' knobs, and a tap on a
  row's pad moves the encoder to that row. Hold a row's F key and press Up or Down to give the row another knob (Shift: the next family), Shift + F1 to F4 puts a set back
  (FACTORY), Shift + F7 and F8 are REDO and UNDO. HOME's FILT rows are rows 3 and 4 of the set in use. The sets are saved in the project (an older project loads with factory
  sets 2 to 4). This replaces the old hold-an-F-key-and-tap assignment and Shift + F1 to F4 list walk on KNOBS.
- **A KNOBS row can hold the wobble's settings and the switches (KN4).** WOB RATE, WOB SHAPE, WOB DEPTH, WOB TARGET and the MODS, GROWL, TRACT and WOB switches join the list a row's
  F key walks (last, after TRACT). A tap sets them at once; REC does not record them (they have no lane) and a toast says so. They are saved in the project as row targets above the
  lane targets, so an older version loads that row with its factory knob. Set 4 is now WOBBLE (it was FX).
- **PERFORM's arrows pick and move the knob.** Left and Right move the latch along F1 to F8 (blank keys skipped, no wrap); Up and Down move the latched knob by
  1/20 of its range, a switch flips, and they repeat while held, so a knob can be worked without touching the F row. With an F key held, Left and Right stay
  minus and plus 1/20, as on HOME. Nothing is saved differently.
- **PERFORM's row 3 is CUTOFF, always.** It no longer follows the F key you tapped last (it could turn into ACCENT without telling you): it is HOME's
  FILT row 3, so a knob assigned there (hold an F key and tap the row) shows on HOME, PERFORM and PERFORM KNOBS, and "LATCHED: ..." is gone. A
  project saved by an older version loads with CUTOFF on that row; the row's two saved bytes stay in the file, unread.
- **TRACT, a page to play the mouth.** Sample Play opens it: F1 VOWEL (A E I O U morphed: a letter on a vowel, "E 37%" between), F2 TONGUE, F3 LIPS,
  F4 RES, F5 GLIDE, F6 SIZE, F7 TRC MIX and F8 the tract on and off (off is Mix 0, the dry sound bit for bit; on gives the last mix back). The
  pads are the pattern's TRACT line: rows 1 and 2 its steps (24 a page), row 3 the palette A E I O U - with GLD and LINE, the bottom row the live
  vowels (with LINE on, hold one to override the line; Rec writes the step that sounds); Shift + F4 is LEN, F5 CLEAR and F6 NEW (a drawn line). The knobs F1 to F3
  and F7 can be MIDI-learned (RES, GLIDE and SIZE lose Shift + F on this page to the line's tools) and are part of the snapshots (an older project's snapshots
  load with the tract as it is). The WOBBLE's TARGET gets a third value, MOUTH: the wobble swings the vowel around the one that sounds. HELP has
  a page for it. It needs listening on the unit.
- **The PERFORM KNOBS rows can hold the TRACT knobs.** Shift + F1 to F4 walks the list of recordable knobs and now ends with TRACT's (VOWEL, TONGUE,
  LIPS, RES, GLIDE, SIZE, TRC MIX): a row on VOWEL plays the mouth from the pads, and with REC on it records. Nothing before them moved.
- **PERFORM KNOBS, PERFORM's second page.** Press Performance twice: the four rows of pads are four knobs, one pad a value (a list or a
  switch has a pad for each of its values). They start as ENV MOD, DECAY, CUTOFF and RESO; rows 3 and 4 are HOME's FILT rows, the same
  knobs and the same saved rows. Hold an F key and tap a row to give it that key's knob; Shift + F1 to F4 points the encoder at a row so a
  turn walks the list of every knob that can be recorded; Shift + F8 puts the four rows back. With REC on while the line plays, a tap is
  written as motion on the knob's lane (the limit of eight lanes applies and says so). A tap on a mod while MODS is off switches MODS on,
  as the encoder does. The colour buttons stay PERFORM's four actions.
- **Performance flips PERFORM and PERFORM KNOBS.** Shift + Performance opens PERFORM SETUP from any screen, and Performance leaves it.
- **A new project starts with eight snapshots.** PERFORM's row 2 plays at once: the sound as it is, CLOSED DUB, SCREAMER, HOLLOW, OVERDRIVEN,
  LONG, FISH (RANGE FISH with the Devil Fish mods) and GROWL. NEW, RESET PROJECT and a first start fill them; a project you loaded keeps the
  snapshots it was saved with, empty slots included. The sounds still need a listen on the unit.
- **Rows with a switch or a short list have a pad a value**, on HOME's FILT and PERFORM's row 3 too: a tap sets the value at once.
- **A saved project holds two more rows** (PERFORM KNOBS' rows 1 and 2). A project saved by an older version loads with ENV MOD and DECAY on
  them, and one saved by this version loads in the older ones with their own defaults for the rows they know. Nothing you hear changes.
- **Copy and Delete with a pattern pad.** Hold Pattern, hold Copy and tap a pad: the pattern you are on is copied onto that pad's
  pattern (one undo). Hold Pattern, hold Delete and tap a pad: that pattern is cleared (one undo). PATTERN SETUP's Shift + F1 / F2 stay.
- **A momentary lift.** With TRNSP on, hold Shift and a key: the line is transposed only while you hold that key, and goes back to its
  earlier lift when you let go (a two-step lift needs no second tap). A plain key stays as before.
- **The root by a key.** On PATTERN SETUP hold F5 and tap a key of the bottom row of pads (C to B): that is the generator's root. Shift + F5
  with the encoder stays.
- **NEW and RHYTHM tie notes.** PATTERN SETUP's F8 SLD % now also sets the ties: after a step that plays, a step becomes a tie (the note
  goes on) with half the chance of a slide, never on step 1. At SLD % 0 NEW, VARY, RHYTHM and NOTES draw exactly the lines they did before;
  at the default 20 % a NEW line differs from v0.6.0's for the same seed, with a tie here and there. By ear on the unit first: it stays only if it sounds right.

### What works differently (a habit changes)

- **WOBBLE with Shift held has no LEARN row and the hint says NOT LEARNABLE** (under the skin), not a dash on every key. Shift + Sample Playback and Shift + Sample Editor
  open GROWL and WOBBLE as they do without Shift (HELP says so), and HOME's empty-pattern hint now says to arm REC, play and hold the keys (a held key is a long note).
- **Sample Play opens TRACT, a second press GROWL.** It opened GROWL; now the press flips TRACT and GROWL (Sample Edit is WOBBLE and Sample Load
  MODDED as before). Pressing it again from another screen opens the first page, TRACT.
- **PERFORM SETUP is Shift + Performance.** It used to be the third press of Performance; now Performance only flips PERFORM and PERFORM
  KNOBS, so a press in play never lands on SETUP. Performance leaves SETUP for the page it was opened from.
- **The library loads a line and keeps your sound and, while the transport runs, your tempo.** FILE LIBRARY starts on LOAD LINE (Shift + F2
  chooses LOAD SOUND or LOAD BOTH). A load while the line plays keeps the tempo and the swing you have and the toast says "TEMPO KEPT"; stopped,
  it sets them as before.
- **Every F key looks like its kind** (knob, switch or action), on the classic screens as under the skin. A switch (WAVE, SONG's PLAYS and LOOP) is a chip that is
  filled when it is on; an action (TAP, ARM, SAVE, DISCARD, CHAIN, CLEAR, the Shift tools) is its name in a frame with no value; RECORD's BARS is a knob and fills.
- **MODS on or off and the growl's last mix are saved with the project.** A project saved with MODS off opens with MODS off (the STOCK chip
  shows); a load, REVERT, NEW and RESET PROJECT no longer always turn MODS on, they bring back what the project holds (NEW and RESET: on). F8
  GROWL's "last value" is saved too. The file gets two more entries; a project from an older version loads with MODS on and a last mix of
  50 %, and an older version skips the two.
- **A new effect, TRACT, sits after the growl, silent for now.** The vocal-tract filter is in the sound path at Mix 0 (a bit-exact bypass:
  nothing you hear changes) and its settings are saved with the project; the page to play it is Sample Play (see above). A project saved by an older
  version loads with TRACT at Mix 0, and an older version skips its entries.
- **RESET PROJECT keeps the sound it takes away.** The voice and distortion go into the library's LAST first, and the notice says
  "PROJECT RESET - LAST: SHIFT F3": Shift + F3 on FILE LIBRARY brings them back.

- **FILE LIBRARY's slots are drawn as the pads are.** The grid is 4 rows of 12 (slots 0 to 47, or 16 to 63 while Shift is held), so the
  first pad of row 2 is the first cell of row 2. Up and Down move a row of twelve (they moved eight), and the pad LEDs dim-mark every
  fourth pad. Nothing saved changes.
- **A chain of two or more patterns loops.** Hold Pattern while the line plays and tap two or more patterns: they now play round and
  round (A, B, C, B, C ...: the pattern that was playing is not part of the round) until a single tap with Pattern held replaces the chain.
  Before, the chain played once and its last pattern then repeated for ever. One queued pattern behaves as before. Nothing in a saved
  project changes.
- **Play, the pads and the encoder wait while the project loads at start-up.** A press in the first moment after switching on (with a
  card) is not taken, so the first Play no longer costs the load and turns autosave off ("PLAYING BEFORE THE LOAD"). After three seconds
  they are yours again whatever the card is doing.
- **CUTOFF covers only the range the voice uses.** On RANGE 303, or with MODS off, CUTOFF's encoder travel, its read-out and the twelve
  pads of the FILT and KNOBS ladders span 200 to 2394 Hz (before, 25 to 4500 Hz, and everything under 200 or over 2394 Hz sounded the same, so
  the first and last pads and the ends of the encoder did nothing new). RANGE FISH keeps the whole 25 to 4500 Hz. A saved project is not
  changed: a stored value outside the range plays as before and reads as the end it sounds like until you move it. Motion lanes, p-locks
  and MIDI CC keep their own scale.
- **In a long pattern the step pads stay on the page you chose while it plays.** A tap on a step pad, or Left or Right across a page
  edge, holds the pads on that page ("PAGE 2 HELD" in the title); before, they jumped to the playhead's page under your fingers.
  Stop or a change of pattern gives the follow back (Shift + Left/Right is ROTATE and does not).
- **UNDO and REDO are Shift + F8 and Shift + F7 wherever you edit.** PATTERN SETUP now has REDO on Shift + F7, and SONG's plain F8 UNDO is
  gone: it answers "UNDO IS SHIFT + F8" for this release (SONG's Shift row had both already).
- **No holes in the F row on RECORD and SONG.** RECORD is ARM, BARS, SAVE and DISCARD on F1 to F4 (BARS was F3, SAVE F6, DISCARD F7);
  SONG is PLAYS, LOOP, CHAIN and CLEAR on F1 to F4 (CLEAR was F7). SAVE and DISCARD and CLEAR still ask twice. Nothing saved changes.
- **Hold a key with REC on and the note lasts as long as you hold it.** While the line plays, a key lands on the nearest step (past half of a
  step, on the next one) and every step that starts while you still hold it is written as a tie; one UNDO takes the whole note back. Played
  with the line stopped, the keys write and move on as before.
- **Make a note longer by hand, with the line stopped.** With REC on, hold a key and press Right: a tie step is added after the note and
  the cursor moves on (Left takes the last tie back, the step becomes a rest). Without REC, hold a step pad and press Right: the next step
  becomes its tie. Before, Right moved the cursor with a key or a step held. The key and its ties are one UNDO step.

## v0.6.1 (2026-11-09), a pre-release

A fix for a voice that looks set up and plays nothing. It needs more testing: the host tests pass, the cause was found and cured on a
unit by listening to its USB output, but the v0.6.1 image itself has not been heard there yet.

### What plays or sounds different

- **OVERDRIVE reads OFF at the bottom of its range.** On MODDED TONE, F3 OVERDRIVE at its lowest value (-24 dB) sends no oscillator into
  the filter at all, as the Devil Fish's pot does, so with MODS on the voice is nearly silent (about -43 dBFS on the master where the
  same line plays at about -4 dBFS at 0 dB). The screen showed "-24.0dB", which looked like a level that still sounds; it now shows
  OFF, without a unit, on the knob and in the TONE read-out of MODDED NOTES. One detent up it reads -23.5dB and plays. The sound is
  unchanged: the bottom is still silent on purpose, so HI RES can whistle alone.
- **If your unit is silent after an update**, look at OVERDRIVE first: a project that has it at the bottom (it may come from v0.5.0 or
  earlier, where the bottom was only a cut of 24 dB) loads with it there. Turn it up to 0.0 dB (the default), or press Shift + Sample
  Load for STOCK. The manual's "No sound" tip says so now.
- Nothing else changes: saved projects load as in v0.6.0, with the same values.

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
