# o3-303 — the TB-303 app on o3sdk

An improved TB-303 for the Tracker hardware (NXP MK66FX1M0, Cortex-M4F at 180 MHz with a single-precision FPU, 256 KiB SRAM, 16 MiB
SDRAM, FT812 display 800×480, SGTL5000 codec, 48 pads with monochrome LEDs, 33 buttons, one encoder): a 303 voice with Devil Fish-style
extras, a step sequencer, and later a HOOKS screen. It is one app, a whole firmware image, built on the o3sdk framework. The design is
`docs/DESIGN-303.md`. Licence GPL-3.0-or-later. Everything in this repository is written in English.

## The framework is ../o3-sdk

The kit (core, hal, eve, ui, dsp, app), the units it ships (synth, delay), the MK66 platform, the simulator and the CMake helpers
(`o3sdk_app`, `o3sdk_unit`, `o3sdk_test_suite`) live in the sibling repository `../o3-sdk` (`-DO3SDK_DIR=<path>` if it is elsewhere). The
top-level `CMakeLists.txt` uses it with `add_subdirectory`. **It is never edited from here**, not even to get a build through: a change
the app needs is made in o3sdk, under its own rules (`../o3-sdk/CLAUDE.md`; the guide is `../o3-sdk/docs/FRAMEWORK.md`, section 11 for
this arrangement), and this repository then builds against it. Read `../o3-sdk/docs/FRAMEWORK.md` before writing a unit or a screen.

What lives here: `app/` (the app, `o3sdk_app(o3_303 ...)`, its scenarios in `app/scenarios/`), `units/` (this project's own units, the
`tb303` voice first, each with its own CMakeLists.txt calling `o3sdk_unit()`), `tests/` (doctest, `o3sdk_test_suite()`), `docs/`.

## Hard rules

- Never add code, assets or reverse-engineering material from the official firmware. "Polyend" and "Tracker" are used only to
  describe the hardware.
- Firmware reaches a unit only through a documented update path, after `fw-gate-check` passes: the bootloader's `.ptf` from the
  SD card, put there by hand, on a mounted card, or by the USB console (`o3console.py --put`, then the console's firmware
  commands), as in track3r-os (maintainer, 2026-10-25). Agents may write to an SD card mounted under `/media` or `/run/media`
  (never `/mnt`, never a raw device). No debug-probe flashing, no improvised uploaders. Formatting a card, or deleting files you
  did not put there, needs the maintainer's ask.
- No secrets, personal data or binaries in git. Commit or push only when the maintainer asks.

## Build and test

```sh
cmake -S . -B build-host && cmake --build build-host -j4 && ctest --test-dir build-host --output-on-failure
mkdir -p <dir> && build-host/app/o3_303_sim --scenario app/scenarios/boot.txt --out <dir>     # screens as PNG, audio as WAV
cmake -S . -B build-mk66 -DO3SDK_PLATFORM=mk66 -DARM_GNU_ROOT=<arm-gnu-toolchain-15.3.rel1 dir> && cmake --build build-mk66 -j4
```

The image is `build-mk66/app/o3_303.elf`, `.hex`, `.bin` and `.ptf` (the `.ptf` is what the bootloader takes from the card); its
ctests (`o3_303_image`, `o3_303_flashconfig`, `o3_303_ramfunc`) check it statically. Run builds under `nice -n 10`. o3sdk's own
apps and tests are not built here (`-DO3SDK_BUILD_TESTS=ON` runs them from this tree). A host test is not hardware verification: name
the remaining unit check.

## Engineering standards

The standards of o3sdk apply unchanged (`../o3-sdk/CLAUDE.md`, "Engineering standards"): layering by link dependency (a unit depends on
`dsp` only, an app on `app` and its units), no allocation or blocking in the real-time paths, no heap, no RTTI, no exceptions, no
`double` (check the cross build for `__aeabi_d*` with `arm-none-eabi-nm -u`), big buffers in the SDRAM with `O3SDK_BIG`, a unit's
parameter id is its index in the table (append, never insert), warning-free under `-Werror`, the CPU measured on the unit before a
per-block feature is relied on, pure logic gets a host test in `tests/`, a screen or a sound change gets a simulator scenario in
`app/scenarios/`.

## Agents, skills and hooks

`.claude/` is the o3sdk harness, copied (agents, skills, hooks); read its agent and skill texts with this layout in mind: the kit,
platform and simulator paths they name are in `../o3-sdk`, this repository's code is `app/`, `units/` and `tests/`. The hooks work here
as they do there: `guard_flash.py` (no probe or raw device; the console's update path and a mounted card are open, as in
track3r-os), `syntax_check.py` (compiles an edited file with the flags of `build-host/compile_commands.json`; files of `../o3-sdk`
are skipped), `shell_edit_check.py`, `session_brief.py`, `verify_gate.py` (an agent that edited `app/`, `units/`, `tests/` or CMake
runs ctest before it stops).
