// SPDX-License-Identifier: GPL-3.0-or-later
//
// ACID: the acid voice of o3-303: as close to a Roland TB-303 as the published measurements allow, with the Devil Fish mods on top.
//
// Two sources, both MIT licensed, and neither is any instrument's firmware:
//
// The oscillator and the shape of the engine come from Replicant 303 by DanimanX, commit d5369be028399f5a403067b58da2936caa070c0e
// (third_party/replicant303/DSP/): Oscillator303.cpp, Common303.h, and handleNoteOn / handleNoteOff of SynthEngine.cpp.
//
//   MIT License
//
//   Copyright (c) 2026 Daniel Rojas Grasset (DanimanX)
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all
//   copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//   SOFTWARE.
//
// The filter, the cutoff law, the amplifier and the high-passes around them are Open303 by Robin Schmidt
// (github.com/RobinSchmidt/Open303): TeeBeeFilter's TB_303 mode with its fitted coefficients, the measured envelope mapping of
// Open303::calculateEnvModScalerAndOffset, and the signal order of Open303::getSample, whose numbers were measured on a TB-303.
//
//   Copyright (c) 2009 Robin Schmidt
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all
//   copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//   SOFTWARE.
//
// The accent sweep, the slide's time, the gate's end and the mods' ranges are from Robin Whittle's published descriptions of the TB-303 and
// the Devil Fish (the Devil Fish manual and "What makes the TB-303 unique"): text and a circuit, no code.
//
// Structure:
//   * per sample, in short passes over a control period: the pitch glide, the oscillator and the input high-pass; the envelopes and the
//     amplifier's de-clicker; the ladder (two sub-steps, one division each); the output's all-pass, high-pass and notch and the amplifier;
//   * every kControlPeriod (16) samples (control()): the smoothers of Volume and Accent, the accent sweep circuit, the cutoff law by
//     core::exp2_fast and the ladder's coefficients by Open303's fits. They are computed for the end of the period and ramped linearly to it
//     (exact at every period boundary), and set at once when a note starts or the cutoff law changes;
//   * on an event: pitch (exp2_fast), envelope coefficients (std::exp), the note stack, the tune and resonance values.
// Real-time: no allocation, no double, no libm in the sample loop; bounded loops.

#include <o3_303/units/acid_voice/acid_voice.h>

#include <o3sdk/core/fast_math.h>

#include <cmath>

