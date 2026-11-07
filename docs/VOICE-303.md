# o3-303: the TB-303 voice

The maintainer asked on 2026-11-06 for "el sonido mas identico a la 303 de roland y con las modificaciones de devil fish". This page says what
changed in `units/acid_voice` to get there, where each number comes from, what was measured, and what is still not known. It replaces the
rule "Stock is today" of `docs/MODS-PLAN.md`: stock is now a TB-303.

Status: built, and it needs more testing on a unit. The render's CPU of v0.5.1 was measured on the unit (`docs/hw/2026-11-06-v051.md`); the Unreleased changes (section 2, "Deviations from Open303") are not measured there yet. The other figures below come from the simulator and the cross
build. Section 6 lists what is still owed.

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
| Cutoff law | 5.2 octaves of Env Mod above the knob, 2.2 more on an accent | Open303's measured law: the sweep goes below the knob as well as above, and its span depends on Cutoff and Env Mod. The law's offset has a slope of 0.15 in where the knob sits (Open303: 0.0483), its scale and the offset at the bottom are Open303's. On RANGE 303 the knob is held to 200 to 2,394 Hz before the law; FISH leaves it at 25 to 4,500 Hz. The ladder's own top is 10,400 Hz (Open303 stops at 20 kHz, v0.5.1 at 4,800), where the sweep and Tracking can reach | Open303; the top agrees with one recording within about 5 %, the floor is read roughly from it; the slope from the Din Sync recording, the 10.4 kHz from where the polynomials hold |
| Reso law | Feedback in proportion to RESO | `(1 - exp(-3p)) / 0.95` | Open303 |
| Saw | Parabolic bend of .15 | Plain ramp, same level | Open303 |
| Square | 48.5 % duty | Open303's shape, `-tanh(dB2amp(36.9) x saw + 4.37)` (zero crossing at 46.88 % of the period, an edge about 4 % of a period wide), droop at 25 Hz as Open303; its level is 0.54 of the saw's; the rise at the wrap is band-limited | Open303 for the shape; the 0.54 from the Din Sync recording |
| Input and output filters | None | 44.486 Hz input high-pass; 14.008 Hz all-pass, 24.167 Hz high-pass and a 7.5164 Hz notch at the output | Open303 |
| Amplifier | Decay time constant 4 s | Time constant 1.43 s, 0.10 of the main envelope added on every note, a de-clicker at 200 Hz; the main part stays down 4.4 ms (194 samples) after a note starts the voice from quiet (rule below), the accent's part does not | Open303 for the structure; the 1.43 s, the 0.10 and the 4.4 ms from the Din Sync recording (Whittle's figure is a typical 4 ms delay and then a 3 ms attack) |
| Note end | 25 ms release at the gate | 8 ms hold after the gate, then a 3 ms release (50 ms on an accent) | Whittle for the hold, Open303 for the accent's release, ours for the 3 ms |
| Accent | .45 x ACCENT in the amplifier, a fixed lift of the cutoff | `max(0, 2.6 x ACCENT - 0.6)` of the main envelope added in the amplifier (the function `accent_to_amp`), with a decay of 87 ms, and the sweep circuit: at high RESO close accents climb. It reads 0.3, 3.3, 5.5 and 7.3 dB at ACCENT 25, 50, 75 and 100 %, against 0.2 dB below plain, then 3.6, 6.2 and 7.3 dB on the recording | Whittle for the circuit; the line and the 87 ms from the Din Sync recording (Open303 has 4 x ACCENT and 200 ms) |
| Slide | 60 ms as a time constant | 60 ms as the audible time (time constant 0.2 x SLIDE) | Whittle |
| Defaults | Distortion on, send 30 %, level -6 dB | Distortion off and send 0 on new projects and the eight factory lines, level -3 dB | The maintainer's goal: a TB-303 has neither |

Generated vault sounds keep their distortion. A saved project keeps its knobs, and sounds different because the voice under them is new.

### Deviations from Open303

Each of these was set by measuring one recording (section 4, "The Din Sync recording") and replaces an Open303 value. The oracle test (`acid_voice_oracle_test.cpp`) carries each into its
reference as a named constant, so what is still Open303's is still compared exactly, with no tolerance loosened. The Open303 sources under `third_party` are unedited.

