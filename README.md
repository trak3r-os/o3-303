# o3-303

An improved TB-303 for the Tracker hardware (NXP MK66FX1M0, 800×480 display, 48 pads with LEDs, 33 buttons, one encoder), built as an
app on the [o3-sdk](https://github.com/trak3r-os/o3-sdk) framework: a 303 voice with drive and delay (Devil Fish-style extras planned, `docs/DESIGN-303.md` section 3), a 303-style step sequencer with
motion recording and parameter locks, and later a HOOKS screen that launches phrases in time. The design is `docs/DESIGN-303.md`. The
app is a whole firmware image; o3-sdk, checked out next to this repository, is the library under it and is used as it is.

Unofficial; not affiliated with or endorsed by Polyend. "Tracker" is used only to describe the hardware it runs on.

**To play it, read the [player's manual](docs/MANUAL.md)**: installing and going back, the panel, the first five minutes, and every screen
with its picture.

## Status

| Milestone | What | State |
|---|---|---|
| M3 | o3-303 on the kit: the voice and distortion ported from Replicant 303, the delay, the sequencer (8 to 64 steps, accent, slide, tie, swing, gate, motion, p-locks, chains, undo), the GEN vault, the 303 screens | built and verified on the host, image statically gated; its unit check (M3-PLAN item 4) has no page in `docs/hw/` (`docs/M3-PLAN.md`) |
| M3b | The performance round: pad filter ladders, snapshots and morph, momentary FILL, ratchets, trig conditions, beat freeze, tempo nudge, scale-locked keyboard, assignable performance buttons | built 2026-10-25 and 2026-10-26; the beat freeze and the snapshot morph checked on the unit, the rest owed (`docs/M3B-PLAN.md`) |
| M3c | The bass-music round: growl, wobble, rolls, tape stop and reverse (crush, trance gate, pump, dive, riser and OTT are backlog) | built and unit-checked for growl, wobble and rolls (2026-10-26); by-ear checks owed (`docs/M3C-PLAN.md`) |
| UX | The new way the app is used: PERFORM, the HOME map, PATTERN SETUP, INSTRUMENT, MASTER, FILE, RECORD, the Config pages, the project with autosave, HELP | waves 1 to 6 built (2026-10-30 and 2026-10-31), checked on the unit by console only; SONG built and host-verified, unit check owed (`docs/UX-PLAN.md`) |
| SKIN | The obsidian skin, CLASSIC by default until SK8: the assets (SK1), the core, header and F row on HOME and SETUP (SK2, SK3), SONG (with SG2) and PERFORM (SK4) committed; INSTRUMENT (SK5) built and host-verified; MASTER, RECORD, HOOKS, FILE (SK6, SK7) and OBSIDIAN by default (SK8) not started | in progress, unit check owed (SU1, then SU2; `docs/SKIN-PLAN.md`) |
| M4 | The HOOKS screen (sample launcher, synced sampling) | planned; the three Sample buttons open a placeholder |

### On the Tracker

The unit sessions are in `docs/hw/`. They ran over USB, by the console and the tester's screenshots. Nobody has judged the app by ear or
by hand unless the session page says so.

| Session | Image (commit) | What it showed |
|---|---|---|
| 2026-10-26, M3b-a | `f3c08e6` | The beat freeze: exact loop lengths, no clicks, a 10-minute soak clean; playback as before. Render CPU about 37 % of a block playing, peak 44.4 % |
| 2026-10-26, M3b ui and M3c | `78109ff`, then `2bf2944` | The growl costs +2.4 % of a block after the optimisation (it was +7 %); everything on peaks at 49.9 %, 0 over-budget; a 10-minute soak clean; the snapshot morph clean |
| 2026-10-30, FT812 fill rate | o3-sdk `apps/fillrate` | The display's sprite limit per line (about 216 at cell 100); the input to the skin's design |
| 2026-10-30, U3 | `93382bf` | SETUP and INSTRUMENT on the screen; autosave with 0 underruns; the project back after a reboot |
| 2026-10-30, U5 | `0f95cc5` | Waves 4 and 5: MASTER, FILE, the Shift row's labels, PERFORM SETUP; a 1-bar and a 30 s take written to the card, CPU 33 to 35 % while recording |
| 2026-10-31, U6a | `767f33e` | Wave 6 (C1): the Config pages 303 and DEEP and their values after a reboot; a DEEP value no longer drops the project at boot |

U1 and U2 have no results recorded (their pages say `to fill`); U4 was superseded by U5 before it was installed.

Still owed on the unit: the pads, the knobs and the LEDs by hand; the encoder rows, tap tempo, NameEntry, LOAD and REVERT; MIDI learn with
a real controller; the SONG screen (a song with repeats by ear); HELP by eye; the growl's vowels and the wobble's rates by ear; SU1, the
skin's first unit session. The card cases of U2 (a card pulled during a save, no card, write-protect, a card swap) were dropped by the
maintainer and are not planned.

USB audio out, saving to the card and MIDI come from o3-sdk's M2b and are used here: the project autosaves to the card, and MIDI notes
play the voice.

## Documents

| Document | What it is | Read it when |
|---|---|---|
| `docs/MANUAL.md` | The player's manual: install, the panel, the first five minutes, every screen with its picture, projects, settings, MIDI, troubleshooting | to play the instrument |
| `CLAUDE.md` | The rules for agents and contributors: the framework, the hard rules, the build and the tests | before changing anything |
| `docs/DESIGN-303.md` | The design: the sound, the sequencer's data, HOOKS and the open decisions (section 8). Its status block says which controls have moved on | for what the app is and why |
| `docs/UX-303.md` | The specification of the screens, buttons, pads, Config, HELP and the project | before touching a screen, a button or a pad |
| `docs/UX-PLAN.md` | The slice plan for the UX waves, with each slice's state and the unit checks owed | to see what is built and what the unit still has to show |
| `docs/SKIN-PLAN.md` | The obsidian skin: the look, the budgets, the o3-sdk contracts and slices SK1 to SK8 | before working on the skin |
| `docs/M3-PLAN.md` | M3, the first playable 303: the voice, the distortion, the sequencer and the vault | for the voice, the distortion and the sequencer's first form |
| `docs/M3B-PLAN.md` | M3b, the performance round: ladders, snapshots, FILL, ratchets, the freeze | for the performance features |
| `docs/M3C-PLAN.md` | M3c, the bass-music round: growl, wobble, rolls; the cut items as backlog | for the growl, the wobble, the rolls |
| `docs/hw/` | The unit sessions: one page per session with its image, checklist, results and what was left | before a unit run, and to know what the unit has shown |
| `../o3-sdk/docs/FRAMEWORK.md` | The kit's guide: layers, units, the engine, the controls convention, HELP, the file API and the rules | before writing a unit or a screen |

## Build

The two repositories side by side (o3-sdk next to this directory, `-DO3SDK_DIR=<path>` otherwise):

```sh
git clone https://github.com/trak3r-os/o3-sdk.git
git clone https://github.com/trak3r-os/o3-303.git
cd o3-303
```

The host build needs CMake 3.24 or newer and a C++17 compiler; the image needs the ARM GNU toolchain 15.3.rel1.

```sh
cmake -S . -B build-host
cmake --build build-host -j4
ctest --test-dir build-host --output-on-failure

# the app in the simulator: PNG snapshots of the screen and the pads, and the audio as a WAV
mkdir -p /tmp/o3_303
build-host/app/o3_303_sim --scenario app/scenarios/boot.txt --out /tmp/o3_303

# the firmware image: build-mk66/app/o3_303.ptf
cmake -S . -B build-mk66 -DO3SDK_PLATFORM=mk66 -DARM_GNU_ROOT=<arm-gnu-toolchain-15.3.rel1 dir>
cmake --build build-mk66 -j4
```

## Layout

`app/` the app, its screens and its scenarios · `app/seq/` the sequencer, the pattern operations and the preset generator ·
`units/` its DSP units (`acid_voice`, the voice; `distortion`; `freeze`, the beat freeze and its modes; `growl`) · `tests/` host tests · `third_party/replicant303/` the pinned Replicant 303
sources (MIT) the ports are tested against · `docs/` the design and the plans · `.claude/` the agent harness, from o3-sdk. Start with
`CLAUDE.md`.

## Licence

GPL-3.0-or-later (`LICENSE`).
