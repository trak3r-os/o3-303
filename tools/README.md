# tools

| Tool | What it is |
|---|---|
| `ci.sh`, `remote_ci.sh` | The CI of this repository (host or MK66), locally or on the remote build machine (`../o3-sdk/docs/ci.md`) |
| `gen_skin.py`, `manual_images.py`, `skin_assets.sha256` | The obsidian skin's bitmaps and the manual's pictures |
| `midi_check.py`, `midi_check_fake.py` | The MIDI test of the unit, driven from a computer over USB (below) |

## midi_check.py: the MIDI test of o3-303 on the real Tracker

The computer is the other end of the unit's USB MIDI port (a raw ALSA node, `amidi -l`), listens to the unit's USB audio (profile P8, the master on
channels 7-8 of the capture stream, `arecord`), reads the unit's console (`version`, `state`, `midi`, `cpu`, `crash`) and presses its keys (`key`,
`enc`) as a finger would. Python 3 and the standard library only, plus the `arecord` binary; Linux. The console and audio helpers of
`../o3-sdk/platform/mk66/tools` are imported from `$O3SDK_DIR` (default `../o3-sdk`), never edited.

```sh
tools/midi_check.py --expect-version <text of the unit's `version`> [--midi-dev /dev/snd/midiC1D0] [--phases identity,notes,follow,clockout,learn,stress,soak] \
                    [--out DIR] [--learn-baseline none] [--quick]
tools/midi_check.py --restore-from DIR --expect-version TEXT     # put the Config back after a run that died
tools/midi_check.py --self-test                                  # no hardware: a fake unit on a pty
```

- It refuses unless `version` contains `--expect-version` and `state` names the app `O3-303`: until then nothing but `version` and `state` is
  sent, and neither the MIDI node nor the audio device is opened.
- The unit is shared. The Config rows, the screen, the transport and the tempo found are written to `DIR/start_state.json`, and a `finally`
  block puts every one of them back and prints the end state against the start state. The only card writes are the kit's own settings file
  (`Apps/o3_303/system.o3s`) that the Config rows save by themselves.
- Every phase prints PASS or FAIL with the measured number and the limit; the limits are constants at the top of the file, each with its
  reason. `DIR/report.json`, `DIR/summary.md` and the console transcript are the record. Exit 0 all passed, 1 a check failed, 2 refused, 3 a
  check could not run (no audio, learn without `--learn-baseline none`), 4 the unit was not fully restored.
- `--self-test` (the ctest `midi_check_selftest`, label `tools`) runs the parsers, the signal analysis, the clock sender's timing on this machine, the
  refusal, the restore after a failure in the middle of a phase and the whole tool against `midi_check_fake.py`, a model of the unit's console,
  MIDI node and voice. It proves the tool, not the firmware. It returns 77 (skipped) when there is no pty or no o3-sdk.

The page that says how to run it on the unit is `docs/hw/2026-11-01-midi.md`. DIN MIDI is not covered: it needs a loopback cable.
