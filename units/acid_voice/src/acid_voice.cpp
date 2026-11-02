// SPDX-License-Identifier: GPL-3.0-or-later
//
// ACID: the acid voice of o3-303. A port of the DSP of Replicant 303 by DanimanX, commit d5369be028399f5a403067b58da2936caa070c0e
// (third_party/replicant303/DSP/): Oscillator303.cpp, DiodeLadderFilter.cpp, Envelope303.h, Common303.h, and the way SynthEngine.cpp
// combines them (handleNoteOn / handleNoteOff / renderSamples). Replicant 303 is distributed under the MIT License:
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
// Structure of the port (the numbers of the original are kept; what moved is where they are computed):
//   * per sample: the pitch glide, the oscillator, the three envelopes, the ladder (two sub-steps, 13 divisions: the tanh of each state
//     is computed once and reused by the next stage and the next sub-step, which is what the original recomputes), the amplifier;
//   * every kControlPeriod (16) samples (control()): the smoothers of Volume and the accent lift, the cutoff law
//     base x 2^(envmod x env x 5.2 + accent x 2.2) by core::exp2_fast, and g = tan(pi fc / fs) by a rational fit (error 1.6e-6) with the
//     original's ceiling of 0.95. The original calls pow and tan every sample. Here g is computed for the end of the period and ramped
//     linearly to it (exact at every period boundary; the cutoff moves by at most 0.01 % a sample), and set at once when a note starts
//     or the cutoff law changes (Cutoff, Env Mod, Accent), as the original reads those every sample;
//   * on an event: pitch (exp2_fast), envelope coefficients (std::exp, as the original), the note stack, the tune and resonance values.
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

constexpr const char* kWaveNames[] = {"SAW", "SQR"};
constexpr const char* kSoftAtkNames[] = {"OFF", "0.3 MS", "1 MS", "3 MS", "10 MS", "30 MS"};
constexpr const char* kSubNames[] = {"OFF", "LOW", "MID", "HIGH"};
constexpr const char* kSweepNames[] = {"OFF", "NORMAL", "HI RES"};
constexpr const char* kMufflerNames[] = {"OFF", "SOFT", "HARD"};
constexpr const char* kSpeedNames[] = {"FAST", "NORMAL", "SLOW"};
constexpr const char* kOffOnNames[] = {"OFF", "ON"};

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
    {"DECAY", "ms", nullptr, nullptr, 200.0f, 2500.0f, 750.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Log, 0, 0},
    {"ACCENT", "%", nullptr, nullptr, 0.0f, 1.0f, 0.7f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, kSmooth, 0},
    {"WAVE", "", kWaveNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"TUNE", "st", nullptr, nullptr, -12.0f, 12.0f, 0.0f, 240, 10, 0, ParamUnit::Fixed1, ParamCurve::Linear, 0, 0},
    {"VOLUME", "dB", nullptr, nullptr, -60.0f, 6.0f, -3.0f, 132, 4, 0, ParamUnit::Fixed1, ParamCurve::Linear, kSmooth, 0},
    // The mods (docs/MODS-PLAN.md 3.1): each default is the stock voice. Time rows are Log with a positive minimum.
    {"ACC DECAY", "ms", nullptr, nullptr, 30.0f, 3000.0f, 180.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Log, 0, 0},
    {"TRACKING", "%", nullptr, nullptr, 0.0f, 150.0f, 0.0f, 150, 10, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0},
    {"SLIDE", "ms", nullptr, nullptr, 60.0f, 360.0f, 60.0f, 60, 10, 0, ParamUnit::Int, ParamCurve::Log, 0, 0},
    {"SOFT ATK", "", kSoftAtkNames, nullptr, 0.0f, 5.0f, 0.0f, 5, 1, 6, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"SUB OSC", "", kSubNames, nullptr, 0.0f, 3.0f, 0.0f, 3, 1, 4, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"FILTER FM", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, kSmooth, 0},
    {"ACC SWEEP", "", kSweepNames, nullptr, 0.0f, 2.0f, 1.0f, 2, 1, 3, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"MUFFLER", "", kMufflerNames, nullptr, 0.0f, 2.0f, 0.0f, 2, 1, 3, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"SWP SPEED", "", kSpeedNames, nullptr, 0.0f, 2.0f, 1.0f, 2, 1, 3, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"OVERDRIVE", "dB", nullptr, nullptr, -24.0f, 36.0f, 0.0f, 120, 4, 0, ParamUnit::Fixed1, ParamCurve::Linear, kSmooth, 0},
    {"AMP DECAY", "ms", nullptr, nullptr, 16.0f, 4000.0f, 4000.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Log, 0, 0},
    {"STOCK", "", kOffOnNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
};
static_assert(AcidVoice::kParamCount == 20, "the voice has 8 parameters of the 303 and 12 mods");

