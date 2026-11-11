# TRACT (the formant) in o3-303 and o3-voc03er, and the KNOBS pads mode in o3-303: the plan

Date 2026-11-11. Covers trak3r-os/o3-voc03er#1 (TRACT mode), trak3r-os/o3-303#1 (the formant cut) and trak3r-os/o3-303#2 (pads as
knobs). Nothing is built. Every CPU figure is an estimate [est] until a unit reads it. What I did not verify is listed in section 9.

Facts this plan stands on (read in the trees):

- A block is 64 frames, 261,224 cycles; 4,082 cycles a frame is 100 %, so **40.8 cycles a frame is 1 %**. The growl's measured cost was
  1.25 x its instruction-count model (`units/growl/src/growl.cpp` header); the same factor is applied below.
- o3-303 v0.6.0: render at most 49.5 % with voice + distortion + growl + delay; the governor sheds at 60 %. Room: about 10 points.
- o3-voc03er: the `Vocoder` unit's table is **full at 64 parameters** (`vocoder.h`, `kMaxParamsPerUnit`). Idle BANDS is 40.9 %, TALK
  with 8 notes and 4 loops 142.8k (54.7 %), budget 156,734 (60 %). SRAM_U margin 9,784 B.
- o3-303 already has assignable ladder rows (backlog item 8): `Wiring::ladder[3]` (HOME row 3, HOME row 4, PERFORM row 3), stored as
  `LaneRef` in the project's PERF chunk, assigned by "hold an F key + tap a pad of the row", recorded as motion with REC. Issue #2 is
  therefore an extension of built code, not a new system.
- o3-303's working tree has an uncommitted slice (OVERDRIVE reads OFF: `pages.cpp`, `pages_test.cpp`, `modded.txt`, the manual). **It
  is committed before any slice below touches `pages.cpp`.**

---

## 1. Where the DSP lives (settled by the maintainer)

One tract DSP for both apps, in **`o3-sdk/extras/tract/`**, opt-in: the sdk's own CMake never adds it, its CI never builds it. Each app
adds it with `add_subdirectory(${O3SDK_DIR}/extras/tract ${CMAKE_BINARY_DIR}/extras_tract)` and compiles the extras' test sources
into its own test suite. Each app wraps it in a thin unit of its own (`units/tract/` in each repository) with its own parameter table,
so the 303's cut and the vocoder's full mode are two uses of the same code and each app appends ids on its own schedule.

Why the split core / wrapper and not one shared `Unit`: the two tables differ (7 knobs against 11), ids are append-only per unit, and
the 303 must be able to stay on an older table while the vocoder grows.

Rules for the sdk side:

- The sdk commits touch `extras/tract/**` and one row of `THIRD_PARTY.md` only. No kit, unit, platform or CMake file of the sdk
  changes, so no image of any app changes except through the build label (the kit's git state is in it).
- Before the first sdk commit the orchestrator tells the session working in o3-sdk (memory rule "sync SDK changes"). The message is
  "an additive folder the sdk never compiles; your builds are unaffected".
- The sdk's `verify_gate` cannot prove extras (it does not build them). **An extras change is verified from o3-voc03er's build**
  (host ctest + the mk66 build for the cycle count and the `__aeabi_d*` check), and the commit message says so.
- Licence: the code is written here from textbook material (Kelly and Lochbaum 1962; the lossless-tube lattice of Markel and Gray;
  Peterson and Barney's formant averages, the same data the growl uses). `extras/tract/LICENSES.md` states GPL-3.0-or-later, credits
  CHIMERA (Jameson Hodge, MIT) as the inspiration and Pink Trombone (Neil Thapen, MIT) as a studied reference, and says no code of
  either is included. If an implementer does copy a routine, it goes through the `dsp-port` licence gate first and the MIT notice goes
  in that file. **Deviation from issue voc#1**: no sample-for-sample oracle against Chimera's Rust engine, because this is not a port;
  the oracle is physics (section 2.6).

### Layout

```
o3-sdk/extras/tract/
  CMakeLists.txt                      o3sdk_unit(tract_dsp SOURCES src/tube.cpp src/shape.cpp src/halfband.cpp src/processor.cpp)
                                      target_compile_options(unit_tract_dsp PRIVATE -O2 -ffp-contract=off)   # as the growl
  include/o3sdk/extras/tract/{tube.h,shape.h,halfband.h,processor.h}
  src/…
  tests/{tube_test.cpp,shape_test.cpp,halfband_test.cpp,processor_test.cpp,tract_probe.h}   # doctest, compiled by the apps
  tools/fit_vowels.cpp                host-only helper that prints the vowel table (not shipped in an image)
  README.md  LICENSES.md
```

Namespace `o3sdk::extras::tract`. The code depends on `<cmath>`, `<cstdint>`, `<cstring>` only (the `dsp` link that `o3sdk_unit` adds
is unused). No heap, no double, no exceptions; all state is members, under 1.5 KB an instance (no SDRAM needed).

---

## 2. The shared DSP

### 2.1 The tube (`tube.h`): a Kelly-Lochbaum lattice, backward path delayed

Sections `i = 0..N-1`, 0 at the glottis, `N-1` at the lips, area `A[i] = d[i]^2`. Pressure-wave reflection at the junction between
`i` and `i+1`: `k[i] = (A[i] - A[i+1]) / (A[i] + A[i+1])`. One array of state `b[0..N-1]` (the left-going wave of each section, one
sample old), updated in place. A tract sample:

```
f = x + rG * b[0];                       // glottis: the source plus the returning wave (rG > 0: a closed end)
for (i = 0; i + 1 < N; ++i) {            // junction i | i+1
  w = k[i] * (f - b[i + 1]);
  b[i] = b[i + 1] + w;                   // into section i, leftwards (slot i was read one iteration ago: in place is safe)
  f = f + w;                             // into section i+1, rightwards
}
b[N - 1] = -rL * f;                      // lips: an open end
y = (1 - rL) * f;                        // what leaves the mouth
```

The forward path has no delay and each section delays the backward wave one tract sample, so the round trip is `N` samples and a
uniform tube resonates at `(2m-1) * fs_t / (2N)`. Physical length `L = N * c / (2 * fs_t)`: **N = 22 at fs_t = 22,050 Hz is 17.5 cm**
(501, 1503, 2506 Hz). `|k| < 1` and `rG, rL < 1` make it passive: it cannot blow up, also while `k` moves.
Losses are at the two ends only (no per-section multiply). If a test of 2.6 contradicts these equations' signs, the tests win.

Cost of the loop [est]: 2 loads, 1 store, 1 multiply, 3 add/sub, the pointer step: about 12.5 cycles a section-sample.

### 2.2 Rate and size (`processor.h`, `halfband.h`)

The tube runs **decimated**. `Quality::Full`: fs/2, N = 22 (formants right up to 11 kHz). `Quality::Lite`: fs/4, N = 11 (right up to
5.5 kHz; the same band TALK already uses in the vocoder). The quality is a constructor argument of `Processor`; both are tested.

- Resampling: a symmetric FIR half-band, 15 taps (4 distinct coefficients and the centre), polyphase, one stage for Full and two for
  Lite. The coefficients are designed by a host script and frozen as constants; `halfband_test.cpp` asserts at most 0.5 dB of ripple
  below 0.2 fs and at least 45 dB of rejection above 0.3 fs. The decimation phase is state, so any block size gives the same samples.
- The wet path is band-limited (11 kHz or 5.5 kHz) and about 0.3 ms late; the dry path is not delayed (no compensation in v1).
- **SIZE**: five lengths, `N = 26, 24, 22, 20, 18` at Full (13, 12, 11, 10, 9 at Lite): formants x0.85, x0.92, x1, x1.1, x1.22.
  The area function is a function of `x in [0,1]` sampled at N points, so a size is only another N. A change of N keeps `b[]`, zeroes the
  new tail and runs a 5 ms dip of the wet gain (test: the step at the change stays under a bound). `kMaxSections = 28`.

### 2.3 The shape (`shape.h`): three numbers, a table of five vowels

```
struct Shape { float tongue; float height; float lips; };   // tongue 0 back .. 1 front; height 0 open .. 1 close; lips 0 round .. 1 spread
Shape vowel_shape(float v) noexcept;                         // v 0..1 through A E I O U, piecewise linear between five anchors in Shape space
void diameters(const Shape& s, const float closure[3], unsigned n, float* d) noexcept;
```

The area model (the constants are starting points; the fit tool may move them, then they are frozen):

- `x_i = (i + 0.5) / n`. Rest diameter `d0(x)`: 0.6 for `x < 0.12` (the larynx tube), a straight ramp to 1.5 at `x = 0.25`, 1.5 after.
- Tongue: centre `c = 0.30 + 0.50 * tongue`, depth `h = 1.35 * height`, `bump(u) = (1 - u^2)^2` for `|u| < 1`, else 0 (no `cosf`):
  `d(x) = d0(x) - h * bump((x - c) / 0.30) + 0.35 * h * (bump((x - c - 0.45) / 0.35) + bump((x - c + 0.45) / 0.35))`
  (the tongue's volume goes to both sides: a front constriction widens the throat, as in /i/).
- Lips: for `x > 0.92`, `d = min(d, 0.25 + 1.5 * lips)`.
- Closures `closure[3]` (lips at the last section, tip at `x = 0.85`, back at `x = 0.55`), each 0..1: `d` goes towards `dmin` over the
  section and half of each neighbour. `d >= dmin = 0.03` everywhere.

Anchors to start the fit from (tongue, height, lips): A (0.05, 0.70, 1.0), E (0.75, 0.45, 0.9), I (0.90, 0.90, 1.0), O (0.20, 0.60,
0.35), U (0.45, 0.80, 0.10). `tools/fit_vowels.cpp` does a grid search around them for the Peterson-Barney male F1, F2, F3 (the
growl's table: 730/1090/2440, 530/1840/2480, 270/2290/3010, 570/840/2410, 300/870/2240) at N = 22 and prints the table.

The vowel line interpolates the three numbers, not the formant frequencies: between two vowels the mouth passes through shapes a
mouth can hold. That, and F3 to F5 in their natural places, is what this adds over the growl's three parallel band-passes.

### 2.4 The processor (`processor.h`): what both units wrap

```
class Processor {
 public:
  enum class Quality : std::uint8_t { Full, Lite };
  explicit Processor(Quality q) noexcept;
  void prepare(float sample_rate) noexcept;
  void reset() noexcept;
  void set_vowel(float v) noexcept;                      // 0..1, A E I O U
  void set_bend(float tongue, float jaw, float lips) noexcept;   // each -1..+1, added to the vowel's shape (jaw: -height), clamped
  void set_closure(unsigned which, bool on) noexcept;    // 0 lips, 1 tip, 2 back; moves in 12 ms whatever GLIDE says
  void set_glide_ms(float ms) noexcept;                  // 5..500: the one-pole of vowel and bend
  void set_size(int steps) noexcept;                     // -2..+2
  void set_res(float r) noexcept;                        // 0..1: rG = rL = 0.60 + 0.37 r
  void set_hiss(float h) noexcept;                       // 0..1: noise at the narrowest place
  void set_mix(float m) noexcept;                        // 0..1, 5 ms ramp
  void process(const float* in, float* out, std::uint32_t n) noexcept;   // mono, full rate, any n, in may equal out
  bool bypassed() const noexcept;                        // Mix 0 and settled
};
```

- **Control period**: every 16 full-rate frames. The shape's one-poles step; **only if the shape moved** are the diameters and the
  `k[]` recomputed (N evaluations of a polynomial bump and N divisions: about 600 cycles a period [est], 37 a frame, while moving;
  nothing when settled). `approach()` lands a value on its target when close, as the growl's pitch does, so "settled" is reached.
- **Bypass**: Mix below 1e-3 and the ramp settled: `out = in` bit for bit (a memcpy when the buffers differ), no state runs, as the
  growl. Coming back from bypass resets the tube and the resamplers (stale state would click).
- **Level**: wet gain `kMakeup * sqrt(1 - rG * rL)`; `kMakeup` is calibrated once so that a C2 saw through /a/ at RES 0.5 has the dry
  signal's RMS. Test: RMS within +-4 dB of dry over the five vowels and RES 0, 0.5, 1.
- **HISS**: when the smallest diameter is under 0.3, white noise (a 32-bit LCG) times `hiss * (0.3 - dmin) / 0.3` times a one-pole
  follower of `|f|` is added to `b[j]` at the narrowest section after the loop (nothing added inside the loop). At hiss 0 the path
  is skipped.
- Denormals and NaN: states under 1e-15 are zeroed and a non-finite or huge state clears the tube, once a control period (the
  growl's `kTinyState`, `kHugeState` pattern). A NaN parameter is clamped to its default.

### 2.5 Cost [est]

| Part | Full (fs/2, N 22) | Lite (fs/4, N 11) |
|---|---|---|
| Tube, cycles a full-rate frame | 22 x 12.5 / 2 = 138 | 11 x 12.5 / 4 = 34 |
| Half-bands, down and up | 22 | 35 |
| Mix, sum to mono, overhead | 15 | 15 |
| Model, settled | 175 = 4.3 % | 84 = 2.1 % |
| x 1.25 (the growl's factor) | **5.4 %** | **2.6 %** |
| While the shape moves (wobble on VOWEL) | + 0.9 % | + 0.5 % |

The implementer counts the real loop from the cross build's disassembly and writes it in the file's header, as the growl does.

### 2.6 The tests that judge the DSP (`extras/tract/tests`, run by both apps)

`tract_probe.h`: the magnitude response of a `Tube` from its impulse response (4096 points, a plain DFT on the host) and a peak picker.

1. Uniform tube, N = 22, fs_t = 22,050: peaks within 2 % of 501, 1503, 2506 Hz.
2. Two tubes, narrow back (area 1, half the length) and wide front (area 7): F1 above 650 Hz and F2 below 1400 Hz. Wide back (8) and
   narrow front (1): F1 below 400 Hz, F2 above 1700 Hz. (This fixes the sign of `k` and which end is which.)
3. The five vowels of `vowel_shape` at N = 22: F1 and F2 within 12 % of Peterson-Barney, F3 within 20 %; at N = 11 (Lite): F1, F2 within 18 %.
4. Passivity: 10 s of white noise with `k` redrawn at random every 8 samples and RES 1: the output stays finite and under a bound.
5. Processor: Mix 0 is bit-exact; block sizes 1, 7, 16, 64 give identical output; a vowel jump 0 to 1 at GLIDE 5 ms and a closure on
   and off keep the sample-to-sample step under a bound (click-free); a closure silences the output (-40 dB within 30 ms) and its
   release with a held source gives a peak above the steady level (the pop); SIZE change bound; level test of 2.4; 60 s of silence in
   leaves exact zeros out (no denormal tail); both qualities.
6. `halfband_test`: the response limits of 2.2 and a 1 kHz sine through down + up within 0.1 dB.

---

## 3. o3-303: the TRACT page (issue #1)

### 3.1 What it is

A mouth after the growl: the 303's filtered, distorted line is the voice box's buzz and the tract puts vowels on it. Musically: a
talking bass. VOWEL is locked per step (the line says "a-e-i-o-u"), GLIDE says how the mouth slides from one step's vowel to the
next, the wobble can swing VOWEL, TONGUE and LIPS bend every vowel at once, SIZE moves all the formants (giant to child), RES goes from
a soft mouth to a ringing one. The 303's filter cutoff becomes vocal effort (a bright buzz gives clear vowels), so the classic knobs
keep their meaning.

Against the growl: the growl is 3 band-passes on a straight line of formant frequencies plus a comb; it keeps its sound and its page.
The tract is an all-pole tube (F1 to F5 at natural levels, the bass fundamental passes at unity under F1), morphs in mouth space,
and has tongue, lips, size. They stack (growl's comb into the mouth is a sound of its own). The page name is **TRACT**, the same word
in both apps, the unit and both manuals (one string to change if the maintainer prefers FORMANT; GROWL already says "FMT" on three keys).

### 3.2 The unit: `units/tract` (class `o3_303::TractFx`, Effect, library `unit_tract`)

Wraps one `Processor(Quality::Full)`. Stereo in: the wet path is the mono sum, written to both channels; the dry path stays stereo
(as the growl). Parameter table (id = index; later ones are appended):

| Id | Name | Range | Default | Notes |
|---|---|---|---|---|
| 0 | VOWEL | 0..1, NormPct | 0 | `kParamSmooth`; a lane target; the wobble's third target |
| 1 | TONGUE | -1..+1, shown -100..+100 % | 0 | back .. front |
| 2 | LIPS | -1..+1, shown -100..+100 % | 0 | round .. spread |
| 3 | RES | 0..1, NormPct | 0.5 | |
| 4 | GLIDE | 5..500 ms, Log | 30 | |
| 5 | SIZE | Enum BIG, LARGE, NORMAL, SMALL, TINY | NORMAL | switches; never glides |
| 6 | MIX | 0..1, NormPct | 0 | 0 = bit-exact bypass |

JAW, CLOSE, HISS are not in the 303's cut (the processor's defaults: 0, open, 0).

Chain: `acid_voice - distortion (insert 0) - growl (insert 1) - tract (insert 2) - level - send A`. Attached last: **unit id 5**, so
voice 0, distortion 1, delay 2, freeze 3, growl 4 stay. After the distortion on purpose: a formant filter is only as clear as its
source is rich (the talk box rule).

### 3.3 The page, the button, the read-outs

- **Button**: Sample Play opens GROWL as today; **Sample Play again flips GROWL and TRACT**, exactly as Sample Load flips MODDED's
  NOTES and TONE. `kGrowlGroup` becomes a two-page group (`kMaxGroupPages` is already 2): titles `"GROWL"`, `"TRACT"`, tabs the same.
  `o3_303_app.cpp`: on `growl_screen_`, `next_page()` + redraw. Shift + Sample Play does the same (no new Shift meaning).
- **Keys** (`kTractPage` in `pages.cpp`, `Item::Tract`, `Item::TractOn`):

  | F1 | F2 | F3 | F4 | F5 | F6 | F7 | F8 |
  |---|---|---|---|---|---|---|---|
  | VOWEL | TONGUE | LIPS | RES | GLIDE | SIZE | TRC MIX | TRACT (OFF / ON) |

  F8 is GROWL's F8 pattern: Mix to 0 and back to `Wiring::tract_last_mix` (50 % at boot, not saved). VOWEL's value reads as the
  growl's (0..100 %); the big knob's caption adds the nearest vowel letter ("37 %  E") through `Controls::format`.
- **Read-outs**: a new section `kSecTract` (`"TRACT"`: VOWEL, RES, SIZE, TRC MIX). The section mask is `unsigned`, 9 sections fit.
  GROWL page shows VOICE | WOBBLE as today; TRACT page shows VOICE | WOBBLE; WOBBLE page shows VOICE | GROWL as today (unchanged).
- **Strip when off**: "TRACT OFF: F8", as GROWL's.
- **Hint**: `"SAMPLE PLAY AGAIN = GROWL / TRACT   SHIFT + F = LEARN"`.
- **Wobble**: `WobbleTarget` gains `kWobbleMouth` (appended: 2), shown **MOUTH**, the lane `{kLaneTract, 0}`. With TRC MIX at 0 the
  encoder says TRACT IS OFF, as VOWEL does for the growl. A stored target above the old count loads as it is (older builds clamp).
- **Lanes and locks**: `kLaneTract = 7`, `kLaneTargetCount = 8` (`pattern.h`), `sequencer_.set_unit(kLaneTract, tract)`,
  `Controls::lane_ref/lane_entry/lane_name` take `Item::Tract`. All seven knobs are lockable and recordable like the growl's; a lock
  on SIZE switches. "Slide" is GLIDE: a locked vowel is reached in GLIDE ms. (Following the step's SLIDE flag is a later option; the
  hook would write GLIDE at slid steps.)
- **MIDI learn**: `learn_target()` returns `(wiring.tract, id)` for `Item::Tract`; F8 says "-".
- **Snapshots**: four targets appended: VOWEL, TONGUE, LIPS, TRC MIX. `kSnapTargetCount` 44 becomes **48 = `kSnapValues`**: the
  snapshot is then full, and no format changes. They sit after index `kSnapTargetsBeforeMods`, so a recall that does not move them
  does not write them. An older project's snapshots load with the sound's values, as the code already does for missing targets.
- **STOCK**: unchanged. STOCK is the voice's switch for its circuit mods and does not touch the growl; it does not touch the tract.
  The tract's A/B is F8. The honest "is it still a 303" check is F8 off = bit-exact v0.6.0 output (a host test) and the ear on F8.
- **Project**: the unit's parameters are saved with the sound like the growl's (SND chunk carries a count; the implementer follows
  `carried()` for the growl). A v0.6.0 project loads with the tract's defaults (Mix 0). A lane on `kLaneTract` is accepted by
  `sanitize()` and by PERF's ladder check; a v0.6.0 build that opens such a project drops that lane or row (its checks refuse the
  target), which the CHANGELOG says.

### 3.4 CPU and the gate

5.4 % [est] at Full on top of 49.5 %: about 55 % at the worst, 5 points under the governor. **The rule**: the unit reads `diag cpu`
with voice + distortion + growl + tract (wobble on MOUTH) + delay. If the max is above 56 %, the 303 constructs the processor with
`Quality::Lite` (one line, 2.6 % [est]) and the scenario WAVs are regenerated. No other fallback is needed.

---

## 4. o3-voc03er: the TRACT mode (issue #1 there)

### 4.1 Architecture

- **`Vocoder::Mode` gains value 4, TRACT** (the enum list grows from 4 to 5 names: no id moves). In TRACT the vocoder unit runs the
  carrier only (Bands, Talk and Tune idle, the modulator unheard) and writes the carrier, times LEVEL, to its output: a fifth mix
  ramp `carrier_mix_` beside `bands_mix_ .. robot_mix_`, so the mode change crossfades as the others do. GATE, ARP, GLIDE, BEND,
  VIB, SUB, TILT, NOISE and the CARRIER lane keep working: they are the carrier's.
- **A second unit, `units/tract` (class `o3_voc03er::TractUnit`, Effect)**, on slot 0's insert 0, wrapping `Processor(Quality::Full)`.
  The 64-parameter limit is untouched: the tract's knobs are this unit's.
- **One tract on the summed carrier, not one a voice.** The tract is linear and there is one mouth, so the sum of eight voices
  through one tube is exactly the sum of eight identical tubes. Eight tubes would cost 8 x 5.4 % for the same samples.
- **The glottis is the carrier**: SHAPE (saw to pulse) is the fold's pulse, SUB the deep throat, NOISE the breath and whisper, TILT
  the effort, the step's LEVEL the lung pressure.
- One function owns the coupling, `apply_mode()` in the app: Mode TRACT writes the tract's MIX 1, any other mode writes MIX 0
  (bit-exact bypass, so BANDS, TALK, TUNE and ROBOT are sample-identical to v0.2.0: a host test). Every writer of Mode (PERFORM F1,
  PARAMS, presets, the console, a project load) goes through it.

Tract unit table: 0 VOWEL, 1 TONGUE, 2 JAW, 3 LIPS, 4 CLOSE (Enum OPEN, LIPS, TIP, BACK), 5 GLIDE (5..500 ms, 30), 6 SIZE (five
values), 7 RES (0.5), 8 HISS (0.3), 9 MIX (0). Ten ids; the next are appended.

### 4.2 PERFORM

- F1 MODE: BANDS, TALK, TUNE, ROBOT, **TRACT**. The MODE word over the voiceprint reads TRACT and the voiceprint draws the **area
  function** (the N diameters as a mouth profile, read from the app's own copy of the shape: `diameters()` on the main loop, no
  read of the unit's state). SPEAKS is dimmed with "NO INPUT: THE MOUTH IS PLAYED".
- **F8 by mode: TRACT gives VOWEL** (the encoder morphs A to U over held chords: the "one knob" of the issue).
- **The mouth strip** (no new F7 value: the maintainer wants few pad modes). In TRACT one pad row is the mouth:
  - PADS = KEYS: row 3 (dark today, or the formant strip) is the mouth strip; with BEND ROW on the bend strip keeps row 3 and the
    mouth strip is not shown (the hint line says so).
  - PADS = CHORDS: row 0 (the power chords) is the mouth strip; three chord rows stay and F6 CHORD picks the bottom one's kind, the
    rule the bend strip already uses.
  - Columns 0..8: VOWEL = column / 8 (A, a-e, E, e-i, I, i-o, O, o-u, U). **A tap sets it and it stays** (lit `kLedOn`, the others
    `kLedDim`, the five pure vowels `kLedMid`), as the formant strip: a vowel is picked, not sprung back from.
  - Columns 9, 10, 11: closures LIPS, TIP, BACK, **momentary**: held closes the mouth (CLOSE = that place), the release opens it
    and the pop or click comes from the model (b/m, d/t, g/k with a note held; with NOISE up, p, t, k). SHIFT + pad does not latch them.
- Breath is the carrier's NOISE (and the CARRIER lane's NOISE per step); HISS adds the turbulence at a narrow place.

### 4.3 PARAMS

`kPages` 3 becomes 4: CARRIER, SHAPE, TUNE, **TRACT**. A new `Kind::Tract` (a parameter of the tract unit) beside `Kind::Value`.
SHAPE's MODE key range becomes 0..4.

| F1 | F2 | F3 | F4 | F5 | F6 | F7 | F8 |
|---|---|---|---|---|---|---|---|
| VOWEL | TONGUE | JAW | LIPS | GLIDE | SIZE | RES | HISS |

The page is live in every mode but says "TRACT MODE ONLY" on the strip outside TRACT (the values are stored anyway).

### 4.4 The sequencer: a MOUTH lane

The step word is full (32 bits), so the lane is a parallel array: `mouth[8 patterns][16]` bytes in the bank (`O3SDK_BIG`, read by the
hook with the same relaxed-atomic rule as a step word). Value: 0 FOLLOW (the step leaves the mouth alone), 1..9 the nine vowel
places (jump: reached in the knob's GLIDE), 10..12 the closures LIPS, TIP, BACK for the step's gate (opened at the gate's end),
13..21 the nine vowels **by slide** (the glide lasts the step: the hook writes GLIDE = one step, then the knob's value at the next
step that is not a slide).

- **PATTERN screen**: in TRACT the fourth lane is MOUTH in place of SLICE (the Fx button and F2 LANE reach it; SLICE is not shown,
  slices do not speak in TRACT). The palette: row 0 = the strip of 4.2 (9 vowels, 3 closures), row 1 columns 0..8 = the same vowels
  by slide, columns 9..11 dark. SHIFT + step pad clears the lane's value (FOLLOW), as every lane. The cell shows "A", "E-I", "LIP",
  and a ">" before a slid vowel. No more display-list words than today (one lane row replaces another).
- **The hook** emits Param events to the tract unit at the step's frame: VOWEL, CLOSE, GLIDE. `StepSeq::set_tract(UnitId)`. Stop
  writes CLOSE open and the knob's GLIDE once.
- **File**: `pattern.o3a` gains a chunk `MOUT` (u16 version 0x0100, u8 pattern count, 8 x 16 bytes) after BANK. A file without it
  loads with every value FOLLOW; a value out of range refuses the file, as BANK's. The project file takes the tract unit's ten
  values the way it takes the vocoder's.
- Console: `pat mou <step> <value>` beside `pat car`.

### 4.5 The issue's open questions, answered

1. **Segments**: 22 at fs/2 (the same resolution a 44-section full-rate tube has up to 11 kHz, at a quarter of the cost). 12 to 16
   sections at the full rate would be a 5 to 6 cm tube: the wrong formants. Polyphony does not multiply the cost (one tube).
2. **Gestures**: momentary pads on the mouth strip live, and closure values in the MOUTH lane when sequenced. No separate gesture
   lane: a closure is a mouth shape, and the pop is the model's.
3. **TRACT as a source into BANDS**: not in v1. The tract is an insert after the vocoder unit, so feeding it back as the modulator
   needs either the tube inside the vocoder unit or a kit route. v1's way is the existing one: record TRACT's output into a slot
   (SAMPLE REC) and let the slot speak.

### 4.6 CPU and memory [est]

TRACT stops Bands (58k cycles a block by the unit's own estimate) and adds the tube (175 x 64 x 1.25 = 14k). Expected: idle about
55 to 65k (21 to 25 %), 8 notes with SPREAD about 100 to 110k (about 40 %), under the 60 % budget with room; the unit reads it.
SRAM: one `Processor` under 1.5 KB, the mouth lane 128 B in the SDRAM bank; the margin of 9,784 B holds. The implementer reads
`__bss_end` after the link and writes it in the slice's "Built" paragraph.

---

## 5. o3-303: KNOBS, the pads mode (issue #2)

### 5.1 What already exists and what is added

Built: ladder rows that hold any lane-capable knob, on HOME (FILT: rows 3 and 4) and PERFORM (row 3), assigned by hold F + tap,
saved in the project, recorded with REC. Added by this plan: **a mode where all four rows are ladders**, a way to reach every knob
family from it, and a setup that can be read and edited on screen.

### 5.2 The issue's open questions, answered

1. **HOME or PERFORM: both, one table.** HOME keeps FILT exactly as it is (rows 3 and 4; rows 1 and 2 must stay the steps there).
   The four-row mode is a **new page of PERFORM, KNOBS**: Performance cycles PERFORM, **KNOBS**, PERFORM SETUP. KNOBS' rows 3 and 4
   **are HOME's FILT rows** (`kLadderHome3`, `kLadderHome4`: the same physical rows hold the same knobs on both screens); rows 1
   and 2 are two new entries. PERFORM's own row 3 stays as it is.
2. **Which knobs: every knob a lane can drive** (`Controls::lane_ref()` is the single definition): the eight SOUND knobs, FX,
   MODDED NOTES and TONE, GROWL, TRACT, VOLUME and DLY SEND. The F row of KNOBS holds the eight SOUND knobs (as PERFORM's);
   every other knob is reached through the Shift row's list (5.3). **Deviation from the issue**: it asks for the family to be picked
   with NOTE, INSTR, VOL, FX. Those buttons already mean three things (step rows on HOME, actions on PERFORM, columns on SONG); a
   fourth meaning is what item 7 removes, so on KNOBS they stay PERFORM's four actions.
3. **Ladder shape: 12 equal steps of the knob's own curve**, as today (`ladder_norm` through `param_denorm`): CUTOFF is already
   logarithmic there, so the musical range already gets the pads. One addition: a row that holds a list or a switch (an Enum, or a
   knob of 12 values or fewer) gets **one pad a value** from the left and dark pads after the last. No per-row curve option in v1.

### 5.3 KNOBS on screen and under the hands

```
PERFORM KNOBS                                              [tabs: PERFORM  KNOBS  SETUP]
ROW 1  ENV MOD   [ o o o o o o # . . . . . ]   62 %
ROW 2  DECAY     [ o o o # . . . . . . . . ]   310 ms
ROW 3  CUTOFF    [ o o o o o o o o # . . . ]   1.20k Hz
ROW 4  RESO      [ o o # . . . . . . . . . ]   25 %
F1..F8: the eight SOUND knobs (name and value)             PADS: ROW 1 ENV MOD . ROW 2 DECAY . ROW 3 CUTOFF . ROW 4 RESO
```

- **Pads**: 4 x 12, each row a ladder: a tap glides the knob to that place in LADDER SLEW ms; LEDs a bar up to the place, the place
  at full level (the code of `HomeMap::ladder_leds`).
- **The F row** is the SOUND page, as on PERFORM; the encoder and hold-F work as on every page (the knob still turns by hand).
- **The colour buttons** are PERFORM's four actions here too, held or latched: nothing new to learn.
- **Binding, the gesture**: **hold an F key, tap any pad of a row**: the row now holds that knob; the tap only assigns. The same
  with the same knob again puts the row back to its default. The rows blink while an F key is held. This is the gesture HOME and
  PERFORM already have, so there is one everywhere. The issue's other order (hold the row, tap F) is not built: a ladder pad acts on
  its press, so holding it would move the knob first.
- A knob of another page (FX, MODDED, GROWL, TRACT) is given to rows 3 and 4 from its own page with FILT on (the built gesture), and
  to any row through the list below.
- **Binding, the discoverable way**: with **Shift held the F row reads ROW 1, ROW 2, ROW 3, ROW 4, -, -, -, DEFAULTS**. Shift + F1..F4
  points the encoder at that row's binding: a turn walks the list of every bindable knob in family order, the row's caption changes
  as it goes, and any other key ends it. Shift + F8 puts the four rows back (ENV MOD, DECAY, CUTOFF, RESO), with a toast.
- **REC**: on, while the line plays, a tap writes the jump as motion on the knob's lane, through `ladder_tap()`, which claims a
  lane as the F row does; with the eight lanes taken the toast says so and the knob still moves. Off: live only.
- **MODS (STOCK)**: the manual's rule is that a turn of the encoder on a mod switches MODS on first, and a lane, a lock or MIDI do
  not. A ladder tap is the hand, so **a tap on a row that holds one of the thirteen mods switches MODS on first**, through the same
  helper the encoder's turn uses (today `ladder_tap()` goes through the slew and, as far as read, does not: K1 checks and fixes it
  for HOME's and PERFORM's rows too). The recorded motion of that tap, played back later, does not switch MODS, as any lane.
- **WOB**: the wobble's rate picker stays on PERFORM's first page only.
- **Defaults**: rows 3 and 4 CUTOFF and RESO (unchanged: they are HOME's), rows 1 and 2 ENV MOD and DECAY. Shift + FILT on HOME keeps
  resetting only rows 3 and 4.

### 5.4 Data and project format

- `kLadderRows` 3 becomes 5: `kLadderKnobs1 = 3`, `kLadderKnobs2 = 4`, defaults `{kLaneVoice, 2}` (ENV MOD), `{kLaneVoice, 3}` (DECAY).
- **PERF chunk**: the two new rows are appended after the three (2 bytes each). The reader already works by length: three rows
  present, two missing loads the defaults for the two; v0.6.0 reads the first three and leaves the rest. The chunk's minor version
  goes up by one; no major bump, no migration. The accepted targets gain `kLaneTract`. Only PERFORM's row 3 may be `kLaneNone`.
- The family shown on KNOBS and the page of PERFORM are not saved (a boot opens PERFORM's first page, SOUND family).
- Snapshots do not take the rows (as today).

### 5.5 Text

- Title `PERFORM KNOBS`; PADS line as drawn; toasts `ROW 2: DECAY`, `ROW 2: BACK TO DECAY`, `THAT KNOB HAS NO LADDER` (exists),
  `ROWS: ENV MOD, DECAY, CUTOFF, RESO`, `NO FREE LANE` (the existing wording for the eight-lane limit is reused).
- HELP (`help_texts::Id::Knobs`): "EACH ROW OF PADS IS ONE KNOB: LEFT IS ITS LOWEST, RIGHT ITS HIGHEST. HOLD AN F KEY AND TAP A ROW
  TO GIVE THE ROW THAT KNOB. NOTE, INSTR, VOL, FX CHANGE THE F ROW'S KNOBS. SHIFT + F1..F4 PICKS A ROW'S KNOB FROM A LIST. REC ON
  RECORDS THE TAPS. ROWS 3 AND 4 ARE HOME'S FILT ROWS." HELP's PADS page draws four rows MIN .. MAX.
- Manual: a section "PERFORM KNOBS" after PERFORM, a line in FILT ("the same two rows as KNOBS' rows 3 and 4"), section 6's map.

---

## 6. Slices, in build order

Each slice: one implementer, the files named, ctest green before the commit, the commit message names the unit check still owed.
`-Werror` clean, no heap, no double (`arm-none-eabi-nm` on the cross build, plain `nm` for linked doubles, tools from `ARM_GNU_ROOT`).

### o3-sdk + the first consumer (first)

| Slice | Repository, files | Content | Tests, scenario | Only the unit |
|---|---|---|---|---|
| **X1** | o3-sdk `extras/tract/` (CMake, `tube.h/.cpp`, `shape.h/.cpp`, `tests/tube_test.cpp`, `shape_test.cpp`, `tract_probe.h`, `tools/fit_vowels.cpp`, README, LICENSES), `THIRD_PARTY.md`. o3-voc03er `units/CMakeLists.txt` (the `add_subdirectory` line), `tests/CMakeLists.txt` (the extras' test sources) | The tube, the area model, the fitted vowel table | 2.6 tests 1 to 4, run from o3-voc03er | nothing |
| **X2** | o3-sdk `extras/tract/` (`halfband.*`, `processor.*`, their tests) | Resampling, the processor, bypass, glide, closures, HISS, SIZE, level; the cycle count from o3-voc03er's cross build in the header | 2.6 tests 5 and 6, both qualities | nothing (cost [est]) |

Two commits a slice: the sdk's first, then the app's CMake lines. The sync message to the o3-sdk session goes before X1.

### o3-voc03er

| Slice | Files | Content | Tests, scenario | Only the unit |
|---|---|---|---|---|
| **V1** | `units/tract/` (new), `units/vocoder/src/vocoder.cpp` + `vocoder.h` (Mode 4), `app/src/app.cpp` (attach, insert, `apply_mode`), console (`voc tract`, `tract <param> <v>`) | The mode sounds from the console | `tests/tract_mode_test.cpp`: modes 0..3 sample-identical to before; mode 4 = carrier through the tube; a mode change has no step over a bound; block-size independence. Scenario `tract.txt` (a chord, VOWEL swept from the console, WAV plotted) | **CPU of TRACT idle, 4 and 8 notes** (`cpu`), no underruns; first listen |
| **V2** | `app/src/perform_screen.cpp`, `perform_logic.h`, `params_logic.h`, `params_screen.cpp`, the skin's perform text, help | F1 TRACT, F8 VOWEL, the mouth strip on KEYS and CHORDS, LEDs, the area-function voiceprint, PARAMS page TRACT | `perform_logic_test` (strip map, sticky vowels, momentary closures, LEDs), `params_test` (page 4, `Kind::Tract`), scenarios `perform_tract.txt`, `params.txt` extended | the pads by hand, the frame under the display budget, the pop of a closure by ear |
| **V3** | `app/include/o3_voc03er/stepseq.h`, `pattern_screen.cpp`, `pattern_logic.h`, project/pattern codecs, console `pat mou` | The MOUTH lane, slide, the `MOUT` chunk | `stepseq_test` (events on the step's frame, slide's GLIDE, Stop), `pattern_screen_test`, `project_test` (old file loads FOLLOW, bad value refused), scenario `mouth_lane.txt` ("a-e-i-o-u" over a bass line, WAV) | the sequenced phrase by ear, no click at a slid step |
| **V4** | `docs/MANUAL.md`, `docs/UI-DESIGN.md`, `docs/PLAN.md`, CHANGELOG, `THIRD_PARTY.md` | Docs; the hw-session checklist (skill `hw-session`) | `manual_screens.txt` regenerated | the session itself |

### o3-303 (K slices need nothing from X: they can run beside X1/X2; A slices start after X2)

| Slice | Files | Content | Tests, scenario | Only the unit |
|---|---|---|---|---|
| **K1** | `pages.h` (rows 5), `ladder_rows.*`, `project.cpp`, `tests/ladder_rows_test.cpp`, `project_codec_test.cpp` | The table of five rows, one pad a value for lists, PERF append, a tap on a mod switches MODS on | Old PERF (3 rows) loads with defaults for rows 1, 2; round trip; a v0.6.0-shaped reader stops after three; enum row places | nothing |
| **K2** | `perform_screen.cpp/.h`, `perform_view.h`, `perform.h` (pad map of the page), `o3_303_app.cpp` (the three-page cycle), skin perform files, `help_texts.*` | The KNOBS page: pads, LEDs, hold F + tap, the Shift row's list of knobs, the colour buttons as PERFORM's actions | `perform_screen_test`, `skin_perform_test`, `help_texts_test`, scenarios `knobs.txt`, `skin_knobs.txt`; display list under 1,800 words (the worst-case test) | the pads by hand, LED bars legible, the frame time |
| **K3** | `tests/knobs_equiv_test.cpp`, `app/scenarios/knobs_rec.txt`, `docs/MANUAL.md`, CHANGELOG | The acceptance of the issue | **A pad tap and an encoder turn to the same value give the same engine value, the same motion point (`to_point`) on the same claimed lane, and the same WAV after the slew has settled** (the 30 ms glide is the tap's only difference, and the test says so); REC on writes one lane; the ninth knob says the lanes are full; FILT defaults unchanged | REC by hand while the line plays |
| **A1** | `units/tract/` (new), `units/CMakeLists.txt` (the extras line), `tests/CMakeLists.txt`, `pattern.h` (`kLaneTract`), `app/seq` (`set_unit`, the arrays' size), `o3_303_app.cpp` (attach last, insert 2), `pages.h/.cpp` (`Item::Tract`, `Item::TractOn`, `Wiring::tract`, `tract_last_mix`, Controls), `project.cpp` (sanitize, SND) | The unit wired, silent at Mix 0 | `tests/tract_unit_test.cpp` (table, bypass bit-exact, stereo); **every existing scenario's WAV is byte-identical to before** (the scenarios test); `pages_test`, `lanes_test`, `project_codec_test` (v0.6.0 project loads, a tract lane round-trips); SRAM read from the link | nothing yet |
| **A2** | `pages.cpp` (`kTractPage`, the wobble's MOUTH, snapshot targets 48), `instrument_screen.cpp` (group of two, `kSecTract`, the strip, learn), `o3_303_app.cpp` (Sample Play again), `live_actions.cpp` (wobble target), `help_texts.*`, skin instrument files | The page, the wobble, snapshots, learn, HELP | `instrument_screen_test`, `m3c_ui_test`, `snapshots_app_test` (old snapshot loads, 48 targets), `skin_instrument_test`, scenarios `tract.txt` (VOWEL locked per step over the factory line; A/B against the run with F8 off: bit-exact until on), `tract_wobble.txt`, `skin_tract.txt`, `manual_screens.txt` | **CPU with everything on (the 56 % rule of 3.4)**; the ear |
| **A3** | `docs/MANUAL.md` (TRACT section, WOBBLE's TARGET row, GROWL's intro), `docs/MODS-PLAN.md`, CHANGELOG, `THIRD_PARTY.md`, the hw-session checklist | Docs | manual images regenerated | the session |

Order across repositories: X1, X2, then V1 (the first sound, the first CPU read) and A1 in parallel; K1 to K3 at any time. The
maintainer's ear on V1 comes before A2 is finished, so a retune of the vowel table (an extras commit) lands in both apps at once.

---

## 7. Risks, and what only the maintainer's ear decides

1. **The 303's identity.** With F8 off the output is bit-exact v0.6.0 (tested). With it on, the wet path is band-limited to 11 kHz
   (5.5 kHz at Lite) and the acid top end comes only through TRC MIX's dry side. The ear decides whether TRACT at 100 % is still a
   303 that talks or a different instrument, and whether the default should be a parallel mix.
2. **The vowel table.** The fit hits published formant averages; whether "a e i o u" are heard on a distorted saw at C1 to C3 is the
   ear's. Low notes have few harmonics under F1, so vowels are clearer with the cutoff open and the distortion on.
3. **Tract after or before the distortion.** After (chosen) is clearer; before would growl more. A one-line change of two insert
   indices; decided by ear after A2.
4. **CPU in the 303** (5.4 % [est] against 10 points of room): the 56 % rule, then Lite.
5. **The dry/wet comb**: the wet path is about 0.3 ms late; at TRC MIX 50 % this may colour the sound. If it is heard, the dry path
   gets a matching delay (a 16-sample ring) in an extras slice.
6. **Closures and pops in the vocoder**: the model gives them, their level is `kMakeup` and RES dependent; the ear sets whether a
   limiter is needed after the tube (the kit's output is clipped, not limited).
7. **SIZE's switch** is a 5 ms dip: audible on a held chord. The ear decides if it is acceptable or SIZE becomes "set while silent".
8. **The snapshot is full at 48** in the 303 after A2. The next snapshot target needs `kSnapValues` raised (a SNAP format step).
9. **Rows 1 and 2 of KNOBS take any knob only through the Shift list** (or the eight SOUND knobs by hold F + tap). If that is too
   slow in use, the next step is Up / Down paging the F row through the sound pages on KNOBS; not built in v1.
10. **Two repositories on one sdk folder**: a retune for one app changes the other. Wanted (one sound), and each app's scenarios'
    WAV tests will fail loudly when it happens; the fix is regenerating them in the same round.

## 8. Unit checks owed (for the hw-session lists)

o3-voc03er: CPU of TRACT (idle, 4 notes, 8 notes with SPREAD, with 4 loops), 0 underruns, the mouth strip by hand, closures' pops,
a sequenced phrase, a mode change without a click, 10 minutes of soak. o3-303: CPU with voice + distortion + growl + tract + delay
and the wobble on MOUTH, F8 A/B by ear, vowel locks on the factory line, MIDI learn of VOWEL, KNOBS by hand with REC, the LED bars.

## 9. Not verified while planning

- Chimera Lab's repository was not opened; nothing here depends on it.
- Whether o3-voc03er's pattern loader skips an unknown chunk (`MOUT` read by v0.2.0): V3 checks it and, if it refuses, the chunk
  goes in the project file instead.
- How o3-303's SND chunk decides which units' parameters are carried (A1 follows the growl's path).
- Whether `ladder_tap()` already switches MODS on for a mod knob (the manual gives the encoder's rule; the slew path was not read
  to the end). K1 tests it.
- **o3-voc03er#2 could not be read** (`gh` is not logged in here and the issue is not in `issues.md`). Section 10.2 answers the
  complaint as the coordinator worded it ("how many steps a single note takes").
- The half-band coefficients and every cycle count: designed and counted in X2.
- The 303's free SRAM after `kLaneTargetCount` 8 grows the sequencer's tables (about 32 entries a table): A1 reads the link.

---

## 10. Item 7: o3-303's interface, reviewed (o3-303 only)

### 10.1 What issue #2 reports, read against the code

Most of #2 is built (backlog item 8). What is left, and where this plan puts it: four ladder rows at once (KNOBS, K2), a binding
that can be read and edited on screen (the Shift list, K2), lists as one pad a value (K1), the acceptance tests the issue asks for,
which do not exist yet (K3), and the MODS rule for a tap (K1). The issue's "hold the row, then the F key" order is not built (5.3).

### 10.2 How notes are entered and generated: the 303 has the complaint too

Read in `home_map.cpp` (key handling, lines 418..440), `seq/gen.cpp`, `seq/sequencer.cpp` and the manual:

| Finding | Today | Effect |
|---|---|---|
| **N1. A held key never makes a long note.** | With REC on, a key writes one step (`write_advance`), stopped or playing; the key's release is ignored. A note longer than a step needs the TIE pad on each following step | Playing a long note in gives a 1/16 blip. This is the "one note, one step" complaint |
| **N2. Live REC writes on the step that started last**, never the nearest | `ph.step` is "the step that started last" | A key played a little early lands a whole step early; playing in time feels late-biased |
| **N3. The generator never draws a tie** | `gen.cpp` draws gate, accent, slide only | Every NEW line is all sixteenths; length comes only from GATE (pattern-wide) and slides |
| **N4. GATE is 50 % for the whole pattern** and there is no per-step length | PATTERN SETUP F3 | Correct for a 303 (its gate is fixed); TIE is the 303's way. Kept: no per-step gate |
| **N5. Stopped step entry has no "longer" gesture on the key itself** | TIE pad, or Insert on a step | Two hands and two pad areas for the most common edit after a note |

Fixes (slices U1 to U3 below): hold = length while playing (the steps the key is still down on become ties), nearest-step
quantise, a hold gesture when stopped (key held + Right: the note grows a step, as a tie, and the cursor follows), and ties in NEW.

### 10.3 The F row today, screen by screen

| Screen (button) | F1..F8 | Shift + F |
|---|---|---|
| HOME (Pattern) | The eight SOUND knobs | Pattern tools: NEW VARY RHYTHM NOTES REVERSE CLEAR **REDO UNDO** |
| PATTERN SETUP (Pattern again) | LENGTH SWING GATE TRANSP SCALE DENSITY ACC % SLD % | COPY PAT, PASTE, DUP>NEXT, CLR LOCK, ROOT, -, -, **UNDO** (no REDO) |
| INSTRUMENT VOICE (Params) | The same eight SOUND knobs as HOME | LEARN (F8 "-") |
| INSTRUMENT FX (Params again) | DRIVE TONE MIX MODEL, DLY TIME FB SEND TONE | LEARN |
| MODDED NOTES, TONE (Sample Load, again) | Seven mods, **F8 MODS** | LEARN |
| GROWL (Sample Play) | Seven knobs, **F8 GROWL** | LEARN |
| WOBBLE (Sample Edit) | RATE SHAPE DEPTH TARGET RESET, two blanks, **F8 WOB** | "-" on all |
| PERFORM (Performance) | The eight SOUND knobs | F7 REDO, F8 UNDO |
| PERFORM SETUP (again) | NOTE INSTR VOL FX (actions), FILL FREEZE ROLL STOP | none |
| MASTER (Master) | TEMPO TAP NUDGE MASTER DLY MODE, three blanks | none |
| SONG (Song) | PLAYS LOOP CHAIN, three blanks, CLEAR, **UNDO (plain F8)** | **F7 REDO** |
| RECORD (Sample Rec) | ARM, -, BARS, -, -, SAVE, DISCARD, - | none |

What is stale:

1. **UNDO and REDO sit in four different places** (Shift F7/F8 on HOME and PERFORM; UNDO only on SETUP; plain F8 with Shift + F7 on SONG).
2. **Three kinds of F key look the same**: a knob (tap, then turn), a switch (F8 MODS, GROWL, WOB: a tap flips it) and an action
   (TAP, ARM, CHAIN, CLEAR: a tap does it). Only the label tells them apart.
3. **INSTRUMENT VOICE repeats HOME's F row**: the first press of Params shows nothing new to turn.
4. **The sound is on four buttons with three paging habits**: Params pages, Sample Load pages, Sample Play and Sample Edit do
   nothing on a second press, and no screen shows which buttons have a second page.
5. **Blank keys in the middle** (WOBBLE F6, F7; RECORD F2, F4, F5, F8; SONG F4 to F6; MASTER F6 to F8) and a delay setting on MASTER
   (DLY MODE) because FX is full.
6. **The colour buttons have three meanings** (step rows, actions, song columns). Kept at three: KNOBS adds none (5.2).
7. The Shift row means tools on two screens, LEARN on five, nothing on four, and says so only once Shift is held.

### 10.4 The scheme: four sentences that hold on every screen

1. **F1 to F7 are what the page is about, F8 is the page's switch or its exit.** On an effect page F8 turns the effect on and off
   (MODS, GROWL, TRACT, WOB: already so; TRACT is born with it). Knobs fill from F1 with no hole; actions sit at the right, before F8.
2. **A button opens its screen; the same button again goes to its next page, and wraps.** Every multi-page button shows its tabs in
   the header (the INSTRUMENT group's tab strip, used everywhere): `HOME | SETUP`, `VOICE | FX`, `NOTES | TONE`, `GROWL | TRACT`,
   `PERFORM | KNOBS | SETUP`, `PROJECTS | LIBRARY`. A screen of one page shows no tabs, and its button does nothing on it.
3. **Shift + F7 is REDO and Shift + F8 is UNDO on every screen that edits the pattern or the song** (HOME, PATTERN SETUP, PERFORM,
   KNOBS, SONG). On the sound pages the Shift row is LEARN, and nothing else ever.
4. **A key says what kind it is**: a knob shows its value; a switch shows ON or OFF in a filled chip; an action shows its name in a
   frame with no value. One drawing rule in the F-row painter of both looks.

TRACT and KNOBS come out of sentences 1 and 2: TRACT is the second page of Sample Play with F8 as its switch, KNOBS the second page
of Performance with PERFORM's F row and Shift row. Neither adds a button meaning.

Not taken, with the reason: removing INSTRUMENT VOICE (it is the only screen that shows the whole sound's read-outs at once; it
stays, and sentence 2's tabs explain it); moving the sound pages to fewer buttons (four habits the player already has, and the
Sample buttons were just assigned on 2026-11-01).

### 10.5 Slices of item 7

P = changes a saved project. H = changes a habit the player has.

| Slice | What changes | Files | Test, scenario | The manual says after | P | H |
|---|---|---|---|---|---|---|
| **U1** Hold = length, nearest step | REC + playing: a key lands on the **nearest** step (past half a step: the next one; the note that sounds now is unchanged, only the written step moves), and every step that starts while the key is still down is written as a TIE. One undo for the whole held note. REC + stopped is unchanged | `home_map.cpp/.h` (key press, release, `update`), `seq` only to expose the playhead's fraction if it is not there | `tests/home_rec_test.cpp`: a key down at 40 % and at 60 % of a step; held over 3 steps writes note + 2 ties; release before the next step writes one step; one undo. Scenario `rec_hold.txt` | "Hold a key and the note lasts as long as you hold it: the steps it covers become ties. A key played just before a step lands on it" | no | **yes** (a held key used to write one step) |
| **U2** Longer by hand, stopped | REC + stopped: with the key still held, **Right** adds a tie step and moves on; Left takes one back. Without REC: hold a step pad + Right makes the next step its tie | `home_map.cpp` (arrows with a key or step held) | `step_edit_test.cpp` cases; scenario `step_edit.txt` extended | A line in "Write notes" and in the buttons table | no | no (a new gesture; the slice first checks what Right does today with a key or a step held) |
| **U3** Ties in NEW | The generator may tie a note into the next step: chance = half of SLD %, only after a playing step and never onto step 1. **The factory lines and VARY, RHYTHM, NOTES keep their output for a seed when SLD % is 0**; with the default 20 % a NEW line differs from v0.6.0's for the same seed | `app/seq/src/gen.cpp`, `ops.cpp` (RHYTHM draws ties too), their tests | `seq` generator tests: distribution, no tie on step 1, seed stability at SLD 0; scenario `tools.txt` regenerated | PATTERN SETUP: "F8 SLD %: how many slides, and half as many ties, NEW draws" | no | mild (NEW sounds less staccato) |
| **U4** One UNDO / REDO | Shift + F7 REDO, Shift + F8 UNDO on HOME (is), PERFORM (is), KNOBS, PATTERN SETUP (REDO added), SONG (both on Shift; **plain F8 UNDO goes**, F8 becomes blank). For one release SONG's plain F8 answers with the toast "UNDO IS SHIFT + F8" | `song_screen.cpp`, `main_screen.cpp` (SETUP's Shift row), `help_texts.cpp` | `song_screen_test`, `pattern_setup_test`, `help_texts_test`; scenarios `song.txt`, `pattern_setup.txt` | One sentence in section 7 "Experiment without fear": "Shift + F8 is UNDO and Shift + F7 is REDO wherever you edit" | no | **yes** (SONG's F8) |
| **U5** Tabs and "again" everywhere | The tab strip in the header of every multi-page button (sentence 2), CLASSIC and OBSIDIAN; Sample Play again = TRACT (A2 does the button, U5 the strip); HELP's BUTTONS page says "AGAIN: next page" for each | `frame.cpp`, `skin/` header code, `main_screen.cpp`, `perform_screen.cpp`, `file_screen.cpp`, `vault_screen.cpp`, `help_texts.cpp` | `skin_core_test`, `skin_home.txt`, `skin_perform.txt`, `skin_file.txt`, `manual_screens.txt`; display list under 1,800 words | "Reading the screen" gains one bullet: the tabs | no | no |
| **U6** Key kinds drawn | Knob, switch, action drawn differently (sentence 4); an `Entry` kind helper in `pages.h` (`Controls::kind(entry)`: Knob, Switch, Action, Blank) from the item and the descriptor (Enum of 2 = Switch); MASTER's TAP, RECORD's ARM / SAVE / DISCARD, SONG's CHAIN / CLEAR, the Shift tools are Actions | `pages.h/.cpp`, `knob_strip.cpp`, skin F-row files, `master_screen.cpp`, `record_screen.cpp`, `song_screen.cpp` | `pages_test` (kind of every entry of every page), `skin_frow_test`, `skin_strip.txt`, `skin_master.txt`, `skin_record.txt` | "The F row" paragraph: three kinds of key, one picture | no | no |
| **U7** No holes | WOBBLE: RATE SHAPE DEPTH TARGET RESET stay F1 to F5 (already contiguous). RECORD: ARM, BARS, SAVE, DISCARD on **F1 F2 F3 F4** (from F1, F3, F6, F7). SONG: PLAYS LOOP CHAIN on F1 to F3, CLEAR on **F4** (from F7). MASTER unchanged (already from F1). DLY MODE stays on MASTER (FX is full; noted in HELP's FX text) | `record_screen.cpp`, `song_screen.cpp`, `help_texts.cpp`, their skins | `record_screen_test`, `song_screen_test`, `skin_master_record_test`, scenarios `record.txt`, `song.txt`, `skin_record.txt`, `skin_song.txt` | RECORD's and SONG's key tables | no | **yes** (RECORD F6 SAVE and F7 DISCARD move, SONG F7 CLEAR moves; both destructive ones still ask twice) |
| **U8** Small fixes seen on the way | (a) WOBBLE's Shift row says "-" eight times: it shows nothing instead, and the hint says NOT LEARNABLE. (b) With TARGET VOWEL or MOUTH and that effect off, F8 WOB says which F8 to press ("GROWL IS OFF: SAMPLE PLAY, F8"). (c) Sample Play and Sample Edit with Shift do what they do without it (as today), written in HELP. (d) HOME's EMPTY PATTERN hint names U1 ("ARM REC, PLAY AND HOLD THE KEYS"). (e) The OVERDRIVE OFF slice in the working tree is committed first | `instrument_screen.cpp`, `live_actions.cpp`, `help_texts.cpp`, `main_screen.cpp` | `instrument_screen_test`, `m3c_ui_test`, `help_texts_test`; `growl_wobble.txt`, `home.txt` | The WOBBLE section's last paragraph | no | no |

Slices of the other items that change a project or a habit, for the same list: **K1** (P: PERF gains two rows; old projects load,
v0.6.0 ignores them. H: a ladder tap on a mod switches MODS on), **K2** (H: Performance's second press is KNOBS, SETUP is the third),
**A1** (P: the sound gains the tract's seven values and a lane target 7; old projects load with TRACT off; v0.6.0 drops a tract lane),
**A2** (P: snapshots hold 48 targets, old ones load; the wobble's TARGET may be MOUTH. H: Sample Play's second press is TRACT).
In o3-voc03er: **V1** (P: Mode may be 4), **V3** (P: `MOUT` chunk in the pattern file. H: in TRACT the fourth lane is MOUTH).

Order inside item 7: U4, U7 (the two habit changes of the F row, in one release so the manual changes once), U5, U6 (drawing), U1,
U2 (entry), U3 (by ear: the maintainer hears NEW with ties before it stays), U8 any time. U5 after K2 and A2 (it draws their tabs).

### 10.6 What only the unit or the maintainer decides in item 7

U1: whether "nearest step" feels right under the fingers at 125 and at 160 BPM (the threshold, 50 %, is one constant). U3: whether
NEW's ties sound like a 303 line or too legato (the ratio, half of SLD %, is one constant). U6: whether the three key drawings are
told apart on the real display at arm's length. U7: whether moving SAVE and DISCARD is worth the habit change (it can be dropped
without touching the other slices).

---

## 11. The maintainer's decisions after the plan (2026-11-11)

These replace the matching parts of sections 3 and 6 (A1 to A3) where they differ.

1. **TRACT is Sample Play's first page and GROWL its second.** Sample Play opens TRACT; Sample Play again flips to GROWL and back.
   This changes a habit (Sample Play used to open GROWL), which the CHANGELOG says.
2. **The note pattern stays as it is.** No vowel goes into the step word or the pattern's step data, and no TRACT lock is needed for
   vowels. ACC, SLIDE, HOME's pads and the eight-lane limit are unchanged.
3. **A TRACT pattern, one for each of the 48 patterns**, a parallel line of the mouth, edited on the TRACT page:
   - A step is "-" (follow the VOWEL knob) or one of A, E, I, O, U, with its own GLD flag (the mouth glides to the next step's vowel
     over the step), independent of the notes' SLIDE.
   - Its own length: SAME (the default, the pattern's length) or 4, 8, 16, 32, 64. A different length runs polymetric against the
     notes, on the same clock.
   - It belongs to its pattern: a pattern change and SONG switch it; COPY PAT, PASTE, DUP>NEXT and UNDO carry it.
   - Saved in a new project chunk beside the pattern's, not in the step data. An older project loads with every line empty. About
     3 KB in the SDRAM (48 x 64 bytes plus the lengths).
   - The sequencer's hook writes the tract unit's VOWEL (and GLIDE for a GLD step) at the step's frame; a "-" step leaves the knob's
     value.
4. **The TRACT page:** F1 VOWEL (the base vowel and the live morph), F2 TONGUE, F3 LIPS, F4 RES, F5 GLIDE, F6 SIZE, F7 TRC MIX, F8
   TRACT on / off. Pads: rows 1 and 2 the TRACT pattern's steps (lit = has a vowel, the playhead runs); row 3 the palette A E I O U -
   and GLD (hold a step + tap = that step, as hold a step + ACC on HOME); row 4 live vowels (a tap moves the mouth now; with REC on
   while playing it writes the current step). Shift + F: the line's LEN and CLEAR.
5. **VOWEL stays** as a knob: the base for "-" steps, the live morph through every in-between value, MIDI learnable, recordable as
   motion like any knob, and the wobble's MOUTH target swings around the vowel that sounds. TONGUE, LIPS and SIZE bend whatever
   vowel sounds.
6. **Live and the TRACT pattern, both, with the pattern switchable** (the maintainer, 2026-11-11):
   - **LINE on / off**: a pad (row 3, column 12, lit when on, as FILT on HOME) switches the TRACT pattern. Off: the line is kept but
     not played, and the mouth is the knobs and the live pads only. Saved with the project; a project without it loads with LINE on
     when its lines hold vowels, off when they are empty.
   - **Row 4, live vowels**: with LINE off a tap moves the mouth to that vowel and it stays (it writes the VOWEL knob, so the F1
     read-out follows). With LINE on, holding a pad overrides the line with that vowel, and the release gives the line back. REC on
     while playing writes the tapped vowel into the current step, in both cases.
   - **NEW for the line** (Shift + F row, beside LEN and CLEAR): draws a line of vowels and GLD flags from the pattern's seed,
     with "-" on some steps, so a line can be generated as well as written. One undo.
