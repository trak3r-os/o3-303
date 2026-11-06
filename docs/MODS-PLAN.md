# o3-303: the 303 mods on the Sample buttons, GROWL and WOBBLE on their own buttons, HOOKS cancelled (plan)

Update (2026-11-06): the stock voice is no longer Replicant's. The maintainer asked for "el sonido mas identico a la 303 de roland y con las
modificaciones de devil fish", so `docs/VOICE-303.md` replaces this plan's rule "Stock is today": stock is now a TB-303, and the voice's numbers
below (ACC DECAY 180 ms, AMP DECAY 4000 ms, DECAY's range, the HI RES and sweep models) are history. RANGE (id 20) and SUSTAIN (id 21) follow
STOCK in the table and sit on TONE F6 and F7. What this plan says about the buttons, screens and project format still holds.

Status (2026-11-02): S0 to S4 are built, with KS1 (o3-sdk `Context::strip_area()`, and the skin leaving the strip its area). Commits: S0
c345e39, S1 9fdb583, S2 2661735, S3 afbacd9; KS1 and S4 (the docs and the manual) follow them, and the first release, v0.1.0 (a
pre-release), carries them all. The unit gate (section 11) is next and is owed: nothing here has been on a unit. Status (2026-11-01) below: planned, nothing built. Reviewed the same day ("ready with fixes, no blocker"), and the fixes are folded in:
descriptors, FILTER FM's cost, the pin's portability, the MIDI tools in S0, F8 WOB's API, clicks, stock paths, the lane guard and the
slice tests (MD19 to MD23). The maintainer decided on 2026-11-01 that o3-303 will not get HOOKS: "no vamos a meter hooks,
así que podemos quitar esos botones". o3-303 is "exclusivamente una 303". The three Sample buttons then get "las modificaciones de la 303
que se hicieron mainstream ... lo que lleva la behringer 303TD", plus the growl and the wobble ("¿tú pasarías growl y wobble a estos
botones?", which the orchestrator answered yes, with the layout this plan specifies). M4 (HOOKS) is cancelled.

This plan says what each mod does, its range, stock value, cost and place in `units/acid_voice`; the button map, the screens, the
project format and the skin; what goes with HOOKS and with Config DEEP; the slices and the unit gate. The decisions (section 12) were
taken by the maintainer's rule, "todo lo más simple, intuitivo y divertido posible"; nobody was asked.

Sources: the mods' behaviour comes from public descriptions only (Robin Whittle's Devil Fish manual; reviews and product pages of the
Behringer TD-3-MO; section 14). No code, firmware or reverse-engineering material of any instrument was used, and none may be used in any
slice. The code facts are from the tree at a121f0a; the measurements from docs/hw/2026-11-01-su1.md (SU1p) and
docs/hw/2026-11-01-midi.md. "Polyend" and "Tracker" name only the hardware.

## 1. Rules

- The engineering standards of CLAUDE.md and o3-sdk apply unchanged: a unit depends on `dsp` only; no heap, no allocation or blocking
  in the real-time paths; no RTTI, exceptions or `double` (`arm-none-eabi-nm -u` on the cross build, tools from `ARM_GNU_ROOT`); 0
  warnings under `-Werror`; a host test for pure logic and a scenario for every screen or sound change; the CPU measured on the unit
  before a per-block feature is relied on.
- **Append, never insert.** A unit's parameter id is its index in its table. The new voice parameters follow Volume (7). No existing
  range, default, curve or name changes, because lanes, MIDI maps and projects store values against today's descriptors.
- **Stock is today.** With every mod at its stock value, the voice is today's voice bit for bit:
  - every scenario's PNG, WAV and frame digest stays byte-identical;
  - the new golden checksums (S1) stay the same;
  - the Replicant oracle test still passes;
  - a project saved before this plan loads and sounds as before.
- **One lane.** One engineer at a time (firmware-engineer), with a firmware-reviewer after each slice and a build-verifier for the numbers.
  Builds run in `build-mods-<slice>-host` and `-mk66` under `nice -n 10`. Never flash, never commit (the orchestrator batches the
  commits). Other agents' worktrees under the scratchpad are left alone.
- o3-sdk is not edited for this plan. Nothing in it needs a kit change; if one turns up, it goes to o3-sdk under its own rules.

## 2. At a glance

| Button | Today | After this plan |
|---|---|---|
| Sample Loader | HOOKS placeholder | **MODDED**: the 303 circuit mods on two pages, NOTES and TONE (pressed again flips them). Shift + Sample Loader is **STOCK**, from any screen |
| Sample Playback | HOOKS placeholder | **GROWL**: all seven growl controls (four are on INSTRUMENT MODS today, three in Config DEEP) |
| Sample Editor | HOOKS placeholder | **WOBBLE**: all five wobble settings plus F8 WOB, the on switch (four are on INSTRUMENT MODS today, one in Config DEEP) |
| Instrument Parameters | INSTRUMENT VOICE, FX, MODS | INSTRUMENT **VOICE, FX** (MODS goes) |
| Config | pages 303 and DEEP | page 303 only. DLY MODE moves to MASTER F5. Keys 20-24 are retired and never reused |
| Sample Recorder | RECORD | unchanged |

The TD-3-MO's manual ACCENT button is not added: PERFORM already has ACC ALL, as a pad and as the default Instr colour action.

## 3. The mods (units/acid_voice)

### 3.1 The parameters