| Constant | Open303 or v0.5.1 | Now | What the oracle no longer proves |
|---|---|---|---|
| Amplifier start (`kAmpDelay`) | 5 ms (v0.5.1; Open303 has none) | 4.4 ms, 194 samples | not in the oracle's paths |
| Square level (`kSqrLevel`) | same level as the saw | 0.54 | not in the oracle's paths |
| Ladder top (`x_max_`) | 4,800 Hz | 10,400 Hz (where the coefficient polynomials hold and the step cap of Filter FM sits); `kMaxCutoff` and the parameter's range stay | not in the paths compared |
| Accent decay (`kAccentTau`) | 200 ms | 87 ms; ACC DECAY's default 87 ms | not in the paths compared |
| Amplifier decay (`kHoldTau`) | 1.23 s | 1.43 s; AMP DECAY's default 1430 ms | not in the paths compared |
| Main envelope in the amplifier (`kMainToAmp`) | 0.45 | 0.10 | the 0.45 |
| Accent in the amplifier | 4 x ACCENT | `max(0, 2.6 x ACCENT - 0.6)` | the 4, and that it is a power of the knob; the structure (the accent's share adds to the main input's at the same weight, to 4 %) is still proved |
| Law's offset slope (`kLawOffF`) | 0.048292930943553 | 0.15; the constant term (`kLawOffC`) 0.294391201442418 is unchanged | the slope; the scale and the offset at the bottom of the knob are still compared to float accuracy |

The oscillator, the filter at a fixed cutoff and the reso law are still Open303's, and the oracle compares them as before.

The ladder's states were measured again with the 10.4 kHz top over the 576 corners of the mods test (every mod at its stock value and ends, two hostile mixes, HI RES alone, set before the
start and live): the largest ladder state 8.00 (7.84 at v0.5.1), the ladder's `g` 9.0769 and `k` 77.153 at RESO 1 and HI RES, as the formula gives (12.286 and 104.43 with a 16 kHz top, which was tried and dropped), the feedback high-pass 165.1, the signal before the
output ceiling 28.11, the last output 1.9945. Nothing went unstable. The test's limits in `acid_voice_access.h` follow: `kLadder` 8.5, `kPre` 36, `kSgMax` 9.2, `kKMax` 78 and `kFb` 200 (each the measured value with a margin of 1 to 28 %, stated beside it).

### When a note starts the voice

The amplifier's 4.4 ms start delay applies when the voice starts from quiet. The note's gain at its start is `g0 = clamp(declick / (1 + kMainToAmp), 0, 1)`, where `declick` is the
de-clicker's output (the amplifier's gain a moment ago), and the wait is `194 x (1 - g0)` samples: a note on a tail waits only as much as it takes to avoid a dip, and a voice that is still loud enough waits
less or not at all (a slide or a tied note is not a start). The accent's share is never delayed. Measured in a host test (`acid_voice_behaviour_test.cpp`), from the
sample after the restart: a note and its predecessor's off in the same frame, 7 samples (accented: 0); a plain note 1.5 ms after the off, 7; at 10 ms, 62; at 20 ms, 188; at 50 ms, 192;
at 500 ms, 193 (accented at 10 ms, 24; at 500 ms, 192).

The sequencer ends a step's note and starts the next one in the same frame at a full gate, with no slide flag, so the voice sees a restart on a tail (7 samples of wait, 0 on an accent), not
a legato. The wait never leaves a dip below the tail.

An all-notes-off during the wait stops the start at once: the envelopes are zeroed, the wait and the start gain are cleared, and no blip follows.

## 3. The Devil Fish mods

Ranges and behaviour are from Robin Whittle's published Devil Fish manual. No code or material of any instrument was used.

| Control | Behaviour |
|---|---|
| DECAY, ACC DECAY | 30 ms to 3 s. ACC DECAY's stock value is 87 ms |
| AMP DECAY | 16 ms to 4 s, stock 1430 ms, settling at SUSTAIN |
| SUSTAIN (id 21, TONE F7) | The level the amplifier settles at while the gate is held |
| RANGE (id 20, TONE F6) | 303 or FISH. FISH widens ENV MOD to zero and three times the top, lets CUTOFF reach the whole 25 to 4,500 Hz, fills the bass (high-pass at 12 Hz, notch off), and starts and ends a note at once (de-clicker 1500 Hz, no amplifier delay, no gate hold, 8 ms release) |
| ACC SWEEP | OFF, NORMAL or HI RES. HI RES doubles the feedback, so the filter oscillates: from about 350 Hz at full RESO and from 1 kHz at 65 % |
| OVERDRIVE | -24 to +36 dB, about 63 times (Whittle gives 66.6). At the bottom no oscillator reaches the filter, so HI RES plays it alone |
| SOFT ATK | 0.3 to 30 ms on a plain note: that value is the fade-in, with no 4.4 ms delay. OFF is the stock start, with the delay. An accented note keeps the delay |
| FILTER FM, TRACKING, MUFFLER, SWP SPEED, SUB OSC | As on the MODDED pages. SWP SPEED has FAST, NORMAL and SLOW; the sweep's capacitor keeps its charge between accents |

DECAY stays a time constant in milliseconds, with the range and default it had. The 303's own knob, measured on the Din Sync recording, reads about 100, 250, 660 and 1,050 ms at 25, 50, 75
and 100 % of its travel; the voice's DECAY knob is not that curve, and its numbers are time constants.

