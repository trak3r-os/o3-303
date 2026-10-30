# o3-303

An improved TB-303 for the Tracker hardware (NXP MK66FX1M0, 800×480 display, 48 pads with LEDs, 33 buttons, one encoder), built as an
app on the [o3-sdk](https://github.com/trak3r-os/o3-sdk) framework: a 303 voice with Devil Fish-style extras, drive and delay, a 303-style step sequencer with
motion recording and parameter locks, and later a HOOKS screen that launches phrases in time. The design is `docs/DESIGN-303.md`. The
app is a whole firmware image; o3-sdk, checked out next to this repository, is the library under it and is used as it is.

Unofficial; not affiliated with or endorsed by Polyend. "Tracker" is used only to describe the hardware it runs on.

## Status

| Milestone | What | State |
|---|---|---|
| M3 | o3-303 on the kit: the voice and distortion ported from Replicant 303, the delay, the sequencer (8 to 64 steps, accent, slide, tie, swing, gate, motion, p-locks, chains, undo), the GEN vault, the 303 screens | built and verified on the host, image statically gated; waiting for the unit test (`docs/M3-PLAN.md`) |
| M3b | The performance round: pad filter ladders, snapshots and morph, momentary FILL, ratchets, trig conditions, beat freeze, tempo nudge, scale-locked keyboard, assignable performance buttons | planned (`docs/M3B-PLAN.md`) |
| M4 | The HOOKS screen (sample launcher, synced sampling) | planned |

USB audio out (the master as a class-compliant interface), saving to the card and MIDI come with o3-sdk's M2b. Nothing here has run
on a Tracker yet.

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
`units/` its DSP units (`acid_voice`, the voice; `distortion`) · `tests/` host tests · `third_party/replicant303/` the pinned Replicant 303
sources (MIT) the ports are tested against · `docs/` the design and the plans · `.claude/` the agent harness, from o3-sdk. Start with
`CLAUDE.md`.

## Licence

GPL-3.0-or-later (`LICENSE`).
