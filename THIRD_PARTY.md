# Third-party material in o3-303

The framework's own list is `../o3-sdk/THIRD_PARTY.md`.

| Component | Licence | Where | Use |
|---|---|---|---|
| Replicant 303 by Daniel Rojas Grasset (DanimanX), https://github.com/DanimanX/Replicant303, commit `d5369be` | MIT | `third_party/replicant303/` (reference copy, host oracle only) | o3-303's distortion, step model and preset generator are ports of it, and the voice keeps its oscillator's method and note handling (docs/M3-PLAN.md, docs/VOICE-303.md); each ported file names its origin and carries the MIT notice |
| Open303 by Robin Schmidt, https://github.com/RobinSchmidt/Open303, commit `313bf0d` (its `DSPCode/fft4g.c` is Takuya Ooura's, under Ooura's own terms, and is built only into the host oracle) | MIT | `third_party/open303/` (reference copy, host oracle only) | `units/acid_voice`'s ladder, cutoff law, amplifier and the filters around them are ported from it, with the numbers it measured on a TB-303 (docs/VOICE-303.md); the ported file names its origin and carries the MIT notice |
| Robin Whittle's Devil Fish manual and "What makes the TB-303 unique", https://www.firstpr.com.au/rwi/dfish/ | published description, no code taken | `units/acid_voice` | The accent sweep circuit, the slide's time, the end of a note and the ranges of the mods are written from this text |
| Vowel formant frequencies: G. E. Peterson and H. L. Barney, "Control methods used in a study of the vowels", J. Acoust. Soc. Am. 24 (1952) | published measurements (facts, no code) | `units/growl/src/growl.cpp` (the A E I O U table) | the growl's formant targets, the male averages; attributed in the source |
