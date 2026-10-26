# Third-party material in o3-303

The framework's own list is `../o3-sdk/THIRD_PARTY.md`.

| Component | Licence | Where | Use |
|---|---|---|---|
| Replicant 303 by Daniel Rojas Grasset (DanimanX), https://github.com/DanimanX/Replicant303, commit `d5369be` | MIT | `third_party/replicant303/` (reference copy, host oracle only) | o3-303's voice, distortion, step model and preset generator are ports of it (docs/M3-PLAN.md); each ported file names its origin and carries the MIT notice |
| Vowel formant frequencies: G. E. Peterson and H. L. Barney, "Control methods used in a study of the vowels", J. Acoust. Soc. Am. 24 (1952) | published measurements (facts, no code) | `units/growl/src/growl.cpp` (the A E I O U table) | the growl's formant targets, the male averages; attributed in the source |
