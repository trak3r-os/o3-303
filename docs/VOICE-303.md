# o3-303: the TB-303 voice

The maintainer asked on 2026-11-06 for "el sonido mas identico a la 303 de roland y con las modificaciones de devil fish". This page says what
changed in `units/acid_voice` to get there, where each number comes from, what was measured, and what is still not known. It replaces the
rule "Stock is today" of `docs/MODS-PLAN.md`: stock is now a TB-303.

Status: built, and it needs more testing on a unit. The figures below come from the simulator and the cross build. Section 6 lists what only the
unit can show.

## 1. Why the voice changed

The voice ported from Replicant 303 was far brighter than a TB-303. Measured against real recordings (section 4), the shipped default put 56 %
of its energy between 1.28 and 2.56 kHz, where a TB-303 recording has about 2.5 %, and 5 % below 320 Hz, where a recording has 72 to 81 %.

Most of that was not the filter. The envelope swept too high (3 kHz at the default, 4.8 kHz on an accent, against a median top of about 1,060 Hz
in the real clips), and the factory sound carried GRIT and a delay send. The filter's own share is modest (section 4).

Self-oscillation was not the cause either: the old ladder rings alone without input, but it locks to the note when driven.

## 2. What changed, part by part

| Part | Before (Replicant) | Now | Source |
|---|---|---|---|
| Filter | Replicant's ladder: feedback gain 17.2, 40 Hz high-pass in the feedback, rings alone at high RESO | Open303's four coupled poles at twice the sample rate (two sub-steps), 150 Hz high-pass in the feedback, no self-oscillation at stock | Open303's `TeeBeeFilter`; Stinchcombe and Whittle for the behaviour |
| Cutoff law | 5.2 octaves of Env Mod above the knob, 2.2 more on an accent | Open303's measured law: the sweep goes below the knob as well as above, and its span depends on Cutoff and Env Mod | Open303 |
| Reso law | Feedback in proportion to RESO | `(1 - exp(-3p)) / 0.95` | Open303 |
| Saw | Parabolic bend of .15 | Plain ramp, same level | Open303 |
| Square | 48.5 % duty | 46.88 % duty, same level and droop at 25 Hz | Open303 |
| Input and output filters | None | 44.486 Hz input high-pass; 14.008 Hz all-pass, 24.167 Hz high-pass and a 7.5164 Hz notch at the output | Open303 |
| Amplifier | Decay time constant 4 s | Time constant 1.23 s, .45 of the main envelope added on every note, a de-clicker at 200 Hz | Open303 |
| Note end | 25 ms release at the gate | 8 ms hold after the gate, then a 3 ms release (50 ms on an accent) | Whittle for the hold, Open303 for the accent's release, ours for the 3 ms |
| Accent | .45 x ACCENT in the amplifier, a fixed lift of the cutoff | 4 x ACCENT of the main envelope in the amplifier, about 9 dB at a note's start at ACCENT 70 %, with the sweep circuit: at high RESO close accents climb | Open303 for the level, Whittle for the circuit |
| Slide | 60 ms as a time constant | 60 ms as the audible time (time constant 0.2 x SLIDE) | Whittle |
| Defaults | Distortion on, send 30 %, level -6 dB | Distortion off and send 0 on new projects and the eight factory lines, level -3 dB | The maintainer's goal: a TB-303 has neither |

Generated vault sounds keep their distortion. A saved project keeps its knobs, and sounds different because the voice under them is new.

## 3. The Devil Fish mods

Ranges and behaviour are from Robin Whittle's published Devil Fish manual. No code or material of any instrument was used.

| Control | Behaviour |
|---|---|
| DECAY, ACC DECAY | 30 ms to 3 s. ACC DECAY's stock value is 200 ms |
| AMP DECAY | 16 ms to 4 s, stock 1230 ms, settling at SUSTAIN |
| SUSTAIN (id 21, TONE F7) | The level the amplifier settles at while the gate is held |
| RANGE (id 20, TONE F6) | 303 or FISH. FISH widens ENV MOD to zero and three times the top, fills the bass (high-pass at 12 Hz, notch off), and starts and ends a note at once (de-clicker 1500 Hz, no gate hold, 8 ms release) |
| ACC SWEEP | OFF, NORMAL or HI RES. HI RES doubles the feedback, so the filter oscillates: from about 350 Hz at full RESO and from 1 kHz at 65 % |
| OVERDRIVE | -24 to +36 dB, about 63 times (Whittle gives 66.6). At the bottom no oscillator reaches the filter, so HI RES plays it alone |
| SOFT ATK | 0.3 to 30 ms |
| FILTER FM, TRACKING, MUFFLER, SWP SPEED, SUB OSC | As on the MODDED pages. SWP SPEED has FAST, NORMAL and SLOW; the sweep's capacitor keeps its charge between accents |

MODS off gives the stock voice. RANGE and SUSTAIN follow STOCK in the table and sit last in a snapshot (indices 42 and 43), so a project saved
with 42 values loads and fills them from the sound.