constexpr dsp::UnitInfo kInfo{"ACID", dsp::UnitKind::Instrument, kParams, AcidVoice::kParamCount, nullptr, 0};

// ---- constants of the original ----------------------------------------------------------------------------------------------
constexpr float kPi = 3.14159265f;
constexpr float kSqrDuty = 0.485f;          // Oscillator303: the square's duty
constexpr float kSawBend = 0.15f;           // Oscillator303: the parabolic bend of the saw
constexpr float kSawLevel = 0.95f;
constexpr float kSqrLevel = 0.90f;
constexpr float kSqrDroopHz = 25.0f;        // the RC droop high-pass of the square
constexpr float kFbHpHz = 40.0f;            // DiodeLadderFilter: the feedback coupling high-pass
constexpr float kFbGain = 17.2f;            // feedback gain per unit of resonance
constexpr float kDriveGain = 0.4f;          // input drive per unit of resonance
constexpr float kStateLimit = 4.0f;         // the original clamps each of the four ladder states to +-4; see the guard in run()
constexpr float kEnvOctaves = 5.2f;         // Env Mod sweep, octaves
constexpr float kAccentOctaves = 2.2f;      // accent lift of the cutoff, octaves
constexpr float kAccentLift = 0.45f;        // accent lift of the amplifier
constexpr float kAccentTau = 0.18f;         // seconds
constexpr float kHoldTau = 4.0f;
constexpr float kReleaseTau = 0.025f;
constexpr float kSlideTau = 0.060f;
constexpr float kGMax = 0.95f;              // the original clamps g to [0.001, 0.95]
constexpr float kMinCutoff = 25.0f;         // DiodeLadderFilter::setCutoff clamps to 25..4800 Hz
constexpr float kMaxCutoff = 4800.0f;
constexpr float kDecayMin = 0.15f;          // Envelope303::setDecayTime clamps to 0.15..2.8 s
constexpr float kDecayMax = 2.8f;

// ---- ours --------------------------------------------------------------------------------------------------------------------
constexpr float kSmoothTau = 0.005f;        // seconds: parameter smoothing
constexpr float kAudible = 1.0e-4f;         // amplifier level (-80 dB) under which a released voice no longer counts as sounding
constexpr float kTiny = 1.0e-9f;            // envelopes below this (-180 dB) are flushed to zero: the output never lingers at amplitudes where a
                                            // product of three samples (soft clip in the distortion) is subnormal
constexpr float kSilentDb = -59.5f;         // at or below: silence
constexpr float kLog2Ten20 = 0.16609640f;   // log2(10) / 20: dB to a power of two
constexpr float kAtan095OverPi = 0.24188f;  // atan(0.95) / pi: the cutoff, in units of fs, at which g = tan(pi fc / fs) reaches kGMax

// ---- the mods ----------------------------------------------------------------------------------------------------------------
constexpr float kLog2C2 = 6.0313599f;       // log2(65.4064 Hz): Tracking's reference, C2, where the cutoff is the knob's at any setting
constexpr float kLog2MinCutoff = 4.6438562f;  // log2(25 Hz): the floor of the tracked cutoff
constexpr float kAttDone = 1.0e-4f;         // the soft attack is over this close to 1
constexpr float kAttRise = 2.1972246f;      // ln 9: an RC charge's 10 to 90 % rise takes this many time constants
constexpr float kSoftAtkMs[6] = {0.0f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f};
constexpr float kSubLevel[4] = {0.0f, 0.25f, 0.5f, 0.8f};  // of the main wave's peak: starting values, tuned by ear at the unit gate
constexpr float kRampSeconds = 0.005f;      // a mod's ramp over its whole range
constexpr float kFmDepth = 2.0f;            // Filter FM: k = 2 x the amount (g is scaled by 1 + k y)
constexpr float kFmDcHz = 5.0f;             // the DC blocker of the output that modulates the filter
constexpr float kMufSoftT = 0.7f;           // Muffler SOFT: the level where the whole signal starts to flatten
constexpr float kMufHardT = 0.19952623f;    // Muffler HARD: the band above the corner is clipped at -14 dB
constexpr float kMufHardHz = 150.0f;        // Muffler HARD: below this the bass passes
constexpr float kSwpSlowTau = 0.120f;       // Swp Speed SLOW: the follower's time constant, seconds
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