namespace o3_303 {

namespace {

using o3sdk::core::ParamCurve;
using o3sdk::core::ParamKnot;
using o3sdk::core::ParamUnit;
namespace dsp = o3sdk::dsp;

// A note-on at or above this velocity is accented, as one with kNoteAccent is (a MIDI keyboard); the sequencer sends 80 and 110, the pads 90.
constexpr std::uint8_t kAccentVelocity = 96;
constexpr const char* kWaveNames[] = {"SAW", "SQR"};
constexpr const char* kSoftAtkNames[] = {"OFF", "0.3 MS", "1 MS", "3 MS", "10 MS", "30 MS"};
constexpr const char* kSubNames[] = {"OFF", "LOW", "MID", "HIGH"};
constexpr const char* kSweepNames[] = {"OFF", "NORMAL", "HI RES"};
constexpr const char* kMufflerNames[] = {"OFF", "SOFT", "HARD"};
constexpr const char* kSpeedNames[] = {"FAST", "NORMAL", "SLOW"};
constexpr const char* kOffOnNames[] = {"OFF", "ON"};
constexpr const char* kRangeNames[] = {"303", "FISH"};

// Replicant's cutoff slider is value = 25 + 4475 x p^(1/0.35) (JUCE skew 0.35); a table through that curve, knots where it bends
// (the position error against the smooth curve is about 3 %, the value error under 2 % above 100 Hz).
constexpr ParamKnot kCutoffKnots[] = {
    {0.0000f, 25.0f},   {0.0857f, 29.0f},   {0.1221f, 36.0f},   {0.1581f, 48.0f},   {0.1999f, 70.0f},   {0.2445f, 105.0f},
    {0.2937f, 160.0f},  {0.3484f, 245.0f},  {0.4078f, 370.0f},  {0.4755f, 560.0f},  {0.5533f, 850.0f},  {0.6262f, 1200.0f},
    {0.7015f, 1650.0f}, {0.7768f, 2200.0f}, {0.8565f, 2900.0f}, {0.9289f, 3650.0f}, {1.0000f, 4500.0f},
};

constexpr std::uint8_t kSmooth = o3sdk::core::kParamSmooth;

// name, unit text, names, knots, min, max, default, detents, coarse, list count, display, curve, flags, page
constexpr o3sdk::core::ParamDesc kParams[AcidVoice::kParamCount] = {
    {"CUTOFF", "Hz", nullptr, kCutoffKnots, 25.0f, 4500.0f, 350.0f, 250, 5, 17, ParamUnit::Hz, ParamCurve::Table, 0, 0},
    {"RESO", "%", nullptr, nullptr, 0.0f, 1.0f, 0.65f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"ENV MOD", "%", nullptr, nullptr, 0.0f, 1.0f, 0.6f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"DECAY", "ms", nullptr, nullptr, 30.0f, 3000.0f, 750.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Log, 0, 0},
    {"ACCENT", "%", nullptr, nullptr, 0.0f, 1.0f, 0.7f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, kSmooth, 0},
    {"WAVE", "", kWaveNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"TUNE", "st", nullptr, nullptr, -12.0f, 12.0f, 0.0f, 240, 10, 0, ParamUnit::Fixed1, ParamCurve::Linear, 0, 0},
    {"VOLUME", "dB", nullptr, nullptr, -60.0f, 6.0f, -3.0f, 132, 4, 0, ParamUnit::Fixed1, ParamCurve::Linear, kSmooth, 0},
    // The mods (docs/MODS-PLAN.md 3.1): each default is the stock voice. Time rows are Log with a positive minimum.
    {"ACC DECAY", "ms", nullptr, nullptr, 30.0f, 3000.0f, 200.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Log, 0, 0},
    {"TRACKING", "%", nullptr, nullptr, 0.0f, 150.0f, 0.0f, 150, 10, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0},
    {"SLIDE", "ms", nullptr, nullptr, 60.0f, 360.0f, 60.0f, 60, 10, 0, ParamUnit::Int, ParamCurve::Log, 0, 0},
    {"SOFT ATK", "", kSoftAtkNames, nullptr, 0.0f, 5.0f, 0.0f, 5, 1, 6, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"SUB OSC", "", kSubNames, nullptr, 0.0f, 3.0f, 0.0f, 3, 1, 4, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"FILTER FM", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, kSmooth, 0},
    {"ACC SWEEP", "", kSweepNames, nullptr, 0.0f, 2.0f, 1.0f, 2, 1, 3, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"MUFFLER", "", kMufflerNames, nullptr, 0.0f, 2.0f, 0.0f, 2, 1, 3, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"SWP SPEED", "", kSpeedNames, nullptr, 0.0f, 2.0f, 1.0f, 2, 1, 3, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"OVERDRIVE", "dB", nullptr, nullptr, -24.0f, 36.0f, 0.0f, 120, 4, 0, ParamUnit::Fixed1, ParamCurve::Linear, kSmooth, 0},
    {"AMP DECAY", "ms", nullptr, nullptr, 16.0f, 4000.0f, 1230.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Log, 0, 0},
    {"STOCK", "", kOffOnNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"RANGE", "", kRangeNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"SUSTAIN", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
};
static_assert(AcidVoice::kParamCount == 22, "the voice has 8 parameters of the 303, 11 mods, Stock and two more mods");

constexpr dsp::UnitInfo kInfo{"ACID", dsp::UnitKind::Instrument, kParams, AcidVoice::kParamCount, nullptr, 0};

// ---- Replicant 303: the oscillator -------------------------------------------------------------------------------------------
constexpr float kPi = 3.14159265f;
constexpr float kSawLevel = 0.95f;
constexpr float kSqrLevel = 0.90f;
constexpr float kSqrDroopHz = 25.0f;        // the RC droop high-pass of the square
constexpr float kMinCutoff = 25.0f;         // the knob's ends
constexpr float kMaxCutoff = 4800.0f;

// ---- Open303: what was measured on a TB-303 ------------------------------------------------------------------------------------
constexpr float kSqrDuty = 0.4688f;         // the square: -tanh(dB2amp(36.9) saw + 4.37) crosses zero at saw = -0.0624
constexpr float kFbHpHz = 150.0f;           // TeeBeeFilter: the high-pass in the feedback path
constexpr float kPreHpHz = 44.486f;         // highpass1: before the filter
constexpr float kAllpassHz = 14.008f;       // allpass, highpass2 and notch: after it
constexpr float kPostHpHz = 24.167f;
constexpr float kNotchHz = 7.5164f;
constexpr float kNotchTwoR = 4.9021f;       // the notch's bandwidth of 4.7 octaves: 2 sinh(ln 2 / 2 x 4.7), the damping of its state-variable form
constexpr float kDeclickHz = 200.0f;        // ampDeClicker: a 12 dB low-pass of the amplifier's envelope, Q sqrt(0.5)
constexpr float kDeclickDamp = 1.41421356f;
constexpr float kMainToAmp = 0.45f;         // the filter envelope in the amplifier, on every note
constexpr float kAccentToAmp = 4.0f;        // and on an accent, x Accent
constexpr float kAccentTau = 0.200f;        // seconds: the filter envelope's decay on an accent (Whittle too)
constexpr float kHoldTau = 1.230f;          // the amplifier's decay
constexpr float kReleaseTau = 0.003f;       // the end of a note: see kGateHold
constexpr float kAccReleaseTau = 0.050f;    // and of an accented one
constexpr float kSlideScale = 0.2f;         // the glide's time constant against the audible time of Slide (60 ms): Open303, and Whittle's reading of it
constexpr float kCutoffFloorHz = 200.0f;    // TeeBeeFilter::setCutoff: the sweep stops here (the knob itself can go lower: a mod)
constexpr float kLog2C0 = 8.2938f;          // log2 of c0 and c1, the nominal cutoff range the law was measured over
constexpr float kInvLog2Span = 1.0f / (11.2255f - kLog2C0);
constexpr float kLawOffF = 0.048292930943553f;
constexpr float kLawOffC = 0.294391201442418f;
constexpr float kLawLoF = 3.773996325111173f;
constexpr float kLawLoC = 0.736965594166206f;
constexpr float kLawHiF = 4.194548788411135f;
constexpr float kLawHiC = 0.864344900642434f;
constexpr float kResoSkew = -3.0f;          // TeeBeeFilter::setResonance: (1 - e^(-3 r)) / (1 - e^-3)
constexpr float kResoSkewNorm = 1.0f / 0.95021293f;

// ---- Whittle: the TB-303 and the Devil Fish -------------------------------------------------------------------------------------
constexpr float kGateHold = 0.008f;         // seconds: an unaccented note stays at full level this long after the gate, then falls in about 8 ms
constexpr float kFishReleaseTau = 0.008f;   // Devil Fish: a logarithmic decay that starts at the gate's end
constexpr float kFishHpHz = 12.0f;          // Devil Fish: 32 Hz is down 1 dB, not 5
constexpr float kDecayMin = 0.03f;          // seconds: both decays of the Devil Fish, 30 ms to 3 s
constexpr float kDecayMax = 3.0f;
constexpr float kFishEnvRange = 3.0f;       // Devil Fish: Env Mod goes to zero and to three times the TB-303's top
// The accent sweep circuit, in units of 100 k: a diode and 47 k from the accent's MEG into one end of the second section of the Resonance pot, a
// capacitor at the other end (C13, 1 uF), the wiper through 100 k into the filter's summing node.
constexpr float kSweepRd = 0.47f;
constexpr float kSweepR2Min = 0.05f;        // the pot's track is never quite zero: keeps the node equation finite at full Resonance
constexpr float kSweepTau = 0.100f;         // seconds: 100 k x 1 uF
constexpr float kSweepSlowTau = 0.300f;     // Swp Speed SLOW: "a much longer time-constant"
constexpr float kSweepSlowGain = 2.0f;      // "it can rise twice as high"
constexpr float kSweepOctaves = 2.4f;       // octaves per unit at the summing node: Open303's accent of about one octave, at a Resonance of about half

// ---- ours --------------------------------------------------------------------------------------------------------------------
constexpr float kSmoothTau = 0.005f;        // seconds: parameter smoothing
constexpr float kAudible = 1.0e-4f;         // amplifier level (-80 dB) under which a released voice no longer counts as sounding
constexpr float kTiny = 1.0e-9f;            // envelopes below this (-180 dB) are flushed to zero: the output never lingers at amplitudes where a
                                            // product of three samples (soft clip in the distortion) is subnormal
constexpr float kSilentDb = -59.5f;         // at or below: silence
constexpr float kLog2Ten20 = 0.16609640f;   // log2(10) / 20: dB to a power of two
constexpr float kLadderBound = 2.0f;        // the level at which the ladder's input saturates: nearly linear on the oscillator, and it bounds HI RES and Overdrive
constexpr float kInvLadderBound = 1.0f / kLadderBound;
constexpr float kInvSqrt2 = 0.70710678f;
constexpr float kAmpScale = 0.45f;          // the amplifier's gain: on the factory line an accent peaks just under full scale, and a note sits about 9 dB under it, as on the instrument
constexpr float kKnee = 0.9f;               // the output's ceiling: linear to here, then it bends towards 1. Nothing after the voice limits, and a bright accent has the reach to clip
constexpr float kInvKneeRoom = 1.0f / (1.0f - kKnee);
constexpr float kFishDeclickHz = 1500.0f;   // Devil Fish: notes start within 0.5 ms
constexpr float kB0Max = 0.25f;             // the ladder's step at about 10 kHz: the ceiling of Filter FM's modulated step
constexpr float kOscSeed = 1.0e-6f;         // into the modded ladder: with the oscillator off, something for HI RES to grow from

// ---- the mods ----------------------------------------------------------------------------------------------------------------
constexpr float kLog2C2 = 6.0313599f;       // log2(65.4064 Hz): Tracking's reference, C2, where the cutoff is the knob's at any setting
constexpr float kLog2MinCutoff = 4.6438562f;  // log2(25 Hz): the floor of the tracked cutoff
constexpr float kAttDone = 1.0e-4f;         // the soft attack is over this close to 1
constexpr float kAttRise = 2.1972246f;      // ln 9: an RC charge's 10 to 90 % rise takes this many time constants
constexpr float kSoftAtkMs[6] = {0.0f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f};
constexpr float kSubLevel[4] = {0.0f, 0.25f, 0.5f, 0.8f};  // of the main wave's peak: starting values, tuned by ear at the unit gate
constexpr float kRampSeconds = 0.005f;      // a mod's ramp over its whole range
constexpr float kFmDepth = 2.0f;            // Filter FM: k = 2 x the amount (the ladder's step is scaled by 1 + k y)
constexpr float kFmDcHz = 5.0f;             // the DC blocker of the output that modulates the filter
constexpr float kMufSoftT = 0.7f;           // Muffler SOFT: the level where the whole signal starts to flatten
constexpr float kMufHardT = 0.19952623f;    // Muffler HARD: the band above the corner is clipped at -14 dB
constexpr float kMufHardHz = 150.0f;        // Muffler HARD: below this the bass passes
constexpr float kSwpFastTau = 0.400f;       // Swp Speed FAST: the slow follower that is subtracted
constexpr float kOdMinDb = -24.0f;
constexpr float kOdMaxDb = 36.0f;

inline float clampf(float x, float lo, float hi) noexcept { return x < lo ? lo : (x > hi ? hi : x); }
// For a parameter value that comes from outside: a NaN is the lower end.
inline float clamp_param(float x, float lo, float hi) noexcept { return !(x >= lo) ? lo : (x > hi ? hi : x); }
// For a mod's value that comes from outside: a number that is not finite is the mod's stock value, not the lower end.
inline float mod_param(float x, float lo, float hi, float stock) noexcept {
  return (x - x == 0.0f) ? (x < lo ? lo : (x > hi ? hi : x)) : stock;
}
// The index of an Enum value (0..count-1).
inline unsigned enum_index(float v, unsigned count) noexcept {
  const unsigned i = static_cast<unsigned>(v + 0.5f);
  return i < count ? i : count - 1u;
}
inline float absf(float x) noexcept { return x < 0.0f ? -x : x; }
// The gain of a dB value; 0 dB is exactly 1.
inline float db_gain(float db) noexcept { return db == 0.0f ? 1.0f : o3sdk::core::exp2_fast(db * kLog2Ten20); }
// Overdrive's gain: the bottom of its range is none at all, as the Devil Fish's pot, so HI RES can be heard alone.
inline float od_gain(float db) noexcept { return db <= kOdMinDb ? 0.0f : db_gain(db); }

// Common303.h fastTanh: a Pade rational, one division. Exact in shape for |x| < 3, then it keeps rising with slope 1/9 (it is not a
// saturator beyond that; the ladder never takes it there, see the guard in run()).
inline float tanh_pade(float x) noexcept {
  const float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// tanh_pade with the argument bounded: from |x| = 3 on, where the rational reaches exactly 1, it returns +-1 (the rational itself rises again
// with slope 1/9). The modded loop uses it where a mod can push an argument past 3 (Overdrive, HI RES, the muffler); below 3 it is
// tanh_pade bit for bit. A branch, rarely taken: a compare and a branch is cheaper here than two clamps.
inline float tanh_sat(float x) noexcept {
  const float x2 = x * x;
  if (__builtin_expect(x2 >= 9.0f, 0)) {
    return x < 0.0f ? -1.0f : 1.0f;
  }
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// c^kControlPeriod (kControlPeriod is a power of two): the envelope's factor over one control period.
inline float power_n(float c) noexcept {
  static_assert((AcidVoice::kControlPeriod & (AcidVoice::kControlPeriod - 1u)) == 0u, "kControlPeriod is a power of two");
  for (unsigned n = AcidVoice::kControlPeriod; n > 1u; n >>= 1) {
    c *= c;
  }
  return c;
}

// Oscillator303::polyBlep.
inline float poly_blep(float t, float dt) noexcept {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.0f;
  }
  if (t > 1.0f - dt) {
    t = (t - 1.0f) / dt;
    return t * t + t + t + 1.0f;
  }
  return 0.0f;
}

// The output's ceiling: the sample itself up to kKnee, a rational bend from there that never reaches 1. A branch, rarely taken.
inline float ceiling(float y) noexcept {
  const float a = absf(y);
  if (__builtin_expect(a <= kKnee, 1)) {
    return y;
  }
  const float t = (a - kKnee) * kInvKneeRoom;
  const float c = kKnee + (1.0f - kKnee) * t / (1.0f + t);
  return y < 0.0f ? -c : c;
}

// A one-pole high-pass as Open303's OnePoleFilter computes it, for the states `x` (the last input) and `y`, with `b` = (1 + a) / 2.
inline float high_pass(float in, float& x, float& y, float a, float b) noexcept {
  y = b * (in - x) + a * y;
  x = in;
  return y;
}

// One step of the ladder (TeeBeeFilter::getSample, TB_303 mode): the feedback through its high-pass, the input through the soft bound, the four
// coupled stages. `y1..y4` are the states, `fx` and `fy` the high-pass's, `in` the input, `k` the feedback, `b` the step.
inline void ladder_step(float in, float& y1, float& y2, float& y3, float& y4, float& fx, float& fy, float a, float hb, float k, float b) noexcept {
  const float fb = high_pass(k * y4, fx, fy, a, hb);
  const float y0 = kLadderBound * tanh_sat((in - fb) * kInvLadderBound);
  y1 += 2.0f * b * (y0 - y1 + y2);
  y2 += b * (y1 - 2.0f * y2 + y3);
  y3 += b * (y2 - 2.0f * y3 + y4);
  y4 += b * (y3 - 2.0f * y4);
}

// Oscillator303::process: advances the phase by `inc` and returns the sample. `hp` is the square's droop state, `hp_coeff` its pole.
template <bool Square>
inline float osc_sample(float& phase, float& hp, float inc, float hp_coeff) noexcept {
  phase += inc;
  if (phase >= 1.0f) {
    phase -= 1.0f;
  }
  if constexpr (!Square) {
    return (2.0f * phase - 1.0f - poly_blep(phase, inc)) * kSawLevel;
  } else {
    float sqr = phase < kSqrDuty ? 1.0f : -1.0f;
    sqr += poly_blep(phase, inc);
    float shifted = phase - kSqrDuty;
    if (shifted < 0.0f) {
      shifted += 1.0f;
    }
    sqr -= poly_blep(shifted, inc);
    const float out = hp_coeff * (hp + sqr);
    hp = out - sqr;
    return out * kSqrLevel;
  }
}

}  // namespace

const dsp::UnitInfo& AcidVoice::info() const noexcept { return kInfo; }

// ---- parameters ---------------------------------------------------------------------------------------------------------------

// The cutoff law changed: the filter coefficient follows from the next sample (the original reads the parameters every sample).
void AcidVoice::retune_filter() noexcept {
  fresh_ = true;
  ctl_ = 0;
}

bool AcidVoice::sounding() const noexcept { return gate_ || vca_ > kAudible; }

void AcidVoice::jump(Smoothed& s, float v) noexcept {
  s.target = v;
  s.lp.jump(v);
  s.moving = false;
}

// One control period of a smoother. Returns the value to use now; settles (exactly) on the target when closer than `eps`.
float AcidVoice::step(Smoothed& s, float eps) noexcept {
  const float v = s.lp.next(s.target);
  const float d = v - s.target;
  if ((d < 0.0f ? -d : d) <= eps) {
    s.lp.jump(s.target);
    s.moving = false;
    return s.target;
  }
  return v;
}

// Open303::calculateEnvModScalerAndOffset: how many octaves the envelope sweeps and where the sweep sits around the knob, from the two knobs.
// The measured range of the law is c0..c1: a knob outside it reads as that end. The sweep goes above and below the knob.
void AcidVoice::update_law() noexcept {
  const float c = clampf((cutoff_l2_ - kLog2C0) * kInvLog2Span, 0.0f, 1.0f);
  const float e = env_mod_;
  env_off_ = kLawOffF * c + kLawOffC;
  if (fish_) {  // no floor under the knob, and three times the reach
    env_scale_ = kFishEnvRange * ((1.0f - c) * kLawLoF + c * kLawHiF) * e;
  } else {
    env_scale_ = (1.0f - c) * (kLawLoF * e + kLawLoC) + c * (kLawHiF * e + kLawHiC);
  }
}

// TeeBeeFilter::setResonance, and the sweep circuit's resistances, which follow the same pot.
void AcidVoice::update_reso() noexcept {
  const float p = reso_;
  res_ = (1.0f - std::exp(kResoSkew * p)) * kResoSkewNorm;
  fb_r_ = hires_ ? 2.0f * res_ : res_;
  const float r2 = 1.0f - p < kSweepR2Min ? kSweepR2Min : 1.0f - p;
  sw_ga_ = 1.0f / (kSweepRd + p);
  sw_g2_ = 1.0f / r2;
  sw_gt_ = 1.0f / (sw_ga_ + sw_g2_ + 1.0f);
  sw_off_ = sw_g2_ / (sw_g2_ + 1.0f);
}

// RANGE: what the Devil Fish changes for good. The envelope's reach, the bass and the end of a note.
void AcidVoice::update_range() noexcept {
  const float hz_pre = fish_ ? kFishHpHz : kPreHpHz;
  const float hz_post = fish_ ? kFishHpHz : kPostHpHz;
  pre_a_ = std::exp(-2.0f * kPi * hz_pre * inv_fs_);
  pre_b_ = 0.5f * (1.0f + pre_a_);
  post_a_ = std::exp(-2.0f * kPi * hz_post * inv_fs_);
  post_b_ = 0.5f * (1.0f + post_a_);
  notch_two_r_ = fish_ ? 0.0f : kNotchTwoR;
  declick_f_ = 2.0f * std::sin(kPi * (fish_ ? kFishDeclickHz : kDeclickHz) * inv_fs_);
  update_law();
  retune_filter();
}

void AcidVoice::set(std::uint16_t id, float v) noexcept {
  switch (id) {
    case Cutoff:
      cutoff_l2_ = o3sdk::core::log2_fast(clamp_param(v, kMinCutoff, kMaxCutoff));
      update_law();
      retune_filter();
      break;
    case Reso:
      reso_ = clamp_param(v, 0.0f, 1.0f);
      update_reso();
      break;
    case EnvMod:
      env_mod_ = clamp_param(v, 0.0f, 1.0f);
      update_law();
      retune_filter();
      break;
    case Decay: {
      const float seconds = clamp_param(v * 0.001f, kDecayMin, kDecayMax);  // the filter envelope's time constant
      decay_coeff_ = std::exp(-inv_fs_ / seconds);
      decay_coeff_n_ = power_n(decay_coeff_);
      break;
    }
    case Accent: {
      const float a = clamp_param(v, 0.0f, 1.0f);
      accent_amt_ = a;
      accent_.target = a;
      accent_.moving = true;
      retune_filter();
      break;
    }
    case Wave:
      square_ = v >= 0.5f;
      break;
    case Tune:
      tune_scale_ = o3sdk::core::exp2_fast(clamp_param(v, -12.0f, 12.0f) * (1.0f / 12.0f)) * inv_fs_;
      break;
    case Volume:
      volume_.target = !(v > kSilentDb) ? 0.0f : o3sdk::core::exp2_fast(clamp_param(v, kSilentDb, 6.0f) * kLog2Ten20);  // NaN is silence
      volume_.moving = true;
      break;
    default:
      if (id >= kFirstMod && id < kParamCount) {
        set_mod(id, v);
      }
      break;
  }
}

// ---- the mods: values and derived coefficients ---------------------------------------------------------------------------------

// A mod's value as it is used now: its stock value while Stock is on, else the one that was set.
float AcidVoice::eff(unsigned id) const noexcept { return stock_ ? kParams[id].def : raw_[id - kFirstMod]; }

void AcidVoice::set_mod(std::uint16_t id, float v) noexcept {
  if (id == Stock) {
    const bool on = (v - v == 0.0f) && v >= 0.5f;  // a number that is not finite is OFF
    if (on != stock_) {
      stock_ = on;
      const bool fresh = fresh_;
      refresh_mods();
      if (on) {
        fresh_ = fresh;  // from the mods, loud and with the filter far away: it ramps over the next control period, as a jump of its gain clicks
      }
    }
    return;
  }
  raw_[id - kFirstMod] = mod_param(v, kParams[id].min, kParams[id].max, kParams[id].def);
  apply_mod(id);
}

void AcidVoice::refresh_mods() noexcept {
  for (unsigned id = kFirstMod; id < kParamCount; ++id) {
    if (id != Stock) {
      apply_mod(id);
    }
  }
}

// Derives what the loops use from one mod's value (eff()). The stock value of a coefficient is the one prepare() computed from the
// constant, assigned; any other value goes through std::exp once, here, on a float.
void AcidVoice::apply_mod(unsigned id) noexcept {
  const float v = eff(id);
  switch (id) {
    case AccDecay:
      if (v == kParams[AccDecay].def) {
        accent_coeff_ = stock_accent_c_;
        accent_coeff_n_ = stock_accent_cn_;
      } else {
        accent_coeff_ = std::exp(-inv_fs_ / (v * 0.001f));
        accent_coeff_n_ = power_n(accent_coeff_);
      }
      break;
    case Tracking: {
      const bool on = v > 0.0f;
      const float t = on ? v * 0.01f : 0.0f;
      if (on != track_on_ || t != track_) {
        track_on_ = on;
        track_ = t;
        retune_filter();
      }
      break;
    }
    case Slide:
      slide_coeff_ = v <= kParams[Slide].min ? stock_slide_c_ : 1.0f - std::exp(-inv_fs_ / (kSlideScale * v * 0.001f));
      break;
    case SoftAtk: {
      att_idx_ = static_cast<std::uint8_t>(enum_index(v, 6));
      if (att_idx_ == 0u) {
        att_c_ = 0.0f;
        att_ = 1.0f;  // a ramp that runs ends here: the attack's own edge
      } else {
        att_c_ = 1.0f - std::exp(-kAttRise * inv_fs_ / (kSoftAtkMs[att_idx_] * 0.001f));
      }
      break;
    }
    case SubOsc:
      sub_.target = kSubLevel[enum_index(v, 4)];
      ramp_pending_ = true;
      break;
    case FilterFm:
      fm_.target = kFmDepth * v;
      ramp_pending_ = true;
      break;
    case AccSweep: {
      const std::uint8_t m = static_cast<std::uint8_t>(enum_index(v, 3));
      if (m != sweep_mode_) {
        sweep_mode_ = m;
        hires_ = m == 2u;
        update_reso();
        retune_filter();
        ramp_pending_ = true;  // HI RES lives in the modded loop: the next control() must choose it
      }
      break;
    }
    case Muffler:
      muf_want_ = static_cast<std::uint8_t>(enum_index(v, 3));
      ramp_pending_ = true;
      break;
    case SwpSpeed: {
      const std::uint8_t m = static_cast<std::uint8_t>(enum_index(v, 3));
      if (m != swp_mode_) {
        swp_mode_ = m;
        swp_m_ = 0.0f;
        sw_rate_ = m == 2u ? sw_rate_slow_ : sw_rate_normal_;
        retune_filter();
      }
      break;
    }
    case Overdrive:
      od_db_t_ = v;
      ramp_pending_ = true;
      break;
    case AmpDecay:
      hold_coeff_ = v == kParams[AmpDecay].def ? stock_hold_c_ : std::exp(-inv_fs_ / (v * 0.001f));
      break;
    case Range: {
      const bool fish = v >= 0.5f;
      if (fish != fish_) {
        fish_ = fish;
        update_range();
      }
      break;
    }
    case Sustain:
      sustain_ = v;
      break;
    default:
      break;
  }
}

// Takes the smoothed parameters at their targets: at prepare, and when the engine delivers the stored values (a process() of zero frames).
void AcidVoice::snap() noexcept {
  jump(accent_, accent_.target);
  jump(volume_, volume_.target);
  accent_boost_ = kAccentToAmp * accent_.target;
  gain_ = volume_.target;

  // The mods' ramps are at their targets from the first sample, as a fresh voice has them.
  sub_.v = sub_.target;
  sub_.step = 0.0f;
  subq_ = 1.0f;
  od_db_ = od_db_t_;
  od_g_ = od_gain(od_db_t_);
  od_step_ = 0.0f;
  fm_.v = fm_.target;
  fm_xm1_ = 0.0f;
  fm_ym1_ = 0.0f;
  fm_y_ = 0.0f;
  muf_type_ = muf_want_;
  load_muffler();
  mix_.target = muf_type_ != 0u ? 1.0f : 0.0f;
  mix_.v = mix_.target;
  mix_.step = 0.0f;
  muf_lp_ = 0.0f;
  ramp_pending_ = false;
  modded_ = mods_needed();
}

void AcidVoice::prepare(float sample_rate) noexcept {
  fs_ = sample_rate < 1000.0f ? 1000.0f : sample_rate;
  inv_fs_ = 1.0f / fs_;
  smooth_k_ = 1.0f - std::exp(-static_cast<float>(kControlPeriod) * inv_fs_ / kSmoothTau);
  x_max_ = o3sdk::core::log2_fast(kMaxCutoff);
  x_min_ = o3sdk::core::log2_fast(kMinCutoff);
  x_floor_ = o3sdk::core::log2_fast(kCutoffFloorHz);
  fx_scale_ = kInvSqrt2 * 0.5f * inv_fs_;  // fc / (sqrt 2 x the sub-step's rate): Open303's fx

  // Oscillator303::prepare, DiodeLadderFilter::prepare, Envelope303::updateCoefficients, SynthEngine::updateSlideCoefficient.
  {
    const float rc = 1.0f / (2.0f * kPi * kSqrDroopHz);
    sqr_hp_coeff_ = rc / (rc + inv_fs_);
  }
  fb_a_ = std::exp(-kPi * kFbHpHz * inv_fs_);  // at the sub-step's rate, 2 fs
  fb_b_ = 0.5f * (1.0f + fb_a_);
  {
    const float t = std::tan(kPi * kAllpassHz * inv_fs_);
    ap_c_ = (t - 1.0f) / (t + 1.0f);
  }
  notch_g_ = std::tan(kPi * kNotchHz * inv_fs_);
  accent_coeff_ = std::exp(-inv_fs_ / kAccentTau);
  accent_coeff_n_ = power_n(accent_coeff_);
  hold_coeff_ = std::exp(-inv_fs_ / kHoldTau);
  release_c_ = std::exp(-inv_fs_ / kReleaseTau);
  release_acc_c_ = std::exp(-inv_fs_ / kAccReleaseTau);
  release_fish_c_ = std::exp(-inv_fs_ / kFishReleaseTau);
  gate_hold_ = static_cast<std::uint16_t>(kGateHold * fs_ + 0.5f);
  sw_rate_normal_ = static_cast<float>(kControlPeriod) * inv_fs_ / kSweepTau;
  sw_rate_slow_ = static_cast<float>(kControlPeriod) * inv_fs_ / kSweepSlowTau;
  sw_rate_ = sw_rate_normal_;
  slide_coeff_ = 1.0f - std::exp(-inv_fs_ / (kSlideScale * kParams[Slide].def * 0.001f));
  inc_lo_ = 10.0f * inv_fs_;
  inc_hi_ = 0.45f;

  // The mods: the stock coefficients above are kept for the stock values, the rest of what they need is computed here.
  stock_accent_c_ = accent_coeff_;
  stock_accent_cn_ = accent_coeff_n_;
  stock_slide_c_ = slide_coeff_;
  stock_hold_c_ = hold_coeff_;
  {
    const float periods = kRampSeconds * fs_ / static_cast<float>(kControlPeriod);  // control periods in a ramp
    const float per = 1.0f / (periods < 1.0f ? 1.0f : periods);
    ramp_sub_rate_ = kSubLevel[3] * per;
    ramp_mix_rate_ = per;
    ramp_fm_rate_ = kFmDepth * per;
    ramp_od_rate_ = (kOdMaxDb - kOdMinDb) * per;
  }
  fm_dc_r_ = 1.0f - 2.0f * kPi * kFmDcHz * inv_fs_;
  {
    float k = 0.0f, g = 0.0f;
    ladder_coeffs(x_min_, b0_min_, k, g);
  }
  muf_hard_a_ = 1.0f - std::exp(-2.0f * kPi * kMufHardHz * inv_fs_);
  swp_fast_c_ = 1.0f - std::exp(-static_cast<float>(kControlPeriod) * inv_fs_ / kSwpFastTau);
  stock_ = false;
  track_on_ = false;
  track_ = 0.0f;
  sweep_mode_ = 1;
  swp_mode_ = 1;
  hires_ = false;
  swp_m_ = 0.0f;
  att_ = 1.0f;
  att_c_ = 0.0f;
  att_idx_ = 0;
  reso_ = 0.0f;
  fish_ = false;
  sustain_ = 0.0f;
  last_out_ = 0.0f;
  muf_lp_ = 0.0f;

  accent_.lp.set_coeff(smooth_k_);
  volume_.lp.set_coeff(smooth_k_);
  update_range();
  for (unsigned p = 0; p < kParamCount; ++p) {
    set(static_cast<std::uint16_t>(p), kParams[p].def);
  }
  snap();  // the defaults are in use from the first sample

  phase_ = 0.0f;
  sqr_hp_ = 0.0f;
  pitch_hz_ = 110.0f;
  target_hz_ = 110.0f;
  for (unsigned i = 0; i < 4; ++i) {
    s_[i] = 0.0f;
  }
  fb_x_ = fb_y_ = 0.0f;
  pre_x_ = pre_y_ = 0.0f;
  ap_x_ = ap_y_ = 0.0f;
  post_x_ = post_y_ = 0.0f;
  notch_s1_ = notch_s2_ = 0.0f;
  dk_lp_ = dk_bp_ = 0.0f;
  lad_b_ = b0_min_;
  lad_k_ = 0.0f;
  lad_g_ = 2.0f;
  lad_b_step_ = lad_k_step_ = lad_g_step_ = 0.0f;
  fresh_ = true;
  vcf_ = 0.0f;
  vca_ = 0.0f;
  cap_ = 0.0f;
  sweep_oct_ = 0.0f;
  released_ = true;
  gate_wait_ = 0;
  since_control_ = 0;
  amp_lift_ = 0.0f;
  gate_ = false;
  has_accent_ = false;
  held_count_ = 0;
  ctl_ = 0;
}

// ---- events -------------------------------------------------------------------------------------------------------------------

namespace {
inline float midi_to_hz(std::uint8_t note) noexcept {
  return 440.0f * o3sdk::core::exp2_fast((static_cast<float>(note) - 69.0f) * (1.0f / 12.0f));
}
}  // namespace

void AcidVoice::note_on(std::uint8_t note, bool accent, bool slide) noexcept {
  // SynthEngine::handleNoteOn: legato when a note is held (or, by the slide flag, while the voice still sounds).
  const bool legato = held_count_ != 0u || (slide && sounding());
  target_hz_ = midi_to_hz(note);
  if (!legato) {
    pitch_hz_ = target_hz_;
  }
  // The stack: the new note goes on top. A note that is already held moves to the top instead of being pushed again (a slide onto the
  // same note arrives without a NoteOff for the old one; the one NoteOff that follows must release it), and past kHeld the oldest goes.
  unsigned at = held_count_;
  for (unsigned i = 0; i < held_count_; ++i) {
    if (held_[i] == note) {
      at = i;
      break;
    }
  }
  if (at < held_count_) {
    for (unsigned j = at + 1u; j < held_count_; ++j) {
      held_[j - 1u] = held_[j];
    }
    --held_count_;
  } else if (held_count_ == kHeld) {
    for (unsigned i = 1; i < kHeld; ++i) {
      held_[i - 1u] = held_[i];
    }
    --held_count_;
  }
  held_[held_count_++] = note;

  // Envelope303::noteOn.
  gate_ = true;
  released_ = false;
  gate_wait_ = 0;
  has_accent_ = accent;
  if (!legato) {
    vcf_ = 1.0f;
    vca_ = 1.0f;
  }
  // Soft Atk: a note without accent that starts the voice fades in; an accented, legato or sliding note keeps the instant attack.
  if (att_idx_ != 0u && !legato && !accent) {
    att_ = 0.0f;
    modded_ = true;
  } else {
    att_ = 1.0f;
  }
  fresh_ = true;
  ctl_ = 0;  // the filter follows the new envelopes from the first sample
  since_control_ = 0;  // what passed before the note is not the note's: the sweep starts from here, wherever the period had got to
}

void AcidVoice::note_off(std::uint8_t note) noexcept {
  // SynthEngine::handleNoteOff: the first matching note leaves the stack; the gate closes when none is left, else the pitch slides
  // back to the one held last.
  for (unsigned i = 0; i < held_count_; ++i) {
    if (held_[i] == note) {
      for (unsigned j = i + 1u; j < held_count_; ++j) {
        held_[j - 1u] = held_[j];
      }
      --held_count_;
      break;
    }
  }
  if (held_count_ == 0u) {
    gate_ = false;
    gate_wait_ = fish_ ? 0u : gate_hold_;
  } else {
    target_hz_ = midi_to_hz(held_[held_count_ - 1u]);
  }
}

void AcidVoice::all_notes_off() noexcept {
  held_count_ = 0;
  gate_ = false;
  gate_wait_ = 0;
}

void AcidVoice::apply(const dsp::Event& e) noexcept {
  switch (e.kind) {
    case dsp::EventKind::NoteOn:
      note_on(e.note, (e.flags & dsp::kNoteAccent) != 0u || e.velocity >= kAccentVelocity, (e.flags & dsp::kNoteSlide) != 0u);
      break;
    case dsp::EventKind::NoteOff:
      note_off(e.note);
      break;
    case dsp::EventKind::Param:
      set(e.param, e.value);
      break;
    case dsp::EventKind::AllNotesOff:
      all_notes_off();
      break;
  }
}

// ---- control rate -------------------------------------------------------------------------------------------------------------

// TeeBeeFilter::calculateCoefficientsApprox4 for a cutoff of 2^x Hz, in float, at the sub-step's rate: the ladder's step, its feedback at the
// Resonance set, and its output gain with the factor 2 of getSample.
void AcidVoice::ladder_coeffs(float x, float& b, float& k, float& g) const noexcept {
  x = x > x_max_ ? x_max_ : (x < x_min_ ? x_min_ : x);
  const float fx = o3sdk::core::exp2_fast(x) * fx_scale_;
  b = (0.00045522346f + 6.1922189f * fx) / (1.0f + 12.358354f * fx + 4.4156345f * fx * fx);
  const float k17 = fx * (fx * (fx * (fx * (fx * (fx + 7198.6997f) - 5837.7917f) - 476.47308f) + 614.95611f) + 213.87126f) + 16.998792f;
  g = 2.0f * ((k17 * (1.0f / 17.0f) - 1.0f) * res_ + 1.0f) * (1.0f + res_);
  k = k17 * fb_r_;
}

// What the mods that live at the control rate add to the cutoff, in octaves of log2(Hz): the base, with Tracking's term when it is on.
// Never a zero term at stock: track_on_ is false there, and the result is cutoff_l2_ itself.
float AcidVoice::cutoff_base() const noexcept {
  if (!track_on_) {
    return cutoff_l2_;
  }
  const float hz = pitch_hz_ > 1.0f ? pitch_hz_ : 1.0f;
  const float b = cutoff_l2_ + track_ * (o3sdk::core::log2_fast(hz) - kLog2C2);
  return b < kLog2MinCutoff ? kLog2MinCutoff : b;
}

// The accent sweep circuit for one control period; `m` is the MEG at the end of it. Returns the lift of the cutoff, in octaves.
// NORMAL and SLOW are the circuit: from the MEG, through the diode and 47 k, into a pot whose wiper feeds the filter and whose far end holds C13. With
// Resonance down the wiper sits near the MEG, and the lift is a pulse. With Resonance up it sits on the capacitor: the lift rises late, and
// what is left of its charge makes the next accent climb higher. FAST takes a slow follower away from the pulse: a strong first accent, smaller ones after.
float AcidVoice::accent_sweep(float m, float periods) noexcept {
  const float v = has_accent_ ? accent_amt_ * m : 0.0f;
  if (swp_mode_ == 0u) {
    swp_m_ += (v - swp_m_) * swp_fast_c_ * periods;
    if (swp_m_ < kTiny) swp_m_ = 0.0f;
    return sweep_mode_ == 0u ? 0.0f : kSweepOctaves * (v > swp_m_ ? v - swp_m_ : 0.0f);
  }
  const float on = (sw_ga_ * v + sw_g2_ * cap_) * sw_gt_;
  const float w = v > on ? on : cap_ * sw_off_;  // the diode conducts while the MEG is above the node
  cap_ += (w - cap_) * sw_g2_ * sw_rate_ * periods;
  if (cap_ < kTiny) cap_ = 0.0f;
  if (sweep_mode_ == 0u) {
    return 0.0f;
  }
  return (swp_mode_ == 2u ? kSweepOctaves * kSweepSlowGain : kSweepOctaves) * w;
}

// The output of the last sample before Volume, as far as it is known: seeds the states a mod left stale while it was bypassed (the muffler's
// low-pass, the Filter FM blocker), so none of them is ever heard from where it stopped.
float AcidVoice::tail_estimate() const noexcept { return gain_ > 1.0e-3f ? last_out_ / gain_ : 0.0f; }

void AcidVoice::load_muffler() noexcept {
  // SOFT is the HARD path with no split (a = 0: the low-pass stays 0 and the whole signal is clipped, at a higher level).
  muf_a_ = muf_type_ == 2u ? muf_hard_a_ : 0.0f;
  muf_t_ = muf_type_ == 2u ? kMufHardT : kMufSoftT;
  muf_inv_t_ = 1.0f / muf_t_;
}

bool AcidVoice::mods_needed() const noexcept {
  return hires_ || att_ < 1.0f || od_db_t_ != 0.0f || od_g_ != 1.0f || od_step_ != 0.0f || sub_.v != 0.0f || sub_.target != 0.0f ||
         fm_.v != 0.0f || fm_.target != 0.0f || mix_.v != 0.0f || mix_.target != 0.0f || muf_type_ != muf_want_;
}

// One control period of the mods that run in the loop: their ramps start or continue, they land exactly, the states a mod left stale are
// re-seeded as its ramp leaves 0, the new states are flushed, and the block's loop is chosen.
void AcidVoice::advance_mods() noexcept {
  ramp_pending_ = false;
  if (att_ < 1.0f && att_ > 1.0f - kAttDone) {
    att_ = 1.0f;
  }

  // Sub Osc: a per-sample ramp of the level. The square's flip-flop restarts when the level leaves 0.
  if (sub_.v == 0.0f && sub_.target != 0.0f) {
    subq_ = 1.0f;
  }
  {
    const float d = sub_.target - sub_.v;
    const float ad = absf(d);
    if (ad <= 1.0e-6f) {
      sub_.v = sub_.target;
      sub_.step = 0.0f;
    } else {
      const float per = ad <= ramp_sub_rate_ ? d : (d < 0.0f ? -ramp_sub_rate_ : ramp_sub_rate_);
      sub_.step = per * (1.0f / static_cast<float>(kControlPeriod));
    }
  }

  // Overdrive: a ramp in dB, the gain linear between the control points of the ramp (a per-sample step), exact at 0 dB.
  {
    const float g_t = od_gain(od_db_t_);
    if (od_db_ == od_db_t_) {
      if (absf(od_g_ - g_t) <= 1.0e-6f * g_t) {
        od_g_ = g_t;
        od_step_ = 0.0f;
      } else {
        od_step_ = (g_t - od_g_) * (1.0f / static_cast<float>(kControlPeriod));
      }
    } else {
      const float d = od_db_t_ - od_db_;
      od_db_ = absf(d) <= ramp_od_rate_ ? od_db_t_ : od_db_ + (d < 0.0f ? -ramp_od_rate_ : ramp_od_rate_);
      od_step_ = (od_gain(od_db_) - od_g_) * (1.0f / static_cast<float>(kControlPeriod));
    }
  }

  // Filter FM: a stair at the control rate. Its blocker starts from 0 (and from the tail, so it sees no step) when the depth leaves 0.
  if (fm_.v == 0.0f && fm_.target != 0.0f) {
    fm_y_ = tail_estimate();
    fm_xm1_ = fm_y_;
    fm_ym1_ = 0.0f;
  }
  {
    const float d = fm_.target - fm_.v;
    fm_.v = absf(d) <= ramp_fm_rate_ ? fm_.target : fm_.v + (d < 0.0f ? -ramp_fm_rate_ : ramp_fm_rate_);
  }

  // Muffler: a per-sample ramp of the wet/dry mix, as a stair of it steps the output itself. A change of type dips the mix to 0 and back with the new type; leaving 0 re-seeds the low-pass.
  {
    float t = muf_type_ != 0u ? 1.0f : 0.0f;
    if (muf_type_ != muf_want_) {
      t = 0.0f;
      if (mix_.v == 0.0f) {
        muf_type_ = muf_want_;
        load_muffler();
        t = muf_type_ != 0u ? 1.0f : 0.0f;
      }
    }
    mix_.target = t;
    if (mix_.v == 0.0f && t != 0.0f) {
      muf_lp_ = muf_a_ != 0.0f ? tail_estimate() : 0.0f;
    }
    const float d = t - mix_.v;
    const float ad = absf(d);
    if (ad <= 1.0e-6f) {
      mix_.v = t;
      mix_.step = 0.0f;
    } else {
      const float per = ad <= ramp_mix_rate_ ? d : (d < 0.0f ? -ramp_mix_rate_ : ramp_mix_rate_);
      mix_.step = per * (1.0f / static_cast<float>(kControlPeriod));
    }
  }

  // The states of the mods, flushed as the envelopes are.
  if (absf(fm_xm1_) < kTiny) fm_xm1_ = 0.0f;
  if (absf(fm_ym1_) < kTiny) fm_ym1_ = 0.0f;
  if (absf(fm_y_) < kTiny) fm_y_ = 0.0f;
  if (absf(muf_lp_) < kTiny) muf_lp_ = 0.0f;
  for (unsigned i = 0; i < 4; ++i) {
    if (absf(s_[i]) < kTiny) s_[i] = 0.0f;
  }

  modded_ = mods_needed();
}

void AcidVoice::control() noexcept {
  // What runs on time goes by the samples since the last call: a parameter that moves every block calls this early (retune_filter()).
  const std::uint16_t elapsed = since_control_;
  since_control_ = 0;
  if (vcf_ < kTiny) {
    vcf_ = 0.0f;
  }
  if (vca_ < kTiny) {
    vca_ = 0.0f;
  }
  if (absf(dk_lp_) < kTiny) {
    dk_lp_ = 0.0f;
  }
  if (absf(dk_bp_) < kTiny) {
    dk_bp_ = 0.0f;
  }

  if (accent_.moving) {
    accent_boost_ = kAccentToAmp * step(accent_, 1.0e-4f);
  }
  if (volume_.moving) {
    gain_ = step(volume_, 1.0e-6f);
  }
  if (ramp_pending_ || modded_) {
    advance_mods();
  }

  // The end of a note: the gate fell, and the note holds for kGateHold before it goes (the Devil Fish goes at once).
  if (!gate_ && !released_) {
    if (gate_wait_ <= elapsed) {
      released_ = true;
    } else {
      gate_wait_ = static_cast<std::uint16_t>(gate_wait_ - elapsed);
    }
  }
  amp_lift_ = released_ ? 0.0f : kMainToAmp + (has_accent_ ? accent_boost_ : 0.0f);

  // The cutoff, Open303::getSample: knob x 2^(scale x (env - offset) + accent lift). The sweep stops at the floor, or at the knob if the knob is lower.
  // It is computed for the end of the period and the filter ramps to it, so the coefficients are right at every period boundary; after a
  // note-on they start from the values at the first sample instead of from the old ones.
  const float base = cutoff_base();  // cutoff_l2_ at stock; Tracking adds its term
  const float lo = base < x_floor_ ? base : x_floor_;
  if (fresh_) {
    fresh_ = false;
    const float x = base + env_scale_ * (vcf_ - env_off_) + sweep_oct_;
    ladder_coeffs(x < lo ? lo : x, lad_b_, lad_k_, lad_g_);
  }
  const float env_end = vcf_ * (has_accent_ ? accent_coeff_n_ : decay_coeff_n_);
  sweep_oct_ = accent_sweep(env_end, static_cast<float>(elapsed) * (1.0f / static_cast<float>(kControlPeriod)));
  const float x = base + env_scale_ * (env_end - env_off_) + sweep_oct_;
  float b, k, g;
  ladder_coeffs(x < lo ? lo : x, b, k, g);
  constexpr float kPer = 1.0f / static_cast<float>(kControlPeriod);
  lad_b_step_ = (b - lad_b_) * kPer;
  lad_k_step_ = (k - lad_k_) * kPer;
  lad_g_step_ = (g - lad_g_) * kPer;
}

// ---- audio --------------------------------------------------------------------------------------------------------------------

template <bool Square, bool Modded>
void AcidVoice::run(float* out, std::uint32_t n) noexcept {
  static_assert(kControlPeriod <= 16u, "the scratch arrays below are one control period");
  alignas(8) float xin[kControlPeriod];  // declared, not placed: a doubleword each, so the pairs the compiler loads at once are aligned
  alignas(8) float amp[kControlPeriod];
  static_assert(sizeof(float) * kControlPeriod % 8u == 0u, "the scratch arrays are whole doublewords");
  if constexpr (!Modded) {
    // The stock loop: a TB-303, in four passes over one control period (at most kControlPeriod samples), so no pass keeps more floats live than
    // the FPU has registers (rt-optimize): the oscillator, the envelopes, the ladder, the output's filters.
    {
      float phase = phase_;
      float sqr_hp = sqr_hp_;
      float pitch = pitch_hz_;
      const float target = target_hz_;
      const float slide = slide_coeff_;
      const float scale = tune_scale_;
      const float inc_lo = inc_lo_;
      const float inc_hi = inc_hi_;
      const float sqr_c = sqr_hp_coeff_;
      float px = pre_x_, py = pre_y_;
      const float pa = pre_a_, pb = pre_b_;
      for (std::uint32_t i = 0; i < n; ++i) {
        pitch += (target - pitch) * slide;
        const float inc = clampf(pitch * scale, inc_lo, inc_hi);
        xin[i] = high_pass(osc_sample<Square>(phase, sqr_hp, inc, sqr_c), px, py, pa, pb);
      }
      phase_ = phase;
      sqr_hp_ = sqr_hp;
      pitch_hz_ = pitch;
      pre_x_ = px;
      pre_y_ = py;
    }
    {
      // The envelopes, and the amplifier through its de-clicker (a state-variable low-pass).
      float vcf = vcf_;
      float vca = vca_;
      const float vcf_c = has_accent_ ? accent_coeff_ : decay_coeff_;
      const float vca_c = released_ ? (fish_ ? release_fish_c_ : (has_accent_ ? release_acc_c_ : release_c_)) : hold_coeff_;
      const float vca_add = released_ ? 0.0f : sustain_ * (1.0f - hold_coeff_);
      const float lift = amp_lift_;
      float dlp = dk_lp_, dbp = dk_bp_;
      const float dkf = declick_f_;
      for (std::uint32_t i = 0; i < n; ++i) {
        vcf *= vcf_c;
        vca = vca * vca_c + vca_add;
        dlp += dkf * dbp;
        dbp += dkf * (vca + lift * vcf - dlp - kDeclickDamp * dbp);
        amp[i] = dlp * kAmpScale;
      }
      vcf_ = vcf;
      vca_ = vca;
      dk_lp_ = dlp;
      dk_bp_ = dbp;
    }
    {
      float y1 = s_[0], y2 = s_[1], y3 = s_[2], y4 = s_[3];
      float fx = fb_x_, fy = fb_y_;
      const float fa = fb_a_, fb = fb_b_;
      float b = lad_b_, k = lad_k_, g = lad_g_;
      const float b_step = lad_b_step_, k_step = lad_k_step_, g_step = lad_g_step_;
      for (std::uint32_t i = 0; i < n; ++i) {
        b += b_step;
        k += k_step;
        g += g_step;
        const float in = xin[i];
        ladder_step(in, y1, y2, y3, y4, fx, fy, fa, fb, k, b);
        ladder_step(in, y1, y2, y3, y4, fx, fy, fa, fb, k, b);
        xin[i] = g * y4;
      }
      s_[0] = y1, s_[1] = y2, s_[2] = y3, s_[3] = y4;
      fb_x_ = fx;
      fb_y_ = fy;
      lad_b_ = b;
      lad_k_ = k;
      lad_g_ = g;
    }
    {
      // The output's all-pass, high-pass and notch (a state-variable form, which float holds at 7.5 Hz), then the amplifier and the volume.
      float ax = ap_x_, ay = ap_y_;
      const float ac = ap_c_;
      float qx = post_x_, qy = post_y_;
      const float qa = post_a_, qb = post_b_;
      float n1 = notch_s1_, n2 = notch_s2_;
      const float ng = notch_g_, ntr = notch_two_r_, nd = 1.0f / (1.0f + ng * (kNotchTwoR + ng));
      const float gain = gain_;
      for (std::uint32_t i = 0; i < n; ++i) {
        const float f = xin[i];
        const float ap = ac * (f - ay) + ax;
        ax = f;
        ay = ap;
        const float h = high_pass(ap, qx, qy, qa, qb);
        const float nh = (h - (kNotchTwoR + ng) * n1 - n2) * nd;
        const float nb = ng * nh + n1;
        n1 = ng * nh + nb;
        const float nl = ng * nb + n2;
        n2 = ng * nb + nl;
        out[i] = ceiling((h - ntr * nb) * amp[i]) * gain;
      }
      ap_x_ = ax;
      ap_y_ = ay;
      post_x_ = qx;
      post_y_ = qy;
      notch_s1_ = n1;
      notch_s2_ = n2;
    }
  } else {
    // The modded loop: the same maths in three passes over one control period (at most kControlPeriod samples), so no pass keeps more floats
    // live than the stock loop does. 1: the oscillator, the sub and the overdrive, into xin. 2: the envelopes and the soft attack, into amp.
    // 3: the ladder (with Filter FM on its step), the output's filters, the amplifier, the muffler and the volume: one pass, as Filter FM
    // needs the last output for the next step.
    {
      float phase = phase_;
      float sqr_hp = sqr_hp_;
      float pitch = pitch_hz_;
      const float target = target_hz_;
      const float slide = slide_coeff_;
      const float scale = tune_scale_;
      const float inc_lo = inc_lo_;
      const float inc_hi = inc_hi_;
      const float sqr_c = sqr_hp_coeff_;
      float subq = subq_;
      float subl = sub_.v;
      const float subl_step = sub_.step;
      float odg = od_g_;
      const float od_step = od_step_;
      float px = pre_x_, py = pre_y_;
      const float pa = pre_a_, pb = pre_b_;
      for (std::uint32_t i = 0; i < n; ++i) {
        pitch += (target - pitch) * slide;
        const float inc = clampf(pitch * scale, inc_lo, inc_hi);
        const float p0 = phase;
        const float osc = osc_sample<Square>(phase, sqr_hp, inc, sqr_c);
        // Sub Osc: a square an octave down is a flip-flop on the main oscillator's wraps; its edges are band-limited with the main
        // oscillator's own fractional position (before the edge the next sign is -q, after it the sign is q).
        subq = phase < p0 ? -subq : subq;
        const float blep = poly_blep(phase, inc);
        const float sub = subq + (phase < 0.5f ? subq : -subq) * blep;
        subl += subl_step;
        odg += od_step;
        xin[i] = high_pass((osc + subl * sub) * odg, px, py, pa, pb) + kOscSeed;
      }
      phase_ = phase;
      sqr_hp_ = sqr_hp;
      pitch_hz_ = pitch;
      subq_ = subq;
      sub_.v = subl;
      od_g_ = odg;
      pre_x_ = px;
      pre_y_ = py;
    }
    {
      float vcf = vcf_;
      float vca = vca_;
      float att = att_;
      const float vcf_c = has_accent_ ? accent_coeff_ : decay_coeff_;
      const float vca_c = released_ ? (fish_ ? release_fish_c_ : (has_accent_ ? release_acc_c_ : release_c_)) : hold_coeff_;
      const float vca_add = released_ ? 0.0f : sustain_ * (1.0f - hold_coeff_);
      const float att_c = att_c_;
      const float lift = amp_lift_;
      float dlp = dk_lp_, dbp = dk_bp_;
      const float dkf = declick_f_;
      for (std::uint32_t i = 0; i < n; ++i) {
        vcf *= vcf_c;
        vca = vca * vca_c + vca_add;
        att += (1.0f - att) * att_c;
        dlp += dkf * dbp;
        dbp += dkf * (vca + lift * vcf - dlp - kDeclickDamp * dbp);
        amp[i] = dlp * att * kAmpScale;
      }
      vcf_ = vcf;
      vca_ = vca;
      att_ = att;
      dk_lp_ = dlp;
      dk_bp_ = dbp;
    }
    {
      float y1 = s_[0], y2 = s_[1], y3 = s_[2], y4 = s_[3];
      float fx = fb_x_, fy = fb_y_;
      const float fa = fb_a_, fb = fb_b_;
      float b = lad_b_, k = lad_k_, g = lad_g_;
      const float b_step = lad_b_step_, k_step = lad_k_step_, g_step = lad_g_step_;
      float ax = ap_x_, ay = ap_y_;
      const float ac = ap_c_;
      float qx = post_x_, qy = post_y_;
      const float qa = post_a_, qb = post_b_;
      float n1 = notch_s1_, n2 = notch_s2_;
      const float ng = notch_g_, ntr = notch_two_r_, nd = 1.0f / (1.0f + ng * (kNotchTwoR + ng));
      const float gain = gain_;
      const float fmk = fm_.v;
      const float dcr = fm_dc_r_;
      float xm1 = fm_xm1_;
      float ym1 = fm_ym1_;
      float yp = fm_y_;
      const float b_lo = b0_min_;
      float mix = mix_.v;
      const float mix_step = mix_.step;
      const float ma = muf_a_;
      const float mt = muf_t_;
      const float mit = muf_inv_t_;
      float mlp = muf_lp_;
      for (std::uint32_t i = 0; i < n; ++i) {
        b += b_step;
        k += k_step;
        g += g_step;
        // Filter FM: the last output sample, through a DC blocker, scales the ladder's step (linear, clamped).
        const float dc = (yp - xm1) + dcr * ym1;
        xm1 = yp;
        ym1 = dc;
        float bm = b * (1.0f + fmk * dc);
        bm = bm < b_lo ? b_lo : bm;
        bm = bm > kB0Max ? kB0Max : bm;
        const float xi = xin[i];
        ladder_step(xi, y1, y2, y3, y4, fx, fy, fa, fb, k, bm);
        ladder_step(xi, y1, y2, y3, y4, fx, fy, fa, fb, k, bm);
        const float f = g * y4;
        const float ap = ac * (f - ay) + ax;
        ax = f;
        ay = ap;
        const float h = high_pass(ap, qx, qy, qa, qb);
        const float nh = (h - (kNotchTwoR + ng) * n1 - n2) * nd;
        const float nb = ng * nh + n1;
        n1 = ng * nh + nb;
        const float nl = ng * nb + n2;
        n2 = ng * nb + nl;
        const float y = (h - ntr * nb) * amp[i];
        // Muffler: the band above a corner (the whole signal for SOFT) is clipped, mixed with the dry signal.
        mix += mix_step;
        mlp += ma * (y - mlp);
        const float m = mlp + mt * tanh_sat((y - mlp) * mit);
        yp = y + mix * (m - y);
        out[i] = ceiling(yp) * gain;
      }
      s_[0] = y1, s_[1] = y2, s_[2] = y3, s_[3] = y4;
      fb_x_ = fx;
      fb_y_ = fy;
      lad_b_ = b;
      lad_k_ = k;
      lad_g_ = g;
      ap_x_ = ax;
      ap_y_ = ay;
      post_x_ = qx;
      post_y_ = qy;
      notch_s1_ = n1;
      notch_s2_ = n2;
      fm_xm1_ = xm1;
      fm_ym1_ = ym1;
      fm_y_ = yp;
      mix_.v = mix;
      muf_lp_ = mlp;
    }
  }
}

void AcidVoice::render(float* out, std::uint32_t from, std::uint32_t to) noexcept {
  while (from < to) {
    if (ctl_ == 0u) {
      control();
      ctl_ = kControlPeriod;
    }
    const std::uint32_t n = (to - from) < ctl_ ? (to - from) : ctl_;
    if (square_) {
      if (modded_) {
        run<true, true>(out + from, n);
      } else {
        run<true, false>(out + from, n);
      }
    } else {
      if (modded_) {
        run<false, true>(out + from, n);
      } else {
        run<false, false>(out + from, n);
      }
    }
    last_out_ = out[from + n - 1u];
    ctl_ -= n;
    since_control_ = static_cast<std::uint16_t>(since_control_ + n);
    from += n;
  }
}

void AcidVoice::process(const dsp::ProcessArgs& args) noexcept {
  dsp::for_each_run(
      args, [&](std::uint32_t from, std::uint32_t to) { render(args.out_l, from, to); },
      [&](const dsp::Event& e) { apply(e); });
  if (args.frames == 0u) {
    snap();  // the engine delivering the stored values at start(): take them at once, no glide from the defaults
  }
  for (std::uint32_t i = 0; i < args.frames; ++i) {
    args.out_r[i] = args.out_l[i];
  }
}

}  // namespace o3_303
