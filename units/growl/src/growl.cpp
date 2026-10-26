// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_303/units/growl/growl.h>

#include <cmath>
#include <cstring>

// Cost [est], from the cross build's disassembly (arm-gnu 15.3, -O2 -ffp-contract=off) with Cortex-M4F timings (VMUL, VADD, VSUB 1 cycle,
// VFMA 3, VLDR/VSTR 2, VDIV 14), cycles a frame; a 64-frame block is 261,224 cycles, 4,082 a frame is 1 % of it. Measured on the unit
// (docs/hw/2026-10-26-m3c.md): +7 % of a block at Mix 100 %, 286 cycles a frame, for a first version the same model puts at 229 (170 in the
// loop; 59 in the control period: a powf for the delay, seven divisions, 140 for the run, the render's loads and stores and land()). The
// factor 1.25 is not explained (flash fetches behind the code cache, the bus), so the figures below are model x 1.25 where a % is given.
//   The loop: Steady 74 (Mix 1) or 81, Glide 86 or 92 (the delay ramps), Ramps 108 (Formant Mix, Comb Mix, Feedback or Mix ramp).
//   Settled (nothing moves): one control (56) and one run, render and land (140) for the block, not for each 16 frames: 74 + 196 / 64 = ~78.
//   A Note change glides the delay for ~30-50 ms: + ~200 (control, a polynomial exp2 instead of powf) a period: ~107 a frame.
//   A moving Vowel (the wobble): + ~215 a period (the coefficients: two divisions): ~100 a frame.
//   So 78 to 107 a frame, 1.9 to 2.6 % of a block, x 1.25 = 2.4 to 3.3 %; Ramps ~133, 4.1 %, only while a knob moves the mixes.
//   Idle (Mix 0, settled): a memcpy of the block when the buffers differ, nothing in place: under 0.1 %.
// The measurement is `diag cpu` with the growl at Mix 1 and a pattern playing (docs/hw/2026-10-26-m3c.md).
namespace o3_303 {

namespace {

namespace dsp = o3sdk::dsp;
using o3sdk::core::ParamCurve;
using o3sdk::core::ParamUnit;

constexpr std::uint8_t kSmooth = o3sdk::core::kParamSmooth;

// name, unit text, names, knots, min, max, default, detents, coarse, list count, display, curve, flags, page
constexpr o3sdk::core::ParamDesc kParams[Growl::kParamCount] = {
    {"VOWEL", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, kSmooth, 0},
    {"FORMANT Q", "", nullptr, nullptr, 1.0f, 20.0f, 5.0f, 100, 10, 0, ParamUnit::Fixed1, ParamCurve::Log, 0, 0},
    {"FORMANT MIX", "%", nullptr, nullptr, 0.0f, 1.0f, 1.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, kSmooth, 0},
    {"COMB TUNE", "st", nullptr, nullptr, -24.0f, 24.0f, 0.0f, 48, 12, 0, ParamUnit::Int, ParamCurve::Linear, kSmooth, 0},
    {"COMB FDBK", "", nullptr, nullptr, -0.95f, 0.95f, 0.6f, 38, 5, 0, ParamUnit::Fixed1, ParamCurve::Linear, kSmooth, 0},
    {"COMB MIX", "%", nullptr, nullptr, 0.0f, 1.0f, 0.5f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, kSmooth, 0},
    {"NOTE", "", nullptr, nullptr, 0.0f, 127.0f, 36.0f, 127, 12, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0},
    {"MIX", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, kSmooth, 0},
};

constexpr dsp::UnitInfo kInfo{"GROWL", dsp::UnitKind::Effect, kParams, Growl::kParamCount, nullptr, 0};

constexpr float kPi = 3.14159265f;
constexpr float kVowelSeconds = 0.003f;
constexpr float kPitchSeconds = 0.004f;
constexpr float kMixesSeconds = 0.005f;  // Formant Mix, Comb Mix, Feedback
constexpr float kMixRampSeconds = 0.005f;
constexpr float kTinyAudio = 1.0e-20f;   // below this a comb sample is zero: no denormals in the loop
constexpr float kTinyState = 1.0e-15f;   // the same for the filter states, checked once a period
constexpr float kMixOff = 1.0e-3f;       // a Mix below this is 0: the unit is bypassed (a tenth of the 1 % the page shows)
constexpr float kHugeState = 1.0e6f;     // a filter state above this has blown up: it is cleared, like a NaN

struct VowelRow {
  float hz[Growl::kFormants];
  float level[Growl::kFormants];
};
// A E I O U: Peterson and Barney's male averages for /a/ (as in "hot"), /e/ (bet), /i/ (beet), /o/ (the open o of "bought"), /u/ (boot).
constexpr VowelRow kVowelTable[Growl::kVowels] = {
    {{730.0f, 1090.0f, 2440.0f}, {1.00f, 0.63f, 0.32f}},
    {{530.0f, 1840.0f, 2480.0f}, {1.00f, 0.50f, 0.32f}},
    {{270.0f, 2290.0f, 3010.0f}, {1.00f, 0.32f, 0.25f}},
    {{570.0f, 840.0f, 2410.0f}, {1.00f, 0.50f, 0.16f}},
    {{300.0f, 870.0f, 2240.0f}, {1.00f, 0.32f, 0.10f}},
};

inline float onepole_k(float seconds, float fs) noexcept {
  return 1.0f - std::exp(-static_cast<float>(Growl::kControl) / (seconds * fs));
}

constexpr float kSettlePitch = 1.0e-3f;  // a smoothed pitch this close to its target [semitones] lands on it (a tenth of a cent): a Note
                                         // change then settles in ~40 ms instead of ~70, and the unit goes back to its cheap loop

// A one-pole step that ends on the target once it is close, so a settled value stops moving (and the work that follows it stops). Only the
// pitch needs it: it is the value that changes at every note; the others settle to the float's end in ~100 ms and move rarely.
inline float approach(float s, float t, float k, float eps) noexcept {
  const float d = t - s;
  return (d < eps && d > -eps) ? t : s + k * d;
}

// 2^x for the comb's pitch: x split into an integer and a fraction in [-0.5, 0.5], a degree-5 polynomial for the fraction (relative error
// 8e-8, fitted by least squares on the relative error) and the integer in the exponent. Instead of exp2f, which is powf(2, x) in newlib.
inline float exp2_poly(float x) noexcept {
  const float r = x + (x < 0.0f ? -0.5f : 0.5f);
  const auto n = static_cast<std::int32_t>(r);  // x rounded to the nearest integer
  const float f = x - static_cast<float>(n);
  float p = 0.0013276468f;
  p = p * f + 0.0096755413f;
  p = p * f + 0.0555071329f;
  p = p * f + 0.2402211972f;
  p = p * f + 0.6931469671f;
  p = p * f + 1.0000000717f;
  const std::uint32_t bits = static_cast<std::uint32_t>(n + 127) << 23;  // 2^n, n in -126..127 (the pitch is clamped to keep it there)
  float scale;
  std::memcpy(&scale, &bits, sizeof scale);
  return p * scale;
}

// 1/a, 1/b and 1/c with one division (a, b, c > 0).
inline void inv3(float a, float b, float c, float& ia, float& ib, float& ic) noexcept {
  const float ab = a * b;
  const float r = 1.0f / (ab * c);
  ia = b * c * r;
  ib = a * c * r;
  ic = ab * r;
}

inline float clampf(float v, float lo, float hi, float fallback) noexcept {
  if (!(v == v)) {
    return fallback;
  }
  return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace

Growl::Growl() noexcept = default;

const dsp::UnitInfo& Growl::info() const noexcept { return kInfo; }

void Growl::formants(float vowel, float hz[kFormants], float level[kFormants]) noexcept {
  const float v = vowel > 0.0f ? (vowel < 1.0f ? vowel : 1.0f) : 0.0f;  // also a NaN
  const float pos = v * static_cast<float>(kVowels - 1u);
  unsigned i = static_cast<unsigned>(pos);
  if (i > kVowels - 2u) {
    i = kVowels - 2u;
  }
  const float f = pos - static_cast<float>(i);
  const VowelRow& a = kVowelTable[i];
  const VowelRow& b = kVowelTable[i + 1u];
  for (unsigned k = 0; k < kFormants; ++k) {
    hz[k] = a.hz[k] + f * (b.hz[k] - a.hz[k]);
    level[k] = a.level[k] + f * (b.level[k] - a.level[k]);
  }
}

float Growl::comb_delay(float semitones, float sample_rate) noexcept {
  const float s = clampf(semitones, -48.0f, 160.0f, 36.0f);
  float d = sample_rate * (1.0f / 440.0f) * exp2_poly((69.0f - s) * (1.0f / 12.0f));
  for (unsigned i = 0; i < 8u && d > kMaxDelay; ++i) {
    d *= 0.5f;  // below the line's reach: an octave up
  }
  return d < kMinDelay ? kMinDelay : d;
}

void Growl::clear_state() noexcept {
  for (unsigned k = 0; k < kFormants; ++k) {
    ic1_[k] = 0.0f;
    ic2_[k] = 0.0f;
  }
  std::memset(line_, 0, sizeof line_);
  cleared_ = true;
}

// The comb's output gains from Comb Mix and Feedback as they stand: wet = c1 y + cn yc.
void Growl::derive_comb() noexcept {
  const float fa = fb_ < 0.0f ? -fb_ : fb_;
  c1_ = 1.0f - cmix_;
  cn_ = cmix_ * (1.0f - fa);
}

// The smoothed values jump to their targets (leaving idle, prepare): the wet signal starts at the vowel and the pitch that were asked for.
void Growl::snap() noexcept {
  vowel_s_ = vowel_target_;
  pitch_s_ = note_ + tune_;
  fmix_s_ = fmix_target_;
  cmix_s_ = cmix_target_;
  fb_s_ = fb_target_;
  delay_end_ = comb_delay(pitch_s_, fs_);
  pitch_c_ = pitch_s_;
  coef_dirty_ = true;
  moving_ = true;
  fmix_ = fmix_s_;
  cmix_ = cmix_s_;
  fb_ = fb_s_;
  derive_comb();
  delay_ = delay_end_;
  left_ = 0;
}

void Growl::prepare(float sample_rate) noexcept {
  fs_ = sample_rate > 1000.0f ? sample_rate : 44100.0f;
  k_vowel_ = onepole_k(kVowelSeconds, fs_);
  k_pitch_ = onepole_k(kPitchSeconds, fs_);
  k_mixes_ = onepole_k(kMixesSeconds, fs_);
  mix_step_ = 1.0f / (kMixRampSeconds * fs_);
  for (unsigned p = 0; p < kParamCount; ++p) {
    set(static_cast<std::uint16_t>(p), kParams[p].def);
  }
  mix_ = 0.0f;
  mix_end_ = 0.0f;
  mix_inc_ = 0.0f;
  w_ = 0;
  sweep_ = 0;
  clear_state();
  snap();
}

void Growl::set(std::uint16_t id, float v) noexcept {
  if (id >= kParamCount) {
    return;
  }
  const o3sdk::core::ParamDesc& d = kParams[id];
  const float x = o3sdk::core::param_clamp(d, v);
  if (id != Mix) {
    moving_ = true;  // the next control period steps the smoothing again (the Mix ramps on its own)
  }
  switch (id) {
    case Vowel:
      vowel_target_ = x;
      break;
    case FormantQ:
      q_ = x;
      kq_ = 1.0f / x;
      coef_dirty_ = true;
      break;
    case FormantMix:
      fmix_target_ = x;
      break;
    case CombTune:
      tune_ = x;
      break;
    case CombFeedback:
      fb_target_ = x;
      break;
    case CombMix:
      cmix_target_ = x;
      break;
    case Note:
      note_ = x;
      break;
    case Mix: {
      const bool was_idle = idle();
      // Below the display's last digit is off: a knob walked down in float steps can stop a hair above 0 and show 0 %, and the unit would
      // then run its whole path at no audible effect (the unit round of 2026-10-26 found +7 % of a block that way).
      mix_target_ = x < kMixOff ? 0.0f : x;
      if (was_idle && x > 0.0f) {
        if (!cleared_) {
          clear_state();  // a ramp to 0 that ended on a block's last frame leaves the state uncleared until the next block's process()
        }
        snap();  // the state is zero; the wet path starts from the settings as they are now
      }
      break;
    }
    default:
      break;
  }
}

// ---- control period -----------------------------------------------------------------------------------------------------------

// The values reached at the end of a period are exactly the ones the period aimed at (no drift of the running sums).
void Growl::land() noexcept {
  fmix_ = fmix_s_;
  cmix_ = cmix_s_;
  fb_ = fb_s_;
  derive_comb();
  delay_ = delay_end_;
  mix_ = mix_end_;
}

// The three band-passes for the vowel as it stands. g = tan(pi f / fs) is a [3/2] Pade form N/D, a1 = 1 / (1 + g (g + 1/Q)): the six divisions
// are two (the reciprocals of three numbers cost one division and six products).
void Growl::coefficients() noexcept {
  float hz[kFormants];
  float level[kFormants];
  formants(vowel_s_, hz, level);
  const float pi_fs = kPi / fs_;
  float n[kFormants], dn[kFormants], idn[kFormants];
  for (unsigned k = 0; k < kFormants; ++k) {
    const float x = pi_fs * hz[k];
    const float x2 = x * x;
    n[k] = x * (15.0f - x2);
    dn[k] = 15.0f - 6.0f * x2;
  }
  inv3(dn[0], dn[1], dn[2], idn[0], idn[1], idn[2]);
  float g[kFormants], e[kFormants], ie[kFormants];
  for (unsigned k = 0; k < kFormants; ++k) {
    g[k] = n[k] * idn[k];
    e[k] = 1.0f + g[k] * (g[k] + kq_);
  }
  inv3(e[0], e[1], e[2], ie[0], ie[1], ie[2]);
  for (unsigned k = 0; k < kFormants; ++k) {
    a1_[k] = ie[k];
    a2_[k] = g[k] * ie[k];
    g2_[k] = 2.0f * g[k];
    weight_[k] = level[k] * kMakeup * kq_;
  }
}

void Growl::control() noexcept {
  mode_ = Steady;
  if (moving_) {
    // Something was set, or a smoothed value is still on its way: step them all, and redo the work that depends on what moved.
    const float v0 = vowel_s_, p0 = pitch_s_, f0 = fmix_s_, c0 = cmix_s_, b0 = fb_s_;
    vowel_s_ += k_vowel_ * (vowel_target_ - vowel_s_);
    pitch_s_ = approach(pitch_s_, note_ + tune_, k_pitch_, kSettlePitch);
    fmix_s_ += k_mixes_ * (fmix_target_ - fmix_s_);
    cmix_s_ += k_mixes_ * (cmix_target_ - cmix_s_);
    fb_s_ += k_mixes_ * (fb_target_ - fb_s_);
    // A one-pole in float stops where k (t - s) is below half an ulp of s: the period it stops in is the last that moves.
    moving_ = vowel_s_ != v0 || pitch_s_ != p0 || fmix_s_ != f0 || cmix_s_ != c0 || fb_s_ != b0;

    if (coef_dirty_ || vowel_s_ != vowel_c_) {
      coefficients();
      vowel_c_ = vowel_s_;
      coef_dirty_ = false;
    }
    if (pitch_s_ != pitch_c_) {
      delay_end_ = comb_delay(pitch_s_, fs_);
      pitch_c_ = pitch_s_;
    }
    constexpr float kInvPeriod = 1.0f / static_cast<float>(kControl);
    if (fmix_s_ != fmix_ || cmix_s_ != cmix_ || fb_s_ != fb_) {
      mode_ = Ramps;
      const float fa = fb_s_ < 0.0f ? -fb_s_ : fb_s_;
      fmix_inc_ = (fmix_s_ - fmix_) * kInvPeriod;
      fb_inc_ = (fb_s_ - fb_) * kInvPeriod;
      c1_inc_ = ((1.0f - cmix_s_) - c1_) * kInvPeriod;
      cn_inc_ = (cmix_s_ * (1.0f - fa) - cn_) * kInvPeriod;
      delay_inc_ = (delay_end_ - delay_) * kInvPeriod;
    } else if (delay_end_ != delay_) {
      mode_ = Glide;
      delay_inc_ = (delay_end_ - delay_) * kInvPeriod;
    }
  }

  // The filter states: tiny ones (denormals once they decay) and runaway or NaN ones are cleared. Every fourth period is plenty: a state takes
  // thousands of frames to decay from the floor to a denormal, and a NaN only needs to be gone before the signal that carried it.
  if (++sweep_ >= 4u) {
    sweep_ = 0;
    for (unsigned k = 0; k < kFormants; ++k) {
      const float s1 = ic1_[k] < 0.0f ? -ic1_[k] : ic1_[k];
      const float s2 = ic2_[k] < 0.0f ? -ic2_[k] : ic2_[k];
      if (!(s1 >= kTinyState && s1 < kHugeState)) {  // tiny, blown up or NaN (a NaN fails every comparison)
        ic1_[k] = 0.0f;
      }
      if (!(s2 >= kTinyState && s2 < kHugeState)) {
        ic2_[k] = 0.0f;
      }
    }
  }

  const float delta = mix_target_ - mix_;
  if (delta == 0.0f) {
    mix_end_ = mix_;
    mix_inc_ = 0.0f;
  } else {
    constexpr float kInvPeriod = 1.0f / static_cast<float>(kControl);
    const float step = mix_step_ * static_cast<float>(kControl);
    const float mag = delta < 0.0f ? -delta : delta;
    mix_end_ = mag <= step ? mix_target_ : (delta < 0.0f ? mix_ - step : mix_ + step);
    mix_inc_ = (mix_end_ - mix_) * kInvPeriod;
    if (mode_ != Ramps) {  // only the Mix moves: the other ramps are flat
      fmix_inc_ = 0.0f;
      fb_inc_ = 0.0f;
      c1_inc_ = 0.0f;
      cn_inc_ = 0.0f;
      delay_inc_ = mode_ == Glide ? delay_inc_ : 0.0f;
      mode_ = Ramps;
    }
  }
  left_ = kControl;
}

// ---- the sample loop ----------------------------------------------------------------------------------------------------------

// One piece of a control period: the mono wet path (three band-passes in parallel, then the comb) against the dry stereo input.
template <Growl::Mode kMode, bool kFull>
void Growl::render(const dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept {
  static_assert(kMode != Ramps || !kFull, "the full-wet loop has no Mix ramp");
  constexpr bool kFixedMix = kMode != Ramps;    // Formant Mix, Comb Mix, Feedback and the Mix sit still
  constexpr bool kFixedDelay = kMode == Steady;  // and the delay
  cleared_ = false;
  float ic1_0 = ic1_[0], ic1_1 = ic1_[1], ic1_2 = ic1_[2];
  float ic2_0 = ic2_[0], ic2_1 = ic2_[1], ic2_2 = ic2_[2];
  const float a1_0 = a1_[0], a1_1 = a1_[1], a1_2 = a1_[2];
  const float a2_0 = a2_[0], a2_1 = a2_[1], a2_2 = a2_[2];
  const float g2_0 = g2_[0], g2_1 = g2_[1], g2_2 = g2_[2];
  float fmix = fmix_, fb = fb_, c1 = c1_, cn = cn_, delay = delay_, m = mix_;
  // Fixed mixes: the Formant Mix folded into the weights, y = (1 - fmix) x + fmix w . v1 (one operation fewer a frame, and a register).
  const float w0 = kFixedMix ? fmix * weight_[0] : weight_[0];
  const float w1 = kFixedMix ? fmix * weight_[1] : weight_[1];
  const float w2 = kFixedMix ? fmix * weight_[2] : weight_[2];
  const float c0 = 1.0f - fmix;
  const float fmix_inc = fmix_inc_, fb_inc = fb_inc_, c1_inc = c1_inc_, cn_inc = cn_inc_, delay_inc = delay_inc_, mix_inc = mix_inc_;
  float* const line = line_;
  std::uint32_t w = w_;
  constexpr std::uint32_t kMask = kCombFrames - 1u;
  auto di = static_cast<std::uint32_t>(delay);
  float fr = delay - static_cast<float>(di);

  for (std::uint32_t i = from; i < to; ++i) {
    if constexpr (!kFixedDelay) {
      di = static_cast<std::uint32_t>(delay);
      fr = delay - static_cast<float>(di);
    }
    const float xl = args.in_l[i];
    const float xr = args.in_r[i];
    const float x = 0.5f * (xl + xr);

    // Three trapezoidal state-variable band-passes (v1 is the band output, peak gain 1/k at the centre; the weight has k folded in). The
    // second integrator's output is ic2 + g v1, so its state moves by 2 g v1.
    const float v1_0 = a1_0 * ic1_0 + a2_0 * (x - ic2_0);
    ic1_0 = (v1_0 + v1_0) - ic1_0;
    ic2_0 += g2_0 * v1_0;
    const float v1_1 = a1_1 * ic1_1 + a2_1 * (x - ic2_1);
    ic1_1 = (v1_1 + v1_1) - ic1_1;
    ic2_1 += g2_1 * v1_1;
    const float v1_2 = a1_2 * ic1_2 + a2_2 * (x - ic2_2);
    ic1_2 = (v1_2 + v1_2) - ic1_2;
    ic2_2 += g2_2 * v1_2;
    const float formant = w0 * v1_0 + w1 * v1_1 + w2 * v1_2;
    float y;
    if constexpr (kFixedMix) {
      y = c0 * x + formant;
    } else {
      y = x + fmix * (formant - x);
    }

    // The comb: yc[n] = y + g yc[n - D], D = di + fr read between two neighbours of the line.
    const float d0 = line[(w - di) & kMask];
    const float d1 = line[(w - di - 1u) & kMask];
    const float yd = d0 + fr * (d1 - d0);
    float yc = y + fb * yd;
    yc = (std::fabs(yc) > kTinyAudio) ? yc : 0.0f;
    line[w] = yc;
    w = (w + 1u) & kMask;
    const float wet = c1 * y + cn * yc;

    if constexpr (kFull) {
      args.out_l[i] = wet;  // Mix 100 %: the dry part is gone
      args.out_r[i] = wet;
    } else {
      args.out_l[i] = xl + m * (wet - xl);
      args.out_r[i] = xr + m * (wet - xr);
    }

    if constexpr (!kFixedMix) {
      fmix += fmix_inc;
      fb += fb_inc;
      c1 += c1_inc;
      cn += cn_inc;
      m += mix_inc;
    }
    if constexpr (!kFixedDelay) {
      delay += delay_inc;
    }
  }

  ic1_[0] = ic1_0;
  ic1_[1] = ic1_1;
  ic1_[2] = ic1_2;
  ic2_[0] = ic2_0;
  ic2_[1] = ic2_1;
  ic2_[2] = ic2_2;
  if constexpr (!kFixedMix) {
    fmix_ = fmix;
    fb_ = fb;
    c1_ = c1;
    cn_ = cn;
    mix_ = m;
  }
  if constexpr (!kFixedDelay) {
    delay_ = delay;
  }
  w_ = w;
}

// One run between two events: the parameters do not change inside it. Idle (Mix 0 and settled): the input goes through untouched.
void Growl::run(const dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept {
  while (from < to) {
    if (idle()) {
      if (args.out_l != args.in_l) {
        std::memcpy(args.out_l + from, args.in_l + from, (to - from) * sizeof(float));
      }
      if (args.out_r != args.in_r) {
        std::memcpy(args.out_r + from, args.in_r + from, (to - from) * sizeof(float));
      }
      if (!cleared_) {
        clear_state();
      }
      return;
    }
    if (left_ == 0u) {
      control();
    }
    std::uint32_t n = to - from < left_ ? to - from : left_;
    // A settled period (nothing moves, nothing ramps) is the next one again: its control and its land change nothing but the sweep of the
    // filter states. Carry on to the end of the run, one render for the block instead of one for each period.
    if (mode_ == Steady && !moving_ && mix_target_ == mix_) {
      n = to - from;
    }
    const bool full = mix_ == 1.0f;  // (in the Ramps period the Mix may be moving: not used there)
    if (mode_ == Steady) {
      full ? render<Steady, true>(args, from, from + n) : render<Steady, false>(args, from, from + n);
    } else if (mode_ == Glide) {
      full ? render<Glide, true>(args, from, from + n) : render<Glide, false>(args, from, from + n);
    } else {
      render<Ramps, false>(args, from, from + n);
    }
    if (n < left_) {
      left_ -= n;
    } else {  // a boundary of the period grid was reached (at the end of the render, or inside a settled one): the grid goes on from there
      const std::uint32_t over = n - left_;
      sweep_ += over / kControl;
      left_ = over % kControl == 0u ? 0u : kControl - over % kControl;
      land();
    }
    from += n;
  }
}

void Growl::process(const dsp::ProcessArgs& args) noexcept {
  dsp::for_each_run(
      args, [&](std::uint32_t from, std::uint32_t to) { run(args, from, to); },
      [&](const dsp::Event& e) {
        if (e.kind == dsp::EventKind::Param) {
          set(e.param, e.value);
        }
      });
}

}  // namespace o3_303