// tan(w) for w in [0, 0.76]: w (105 - 10 w^2) / (105 - 45 w^2 + w^4), relative error under 1.6e-6.
inline float tan_fit(float w) noexcept {
  const float w2 = w * w;
  return w * (105.0f - 10.0f * w2) / (105.0f - 45.0f * w2 + w2 * w2);
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

// Oscillator303::process: advances the phase by `inc` and returns the sample. `hp` is the square's droop state, `hp_coeff` its pole.
template <bool Square>
inline float osc_sample(float& phase, float& hp, float inc, float hp_coeff) noexcept {
  phase += inc;
  if (phase >= 1.0f) {
    phase -= 1.0f;
  }
  if constexpr (!Square) {
    const float naive = 2.0f * phase - 1.0f;
    float curved = naive - kSawBend * (naive * naive - 1.0f);
    curved -= poly_blep(phase, inc);
    return curved * kSawLevel;
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

void AcidVoice::set(std::uint16_t id, float v) noexcept {
  switch (id) {
    case Cutoff:
      cutoff_l2_ = o3sdk::core::log2_fast(clamp_param(v, kMinCutoff, kMaxCutoff));
      retune_filter();
      break;
    case Reso: {
      const float r = clamp_param(v, 0.0f, 1.0f);
      reso_ = r;
      fb_k_ = hires_ ? (2.0f * kFbGain) * r : kFbGain * r;  // HI RES doubles the feedback; otherwise this is today's product
      in_drive_ = 1.0f + kDriveGain * r;
      break;
    }
    case EnvMod:
      env_mod_oct_ = clamp_param(v, 0.0f, 1.0f) * kEnvOctaves;
      retune_filter();
      break;
    case Decay: {
      const float seconds = clamp_param(v * 0.001f, kDecayMin, kDecayMax);
      decay_coeff_ = std::exp(-inv_fs_ / seconds);
      decay_coeff_n_ = power_n(decay_coeff_);
      break;
    }
    case Accent: {
      const float a = clamp_param(v, 0.0f, 1.0f);
      accent_oct_ = a * kAccentOctaves;
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
      refresh_mods();
    }
    return;
  }
  raw_[id - kFirstMod] = mod_param(v, kParams[id].min, kParams[id].max, kParams[id].def);
  apply_mod(id);
}

void AcidVoice::refresh_mods() noexcept {
  for (unsigned id = kFirstMod; id < Stock; ++id) {
    apply_mod(id);
  }
}

// Derives what the loops use from one mod's value (eff()). The stock value of a coefficient is the one prepare() computed from the
// constant, assigned, so the stock voice is today's voice to the bit; any other value goes through std::exp once, here, on a float.
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
      slide_coeff_ = v <= kParams[Slide].min ? stock_slide_c_ : 1.0f - std::exp(-inv_fs_ / (v * 0.001f));
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
        fb_k_ = hires_ ? (2.0f * kFbGain) * reso_ : kFbGain * reso_;
        retune_filter();
        ramp_pending_ = true;  // HI RES lives in the modded loop (the clamped tanh): the next control() must choose it
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
        swp_ = 0.0f;
        swp_m_ = 0.0f;
        retune_filter();
      }
      break;
    }
    case Overdrive:
      od_db_t_ = v;
      ramp_pending_ = true;
      break;
    case AmpDecay:
      hold_coeff_ = v >= kParams[AmpDecay].max ? stock_hold_c_ : std::exp(-inv_fs_ / (v * 0.001f));
      break;
    default:
      break;
  }
}