MODS off gives the stock voice, and every constant above applies the same with MODS off or on: the stock coefficients are the defaults (87 and 1430 ms), so the voice is the same at them in both. RANGE and SUSTAIN follow STOCK in the table and sit last in a snapshot (indices 42 and 43), so a project saved
with 42 values loads and fills them from the sound.

## 4. What was measured

Two public recordings were used first as references: the `a1k0n/303` samples and the Wikimedia Commons TB-303 demo. Their knob positions are unknown, so
these are comparisons of shape, not matches at equal settings. The constants in the section above came from a third, the Din Sync recording, described after the tables. Shares are of the total energy. "Clean" means with the distortion and delay turned off; the new voice boots that way.

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

Cost, an estimate from the cross build's disassembly, not a measurement (v0.5.1; the figures for this change are in the paragraph after the next): the stock SAW loop's usual path is about 335 cycles a sample, before the
control period's share, with two divisions. It runs in four short passes over a control period, because one long loop spilled most of its
registers. The old voice's loop counted 424 to 443 cycles, but that count takes in every path, rare ones as well, so the two are not like for like.
Counted the same way the new function is about 700, which includes code that runs once a control period or only at a wrap. The modded loops cost
more than the stock loop.

Measured on the unit (`docs/hw/2026-11-06-v051.md`), on one saved project at 142 BPM: render max 39.7 % with the mods, growl and wobble off, and 49.8 % with the mods, growl, wobble,
distortion and delay all on, with 0 underruns.

The note start, the cutoff floor, the tanh square and the Din Sync constants (Unreleased) are estimates from the cross build, not measured. The stock SAW loop's passes are the same instructions as above, so its
usual path stays near 335 cycles a sample. The amplifier's start adds a fifth short pass (`start_pass`, about 20 cycles a sample, my count of its 0x158 bytes) only for the 194 samples
after a note starts the voice: under 1 cycle a sample on average at 125 BPM with 16th notes. The cutoff's clamp is two compares a control period. The stock SQR loop's oscillator
pass has 9 more instructions than the SAW's (48 against 39 in the body, the `run<true,false>` and `run<false,false>` disassembly), so SQR costs about 10 cycles a sample more on the usual path, and the shaper's
division (a Pade tanh) runs in the edge, about 4 % of the samples at any pitch: another 1 to 2 cycles a sample on average. The cross build has no double helper (`nm` on the linked image and on the unit's archive
find no `__aeabi_d*`). The unit's render CPU for this build is the check still owed.

### The Din Sync recording

The source is http://dinsync.info, "TB-303 reference recordings for x0xb0x builders" (2010). It is one recording of one unit, at approximate knob positions, and its licence is not
stated, so it was only measured: no audio, data or text of it is in this repository, and nothing was copied. The constants of "Deviations from Open303" are values read from it (the decay
times, the accent's level at four positions, the delay of a note's start, the filter's top, the square's level and the offset of the cutoff law). The figures that rely on it in this page are the ones above: the accent's 0.3, 3.3, 5.5 and 7.3 dB against 0.2 dB below plain and 3.6, 6.2 and 7.3 dB at 25, 50, 75
and 100 %, and the DECAY readings in section 3.

The voice was then rendered at the recording's knob settings, note by note, and compared on all 400 notes. The knobs were mapped to our values first, because the recording gives positions, not
values: CUTOFF 220, 240, 430, 1,250 and 2,290 Hz, ENV MOD 0, .03, .16, .65 and 1, DECAY 45, 100, 250, 660 and 1,050 ms at 0, 25, 50, 75 and 100 %, with one overall gain set on the
sound at the top of CUTOFF. Median level error in dB, by stretch of the note:

| Voice | 8 to 50 ms | 50 to 200 ms | 200 to 600 ms | 600 to 1,250 ms |
|---|---|---|---|---|
| v0.5.1 | 7.8 | 8.3 | 6.4 | 4.8 |
| This voice | 2.2 | 2.4 | 1.9 | 1.7 |

Both rows use the same mapping and gain. These figures say how close the level is on one recording. They are not a percentage of likeness.

What is still apart: at high RESO the resonance at high cutoffs reads about 7 dB weaker than the recording, and with CUTOFF, RESO and ENV MOD high the recording stays brighter, by up to about 2 octaves of spectral
centroid at a note's start. A sweep is measured only up to 9 kHz.

## 5. What is not known

- **One reference, with approximate knobs.** The Din Sync recording is one unit at knob positions that are approximate, with an unstated licence, so it was measured and nothing was
  copied. No percentage of likeness can be given, and what a different 303 would read is not known. The a1k0n and Commons recordings have no knob positions at all.
