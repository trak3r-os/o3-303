# Replicant 303 (reference copy)

Source: https://github.com/DanimanX/Replicant303, commit `d5369be028399f5a403067b58da2936caa070c0e` (2026-10-25), MIT License,
Copyright (c) 2026 Daniel Rojas Grasset (DanimanX) (`LICENSE`). The files are byte-for-byte the upstream ones (`SHA256SUMS`).

o3-303 is based on Replicant 303 (the maintainer's decision, 2026-10-25): its voice (`DSP/Oscillator303`, `DiodeLadderFilter`,
`Envelope303.h`, `Common303.h`), its distortion (`DistortionUnit`), its step model and sequencer (`Pattern.h`, `PatternSequencer`) and
its preset generator (`Presets/PresetBank`) are ported into o3-303's units and app (docs/M3-PLAN.md). These copies are the
reference: the host tests build the JUCE-free ones as an oracle for the ports. Nothing in this directory is linked into an image.
`SynthEngine`, `PatternSequencer`, `Pattern.h` and `PresetBank` include JUCE and are read, not built.