// Takes the smoothed parameters at their targets: at prepare, and when the engine delivers the stored values (a process() of zero frames).
void AcidVoice::snap() noexcept {
  jump(accent_, accent_.target);
  jump(volume_, volume_.target);
  accent_boost_ = kAccentLift * accent_.target;
  gain_ = volume_.target;

  // The mods' ramps are at their targets from the first sample, as a fresh voice has them.
  sub_.v = sub_.target;
  sub_.step = 0.0f;
  subq_ = 1.0f;
  od_db_ = od_db_t_;
  od_g_ = db_gain(od_db_t_);
  od_step_ = 0.0f;
  fm_.v = fm_.target;
  fm_xm1_ = 0.0f;
  fm_ym1_ = 0.0f;
  fm_y_ = 0.0f;
  muf_type_ = muf_want_;
  load_muffler();
  mix_.target = muf_type_ != 0u ? 1.0f : 0.0f;
  mix_.v = mix_.target;
  muf_lp_ = 0.0f;
  ramp_pending_ = false;
  modded_ = mods_needed();
}

void AcidVoice::prepare(float sample_rate) noexcept {
  fs_ = sample_rate < 1000.0f ? 1000.0f : sample_rate;
  inv_fs_ = 1.0f / fs_;
  pi_over_fs_ = kPi * inv_fs_;
  smooth_k_ = 1.0f - std::exp(-static_cast<float>(kControlPeriod) * inv_fs_ / kSmoothTau);
  x_max_ = o3sdk::core::log2_fast(fs_ * kAtan095OverPi);

  // Oscillator303::prepare, DiodeLadderFilter::prepare, Envelope303::updateCoefficients, SynthEngine::updateSlideCoefficient.
  {
    const float rc = 1.0f / (2.0f * kPi * kSqrDroopHz);
    sqr_hp_coeff_ = rc / (rc + inv_fs_);
  }
  {
    const float rc = 1.0f / (2.0f * kPi * kFbHpHz);
    hp_alpha_ = rc / (rc + inv_fs_);
  }
  accent_coeff_ = std::exp(-inv_fs_ / kAccentTau);
  accent_coeff_n_ = power_n(accent_coeff_);
  hold_coeff_ = std::exp(-inv_fs_ / kHoldTau);
  release_coeff_ = std::exp(-inv_fs_ / kReleaseTau);
  slide_coeff_ = 1.0f - std::exp(-inv_fs_ / kSlideTau);
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
  g_half_min_ = 0.5f * tan_fit(kPi * kMinCutoff * inv_fs_);
  muf_hard_a_ = 1.0f - std::exp(-2.0f * kPi * kMufHardHz * inv_fs_);
  swp_slow_c_ = 1.0f - std::exp(-static_cast<float>(kControlPeriod) * inv_fs_ / kSwpSlowTau);
  swp_fast_c_ = 1.0f - std::exp(-static_cast<float>(kControlPeriod) * inv_fs_ / kSwpFastTau);
  stock_ = false;
  track_on_ = false;
  track_ = 0.0f;
  sweep_mode_ = 1;
  swp_mode_ = 1;
  hires_ = false;
  swp_ = 0.0f;
  swp_m_ = 0.0f;
  att_ = 1.0f;
  att_c_ = 0.0f;
  att_idx_ = 0;
  reso_ = 0.0f;
  last_out_ = 0.0f;
  muf_lp_ = 0.0f;

  accent_.lp.set_coeff(smooth_k_);
  volume_.lp.set_coeff(smooth_k_);
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
    t_[i] = 0.0f;
  }
  hp_state_ = 0.0f;
  sub_g_ = 0.05f;
  sub_g_step_ = 0.0f;
  fresh_ = true;
  vcf_ = 0.0f;
  vca_ = 0.0f;
  acc_ = 0.0f;
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
  has_accent_ = accent;
  if (!legato) {
    vcf_ = 1.0f;
    vca_ = 1.0f;
    if (accent) {
      acc_ = 1.0f;
    }
  } else if (accent && acc_ < 0.2f) {
    acc_ = 0.8f;
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
  } else {
    target_hz_ = midi_to_hz(held_[held_count_ - 1u]);
  }
}

void AcidVoice::all_notes_off() noexcept {
  held_count_ = 0;
  gate_ = false;
}