Ids 8..15 follow the order docs/M3-PLAN.md reserved ("Acc Decay, Filter Track, Slide Time, Soft Attack, Sub Osc, Filter FM, Sweep,
Muffler; each default reproduces Replicant"). Ids 16..19 are appended after them.

| Id | Name (≤ 9 chars) | Values | Curve | Stock (= today) | Page | Where in acid_voice | Per-sample cost |
|---|---|---|---|---|---|---|---|
| 8 | ACC DECAY | 30..3000 ms | Log | 180 ms (`kAccentTau`) | NOTES F3 | `accent_coeff_` and `accent_coeff_n_` from `set()`; were `prepare()` constants | none |
| 9 | TRACKING | 0..150 % | Linear | 0 % | TONE F1 | `control()`: an offset to the cutoff in octaves | none (control rate) |
| 10 | SLIDE | 60..360 ms | Log | 60 ms (`kSlideTau`) | NOTES F5 | `slide_coeff_` from `set()` | none |
| 11 | SOFT ATK | OFF, 0.3, 1, 3, 10, 30 ms | Enum (six steps) | OFF | NOTES F1 | `note_on()` starts a ramp; the modded loop applies it | about 5 while a ramp runs |
| 12 | SUB OSC | OFF, LOW, MID, HIGH | Enum | OFF | TONE F5 | the modded loop: a square one octave down, added before the ladder | about 15 |
| 13 | FILTER FM | 0..100 % | Linear | 0 % | TONE F2 | the modded loop: the last output sample scales the ladder's g (linear, MD20) | about 16 |
| 14 | ACC SWEEP | OFF, NORMAL, HI RES | Enum | NORMAL | NOTES F6 | `control()` (OFF), and the feedback gain plus the modded loop (HI RES) | none, or the loop's overhead |
| 15 | MUFFLER | OFF, SOFT, HARD | Enum | OFF | TONE F4 | the modded loop: after the VCA, before the volume | about 26 |
| 16 | SWP SPEED | FAST, NORMAL, SLOW | Enum | NORMAL | NOTES F7 | `control()`: the sweep's follower | none (control rate) |
| 17 | OVERDRIVE | -24..+36 dB, step 0.5 | Linear in dB | 0.0 dB | TONE F3 | the modded loop: the oscillator's level into the ladder | about 2 |
| 18 | AMP DECAY | 16..4000 ms | Log | 4000 ms (`kHoldTau`) | NOTES F4 | `hold_coeff_` from `set()`; was a `prepare()` constant | none |
| 19 | STOCK | OFF, ON | Enum | OFF | NOTES and TONE F8 (an action key, through `Item::Stock`) | `set()`: every mod uses its stock value internally while ON | none |

**The descriptor rows** (`ParamDesc` fields in o3-sdk `kit/core/include/o3sdk/core/param.h`: name, unit text, names, knots, min, max,
def, detents, coarse, list count, unit, curve). Each row was checked against how `ParamUnit` prints. `NormPct` prints the position in
the range, so it is used only for 0..1 values. `Int` rounds, so it never carries a fraction that must show.

| Id | Row | Prints |
|---|---|---|
| 8 | `"ACC DECAY", "ms", -, -, 30, 3000, 180, 100, 10, 0, Int, Log` | "180" ms, as DECAY |
| 9 | `"TRACKING", "%", -, -, 0, 150, 0, 150, 10, 0, Int, Linear` | "0".."150" %; normal tracking reads 100 (not `NormPct`, which would print 66.7) |
| 10 | `"SLIDE", "ms", -, -, 60, 360, 60, 60, 10, 0, Int, Log` | "60".."360" ms |
| 11 | `"SOFT ATK", "", kSoftAtkNames, -, 0, 5, 0, 5, 1, 6, Enum, Linear` | OFF, 0.3 MS, 1 MS, 3 MS, 10 MS, 30 MS. The voice reads the time from a constexpr table `kSoftAtkMs[6] = {0, 0.3, 1, 3, 10, 30}`, and index 0 never starts a ramp (stock exact) |
| 12 | `"SUB OSC", "", kSubNames, -, 0, 3, 0, 3, 1, 4, Enum, Linear` | OFF, LOW, MID, HIGH |
| 13 | `"FILTER FM", "%", -, -, 0, 1, 0, 100, 10, 0, NormPct, Linear` + `kSmooth` | "0".."100" %, as ENV MOD |
| 14 | `"ACC SWEEP", "", kSweepNames, -, 0, 2, 1, 2, 1, 3, Enum, Linear` | OFF, NORMAL, HI RES |
| 15 | `"MUFFLER", "", kMufflerNames, -, 0, 2, 0, 2, 1, 3, Enum, Linear` | OFF, SOFT, HARD |
| 16 | `"SWP SPEED", "", kSpeedNames, -, 0, 2, 1, 2, 1, 3, Enum, Linear` | FAST, NORMAL, SLOW |
| 17 | `"OVERDRIVE", "dB", -, -, -24, 36, 0, 120, 4, 0, Fixed1, Linear` + `kSmooth` | "-24.0".."36.0" dB in 0.5 dB detents, as VOLUME |
| 18 | `"AMP DECAY", "ms", -, -, 16, 4000, 4000, 100, 10, 0, Int, Log` | "16".."4000" ms |
| 19 | `"STOCK", "", kOffOnNames, -, 0, 1, 0, 1, 1, 2, Enum, Linear` | OFF, ON |

- Every name fits the knob strip's nine characters and `kParamNameChars` (12).
- Every Enum name fits the skin's six-character cut (`kCaptionChars`).
- Each Log row has a positive min.
- `param_desc_valid()` holds every row (S1's test).

STOCK is a voice parameter, not something the app writes for each mod, for three reasons:
- the switch is atomic in the audio thread;
- the stored mod values are never touched, so lanes, locks, snapshots and MIDI CCs keep writing them while STOCK is on, and they are
  heard again at STOCK OFF;
- a save made during STOCK writes the real values (section 5.11).

STOCK is excluded everywhere else: `carried()` returns false for it (not saved), it is not a snapshot target, and it has no lane, no
lock and no MIDI learn (`Item::Stock` is an app item).

### 3.2 Each mod: behaviour (public sources), model, guards

**ACC DECAY.**
- Source: the Devil Fish (DF) splits the filter envelope's decay. Non-accented notes use a Normal Decay pot and accented notes an Accent
  Decay pot, each 30 ms to 3 s. On the TB-303 accented notes were fixed at about 200 ms ([1] p.4). The TD-3-MO has both as knobs
  ([3], [4]).
- Model: SOUND's DECAY (id 3) stays as it is (200..2500 ms, the non-accented decay); it is the normal decay, and its range is not
  widened (rule 2). ACC DECAY sets the time constant used today by `accent_coeff_`, which drives both the accented note's filter
  envelope (`vcf_` while `has_accent_`) and the accent pulse `acc_` (in the circuit the sweep is derived from the envelope).
- Stock: 180 ms is `kAccentTau` (0.18 s).

**TRACKING.**
- Source: filter tracking "from 0, through normal tracking to over-tracking" ([1] p.5). The filter follows the note's pitch.
- Model: at control rate, `cutoff_l2 += track * log2(pitch / 65.41 Hz)` with `core::log2_fast`. The reference is C2, the factory line's
  root, so C2 notes are unchanged at any setting. `pitch` is the gliding note, not TUNE. 100 % is one octave of cutoff per octave of
  note, 150 % is over-tracking. The result goes through today's clamps (`kMinCutoff`, `kMaxCutoff`, `kGMax`).
- Skipped by a branch at 0 %. The code never adds a zero term, which keeps stock exact.

**SLIDE.**
- Source: the DF's slide time "varies the time from 60 to 360 ms" from the normal 60 ms ([1] p.4, p.16: six times normal). The TD-3-MO
  has it as a knob ([4]).
- Model: the time constant of today's one-pole pitch glide (`slide_coeff_ = 1 - exp(-1/(fs*tau))`).
- Stock: 60 ms is `kSlideTau`, the bottom of the range.

**SOFT ATK.**
- Source: "the attack time of non-accented notes between 0.3 ms and 30 ms". The TB-303 had about 4 ms of delay and then a 3 ms attack
  ([1] p.4).
- acid_voice attacks instantly (Replicant's model), so stock is OFF, below 0.3 ms. The six steps (the DF's ends and four between)
  are an Enum: an `Int` row could not print 0.3, and an OFF needs a name.
- Model: an RC-like charge. A non-legato, non-accented note sets `att_ = 0`; each sample adds `att_ += (1 - att_) * att_c`, and the VCA
  is multiplied by `att_`. Accented notes and legato or slide notes keep the instant attack (`att_ = 1`). Above `1 - 1e-4` the ramp
  sets `att_ = 1`, and the block after that runs the stock loop again.

**SUB OSC.**
- Not a DF mod; the TD-3-MO adds it ([4] "switchable sub-oscillator", [5] "a sub-oscillator alongside the main square/sawtooth
  oscillator").
- The public sources do not agree on its controls. One describes a "three-octave sub-oscillator" with low, mid and high and no level
  control ([3]). The quick start guide's excerpt has an on/off switch and a SUB OSC LEVEL selector (low, medium, high) ([6], not fetched).
- Decided (MD7): one control, OFF, LOW, MID, HIGH, as the level of a square one octave below the note. Its edges come from the main
  oscillator's phase wraps (a flip-flop), with a polyBLEP at each toggle from the same fractional position. It is added before the
  ladder, so the filter and the envelopes shape it like the main wave. Levels 0.25, 0.5 and 0.8 of the main wave's peak are starting
  values, tuned by ear at the unit gate. Two octaves down is not offered: on the 303's lowest notes it is 8 to 16 Hz, below hearing.
- The level is a ramp of about 5 ms (3.3 point 8). A change of position, or STOCK, glides the level rather than stepping it, and the
  flip-flop keeps toggling while the level is above 0.

**FILTER FM.**
- Source: "frequency-modulates the filter frequency with the audio output of the VCA", which includes the Muffler ([1] pp.5-6, p.19).
  Reviews call it "a crunchy bit of chaos" ([3]).
- Model (MD20): the previous output sample `y[n-1]` is taken after the VCA and the muffler, before VOLUME, so VOLUME does not change the
  depth. It goes through a DC blocker (about 5 Hz). Then each sample, in the linear domain:
  `g = clamp(g_ctl * (1 + k * y), kGFmMin, kGMax)`, with `k = 2 * fm` and `kGFmMin` the ladder's g at `kMinCutoff`. That is two
  multiply-adds and a clamp, all inline.
- **No `core::exp2_fast` in the per-sample loop.** It is out of line (o3-sdk `fast_math.h`) with a degree-7 series. A call there would
  force spills (more than 16 live floats; AAPCS keeps s16-s31 callee-saved), and it would sit on the serial `y[n-1] -> g -> ladder` chain.
  The review estimated 60 to 90 cycles a sample for it. The analog DF modulates exponentially; at moderate depths the linear form sounds
  alike, and the clamp keeps the deep end stable.
- The depth is a starting value, tuned at the unit gate. The amount is smoothed at control rate, so a turn does not click.

**ACC SWEEP.**
- Source: the DF's Accent Sweep switch ([1] p.4, p.18) has three positions:
  - 2: sweep on, normal resonance ("acidee");
  - 1: sweep on, high resonance ("whistly");
  - 0: no sweep, high resonance.
  Hi Resonance doubles the feedback ([1] p.5). The TD-3-MO has "accent sweep ... switchable in 3 steps" ([4]), described as "from off to
  lots" ([3]).
- Decided (MD8): OFF, NORMAL, HI RES. NORMAL is today. HI RES is the DF's "whistly" position: the sweep plus `fb_k_` doubled. OFF
  removes the accent's cutoff sweep (the `accent_oct_ * acc_` term) and keeps the accent's level lift and today's resonance. The DF's
  position 0 (no sweep but high resonance) is the one combination left out; OFF with RESO turned up comes close.
- HI RES runs the modded loop, where the arguments of the ladder's input and feedback `tanh_pade` are clamped to ±3. `tanh_pade` rises
  again with slope 1/9 past 3, so doubled feedback would not saturate cleanly without the clamp. The stock loop keeps today's
  unclamped code.

**MUFFLER.**
- Source: a three-position switch on the VCA output with "two types of muted clipping". One "softens the loudest extremes". The other
  gives a "moderate level square wave clipping buzz, whilst allowing the bass to pass largely unaffected" ([1] p.6, p.19). The TD-3-MO
  has "Muffler (VCA soft clipping) switchable in 2 steps" ([4]).
- Model, after the VCA and before VOLUME:
  - SOFT clips the peaks of the whole signal softly: `T * tanh_pade(clamp(y / T, ±3))` with T about 0.7.
  - HARD splits the signal with a one-pole low-pass at about 150 Hz. The band above it is clipped at about -14 dB, and the bass passes.
- It runs as a wet/dry mix that ramps over about 5 ms (3.3 point 8). Turning it on from 0 resets the low-pass state to the current
  sample, so a state left stale while it was bypassed is never heard. A change between SOFT and HARD dips the mix to 0 and back with the
  new type. STOCK does the same as OFF.
- The thresholds are starting values, tuned by ear.

**SWP SPEED.**
- Source: three modes ([1] p.5):
  - Fast: "first accent causes a strong positive output, but subsequent accents produce a smaller output";
  - Normal: the TB-303, where charge kept in C13 makes later accents go higher;
  - Slow: "much longer time-constant ... takes longer to rise, but it can rise twice as high. It also takes longer to cool down".
- **Today's voice has no C13 build-up.** Replicant re-fires the accent pulse at full level on each accented note. NORMAL is today
  (rule 3), so o3-303's NORMAL is Replicant's model, not the hardware's (MD9).
- Model, at control rate, in the cutoff term only (the accent's level lift stays per sample, as today):
  - NORMAL keeps today's expression.
  - SLOW: `swp += (2 * acc - swp) * c_slow`, τ about 120 ms, so it accumulates over close accents and can reach twice as high.
  - FAST: `swp = max(0, acc - m)` with `m += (acc - m) * c_fast`, τ about 400 ms, so a second accent soon after the first is smaller.
- The constants are tuned by ear.

**OVERDRIVE.**
- Source: the DF sets "the level of Oscillator signal fed to the filter ... between none, through normal to heavy overdrive (66.6 times
  normal)" ([1] p.4). On the TD-3-MO it replaces the TD-3's distortion, and the review calls it "completely different" ([3]).
- Decided (MD6): a voice stage, the oscillator's level into the ladder, times `in_drive_`; the ladder's own saturation is the overdrive.
  -24 dB is close to the DF's "none", +36 dB is its 66.6 times (+36.5 dB); smoothed at control rate. It is not a distortion model:
  units/distortion stays on FX as the post-voice stage (GRIT ... FOLD). The two do different things and stack.
- Stock: 0.0 dB, where the drive multiplier is not applied at all.

**AMP DECAY.**
- Source: on the DF and the TD-3-MO the DECAY knob controls the volume envelope ([1] p.4, [3]). The TB-303's was fixed at about 3 to
  4 s.
- o3-303 keeps DECAY on the filter envelope (rule 2), so the volume envelope's decay while the gate is held is this new knob, which
  completes the TD-3-MO's three decays. Model: the time constant of `hold_coeff_`; the release (`kReleaseTau`, 25 ms) is unchanged.
- Stock: 4000 ms is `kHoldTau`, the top of the range. The DF's sustain half of the pot is not offered.

### 3.3 How stock stays exact

1. **The stock loop is today's loop.** The per-sample function becomes `run<Square, Modded>`, and the added stages sit in
   `if constexpr (Modded)` blocks.
   - The block uses `Modded = true` only while it needs it: OVERDRIVE ≠ 0 dB, SUB OSC ≠ OFF, FILTER FM > 0, MUFFLER ≠ OFF, any of
     their ramps or smoothers still moving (point 8), a soft-attack ramp running, or HI RES.
   - The decision is taken once per run of `dsp::for_each_run`.
2. **Coefficients take the constant path at stock.** The coefficient that `prepare()` computes today from the constant is kept and
   assigned; the code does not recompute `60 * 0.001f`, which can differ by an ulp.
   - SLIDE and AMP DECAY have their stock at a range end, and the engine clamps there exactly. So the test is `v <= 60.0f` and
     `v >= 4000.0f`.
   - ACC DECAY's 180 ms is mid-range, so it is exact only at the default, `v == 180.0f`: a fresh project, an old project, RESET, or
     STOCK. A value turned back to 180 by hand through the Log curve may land a hair off; it is then computed through `std::exp`,
     within an ulp or so of the constant, which is inaudible.
3. **Control-rate terms are branched out at stock, never multiplied by zero:** TRACKING 0 %, ACC SWEEP NORMAL, SWP SPEED NORMAL.
4. **Smoothers and ramps land exactly on their target** (they snap below 1e-6), so a mod turned back to stock returns to the stock loop.
5. **Non-finite input reads as stock.** Each mod's `set()` maps NaN or ±inf to its stock value, not to the low end that `clamp_param`
   gives today's parameters, so a corrupt project value gives the plain 303.
6. **Pinned.** S1 first adds golden checksums from today's code, before any mod exists. They follow `freeze_modes_test.cpp`: FNV-1a
   over 200,000 frames of a gesture with notes, accents, slides, ties, every SOUND parameter moved, a note-off and an all-notes-off, for
   SAW and SQR. Today the oracle test only bounds the error (SNR), so nothing pins the voice bit for bit until then.
   - **The pin is a host pin, and its test says so.** `prepare()` and `set()` call libm's `std::exp`, so the bits depend on the host's
     glibc. The test records the glibc version (`gnu_get_libc_version()`) beside the hash and checks only on an x86-64 host with that
     version; anywhere else it reports, by name, that it did not check, and why.
   - Build type (MD19): it checks in Debug and Release alike. x86-64 GCC emits no FMA without `-march`, and float SSE arithmetic is the
     same at -O0 and -O2. This widens the review's "Release only", because `build-host` is Debug by default (o3-sdk
     `cmake/O3sdkPlatform.cmake` 56-61), so a Release-only pin would never run in a slice.
   - The MK66 is not covered by the pin. The tree builds as gnu++17 (`CMAKE_CXX_EXTENSIONS ON`, O3sdkPlatform.cmake 67), so GCC
     contracts `a * b + c` into VFMA there (`-ffp-contract=fast`; only three ported audio files get `-ffp-contract=off`,
     platform/mk66/audio/CMakeLists.txt 21). **S1 therefore requires an objdump diff on the cross build:** the stock instantiations
     `run<false, false>` and `run<true, false>` against today's `run<false>` and `run<true>`, identical instruction for instruction
     (addresses and symbol names aside).
7. **STOCK keeps raw copies.** The voice stores each mod's raw value as `set()` received it. A STOCK toggle recomputes every derived
   coefficient and target from either the stock values or those copies: three float `std::exp` and their `power_n` once, in the audio
   thread, about 1 to 2 thousand cycles at the toggle.
8. **No click at a toggle.** The sub's level, the muffler's wet/dry mix, OVERDRIVE's gain and FILTER FM's amount all move along ramps of
   about 5 ms (a per-sample step, set at control rate), and a mod stays in the modded loop until its ramp lands.
   - States left stale by a bypass are re-seeded when their ramp leaves 0 and run until it lands at 0 again: the muffler's low-pass
     takes the current sample, the FM DC blocker starts from 0, and the sub's flip-flop runs while its level is above 0. They do not run
     in the stock loop, which stays today's.
   - SOFT ATK only starts at a note-on, so a toggle mid-note never starts a ramp; a toggle during one ends it (the attack's own edge).
   - HI RES and the control-rate mods change as RESO and CUTOFF do today: per block, with the cutoff stepped across the control period.
   - A fresh voice has every ramp at its target, so "STOCK ON from the start is bit for bit stock" holds.
9. **Back to stock is stock arithmetic at once, not stock bits at once.** When the last mod leaves (a value turned back, or STOCK), the
   next control period runs the stock loop. The voice state (the ladder, the feedback high-pass, the envelopes, the pitch glide) still
   carries what the modded run left in it, because the modded loop is not bit-identical to the stock loop in the last bits and a mod
   changes the state while it runs. The bits equal a never-modded voice's only after that state has decayed: measured, a note 1,000
   frames after the return differs by up to 0.04, and a note 18,000 frames after (about 0.4 s of silence) is bit-identical. Where the
   idle ladder rings on its own feedback (RESO high with a very low drive, OVERDRIVE at -24 dB) the state never converges to the same
   phase, and such a case is compared by level, not by bits.
10. **A mod set live reaches the loop that computes it.** Every mod whose stage lives in the modded loop (SUB OSC, FILTER FM, MUFFLER,
    OVERDRIVE, HI RES) sets `ramp_pending_` when its value moves, so the next `control()` takes the modded loop even with no note and no
    ramp going; the control-rate mods and SOFT ATK (chosen at a note-on) need no such flag.

### 3.4 CPU (Cortex-M4F at 180 MHz, a 64-sample block at 44.1 kHz = 261,224 cycles; 1 % = 2,612 cycles = 41 cycles a sample)

Today the voice is estimated at about 445 (SAW) and 465 (SQR) cycles a sample, 28.5k to 29.8k cycles a block, 10.9 to 11.4 %
(acid_voice.h). On SU1p the whole render peaked at 48.1 % (A10) and 49.4 % while recording (B6). The 2,000-note MIDI storm peaked at
67 %, with 6 blocks over budget and 0 underruns, against the stress criterion of at most 75 %.

| What | Cycles a sample (estimate) | A block | % of the block |
|---|---|---|---|
| Every mod at stock (the stock loop; coefficients and control-rate branches) | 0 | under 100 | 0.0 |
| ACC DECAY, SLIDE, AMP DECAY (coefficients, computed in `set()` with a float `std::exp`) | 0 | 0 | 0.0 |
| TRACKING, SWP SPEED, ACC SWEEP OFF (once every 16 samples, outside the per-sample loop: an out-of-line `log2_fast`, two one-poles) | about 5 | about 320 | 0.12 |
| The modded loop's own overhead (selection, four clamps a sub-step, HI RES, the ramps' steps) | about 13 | about 830 | 0.32 |
| OVERDRIVE | about 2 | about 130 | 0.05 |
| SOFT ATK, while a ramp runs | about 5 | about 320 | 0.12 |
| SUB OSC | about 15 | about 960 | 0.37 |
| FILTER FM (DC blocker, two multiply-adds, clamp; linear, MD20) | about 16 | about 1,020 | 0.39 |
| MUFFLER (split, `tanh_pade` with one 14-cycle divide, the wet/dry mix) | about 26 | about 1,660 | 0.64 |
| **Every mod at its heaviest** (with stalls rounded up) | **about 95** | **about 6,100** | **about 2.3** |

The table had FILTER FM at 24 cycles through `exp2_fast`. The review showed that call costs 60 to 90 cycles in the loop (1.5 to 2 points
alone), which made it the riskiest mod. The linear form replaces it; the total stays about 2.3 points because the ramps (point 8) and
the per-control-period `log2_fast` are now counted.

**These numbers are made measurable in S1 by disassembly**, before the unit gate:
- the build-verifier disassembles `run<false, true>` and `run<true, true>` from the cross build (`arm-none-eabi-objdump -d`, tools from
  `ARM_GNU_ROOT`) and counts the loop body's instructions per stage;
- the loop body must hold no `bl` (no out-of-line call), and no more stack loads and stores than the stock instantiation's loop holds;
- the per-sample cycle count from that listing is reported per mod against this table.

Expected on the unit: the render peak from about 49 % to about 51.5 %, the storm from 67 % to about 69.5 % (under 75 %), the voice
about 13.2 to 13.7 %. These are estimates. **The measurement the unit owes** (section 11), before any mod is relied on: render max and
mean over 2 minutes on the same pattern for stock, each mod alone at its heaviest, and all together; and the storm with every mod on.
Pass: everything on costs at most 4 points over stock, and the storm stays at or under 75 %.

The image grows by the modded instantiations, about 3 to 4 KB of text. The image's ctests (`o3_303_image`, `o3_303_flashconfig`,
`o3_303_ramfunc`) stay green.

### 3.5 Guards

- Float only, with `f` literals. `std::exp` appears only in `set()` and at a STOCK toggle, on a float (the float overload).
  `core::log2_fast` runs only at control rate, and no out-of-line call sits in the per-sample loop.
- No new buffers: about 20 floats of state with the ramps and the raw copies.
- Every new state is flushed to 0 below `kTiny` (1e-9) at control rate, as the envelopes are: `att_`, the sub's BLEP memory, the FM DC
  blocker, the muffler's low-pass, `swp`, `m`.
- The ladder's states keep today's `kStateLimit` clamp, and the modded loop also clamps the `tanh_pade` arguments.
- **The 576-corner sweep is not a test today.** It is a one-off measurement recorded in the comment at acid_voice.cpp 508-512. S1 writes
  it as a test: the 576 corners as listed there, then again with every mod at each extreme. Every sample must be finite, every state
  inside the limit, and the peak before VOLUME at most 2.0.

### 3.6 Licence and the port

- `acid_voice.cpp` ports the DSP of Replicant 303 (DanimanX, MIT, commit d5369be) and carries its MIT notice under the file's
  GPL-3.0-or-later header. `THIRD_PARTY.md` lists it.
- The mods are new code written in o3-303 from public descriptions. The MIT notice stays as it is, and S4 adds a line to the header
  comment's "what is the same and different" list.
- **No ported-identical rule applies.** o3-sdk's rule ("code ported from track3r-os stays identical") covers kit code from track3r-os.
  The Replicant port is already restructured (control rate, two sub-steps, flush), and its fidelity is guarded by the oracle test. That
  test must still pass with every mod at stock. The golden checksums pin it exactly on the host, and the objdump diff covers the MK66
  (3.3 point 6).

## 4. The button map (UX-303 2.1, 2.3 and 2.6 rewritten)

### 4.1 Screen buttons (2.1, the rows that change)

| Button | Opens | Pressed again | Held | Shift + button |
|---|---|---|---|---|
| Instrument Parameters (`Params`) | **INSTRUMENT VOICE** | FX, and back | — | — |
| Sample Loader (`SampleLoad`) | **MODDED NOTES** | MODDED TONE, and back | — | **STOCK** on / off from any screen, with no screen change; toast "STOCK ON" / "STOCK OFF" |
| Sample Playback (`SamplePlay`) | **GROWL** | nothing (one page) | — | as alone |
| Sample Editor (`SampleEdit`) | **WOBBLE** | nothing (one page) | — | as alone |
| Config | the kit's system Config with the app's page "303" (5.12) | the kit's | — | the kit's |

**Why Shift + Sample Loader for STOCK (MD3):**
- The app already has the pattern "Shift + a screen button runs that screen's main action from anywhere": Shift + Master taps the tempo,
  Shift + File saves. STOCK is MODDED's main action.
- An A/B is made while listening, on HOME or PERFORM. This gesture reaches it there without leaving the screen.
- It takes no pad, F key or colour button from any screen. PERFORM's row 1 is full (eleven actions and ALL OFF).
- The printed button is where the mods live, so it is easy to remember: "the mods' button, with Shift, takes them out".
- A toggle, not a hold: an A/B is listened to for bars, not for a held breath.

### 4.2 The colour buttons (2.3, the per-screen table)

| Screen | Note | Instr | Vol | Fx |
|---|---|---|---|---|
| HOME, PATTERN SETUP, INSTRUMENT, MODDED, GROWL, WOBBLE | NOTE row | GATE row | PLAYS row | LOCK row |
| FILE, MASTER, RECORD | nothing | | | |

The PERFORM and SONG rows are unchanged. The HOOKS row goes.

### 4.3 Global routing (2.6, rewritten)

The app sees every input first. It takes:
- Play and Shift + Play;
- the screen buttons, which open or page their screen as in 2.1;
- Master: a tap opens MASTER at the release, hold + turn nudges, Shift taps the tempo;
- Shift + File (SAVE), Shift + Rec (audio take) and **Shift + Sample Loader (STOCK)**.

Sample Playback and Sample Editor open GROWL and WOBBLE with or without Shift. Everything else goes to the current screen. The Config
button is the kit's; the app adds its page 303 (5.12). Shift + Config is the kit's HELP.

In `o3_303_app.cpp` the HOOKS block becomes:

```cpp
if (in.press(Button::SampleLoad)) {
  if (ctx.panel().shift()) { toggle_stock(ctx); return true; }        // STOCK: no screen change
  if (ctx.screen() == &modded_) { modded_.next_page(); } else { modded_.open_first_page(); ctx.show(modded_); }
  return true;
}
if (in.press(Button::SamplePlay)) { if (ctx.screen() != &growl_) { growl_.open_first_page(); ctx.show(growl_); } return true; }
if (in.press(Button::SampleEdit)) { if (ctx.screen() != &wobble_) { wobble_.open_first_page(); ctx.show(wobble_); } return true; }
```

A held live action ends at a screen change, as on any screen change [D4]; a latched one stays on.

## 5. The screens

### 5.1 One screen class, four instances

MODDED, GROWL and WOBBLE behave exactly as INSTRUMENT does:
- the page's eight F keys drawn large, with read-out sections and the two-line drawer of the page of 16 steps;
- the home map's pads, arrows, Copy, Insert, Delete and Rec;
- the colour buttons are the drawer rows;
- the Shift row is MIDI learn;
- the encoder is the latched key or a row;
- classic and skinned drawings from one view.

So `InstrumentScreen` gets a page group at `init()`, and the app holds four instances: `instrument_`, `modded_`, `growl_`, `wobble_`.
One code path serves learn, LOCK, motion record, HELP and both drawings, and "pressed again" is `ctx.screen() == &modded_`.

```cpp
// instrument_screen.h: what an instance shows. Constexpr data in instrument_screen.cpp, one per button.
struct InstrumentGroup {
  unsigned count;                      // pages: 2, 2, 1, 1
  const char* const* titles;           // "INSTRUMENT VOICE", "INSTRUMENT FX" | "MODDED NOTES", "MODDED TONE" | "GROWL" | "WOBBLE"
  const char* const* tabs;             // "VOICE" "FX" | "NOTES" "TONE" | "GROWL" | "WOBBLE"
  const Page* const* pages;            // nullptr: the page is the SOUND page on Rig::sound_keys (INSTRUMENT VOICE only, D3)
  const unsigned* shown;               // the read-out sections of each page (5.1.1)
  const help_texts::Id* help;          // one Text a page
};
```

`kInstrumentPageCount`, `InstrumentPage` and `kInstMods` go; `own_` holds a `PageKeys` for each page that has its own table. Each page
keeps its own latch (FX and MODS do so today), and VOICE keeps the shared SOUND latch [D3]. Leaving a screen or changing its page cancels
that screen's armed learn, as today.

#### 5.1.1 Read-out sections (four rows each)

| Section | Rows |
|---|---|
| VOICE | CUTOFF, RESO, ENV MOD, DECAY (today's) |
| DISTORTION | MODEL, DRIVE, TONE, MIX (today's) |
| DELAY | TIME, FB, SEND, TONE (today's) |
| MODDED | ACC DEC, SLIDE, SUB, STOCK (OFF / ON) |
| NOTES | SOFT ATK, ACC DEC, SLIDE, SWEEP |
| TONE | TRACK, FM, DRIVE (the OVERDRIVE), MUFFLER |
| GROWL | VOWEL, FMT Q, CMB MIX, GRL MIX |
| WOBBLE | RATE, DEPTH, TGT, WOB (ON / OFF; today's "WOB" row) |

Each page shows VOICE, the anchor of the sound, plus its closest neighbour. Pages with two boxes leave the third empty, as FX does today.

| Page | Sections |
|---|---|
| INSTRUMENT VOICE | DISTORTION, DELAY, MODDED |
| INSTRUMENT FX | VOICE, MODDED |
| MODDED NOTES | VOICE, TONE |
| MODDED TONE | VOICE, NOTES |
| GROWL | VOICE, WOBBLE |
| WOBBLE | VOICE, GROWL |

In S0, before MODDED exists, VOICE shows DISTORTION, DELAY, GROWL, and FX shows VOICE, WOBBLE.

### 5.2 INSTRUMENT (Params): VOICE and FX

These are today's two pages, unchanged. Params cycles VOICE and FX. HELP's Params text becomes "NEXT PAGE: VOICE, FX".

### 5.3 MODDED (Sample Loader): NOTES and TONE

```
+------------------------------------------------------------------------------------------------+
| MODDED NOTES                              125.0 BPM   [> PLAY]  STOCK                           |
+------------------------------------------------------------------------------------------------+
| ENC > F3 ACC DECAY   KEYS  OCT 2-3                                                              |
|  (the eight keys of the page, drawn large: name, value, bar)                                   |
|  +-- VOICE -------------------------+ +-- TONE ------------------------------+                  |
|  | CUTOFF 350Hz RESO 65% ENV MOD 60%| | TRACK 0%  FM 0%  DRIVE 0.0dB  MUFF OFF|                  |
|  | DECAY 750ms                      | |                                      |                  |
|  +----------------------------------+ +--------------------------------------+                  |
|  NOTE  C2  C2  C3  --  D#2 C2  G2  C2  C2  C3  C2  --  C2  G2  C2  C2                           |
|  GATE  .   A   .   .   AS  .   A   .   .   S   A   .   .   .   .   .                            |
| PADS  as HOME                                                                                  |
| [F1 SOFT ATK][F2 DECAY][F3 ACC DECAY][F4 AMP DECAY][F5 SLIDE][F6 ACC SWEEP][F7 SWP SPEED][F8 STOCK]|
+------------------------------------------------------------------------------------------------+
| Shift: LEARN LEARN LEARN LEARN LEARN LEARN LEARN -                                               |
+------------------------------------------------------------------------------------------------+
```

- **NOTES** (how each note moves):
  - F1 SOFT ATK;
  - F2 DECAY, the SOUND knob itself (`Item::Voice`, id 3), placed beside ACC DECAY so the TD-3-MO's normal and accent decays sit side by
    side;
  - F3 ACC DECAY, F4 AMP DECAY, F5 SLIDE, F6 ACC SWEEP, F7 SWP SPEED;
  - F8 STOCK.
- **TONE** (what the filter is fed and gives): F1 TRACKING, F2 FILTER FM, F3 OVERDRIVE, F4 MUFFLER, F5 SUB OSC, F6 and F7 blank,
  F8 STOCK.
- **F8 STOCK** is an action key: it runs at the press, toggles STOCK, and is lit while STOCK is on. It is the same switch as Shift +
  Sample Loader, placed where the mods are seen. Its Shift label is "-".
- **A turn of a mod while STOCK is on ends STOCK**, with the toast "STOCK OFF": a player who reaches for a mod wants to hear it.
  - DECAY (F2) is not a mod and does not end STOCK.
  - Lanes, locks, snapshots and MIDI CCs write the stored values and leave STOCK as it is.
- **Encoder:** the latched key or a drawer row; Shift + turn is coarse; a switch steps one position per detent. **Pads:** the home map.
  **Colour buttons:** the drawer rows.

### 5.4 GROWL (Sample Playback)

- **F keys:** F1 VOWEL, F2 FMT Q, F3 FMT MIX, F4 CMB TUNE, F5 CMB FB, F6 CMB MIX, F7 GRL MIX, F8 blank. All are `Item::Growl` entries,
  in signal order: the formant, the comb, then the growl's mix. Today four are on INSTRUMENT MODS and three in Config DEEP.
- **Shift row:** LEARN on F1..F7 (unit parameters).
- **Pads:** the home map, the same as INSTRUMENT, so the line keeps playing and editing while the growl is set (MD4).
  - Presets and ladders on the pads are not built. A growl ladder would give the pads a second meaning on one screen, and the player
    would have to learn which screen's pads play.
  - PERFORM's row 3 ladder already follows the SOUND latch.
- **Colour buttons and encoder:** as on INSTRUMENT.

### 5.5 WOBBLE (Sample Editor)

- **F keys:** F1 RATE, F2 SHAPE, F3 DEPTH, F4 TARGET, F5 RESET, F6 and F7 blank, F8 WOB.
  - F1..F5 are `Item::Wobble` (`WobbleField` Rate, Shape, Depth, Target, Reset). RESET moves here from Config DEEP.
  - F4's label is TARGET: the big knob row draws nine characters and the skin's switch names cut at six (WOB TGT was a strip-width
    workaround).
- **F8 WOB** is an action key that latches or releases PERFORM's WOB live action. Without it, a wobble set here could only be heard
  after a trip to PERFORM.
  - **The LiveActions API (MD21).** Today `LiveActions` latches only through `action_press(a, shift)` (a pad bit) or `button_press`
    (a colour-button bit). Held actions end at a screen change [D4], and `is_on()` counts held ones too.
  - So F8 calls a new method, `void toggle_latch(Context&, PerfAction a)`. If `latched(a)`, it clears every latch source of `a`: the pad
    bit, and the latch of any colour button that carries `a`. Otherwise it sets the pad bit as Shift + the pad would. Then it applies.
  - F8 is lit while `latched(PerfAction::Wobble)`. Nothing can hold WOB on this screen, because the colour buttons are drawer rows here.
  - ALL OFF still lets it go [D27], and the latch survives a screen change [D4].
- **Shift row:** "-" on every key. The wobble settings are app values, and `learn_target()` refuses them [D15].
- **LOCK:** wobble settings have no lane, so the LOCK row falls back to the last sound knob latched, as on SETUP's own knobs today.
- **Pads, colour buttons, encoder:** as on GROWL.

### 5.6 MASTER: DLY MODE on F5

- F5 DLY MODE: `Entry{Item::Delay, Delay::Mode}`, a switch, MONO / PING.
- FX has eight live keys and no room. MASTER is "tempo and output", has four empty keys, and ping-pong is the output's stereo spread.
- MASTER has no Shift row, so DLY MODE is not learnable; it was not learnable in Config either.
- Classic and skinned MASTER each draw the slot: a switch in the F row (MD5).

### 5.7 PERFORM

No change:
- the WOB action (pad and colour-button assignable), its row-3 wobble-rate ladder while it is on, and D27's override and ALL OFF stay
  as built;
- the ladder follows the SOUND latch [D3], so it never targets a mod;
- ACC ALL stays the Instr default and stands in for the TD-3-MO's manual ACCENT button.

### 5.8 LOCK row and motion lanes

- `Controls::lane_ref()` already accepts `Item::Voice` and `Item::Growl` ids below `seq::kKnobs` (32). So every mod (voice ids 8..18)
  and every growl knob can be locked per step and motion-recorded with no new code.
- Switches lock like WAVE: a step with MUFFLER HARD or SUB OSC HIGH plays it.
- STOCK and the wobble settings have no lane: they are app items, which `lane_ref()` refuses.
- Locks share the pattern's 8 lanes with everything else. The toast "8 LANES USED: CLEAR LOCKS (SETUP)" is unchanged.
- **Review check (S1):** the sequencer and engine path that applies a lane must drop an id the voice does not have. Then a project with
  a lane on a mod, opened by an older image, does nothing for that lane.
- **Lane guard (S1):** voice id 19 (STOCK) is below `kKnobs`, so a hand-built or corrupt lane on (voice, 19) could toggle STOCK.
  - `lane_ref()` refuses it.
  - The project decoder drops such a lane when it loads a pattern.
  - The sequencer's lane apply skips it.
  - A test builds that pattern by hand and checks STOCK never moves.
  - SND is already safe: `apply()` looks up only carried parameters, so an SND entry for id 19 is ignored.

### 5.9 Snapshots

- `kSnapValues` goes from 24 to 40, and `kSnapTargetCount` from 22 to 36.
- Fourteen targets are appended after today's 22 (`snap_target()`):
  - indices 22..32: the voice's ids 8..18, in id order;
  - indices 33..35: the growl's FMT MIX, CMB TUNE and CMB FB. They are sound now that DEEP is gone; today only four growl knobs are
    targets (D29).
- The wobble stays out (D29). STOCK is not a target.
- A snapshot holds the stored values, so a recall during STOCK is heard at STOCK OFF.
- **A recall or a morph does not write a new target it leaves where it is** (`kSnapTargetsBeforeMods`, `LiveActions::snap_apply` and
  `snap_finish`). The older targets glide on the sequencer's slews, and `Sequencer::slew_value` maps the value through
  `param_denorm(param_norm(x))` even when from equals the goal. Measured: ACC DECAY 180 comes back 180.000015, so the Log time row takes
  its exp path instead of the stock constant, a few ulps off (SLIDE at its minimum, AMP DECAY at its maximum, TRACKING, OVERDRIVE and
  FILTER FM come back exact, and ACC DECAY is not in `mods_needed()`, so no modded loop is involved). Left alone, a recall on a
  project that never used the mods moved `perform_pads.wav` and both `snapshots` WAVs by those last bits. Only the 14 new targets are
  skipped, so the older ones keep their writes and their WAVs. A mod that a glide of the same morph had started is stopped and set exactly.
- **Morph:** `snap_discrete()` is true for Enum, so SUB OSC, MUFFLER, ACC SWEEP and SWP SPEED switch at the middle of the morph, as WAVE
  and MODEL do. The continuous ones glide.
- `SnapBank` grows from 8 × 24 + 2 × 24 floats to 8 × 40 + 2 × 40, 640 B more of SRAM. `project.cpp` `gather()` reads the slots with
  `SnapBank::stored()` and no longer copies the bank onto the stack (the copy would have been about 1.6 KB).

### 5.10 MIDI learn

- `learn_target()` keeps its one rule: Voice, Dist, Delay, Growl and Freeze entries are unit parameters, and every app value is not.
  So:
  - the mods and the growl's seven are learnable;
  - STOCK, the wobble settings and WOB show "-";
  - DECAY on MODDED is learnable, the same parameter as VOICE F4.
- The new parameters get no default CC; learn only, as the growl's.

### 5.11 STOCK

- `Item::Stock` is an app item (not a unit item, so no lane, no learn and no snapshot). It writes voice id 19 through `Controls`, and
  `Rig` keeps a mirror for the chip.
- STOCK is OFF at boot. A project load, NEW, REVERT and RESET PROJECT also turn it OFF (`project::apply()`, the one entry of all of them),
  so a project is always heard as saved first. It is never saved.
- A turn of the encoder on any of the eleven mods (not DECAY, not a lock) ends STOCK first, with the words STOCK OFF: a player who reaches
  for a mod wants to hear it. A controller, a lane, a lock or a snapshot recall writes the stored value and leaves STOCK as it is. MODDED's HELP summary says both ("A TURN ENDS STOCK. A CC, LANE, LOCK OR
  SNAPSHOT MOVES A MOD UNDER STOCK"); S4's manual says it in full.
- STOCK takes only the eleven mods to stock. The 303's own knobs (CUTOFF..TUNE, DECAY included), FX, the growl and the wobble are
  untouched. "Stock" means o3-303's plain voice (Replicant's model), not a hardware TB-303.

### 5.12 Status line and chips

- Classic: `StatusView` gains `bool stock`, drawn as a "STOCK" chip in the Accent (amber) colour after SONG's, on every screen with the
  status line.
- Skin: the header's tagline "ACID BASS VOICE" reads "STOCK . MODS OFF" in Accent while STOCK is on. SAVE FAILED in red wins. It is one
  text in place of another, so no new primitive.

### 5.13 HELP (help_texts.{h,cpp})

- `Id::Mods` and `Id::Hooks` go, and so does `Where::Hooks`. New: `Id::ModdedNotes`, `Id::ModdedTone`, `Id::Growl`, `Id::Wobble`, each
  with:
  - a summary;
  - a text for every labelled F key (for example "SOFT ATK: NOTES WITHOUT ACCENT FADE IN, 0.3 TO 30 MS; OFF IS THE PLAIN 303");
  - the encoder;
  - the home map's pads (`fill_home_pads`).
- The global buttons:
  - Params: "NEXT PAGE: VOICE, FX" on INSTRUMENT, otherwise "OPEN INSTRUMENT".
  - SampleLoad: "OPEN MODDED", or on MODDED "NEXT PAGE: NOTES, TONE". Its Shift text is "STOCK: ALL MODS OFF / ON".
  - SamplePlay: "OPEN GROWL". SampleEdit: "OPEN WOBBLE".
- UX-303 5.13's "HOOKS has no pads" goes. `help_texts_test` holds every new table to help.h's limits.

## 6. The project format

- **SND.** The voice's ids 8..18 are carried from their first build: `for_each_sound()` walks every unit parameter by id, and `carried()`
  gains `id != AcidVoice::Stock`.
  - That is eleven more entries, about 51 of `kMaxSound` 128.
  - Targets and the entry layout are unchanged: {u8 target, u8 0, u16 id, f32}.
  - The growl's seven are target 5, DLY MODE target 3, WOB RESET app value 7 (target 32), all as today.
- **A project saved before this plan** has no entries for ids 8..18. `apply()` gives a missing parameter its descriptor's default
  (`find_entry()`), which is the stock value, so it loads and sounds as it did. It needs no version bump and no migration.
- **A project saved after, opened by an older image:** the unknown voice entries are not looked up, so the mods are dropped and the plain
  voice plays. Lanes on mods are the review check in 5.8.
- **SNAP.** Each slot's value count goes from 22 to 36.
  - An older image reads all 36 and keeps 24 (`read_snap` clamps to its `kSnapValues`).
  - This image reads an older file's 22 and fills 22..35 from the loaded sound (`apply()`), which for an old project is stock.
  - `kMaxImageBytes` grows by 8 × 14 × 4 = 448 B, still under the 128 KiB `static_assert`.
- **DEEP's five values now come only from the project:** FMT MIX, CMB TUNE, CMB FB (SND target 5), DLY MODE (target 3), WOB RESET (app
  value 7). The project already held them, so the format does not change.

## 7. Config DEEP retired

- `ConfigPages` lists one page, 303. `config_pages()` returns 1. The DEEP descriptors and `deep_entry()` go.
- `config_keys.h` keeps 20..24 as comments: "retired 2026-11-01 (MODS-PLAN), never reused".
- **A stored value of a retired key is inert.** The kit's `apply_kit_app` drops "a value of an item the app no longer has" (o3-sdk
  runtime.cpp), so a `system.o3s` holding DEEP values loads with them ignored, and they leave at the next settings save. No migration
  code.
- `ProjectService::settings_moved()` goes. Its only caller was DEEP's branch of `ConfigPages::set()`, and no key that remains is sound.
  Its D18 test goes with it.
- **UX-303 5.12's "Known gap, a card swap" goes.** It existed because a swapped card's DEEP values reached the live sound through
  `config_set()`. No Config value is sound now (AUTOSAVE, KEYS AT BOOT, LADDER SLEW, PAD LEGEND, SKIN), so the project is the only owner
  of the sound and the gap is closed. D18 keeps its rule (the project owns the sound) and loses its DEEP clause.

## 8. HOOKS removal (by grep at a121f0a: `grep -rIl -i hooks` without build dirs, `.git`, `.claude`, `third_party`)

| Path | What happens | Slice |
|---|---|---|
| `app/src/hooks_screen.h`, `hooks_screen.cpp`, `hooks_view.h` | deleted | S0 |
| `app/src/skin/hooks.h`, `skin/hooks.cpp` | deleted (the CMake globs pick the change up) | S0 |
| `app/src/o3_303_app.cpp` (lines 15, 23, 158, 221, 236-238, 324) | HOOKS member, include, init and routing replaced by 4.3 | S0 |
| `app/src/help_texts.h` (Id::Hooks), `help_texts.cpp` (38, 53-56, 135, 166, 349-356) | Hooks text and Where::Hooks out; globals as 5.13 | S0 |
| `app/scenarios/hooks.txt` | deleted | S0 |
| `app/scenarios/record.txt` (97, 113-119: `20_hooks_refused`) | the refusal shown on GROWL instead (`20_growl_refused`) | S0 |
| `app/scenarios/skin_record.txt` (1, 13, 83-94: snaps 12, 13) | the take on GROWL instead (`12_growl_recording`, `13_growl`) | S0 |
| `app/scenarios/help.txt` (2, 107-115: `18_hooks_buttons`, `19_hooks_pads`) | GROWL's and WOBBLE's pages instead | S0 |
| `app/scenarios/manual_screens.txt` (1-9, 49-51: `08_hooks`) | `08_growl` | S0 |
| `tests/o3_303_scenarios_test.cpp` (143) | the required list: `hooks` out and `growl_wobble` in (S0); `modded`, `mods`, `mods_stock`, `mods_stock_ab` in, plus the check that the two STOCK WAVs are identical (S2); `skin_modded` in (S3) | S0, S2, S3 |
| `tests/record_screen_test.cpp` (2-9, 290-327, 809) | Sample buttons case rewritten (4.1); HOOKS case deleted; title table row | S0 |
| `tests/skin_master_record_test.cpp` (2, 9, 13, 303-305, 508-510, 558-584, 591-601) | HOOKS cases and rows deleted | S0 |
| `tests/help_texts_test.cpp` (89-95, 334, 344, 375-376) | HOOKS out; GROWL, WOBBLE, MODDED in | S0, S2 |
| `tools/manual_images.py` (14, 74) | `hooks` out | S4 |
| `tools/midi_check.py` (1027-1028 `SCREEN_KEY`; 2686, the start screen restored only if it is in `SCREEN_KEY`), `tools/midi_check_fake.py` (26-27 `SCREEN_OF_KEY`, `INSTR_PAGES`; 423-426, Params cycling three pages) | **S0 makes them agree with the firmware it builds:** `INSTRUMENT_MODS` and `HOOKS` out; Params cycles two pages; `sampleplay` → `GROWL` and `sampleedit` → `WOBBLE`, in both maps and in the fake; `sampleload` maps to no screen (it opens none until S2). S2 adds `sampleload` → `MODDED_NOTES`, the NOTES/TONE flip in the fake, and `MODDED_NOTES` / `MODDED_TONE` in `SCREEN_KEY`. Then a unit found on GROWL, WOBBLE or MODDED can be restored. The files are the MIDI lane's (untracked at a121f0a): the orchestrator hands them to S0 first | S0, S2 |
| `docs/manual/img/hooks.png` | deleted | S4 |
| `docs/MANUAL.md` (18, 120, 569-576, 625, 788) | HOOKS section out; button table, pads, skin line | S4 |
| `docs/UX-303.md` (6, 62, 109, 168, 513-517, 579-580, 648, 929, 982) | 2.1, 2.3, 4 titles, 5.11 (replaced by MODDED / GROWL / WOBBLE), 5.13, 6.3 row, 11, 13 | S4 |
| `docs/DESIGN-303.md` (7, 23, 111-112, 174-188) | status block, intro, section 4 rows; section 7 kept, headed "cancelled 2026-11-01" | S4 |
| `docs/SKIN-PLAN.md` (4, 101, 117, 170, 588, 602, 814; as-built 717-756) | 2.4 HOOKS row out and MODDED / GROWL / WOBBLE in; `lamp` users "PERFORM"; 4.3 row; 7 code layout. As-built text stays as history | S4 |
| `README.md` (5, 21, 22, 54) | intro sentence; the SKIN row's HOOKS mention; the M4 row reads "cancelled 2026-11-01"; a MODS row added; the docs table | S4 |
| `CLAUDE.md` (5): "and later a HOOKS screen" | changed in S0 **by the orchestrator**, on the maintainer's decision already given ("no vamos a meter hooks"); agents do not edit CLAUDE.md | S0 (orchestrator) |
| `docs/M3-PLAN.md` (15, 32, 56), `M3B-PLAN.md` (102, 116), `UX-PLAN.md` (19-264) | done plans: one status line each ("HOOKS cancelled, MODS-PLAN"); M3-PLAN's id reservation gets "used by MODS-PLAN" | S4 |
| `docs/hw/2026-10-30-u5.md`, `docs/hw/2026-11-01-su1.md` | unit-session records: never edited | — |

False positives: the "Setup and screen hooks" section comments in `file_screen.cpp`, `home_map.cpp`, `instrument_screen.cpp`,
`main_screen.cpp`, `master_screen.cpp`, `perform_screen.cpp`, `record_screen.cpp`, `song_screen.cpp`, and the CLAUDE.md lines 54-57
(the harness's hooks). These stay.

**INSTRUMENT MODS and DEEP** (`grep kInstMods|INSTRUMENT MODS|Id::Mods|DEEP|kWobReset|kDlyMode ...`):
- code: `instrument_screen.{h,cpp}`, `instrument_view.h`, `skin/instrument.{h,cpp}` (the tabs, `kPages[3]`), `pages.h` (the growl and
  wobble comments), `snapshot.h`, `config_pages.{h,cpp}`, `config_keys.h`, `project_service.h`;
- tests: `instrument_screen_test.cpp`, `app_pages_test.cpp`, `m3c_ui_test.cpp` (125, 149), `snapshots_app_test.cpp` (83-85),
  `config_pages_test.cpp`, `pages_test.cpp` (433, a comment), the `hands.h` and `app_hands.h` helpers (`to_instrument(page)`, plus new
  `to_modded()`, `to_growl()`, `to_wobble()`);
- scenarios: `instrument.txt`, `skin_instrument.txt` (03, 10), `growl.txt`, `snapshots.txt` (77, 98, 107), `home_rows.txt` (73, 98),
  `step_edit.txt` (31-34, three Params taps that now wrap), `config_303.txt` (06, 07, 10), `help.txt` (12, 13), `manual_screens.txt`
  (02);
- manual: `instrument-mods.png`, `config-deep.png`, MANUAL.md 114, 417-431, 687-726.

## 9. The skin

Budgets: SKIN-PLAN's start ceiling of 1,350 display-list words, at most 100 primitives and 4,800 box px on any scan line, no bilinear.
The scenarios test already bounds every skinned frame.

**The new pages reuse `skin/instrument.cpp` (mockup 06) as they are:** the `knob58` row through `queue_label()`, `queue_readout()`
and `queue_switch()`, the section boxes, the strip of 16 with its LEDs and keys, the PADS line. Three things are new:
- **Tabs from the group:** two, one or one tab instead of three.
- **A blank slot in the knob row:** an empty well with its F label, as the F row's blank. It is needed for GROWL F8, WOBBLE F6 and F7,
  and TONE F6 and F7.
- **A lit action key in the knob row** (STOCK, WOB): the F row's action-key look with `FSlot::lit`, as RECORD's ARM. Its readout reads
  ON or OFF.

No new asset, no RAM_G, and no change to `Panel::kShapes` (384) or `kTexts` (192).

| Frame | Estimate (words / prims on a line / box px) | Basis (`skin_instrument` as built) |
|---|---|---|
| INSTRUMENT VOICE (DIST, DELAY, MODDED) | about 1,105 / 72 / 3,654 | 01 voice 1,103 with three boxes |
| INSTRUMENT FX (VOICE, MODDED) | about 1,010 / 72 / 3,654 | 02 fx 1,007 with two boxes |
| MODDED NOTES, MODDED TONE (two boxes, an action key, TONE two blanks) | about 1,000 to 1,030 / 72 / 3,654 | as FX, plus an action slot, minus two knobs on TONE |
| GROWL (one blank), WOBBLE (three blanks, an action key) | about 960 to 1,010 / 72 / 3,654 | as FX, minus knobs |
| Worst frame (any page, playing, steps held, Shift, LEARN armed, toast) | about 1,130 / 92 / 4,166 | 09 and 10a: today's peaks |

- The header's STOCK tagline adds no primitive (5.12).
- MASTER's F5 switch adds about 10 words to `skin_master`'s 907 peak.
- Removing HOOKS removes the lightest skinned screen (481 words).

**Draw time:** SU1p measured `peak_us` 4,207 to 5,053 µs on the INSTRUMENT pages and 4,424 on MODS (1,123 words), against the page's
2,000 µs (A9), with 0 dropped frames and the audio unaffected. The new pages draw the same kinds of things in fewer or equal words, so
they will sit in the same 4.0 to 5.1 ms band.

**A9 is a failed check, and these pages inherit it.** `peak_us` at 3 to 5 ms against 2,000 µs failed on every skinned screen measured,
and three more skinned pages (MODDED, GROWL, WOBBLE) now fail it the same way. Nothing here fixes it, and **SK8 (OBSIDIAN by default)
stays blocked on A9** as before. The unit gate records `peak_us` and dropped frames for each new page beside INSTRUMENT VOICE, so that
the A9 fix, when it comes, has their numbers too. (Settled 2026-11-06: the maintainer raised the budget to 6,000 µs, `docs/SKIN-PLAN.md` D21, and SK8 went ahead.)

## 10. Slices (one lane; each with a firmware-reviewer and a build-verifier; ctest green and 0 warnings before stopping)

Common to every slice:
- a host build plus ctest and every scenario;
- the cross build's sizes;
- the `__aeabi_d*` check;
- the before-and-after sha256 of every scenario's PNG, WAV and `digests.txt` line, compared by the build-verifier.

**Keeping a WAV byte-identical when a scenario's navigation changes:** keep every later event at its old time.
- A `tap` takes 1 + hold + 1 ms (o3-sdk `sim/src/scenario.cpp`), so a single tap with a longer hold replaces several:
  `tap sampleplay 184` takes the 186 ms that three `tap params` took.
- A `wait` absorbs an extra tap.

### S0: HOOKS out, the buttons routed, GROWL and WOBBLE moved, DEEP retired (M; pure UI, no sound change)

- **Changes:**
  - section 8's S0 rows; Config DEEP retired (7) and `settings_moved()` removed; MASTER F5 DLY MODE in both drawings (5.6);
  - `InstrumentScreen` takes an `InstrumentGroup` (5.1): INSTRUMENT {VOICE, FX}, GROWL, WOBBLE (F8 blank for now); sections as 5.1.1's
    S0 interim; the skin's tabs and blank slot (9); help texts (5.13, without MODDED);
  - Sample Playback opens GROWL and Sample Editor opens WOBBLE. **Sample Loader does nothing until S2:** HELP draws it dim, and no image
    ships between slices;
  - `tools/midi_check.py` and `tools/midi_check_fake.py` made to agree with this firmware (section 8's row);
  - the orchestrator changes CLAUDE.md line 5 (section 8's row).
- **Tests:**
  - record and skin tests as section 8; `help_texts_test`: GROWL and WOBBLE in, HOOKS and MODS out;
  - `midi_check_selftest` (already in tests/CMakeLists.txt) passes against the updated fake;
  - `instrument_screen_test` (groups, titles, F entries, sections per page, Params cycles two pages, latches per page); `app_pages_test`
    (titles; GROWL and WOBBLE open from every screen; pressed again stays); `m3c_ui_test` and `snapshots_app_test` on GROWL and WOBBLE;
  - `config_pages_test`: one page; keys 1-5 and 7 unchanged; a `system.o3s` written with keys 20..24 loads with them dropped and the
    engine's growl, delay and wobble values untouched;
  - `master_screen_test`: F5 steps MONO and PING, writes `Delay::Mode` and reads it back.
- **Scenarios:** `hooks.txt` deleted; `growl.txt`, `snapshots.txt`, `home_rows.txt`, `step_edit.txt`, `instrument.txt`,
  `skin_instrument.txt`, `help.txt`, `record.txt`, `skin_record.txt`, `config_303.txt` and `manual_screens.txt` re-navigated; a new
  `growl_wobble.txt`: both screens, LEARN on GROWL, "-" on WOBBLE, a growl lock on a held step, WOBBLE skinned and classic.
- **Acceptance:** every WAV byte-identical. PNGs move only in the re-navigated or redrawn snaps (INSTRUMENT's tabs and sections,
  MASTER's F5, Config, HELP, the HOOKS snaps replaced), and the slice report lists every moved PNG with its reason. A text size delta is
  reported; ctest green, `midi_check_selftest` included; `growl_wobble` in the scenarios test's required list.

### S1: the voice mods (M; units/acid_voice and the project's `carried()`; no UI)

- **Changes:**
  1. First, before any mod: `units/acid_voice/tests/acid_voice_golden_test.cpp` with the checksums of today's code (3.3 point 6).
  2. Then ids 8..19 appended (3.1), with `run<Square, Modded>`, the control-rate terms, the `set()` paths with exact stock (3.3), and
     guards (3.5).
  3. The cost comment in `acid_voice.h` updated with the 3.4 estimates.
  4. `carried()` excludes `Stock`.
- **Tests** (new `acid_voice_mods_test.cpp`):
  - exactness: golden checksums unchanged (on the recorded host, 3.3 point 6); each parameter set explicitly to its stock value renders
    bit for bit as a fresh voice; STOCK ON, before the first block, with every mod at an extreme renders bit for bit as stock. A mod set
    to an extreme and back, or STOCK toggled while the voice runs, is stock arithmetic at once, and renders bit for bit as a voice that
    was never modded only after the voice state has decayed (3.3 point 9: about 0.4 s of silence; the tests leave 40,000 to 100,000
    frames). A mod set live, between notes, sounds as if it had been set before the start (the same bits, but for OVERDRIVE at -24 dB,
    where the idle ladder rings and the two rings are not in step); every descriptor passes `param_desc_valid()`;
  - no click at a toggle (3.3 point 8): a held note with every mod at an extreme, STOCK toggled mid-note both ways and each switch mod
    changed mid-note. The largest sample-to-sample step within 10 ms of the change must not exceed 1.25 times the largest step of the
    same note rendered without the change (stock and modded alike);
  - behaviour per mod: SOFT ATK's 10..90 % rise within 20 % of the setting, accented notes still instant; ACC DECAY's time constant from
    the cutoff path, AMP DECAY's from the level; SLIDE's 63 % glide time; TRACKING puts a C4 note's spectral centroid about two octaves
    above C2's at 100 % and equal at 0 %; FILTER FM changes the spectrum and stays finite; MUFFLER lowers the peak while the band under
    100 Hz stays within 1 dB; SUB OSC adds energy at f/2; OVERDRIVE raises the harmonics and -24 dB nearly silences the oscillator;
    SLOW's fourth close accent opens wider than its first and FAST's second is smaller than its first; ACC SWEEP OFF gives no cutoff
    lift on accents and HI RES rings longer;
  - guards: range ends; NaN reads as stock for every mod; the 576-corner sweep, new as a test (3.5), with every mod at each extreme
    stays finite and bounded; states flush to exactly 0 after silence; the oracle test unchanged and passing;
  - `project_codec_test`: an image without ids 8..18 loads with stock mods; one with them round-trips; STOCK is never written; a pattern
    with a hand-built lane on (voice, 19) loads with the lane dropped and STOCK never moves (5.8).
- **Acceptance:**
  - all of the above;
  - every scenario PNG and WAV byte-identical (no screen reaches a mod yet);
  - the cross build's text delta reported, with no `__aeabi_d*`;
  - **the objdump diff (3.3 point 6):** the stock instantiations identical to today's `run<false>` and `run<true>`, instruction for
    instruction;
  - **the disassembly of `run<false, true>` and `run<true, true>` (3.4):** no `bl` in the loop body, no more stack traffic in the loop
    than the stock loop, and a per-mod cycle count reported against 3.4's table;
  - **the fallback, applied in S1, not left to the unit:** if that count puts every mod at its heaviest above 120 cycles a sample (about
    3 points), FILTER FM's `g` and the muffler update every other sample before S1 is accepted (the ladder's two sub-steps already share
    one `g`). The count is then taken again;
  - the reviewer checks the lane-drop path and the lane guard (5.8).

### S2: MODDED and STOCK (M)

- **Changes:**
  - the MODDED group (5.3) and Sample Loader's routing with Shift (4.3); sections as 5.1.1's final table; HELP for MODDED NOTES and
    TONE, plus the globals;
  - `Item::Stock` and `Rig`'s mirror; STOCK off at load, NEW and RESET (5.11); a turn of a mod ends STOCK; `StatusView::stock` and the
    classic chip; the skin tagline (5.12);
  - the action key in the classic knob strip (F8 STOCK on MODDED, F8 WOB on WOBBLE);
  - snapshots at 40 and 36 (5.9).
- **Tests:**
  - new `modded_screen_test.cpp`: titles and the flip with Sample Loader; F entries and ids; LEARN on mods and "-" on STOCK; LOCK on a
    mod and on a switch, a lane claimed, motion record of a mod turn; Shift + Sample Loader toggling STOCK from HOME, PERFORM, MASTER,
    FILE and SONG with no screen change, with the toast and the chip; a mod turn ending STOCK and a DECAY turn not; STOCK off after a
    load and after NEW; the app's render with every mod at an extreme and STOCK on is bit for bit a fresh project's;
  - `snapshot_test` and `snapshots_app_test`: 36 targets; a switch changes at the middle of a morph; an old SNAP chunk with 22 values
    fills 22..35 from the sound; 36 values read back with 24 (the older image's clamp, simulated by the decoder's bound);
  - `help_texts_test`: MODDED in;
  - F8 WOB and `LiveActions::toggle_latch()` (5.5): WOB latched by a colour button on PERFORM (Shift + its button), then F8 on WOBBLE
    turns it off and clears the button's latch; F8 on, then a screen change, and WOB stays on; F8 on, then ALL OFF, and WOB is off;
  - `tools/midi_check_fake.py` and `midi_check.py` know MODDED (section 8's row), and `midi_check_selftest` passes.
- **Scenarios:**
  - new `modded.txt`: both pages, LEARN, a lock, STOCK on HOME and PERFORM with the chip, a mod turn ending it;
  - new `mods.txt`, the sound: the factory line through twelve sections, stock and then each mod alone at a telling setting, with one
    WAV at the end and the sections' times in its comment;
  - new `mods_stock.txt` and `mods_stock_ab.txt`, the A/B. A scenario's `wav` writes all the audio since the start
    (`Harness::write_wav`), so the A/B takes two scenarios with the same timeline. **As built (S2), STOCK goes on first and the mods
    reach their extremes afterwards by a path that does not end STOCK**: both scenarios turn STOCK on with Shift + Sample Loader and
    learn the eleven mods to controllers 20 to 30 (Shift + F, a binding message); `mods_stock` stops there, so the controllers do
    nothing, and `mods_stock_ab` then sends each controller through its knob to the end (pickup mode needs it to cross the
    parameter). The voice never leaves its stock loop, so there is no perturbation to wait out (3.3 point 9) and the two files can be
    equal to the byte. The first design here (the mods set, then STOCK, with the voice silent) would need the state to converge, which
    it does not where the idle ladder rings. Both then play the factory line from step 1 for 8 bars and write `stock.wav`;
  - **the scenario-test change:** `tests/o3_303_scenarios_test.cpp` gains a check, after every scenario has run, that
    `mods_stock/stock.wav` and `mods_stock_ab/stock.wav` are byte-identical. The four new scenarios join the required list (the
    `for` over the names at line 143 of that file: 31 names before S2, 35 after; "143" in section 8's table is that line, not a count).
- **Acceptance:**
  - the tests;
  - every earlier WAV byte-identical;
  - PNGs move only where the MODDED section, the action keys, the chip or the tagline are drawn.

### S3: the skin pass (S)

- **Changes:** what section 9 lists that S0 and S2 did not already draw: the lit action key in the knob row, polished against mockup 06,
  and any layout fix the snaps show. No new asset.
- **Tests:** `skin_instrument_test` extended:
  - tabs per group; blank slots; the lit STOCK and WOB keys;
  - the tagline under STOCK, and SAVE FAILED winning;
  - the fullest frame of each new page, queue use with `dropped() == 0`.
- **Scenario:** new `skin_modded.txt`: MODDED NOTES and TONE, GROWL, WOBBLE, STOCK on HOME, the worst frame, and HOME right after a
  MIDI learn toast (5,296 px a line then, over the 4,800 ceiling: section 13, fixed by KS1: 4,458), which S3 measured: it is the kit's strip, and not that frame alone (SKIN-PLAN 4.3). It joins the scenarios
  test's required list (that same `for`, 36 names then).
- **Acceptance:** every skinned frame under 1,350 words, 100 primitives and 4,800 px. The numbers are recorded in the slice report and
  SKIN-PLAN, and everything else is byte-identical. The host cannot show A9; the pages inherit its failure (section 9).

### S4: the docs and the manual (S; doc-keeper). Done 2026-11-02 (reviewed against the code, accepted with fixes, the fixes folded; the pictures made again from the scenarios)

- **Changes:**
  - section 8's S4 rows; README's status table; `acid_voice.h`'s "same and different" list;
  - UX-303: 2.1, 2.3, 2.6 as section 4; 4's titles; 5.5 two pages; a new 5.11 MODDED, GROWL and WOBBLE (from section 5); 5.12 one page,
    its gap removed, D18's clause; 5.13; 6.2's list of home-map screens; 6.3; 8.1; 10's GROWL, WOBBLE, MODS and "buttons with no
    function" rows; 11; 13; 14 gains D32 onward from section 12;
  - DESIGN-303 sections 3, 4 and 7; SKIN-PLAN 2.4, 3, 4.3, 7;
  - MANUAL.md: buttons; INSTRUMENT; new MODDED, GROWL and WOBBLE sections with a short "what each mod does" table; MASTER F5; Config;
    pads;
  - `tools/manual_images.py`: out `hooks`, `config-deep`, `instrument-mods`; in `modded-notes`, `modded-tone`, `growl`, `wobble`;
    `master` regenerated; the pictures regenerated.
- **Acceptance:** the doc-keeper's review that every claim matches the code; the pictures come from the scenarios; no file other than
  docs and tools.

## 11. The unit gate (one hardware session after S4; `hw-session` skill; image through `fw-gate-check`)

The image reaches the unit only by the documented path: the `.ptf` on a mounted card or via `o3console.py --put`. The Tracker is shared:
start from the found state and restore exactly it.

1. **Buttons:** Sample Loader, Sample Playback and Sample Editor open MODDED NOTES, GROWL and WOBBLE; Sample Loader again flips to TONE
   and back; Shift + Sample Loader toggles STOCK on HOME, PERFORM and FILE, with the chip or tagline; Params cycles VOICE and FX; Config
   has one page; MASTER F5 switches the delay's ping-pong.
2. **CPU** (the console's render read-out, as SU1 A10; the factory line at 125 BPM; 2 minutes each; max and mean): stock; each of
   OVERDRIVE +36 dB, SUB OSC HIGH, FILTER FM 100 %, MUFFLER HARD, SOFT ATK 30 ms, ACC SWEEP HI RES and SWP SPEED SLOW alone; all of them
   together; then `tools/midi_check.py` stress with all on. Pass: all on ≤ stock + 4 points; storm ≤ 75 % with 0 underruns.
3. **Each mod by ear**, against 3.2: SOFT ATK softens non-accented notes only; ACC DECAY lengthens accented sweeps; AMP DECAY turns notes
   into plucks; SLIDE up to six times longer; TRACKING opens high notes; FILTER FM grits; MUFFLER SOFT and HARD (bass kept on HARD); SUB
   OSC levels; OVERDRIVE from thin to heavy; ACC SWEEP OFF and HI RES; SWP SPEED FAST and SLOW over a run of accents. Record the
   starting values of 3.2 that need tuning.
4. **STOCK A/B by ear:** no click worse than a knob turn; the plain voice comes back exactly; a mod turn ends STOCK.
5. **Old projects:** a project from the SU1 card loads and sounds as before; a snapshot morph across a switch flips at the middle.
6. **MIDI learn** of a mod and a growl knob with a real controller.
7. **Skin:** `peak_us` and dropped frames for MODDED NOTES, TONE, GROWL and WOBBLE beside INSTRUMENT VOICE (expected in A9's 4 to 5 ms
   band; 0 dropped; audio unaffected).

A host test is not this check. The slices name the unit check they leave to it.

## 12. Decisions (taken by the maintainer's rule; nobody was asked)

| # | Decision | Why |
|---|---|---|
| MD1 | Sample Loader opens MODDED (two pages, pressed again flips); Sample Playback opens GROWL; Sample Editor opens WOBBLE; INSTRUMENT keeps VOICE and FX | the maintainer's layout; each button now opens one idea, and every growl and wobble control is in one place instead of split across INSTRUMENT and Config |
| MD2 | MODDED pages are NOTES (how a note moves) and TONE (what the filter is fed and gives), not "1" and "2" | a title that says what is on it |
| MD3 | STOCK is Shift + Sample Loader from any screen, plus F8 on both MODDED pages, a toggle | the app's pattern "Shift + screen button = its action from anywhere"; reachable while listening; takes nothing else; F8 makes it visible where the mods are (4.1) |
| MD4 | GROWL's and WOBBLE's pads are the home map, not presets or ladders | the simplest: the pads mean one thing on every sound screen, and the line keeps playing; presets and ladders go to the backlog |
| MD5 | DLY MODE goes to MASTER F5 | FX is full; MASTER is tempo and output and has four empty keys; ping-pong is output spread |
| MD6 | OVERDRIVE is a voice stage (the oscillator's level into the ladder); the FX distortion unit stays | that is what the Devil Fish mod is; the post stage is a different sound and both stack |
| MD7 | SUB OSC: OFF, LOW, MID, HIGH as the level of a square one octave down, added before the ladder | the sources agree on a switchable sub with three steps; two octaves down is sub-audio on low notes |
| MD8 | ACC SWEEP: OFF (no sweep), NORMAL (today), HI RES (the DF's "whistly": sweep plus doubled feedback) | three positions as the sources say; OFF reads as it sounds; the DF's "no sweep, high resonance" is left out |
| MD9 | SWP SPEED NORMAL is today's sweep (no C13 build-up); SLOW accumulates, FAST tires | stock must be today; the hardware's build-up is what SLOW gives |
| MD10 | SOUND's DECAY stays the filter's normal decay, its range unchanged; AMP DECAY is new | projects, lanes and MIDI maps store values against today's descriptors; the TD-3-MO's volume decay still gets a knob |
| MD11 | STOCK is a voice parameter (id 19), driven by an app item; never saved; OFF at boot, load, NEW and RESET | atomic, keeps the stored values (lanes, snapshots and CCs go on writing them), and a project is always heard as saved |
| MD12 | A mod turned while STOCK is on ends STOCK | a player who reaches for a mod wants to hear it |
| MD13 | One `InstrumentScreen` class in four instances with page groups | one path for learn, LOCK, lanes, HELP and both drawings; no new screen class to keep in step |
| MD14 | The voice ids 8..15 keep M3-PLAN's reserved order, with "Sweep" as ACC SWEEP (default NORMAL, the position that reproduces Replicant) | the reservation's own rule ("each default reproduces Replicant") |
| MD15 | Snapshots gain the eleven mods and the growl's three former DEEP knobs (36 targets, room for 40); the wobble and STOCK stay out | they are sound; D29's reasons for the wobble hold |
| MD16 | Golden checksums of today's voice come first in S1 | nothing pins the voice bit for bit today; "stock is today" needs a test before the code moves |
| MD17 | Config DEEP's keys 20..24 are retired and never reused; no migration; the card-swap gap goes | the kit drops values of keys it no longer lists, and no remaining Config value is sound |
| MD18 | Order S0, S1, S2, S3, S4, then one unit gate | UI moves with no sound risk first; the voice proven exact before any screen reaches it |
| MD19 | The golden pin checks on an x86-64 host with the recorded glibc, in Debug and Release alike; elsewhere it reports that it did not check; the MK66 is covered by an objdump diff of the stock instantiations | libm and FMA contraction make bits platform-bound. x86-64 GCC has no FMA without `-march`, so -O0 and -O2 agree. `build-host` is Debug by default, so a Release-only pin (the review's wording) would never run in a slice |
| MD20 | FILTER FM modulates `g` in the linear domain, `g * (1 + 2 * fm * y)` clamped, with no `exp2_fast` in the per-sample loop | the out-of-line degree-7 call cost an estimated 60 to 90 cycles a sample (spills, serial chain); the linear form is about 16 and sounds alike at moderate depth |
| MD21 | F8 WOB uses a new `LiveActions::toggle_latch()`: on, it sets the pad latch; off, it clears every latch source | the existing API latches only through a pad or a colour button, and held actions end at a screen change |
| MD22 | SOFT ATK is a six-step Enum (OFF, 0.3, 1, 3, 10, 30 ms) with a time table; TRACKING is `Int` 0..150 % | `param.h` cannot print OFF or 0.3 through `Int`, and `NormPct` prints the position (normal tracking would read 66.7) |
| MD23 | The sub's level, the muffler's mix, OVERDRIVE and FILTER FM move along ramps of about 5 ms, and STOCK uses them | a toggle of STOCK or a switch mod mid-note would otherwise click; a fresh voice starts with every ramp landed, so stock stays exact |

## 13. Risks, and what is not settled

- **Not settled, by source:** the TD-3-MO's sub-oscillator (one review's "three-octave", its waveform, its levels; Behringer's quick
  start guide was seen only as a search excerpt, [6]) and its ACC SWEEP labels (the DF's switch or a three-step amount). MD7 and MD8
  follow the Devil Fish manual and the agreeing sources; renaming a position later is a name change in the table, not a format change.
- **Not settled, by ear:** the starting values of 3.2 (sub levels, FM depth, muffler thresholds and corner, FAST and SLOW time
  constants, OVERDRIVE's ends). The unit gate tunes them. Changing a range or a default after the gate breaks rule 2, so they are fixed
  at the gate and not after.
- **CPU:** 2.3 points is an estimate.
  - S1 makes it a disassembly count, and it applies the every-other-sample fallback itself when the count is over 120 cycles a sample
    (S1's acceptance).
  - If the unit still measures the storm over 75 % after that, SUB OSC's BLEP goes to a plain square next, before any mod is cut.
- **HI RES** can self-oscillate loudly with RESO high. The clamps bound it, and S1's test bounds the peak (at most 2.0 before VOLUME);
  the unit gate listens for it.
- **Bit-exactness on the MK66:** GCC contracts to FMA there (gnu++17). The objdump diff of S1 proves that the stock instantiations are
  today's code; after that, the unit is checked only by ear.
- **The host pin depends on glibc:** on another glibc the test reports that it did not check, so S1's before-and-after comparison must
  run on one machine.
- **Lanes on mods in older images,** and the STOCK lane guard: 5.8.
- **S2's `mods_stock_ab` scenario is byte-identical to a stock render only if the voice never leaves its stock loop.** Mods that are
  set or removed while the voice runs leave the voice state perturbed for about 0.4 s (3.3 point 9), and never converge where the idle
  ladder rings, so a scenario that sets them mid-run with STOCK off and compares with a never-modded run differs, and that is the
  plan, not a defect. S2's design: STOCK first, then the mods by controllers (a controller, a lane, a lock and a snapshot recall write
  the stored value and do not end STOCK; only a turn of the encoder on a mod does, 5.11). The unit test
  (`modded_screen_test.cpp`, "mods_stock_ab") checks the same by the engine's own set_param, with every mod at its maximum and at its
  minimum, three orders (STOCK then mods, mods and STOCK in one block, STOCK alone), and that the same mods without STOCK are heard.
  S2 also owns resetting STOCK when a project loads (5.11): S1's `apply()` does not, and S2's does.
- **Skinned frames over the ceiling were the kit's strip, not one frame: fixed in KS1.** While any kit strip was up (learn, save, card, USB: 3 s
  each), every HOME and PERFORM F-row frame was over 4,800 px on its busiest line: HOME 5,296 just after a MIDI learn (4,306 without the
  strip), HOME SETUP with F3 held 5,366, PERFORM SETUP 5,470, and a notice of the 71 characters `kStripChars` allows adds 10 px each (5,816,
  5,886 and 5,990). It was so at 9fdb583 and after S2. S3 found the cause: the strip `Runtime` draws over the bottom 28 px (an 800 px
  panel and 10 px a character), over the F row's readouts that the skin had already queued; `Context` had no query for it, and S3's interim
  was the `strip_*` snaps held to 5,300 px. KS1 added the o3-sdk `ui::Rect Context::strip_area()` (the strip's exact rect, empty when no strip
  is up), and the skin's `Panel` now leaves out every shape and text wholly inside it (the strip is opaque, so every PNG and WAV is
  byte-identical). Every strip frame now meets the ceiling: HOME after a learn 4,458 px, SETUP with F3 held and PERFORM SETUP 4,616, the same
  under a 71-character notice; the `strip_*` bound is gone and the test keeps the strip-up check (SKIN-PLAN 4.3 has the table). On the unit
  4,616 px is 72 % of the 6,400 clean point, under the 75 % rule.
- **Follow-up for a re-baseline slice: `Sequencer::slew_value` returns the goal exactly when from equals goal.** Today it goes through
  `param_denorm(param_norm(x))` and ACC DECAY 180 comes back 180.000015 (5.9). The clean fix would let snapshots write the mods like
  every other target, but it moves older WAVs by their last bits, so it waits for a deliberate re-baseline. S2 does not do it.
- **Skin draw time: A9 failed** (`peak_us` 3 to 5 ms against 2,000 µs), and MODDED, GROWL and WOBBLE inherit the failure. SK8 stays
  blocked on A9; it is not fixed here. (Settled 2026-11-06 by the new budget, `docs/SKIN-PLAN.md` D21.)
- **The MIDI tools** belong to the MIDI lane until the orchestrator hands them to S0. S0 cannot pass `midi_check_selftest` without them.
- **SRAM_U has almost no room.** KF1 (o3-sdk kit-fix1, 2026-11-01) measured the o3_303 image at SRAM_U 138,832 B. A 512-byte `.bss`
  growth (an alignment cliff before usb0's `g_bdt`) then tripped the link's "SRAM_U: less than 32 KiB free" assert, which had only
  about 440 B of margin. 5.9's `SnapBank` growth (+640 B) does not fit as written, and S1's per-voice state must be counted too.
  - Every slice that adds a member to a long-lived object reads the mk66 link's region use and the assert. That means the Rig, the
    wiring, a unit's state and LiveActions.
  - Control-rate data that is not read per sample moves to the SDRAM with `O3SDK_BIG` (the snapshot bank is read at a recall and a
    morph, not per sample).
  - Only the voice's per-sample state stays in fast RAM.
  - S0 and S1 each report the SRAM_U bytes before and after.

## 14. References

1. Robin Whittle, *User Manual for the Devil Fish mods to the TB-303*, Real World Interfaces, 9 February 2019,
   https://www.firstpr.com.au/rwi/dfish/Devil-Fish-Manual.pdf. Cited pages: p.4 (Overdrive, Slide Time, Soft Attack, Decay (VEG), MEG,
   Normal and Accent Decay, the Accent Sweep and Sweep Speed switches); p.5 (Sweep Speed modes, Hi Resonance, Filter Tracking, Filter
   FM); p.6 (Filter FM, Muffler); p.8 ("Limiting the Devil Fish to TB-303 sounds"); p.16 (slide slew); p.18 (the Accent Sweep switch
   table); p.19 (the Muffler on the VCA output feeding Filter FM).
2. Robin Whittle, the Devil Fish pages, https://www.firstpr.com.au/rwi/dfish/.
3. Sound On Sound, review of the Behringer TD-3-MO, https://www.soundonsound.com/reviews/behringer-td-3-mo: overdrive in place of the
   distortion, sub-oscillator, normal and accent decay, decay on the volume envelope, filter FM, muffler, sweep speed, accent sweep,
   manual accent.
4. Thomann, Behringer TD-3-MO product page, https://www.thomann.co.uk/behringer_td_3_mo.htm: switchable sub-oscillator; muffler (VCA soft
   clipping) in 2 steps; accent sweep and sweep speed in 3 steps; soft attack, normal and accent decay, filter tracking, slide time
   (six-fold) and filter FM controls.
5. MusicTech, "Behringer's TD-3-MO is a wicked acid machine with a sub-oscillator for deep bass",
   https://musictech.com/news/behringers-td-3-mo-is-a-wicked-acid-machine-with-a-sub-oscillator-for-deep-bass.
6. Behringer TD-3-MO quick start guide: seen only as a search-result excerpt on ManualOwl (the page answered 403 and was not read), for
   the SUB OSC switch and its level selector. To be checked against the printed guide.
7. In this tree: docs/M3-PLAN.md (the voice's table and the reserved ids 8..15); docs/UX-303.md (2, 5.5, 5.11-5.13, 8, 14);
   docs/SKIN-PLAN.md (2.4, 4, SK5, SK6); docs/hw/2026-11-01-su1.md (A9, A10, B6) and docs/hw/2026-11-01-midi.md (the storm);
   `units/acid_voice` (acid_voice.cpp, acid_voice.h, tests); `third_party/replicant303/LICENSE` and THIRD_PARTY.md;
   `tools/midi_check.py` and `tools/midi_check_fake.py`.
8. In o3-sdk (read, not edited): `kit/core/include/o3sdk/core/param.h` (ParamDesc, ParamUnit), `kit/core/include/o3sdk/core/fast_math.h`
   (`exp2_fast`, `log2_fast`), `cmake/O3sdkPlatform.cmake` (build type, gnu++17), `platform/mk66/audio/CMakeLists.txt`
   (`-ffp-contract=off` on three files), `sim/src/scenario.cpp` (`tap`, `wav`), `kit/app/src/runtime.cpp` (`apply_kit_app`).