## 4. What was measured

Two public recordings were used as references: the `a1k0n/303` samples and the Wikimedia Commons TB-303 demo. Their knob positions are unknown, so
these are comparisons of shape, not matches at equal settings. Shares are of the total energy. "Clean" means with the distortion and delay turned off; the new voice boots that way.

| Set | Centroid (Hz) | Below 320 Hz | 1.28 to 2.56 kHz | Crest (dB) |
|---|---|---|---|---|
| Real, a1k0n | 278 | 72 % | 2.2 % | 17.0 |
| Real, Commons | 490 | 81 % | 2.5 % | 19.8 |
| Old voice, as shipped | 1,474 | 5 % | 56 % | 10.5 |
| Old voice, clean | 767 | 52 % | 21.7 % | 12.0 |
| New voice, as it boots | 253 | 84 % | 5.8 % | 18.5 |

The new voice's 1.28 to 2.56 kHz share is still more than twice the recordings', and it has almost nothing above 2.56 kHz, where they have 0.4 to
2.7 %. Both depend on the notes and knobs of the pattern, which the recordings do not state, so these may or may not be real differences.

The filter alone, fit harmonic by harmonic at steady state to 30 clips from the a1k0n set with the best knobs for each clip (rms error in dB):

| Filter | Median | Mean | Worst |
|---|---|---|---|
| Shipped | 3.66 | 3.90 | 6.34 |
| Coupled (now) | 3.99 | 3.36 | 6.49 |

The coupled filter is better on 23 of the 30 clips, and its median is slightly worse. It is a modest gain. Driving it harder did not help.

Ladder checks, on a prototype of this ladder: peak frequency and level at two sub-steps are within 3 % and 1 dB of sixteen. At stock feedback it does not self-oscillate at any
cutoff up to full RESO. With HI RES's doubled feedback it stays bounded in a fast sweep, with a worst level of .84.

Cost, an estimate from the cross build's disassembly, not a measurement: the stock SAW loop's usual path is about 335 cycles a sample, before the
control period's share, with two divisions. It runs in four short passes over a control period, because one long loop spilled most of its
registers. The old voice's loop counted 424 to 443 cycles, but that count takes in every path, rare ones as well, so the two are not like for like.
Counted the same way the new function is about 700, which includes code that runs once a control period or only at a wrap. The modded loops cost
more than the stock loop. Only the unit can settle this.

## 5. What is not known

- **No reference with known knobs.** No freely licensed TB-303 recording with stated knob positions was found, so no figure here is a like-for-like
  match, and no percentage of likeness can be given.
- **Accent level.** No published figure in dB was found. 9 dB at ACCENT 70 % follows from Open303's constants.
- **Our own choices.** `kSweepOctaves`, `kSweepSlowTau`, `kFishHpHz`, `kFishDeclickHz`, `kReleaseTau`, `kAmpScale` and `kKnee` have no source: they
  were set by judgement.
- **The bass.** Whittle gives -5 dB at 32 Hz; Open303's chain gives about -10.6 dB. Open303's is kept, because it fits the a1k0n low end better.
- **Not modelled.** The sweep's path into the amplifier: the direct term of the main envelope is Open303's and stands in for it. The stock ladder's
  nonlinearity is a soft bound only.

## 6. Owed on a unit

- The render's CPU: the figure to set against the estimate, against 50.1 % at the last measurement.
- By ear, the maintainer: the accents and how they climb, the height of the sweep, RANGE, HI RES at the top of RESO, SUSTAIN, and a slide.
- A project saved by v0.5.0 loads, and its sound is as `state` says.

## 7. Sources

- Robin Whittle, Devil Fish manual: https://www.firstpr.com.au/rwi/dfish/Devil-Fish-Manual.pdf
- Robin Whittle, what is unique about the 303, and its slide: https://www.firstpr.com.au/rwi/dfish/303-unique.html, https://www.firstpr.com.au/rwi/dfish/303-slide.html
- Open303, Robin Schmidt (MIT): https://github.com/RobinSchmidt/Open303
- Tim Stinchcombe on the diode ladder: http://www.timstinchcombe.co.uk/index.php?pge=diode
- Kernmayer's thesis (2020): https://phaidra.ustp.at/api/object/o:4344/download
- Recordings: https://github.com/a1k0n/303, https://commons.wikimedia.org/wiki/File:The_Sound_of_Roland_TB-303_for_Wikipedia.ogg

Looked at and not used: JC303 (GPL-3.0, its filter is Open303's and adds no measurement), Nekobee and Nekobi (GPL-2.0, a Chamberlin filter, not
faithful), LMMS Lb302 (GPL-2.0-or-later, three poles, incomplete), Faust's `diodeLadder` (STK-4.3) and Csound's `diode_ladder` (LGPL-2.1). Whittle's
own sound samples are copyright and were not used.