void AcidVoice::apply(const dsp::Event& e) noexcept {
  switch (e.kind) {
    case dsp::EventKind::NoteOn:
      note_on(e.note, (e.flags & dsp::kNoteAccent) != 0u, (e.flags & dsp::kNoteSlide) != 0u);
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

// g = tan(pi fc / fs) for fc = 2^x Hz, with the original's ceiling of 0.95 (x is clamped to where it is reached).
float AcidVoice::ladder_g(float x) const noexcept {
  x = x > x_max_ ? x_max_ : x;
  const float g = tan_fit(o3sdk::core::exp2_fast(x) * pi_over_fs_);
  return g > kGMax ? kGMax : g;
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

// The accent's lift of the cutoff, in octaves. Acc Sweep NORMAL or HI RES with Swp Speed NORMAL is today's: the accent pulse itself. OFF takes
// it away; SLOW and FAST read the follower instead. Written without an early return: the product stays one expression, so the compiler
// fuses it into the caller's add exactly as it did before the mods (a different rounding of every stock cutoff otherwise).
float AcidVoice::sweep_term(float acc_now, float swp_now) const noexcept {
  const float lift = sweep_mode_ == 0u ? 0.0f : (swp_mode_ == 1u ? acc_now : swp_now);
  return accent_oct_ * lift;
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
    const float g_t = db_gain(od_db_t_);
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
      od_step_ = (db_gain(od_db_) - od_g_) * (1.0f / static_cast<float>(kControlPeriod));
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

  // Muffler: a stair of the wet/dry mix. A change of type dips the mix to 0 and back with the new type; leaving 0 re-seeds the low-pass.
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
    mix_.v = absf(d) <= ramp_mix_rate_ ? t : mix_.v + (d < 0.0f ? -ramp_mix_rate_ : ramp_mix_rate_);
  }

  // The states of the mods, flushed as the envelopes are.
  if (absf(fm_xm1_) < kTiny) fm_xm1_ = 0.0f;
  if (absf(fm_ym1_) < kTiny) fm_ym1_ = 0.0f;
  if (absf(fm_y_) < kTiny) fm_y_ = 0.0f;
  if (absf(muf_lp_) < kTiny) muf_lp_ = 0.0f;

  modded_ = mods_needed();
}

void AcidVoice::control() noexcept {
  if (vcf_ < kTiny) {
    vcf_ = 0.0f;
  }
  if (acc_ < kTiny) {
    acc_ = 0.0f;
  }
  if (vca_ < kTiny) {
    vca_ = 0.0f;
  }

  if (accent_.moving) {
    accent_boost_ = kAccentLift * step(accent_, 1.0e-4f);
  }
  if (volume_.moving) {
    gain_ = step(volume_, 1.0e-6f);
  }
  if (ramp_pending_ || modded_) {
    advance_mods();
  }

  // The accent's sweep for SLOW and FAST (Swp Speed): a follower of the accent pulse at the end of this period, at the control rate.
  const float acc_end = acc_ * accent_coeff_n_;
  float swp_end = 0.0f;
  if (swp_mode_ != 1u) {
    if (swp_mode_ == 2u) {
      swp_ += (2.0f * acc_end - swp_) * swp_slow_c_;
    } else {
      swp_m_ += (acc_end - swp_m_) * swp_fast_c_;
      swp_ = acc_end > swp_m_ ? acc_end - swp_m_ : 0.0f;
    }
    if (swp_ < kTiny) swp_ = 0.0f;
    if (swp_m_ < kTiny) swp_m_ = 0.0f;
    swp_end = swp_;
  }

  // DiodeLadderFilter::process, the cutoff: base x 2^(envmod x env x 5.2 + accent x 2.2), g = tan(pi fc / fs) up to 0.95. The original
  // computes it every sample from the envelopes as they stand after that sample's decay. Here it is computed at the end of the period
  // (the envelopes decayed kControlPeriod times) and the filter ramps to it, so the coefficient is right at every period boundary and
  // linear between; after a note-on it starts from its value at the first sample instead of from the old one.
  const float base = cutoff_base();  // cutoff_l2_ at stock; Tracking adds its term
  if (fresh_) {  // the coefficient the filter has just before the first sample (the envelopes as they stand)
    fresh_ = false;
    const float swp_now = swp_mode_ == 0u ? (acc_ > swp_m_ ? acc_ - swp_m_ : 0.0f) : swp_;
    sub_g_ = 0.5f * ladder_g(base + env_mod_oct_ * vcf_ + sweep_term(acc_, swp_now));
  }
  const float end_g = ladder_g(base + env_mod_oct_ * (vcf_ * (has_accent_ ? accent_coeff_n_ : decay_coeff_n_)) + sweep_term(acc_end, swp_end));
  sub_g_step_ = (0.5f * end_g - sub_g_) * (1.0f / static_cast<float>(kControlPeriod));
}

// ---- audio --------------------------------------------------------------------------------------------------------------------

template <bool Square, bool Modded>
void AcidVoice::run(float* out, std::uint32_t n) noexcept {
  if constexpr (!Modded) {
    // The stock loop: the voice as it was before the mods. Nothing here may change (the objdump of this instantiation is compared with it).
    // The state is copied into locals for the loop (register pressure, rt-optimize) and written back after it.
    float phase = phase_;
    float sqr_hp = sqr_hp_;
    float pitch = pitch_hz_;
    const float target = target_hz_;
    const float slide = slide_coeff_;
    const float scale = tune_scale_;
    const float inc_lo = inc_lo_;
    const float inc_hi = inc_hi_;
    const float sqr_c = sqr_hp_coeff_;

    float vcf = vcf_;
    float vca = vca_;
    float acc = acc_;
    const float vcf_c = has_accent_ ? accent_coeff_ : decay_coeff_;
    const float acc_c = accent_coeff_;
    const float vca_c = gate_ ? hold_coeff_ : release_coeff_;

    float s0 = s_[0], s1 = s_[1], s2 = s_[2], s3 = s_[3];
    float t0 = t_[0], t1 = t_[1], t2 = t_[2], t3 = t_[3];
    float hps = hp_state_;
    const float hpa = hp_alpha_;
    const float k = fb_k_;
    const float drive = in_drive_;
    float sg = sub_g_;
    const float sg_step = sub_g_step_;
    const float boost = accent_boost_;
    const float gain = gain_;

    for (std::uint32_t i = 0; i < n; ++i) {
      // SynthEngine::renderSamples: the pitch glide, the oscillator (Oscillator303::setFrequency: tune, then the 10 Hz .. 0.45 fs clamp).
      pitch += (target - pitch) * slide;
      const float inc = clampf(pitch * scale, inc_lo, inc_hi);
      const float osc = osc_sample<Square>(phase, sqr_hp, inc, sqr_c);

      // Envelope303::process.
      vcf *= vcf_c;
      acc *= acc_c;
      vca *= vca_c;

      // DiodeLadderFilter::process: two sub-steps of the four-stage ladder.
      sg += sg_step;
      for (int sub = 0; sub < 2; ++sub) {
        const float hp = hpa * (hps + s3);  // feedback high-pass (AC coupling)
        hps = hp - s3;
        const float sat = tanh_pade(k * hp);
        const float u = tanh_pade(osc * drive - sat);
        s0 += sg * (u - t0);
        t0 = tanh_pade(s0);
        s1 += sg * (t0 - t1);
        t1 = tanh_pade(s1);
        s2 += sg * (t1 - t2);
        t2 = tanh_pade(s2);
        s3 += sg * (t2 - t3);
        // The original clamps each of the four states to +-4 after its update. None of them reaches it: over 576 corner cases (both waves,
        // reso 0.65 and 1, cutoff 25 / 350 / 4500, env mod 0 and 1, accent 0 and 1, tune -12 / 0 / 12, notes 0 / 24 / 60 / 127 with a slide,
        // volume +6 dB, decay 2500) the largest |state| is 1.33 (stage 0), 1.10, 0.98 and 0.94, and the samples are bit-identical with the
        // limit at 2 or at 100. One guard remains, on the last stage (the one the feedback is taken from), as a net against the unforeseen; it
        // never acts in the reachable range.
        s3 = clampf(s3, -kStateLimit, kStateLimit);
        t3 = tanh_pade(s3);
      }
      const float filtered = tanh_pade(s3 * 1.5f);

      // The amplifier with the accent lift, then the unit's volume (SynthEngine's master gain; the distortion follows in the slot).
      out[i] = filtered * vca * (1.0f + acc * boost) * gain;
    }

    phase_ = phase;
    sqr_hp_ = sqr_hp;
    pitch_hz_ = pitch;
    vcf_ = vcf;
    vca_ = vca;
    acc_ = acc;
    s_[0] = s0, s_[1] = s1, s_[2] = s2, s_[3] = s3;
    t_[0] = t0, t_[1] = t1, t_[2] = t2, t_[3] = t3;
    hp_state_ = hps;
    sub_g_ = sg;
  } else {
    // The modded loop: the stock voice's maths in three passes over one control period (at most kControlPeriod samples), so no pass keeps
    // more floats live than the stock loop does. 1: the oscillator, the sub and the overdrive, into xin. 2: the three envelopes and the soft
    // attack, into amp. 3: the ladder (with Filter FM on its g), the amplifier, the muffler and the volume.
    static_assert(kControlPeriod <= 16u, "the scratch arrays below are one control period");
    alignas(8) float xin[kControlPeriod];  // declared, not placed: a doubleword each, so the pairs the compiler loads at once are aligned
    alignas(8) float amp[kControlPeriod];
    static_assert(sizeof(float) * kControlPeriod % 8u == 0u, "the scratch arrays are whole doublewords");
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
      const float drive = in_drive_;
      float subq = subq_;
      float subl = sub_.v;
      const float subl_step = sub_.step;
      float odg = od_g_;
      const float od_step = od_step_;
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
        xin[i] = (osc + subl * sub) * (drive * odg);
      }
      phase_ = phase;
      sqr_hp_ = sqr_hp;
      pitch_hz_ = pitch;
      subq_ = subq;
      sub_.v = subl;
      od_g_ = odg;
    }
    {
      float vcf = vcf_;
      float vca = vca_;
      float acc = acc_;
      float att = att_;
      const float vcf_c = has_accent_ ? accent_coeff_ : decay_coeff_;
      const float acc_c = accent_coeff_;
      const float vca_c = gate_ ? hold_coeff_ : release_coeff_;
      const float att_c = att_c_;
      const float boost = accent_boost_;
      for (std::uint32_t i = 0; i < n; ++i) {
        vcf *= vcf_c;
        acc *= acc_c;
        vca *= vca_c;
        att += (1.0f - att) * att_c;
        amp[i] = vca * att * (1.0f + acc * boost);
      }
      vcf_ = vcf;
      vca_ = vca;
      acc_ = acc;
      att_ = att;
    }
    {
      float s0 = s_[0], s1 = s_[1], s2 = s_[2], s3 = s_[3];
      float t0 = t_[0], t1 = t_[1], t2 = t_[2], t3 = t_[3];
      float hps = hp_state_;
      const float hpa = hp_alpha_;
      const float k = fb_k_;
      float sg = sub_g_;
      const float sg_step = sub_g_step_;
      const float gain = gain_;
      const float fmk = fm_.v;
      const float dcr = fm_dc_r_;
      float xm1 = fm_xm1_;
      float ym1 = fm_ym1_;
      float yp = fm_y_;
      const float sg_lo = g_half_min_;
      const float sg_hi = 0.5f * kGMax;
      const float mix = mix_.v;
      const float ma = muf_a_;
      const float mt = muf_t_;
      const float mit = muf_inv_t_;
      float mlp = muf_lp_;
      for (std::uint32_t i = 0; i < n; ++i) {
        sg += sg_step;
        // Filter FM: the last output sample, through a DC blocker, scales the ladder's g (linear, clamped).
        const float dc = (yp - xm1) + dcr * ym1;
        xm1 = yp;
        ym1 = dc;
        float sgm = sg * (1.0f + fmk * dc);
        sgm = sgm < sg_lo ? sg_lo : sgm;
        sgm = sgm > sg_hi ? sg_hi : sgm;
        const float xi = xin[i];
        for (int sub = 0; sub < 2; ++sub) {
          const float hp = hpa * (hps + s3);
          hps = hp - s3;
          const float sat = tanh_sat(k * hp);
          const float u = tanh_sat(xi - sat);
          s0 += sgm * (u - t0);
          t0 = tanh_pade(s0);
          s1 += sgm * (t0 - t1);
          t1 = tanh_pade(s1);
          s2 += sgm * (t1 - t2);
          t2 = tanh_pade(s2);
          s3 += sgm * (t2 - t3);
          s3 = clampf(s3, -kStateLimit, kStateLimit);
          t3 = tanh_pade(s3);
        }
        const float y = tanh_pade(s3 * 1.5f) * amp[i];
        // Muffler: the band above a corner (the whole signal for SOFT) is clipped, mixed with the dry signal.
        mlp += ma * (y - mlp);
        const float m = mlp + mt * tanh_sat((y - mlp) * mit);
        yp = y + mix * (m - y);
        out[i] = yp * gain;
      }
      s_[0] = s0, s_[1] = s1, s_[2] = s2, s_[3] = s3;
      t_[0] = t0, t_[1] = t1, t_[2] = t2, t_[3] = t3;
      hp_state_ = hps;
      sub_g_ = sg;
      fm_xm1_ = xm1;
      fm_ym1_ = ym1;
      fm_y_ = yp;
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