- **The amplifier's start.** 4.4 ms (194 samples), read from the Din Sync recording: one unit. The voice's de-clicker (about 1.7 ms from 10 to 90 %) makes the rise a little later than the
  delay itself. Whittle's own figure is a typical 4 ms delay and then a 3 ms attack. The manual gives no attack figure for an accented note, and says the clicks of a short attack come only on notes
  that are not accented; the accent's share starts at once here. How it sounds on a unit is not known, and neither is how the rule for a note on a tail (above) sounds.
- **The cutoff's range.** The top, 2,394 Hz, is Open303's and a recording's fit reads 2,286 Hz (within about 5 %). The ladder's own top, 10.4 kHz, is where the coefficient polynomials hold (valid up to pi/4 of the sub-step rate, `rosic_TeeBeeFilter.h:115-117`: about 11 kHz at 88.2 kHz) and where Filter FM's step cap sits; Open303 itself clamps at 20 kHz (its 4,800 Hz is Replicant's knob top) and the Din Sync recording shows the sweep past 9 kHz. The floor of 200 Hz is read roughly from the same recording (its 0 to 25 % positions sat
  near 200 to 250 Hz): one recording of one unit. About a third of the knob's travel at the bottom and a fifth at the top sound the same on RANGE 303.
- **The square.** Open303's `-tanh(...)` shape, with a Pade tanh that is off by .0235 at most. A host measurement of the aliasing against the earlier BLEP square: better at C2 and C5 (about 2.3 and 2.7 dB), the same at C6, and
  2.4 and 3.1 dB worse at C7 and C8. Whether that is audible is for a unit.
- **Accent level.** Fitted by a line to four readings of the Din Sync recording (section 2); between and beyond those four positions it is the line, not a measurement. No published figure in dB was found.
- **The models against GRIT.** The distortion's trim table was fitted on the earlier voice and `src/distortion.cpp` is not touched here: with this voice the four models run hotter than GRIT, by +0.2 to +2.4 dB at the default and up to 3.4 dB on average across Drive, and +1.6 to +9.2 dB at Drive 0 to 0.2 (the host chain test prints them). The trim table should be fitted again.
- **Our own choices.** `kSweepOctaves`, `kSweepSlowTau`, `kFishHpHz`, `kFishDeclickHz`, `kReleaseTau`, `kAmpScale` and `kKnee` have no source: they
  were set by judgement.
- **The bass.** Whittle gives -5 dB at 32 Hz; Open303's chain gives about -10.6 dB. Open303's is kept, because it fits the a1k0n low end better.
- **Not modelled.** The sweep's path into the amplifier: the direct term of the main envelope is Open303's and stands in for it. The stock ladder's
  nonlinearity is a soft bound only.

## 6. Owed on a unit

- By ear, the maintainer: the 4.4 ms note start and a note on a tail, the two ends of CUTOFF on RANGE 303, SQR, the accents and how they climb, the height of the sweep, RANGE, HI RES at the top of RESO, SUSTAIN, and a slide.
- How a project saved by v0.5.0 or v0.5.1 sounds. It loads without an error. With MODS off it plays the new stock sound. With MODS on, its stored AMP DECAY of 1230 ms and ACC DECAY of 200 ms are
  no longer the defaults, so they play as set (1.23 s and 200 ms, against 1.43 s and 87 ms for stock): that project sounds different from the new stock, and from v0.5.1 too, since the voice under its
  knobs has changed. Pressing MODS off, or setting the two to 1430 and 87, gives the new stock.

## 7. Sources

- Robin Whittle, Devil Fish manual: https://www.firstpr.com.au/rwi/dfish/Devil-Fish-Manual.pdf
- Robin Whittle, what is unique about the 303, and its slide: https://www.firstpr.com.au/rwi/dfish/303-unique.html, https://www.firstpr.com.au/rwi/dfish/303-slide.html
- Open303, Robin Schmidt (MIT): https://github.com/RobinSchmidt/Open303
- Tim Stinchcombe on the diode ladder: http://www.timstinchcombe.co.uk/index.php?pge=diode
- Kernmayer's thesis (2020): https://phaidra.ustp.at/api/object/o:4344/download
- Din Sync, "TB-303 reference recordings for x0xb0x builders" (2010): http://dinsync.info (one recording of one unit, approximate knob positions, licence unstated: measured only, nothing copied)
- Recordings: https://github.com/a1k0n/303, https://commons.wikimedia.org/wiki/File:The_Sound_of_Roland_TB-303_for_Wikipedia.ogg

Looked at and not used: JC303 (GPL-3.0, its filter is Open303's and adds no measurement), Nekobee and Nekobi (GPL-2.0, a Chamberlin filter, not
faithful), LMMS Lb302 (GPL-2.0-or-later, three poles, incomplete), Faust's `diodeLadder` (STK-4.3) and Csound's `diode_ladder` (LGPL-2.1). Whittle's
own sound samples are copyright and were not used.
