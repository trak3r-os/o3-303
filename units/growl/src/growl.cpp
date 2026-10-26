// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_303/units/growl/growl.h>

#include <cmath>
#include <cstring>

// Cost [est], not measured on a unit (the measurement is `diag cpu` with the growl at Mix 1 and a pattern playing). A block is 64
// frames, 261,224 cycles. Counted from the cross build's disassembly (arm-gnu 15.3, -O2), see the "Made (dsp+seq)" paragraph of
// docs/M3C-PLAN.md for the numbers:
//   Idle (Mix 0, settled): a memcpy of the block when the buffers differ, nothing in place: under 0.1 %.
//   Active: the sample loop, mono wet path: three state-variable bands, the comb with its interpolated read and the mix.
//   Control period (every 16 frames): one exp2 for the comb pitch, the Pade tangents, three divisions.
// Against the 37 % the unit measured o3-303 at, playing (docs/hw/2026-10-26-m3ba.md), the growl adds its active figure.
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

// tan(x) for the band-pass centres (x = pi f / fs, up to about 0.3): a [3/2] Pade form, error under 1e-5 relative up to x = 0.3.
inline float tan_pade(float x) noexcept {
  const float x2 = x * x;
  return x * (15.0f - x2) / (15.0f - 6.0f * x2);
}

inline float onepole_k(float seconds, float fs) noexcept {
  return 1.0f - std::exp(-static_cast<float>(Growl::kControl) / (seconds * fs));
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
  float d = sample_rate * (1.0f / 440.0f) * std::exp2((69.0f - s) * (1.0f / 12.0f));
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

// The smoothed values jump to their targets (leaving idle, prepare): the wet signal starts at the vowel and the pitch that were asked for.
void Growl::snap() noexcept {
  vowel_s_ = vowel_target_;
  pitch_s_ = note_ + tune_;
  fmix_s_ = fmix_target_;
  cmix_s_ = cmix_target_;
  fb_s_ = fb_target_;
  delay_end_ = comb_delay(pitch_s_, fs_);
  fmix_ = fmix_s_;
  cmix_ = cmix_s_;
  fb_ = fb_s_;
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
  clear_state();
  snap();
}

void Growl::set(std::uint16_t id, float v) noexcept {
  if (id >= kParamCount) {
    return;
  }
  const o3sdk::core::ParamDesc& d = kParams[id];
  const float x = o3sdk::core::param_clamp(d, v);
  switch (id) {
    case Vowel:
      vowel_target_ = x;
      break;
    case FormantQ:
      q_ = x;
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
  delay_ = delay_end_;
  mix_ = mix_end_;
}

void Growl::control() noexcept {
  vowel_s_ += k_vowel_ * (vowel_target_ - vowel_s_);
  pitch_s_ += k_pitch_ * ((note_ + tune_) - pitch_s_);
  fmix_s_ += k_mixes_ * (fmix_target_ - fmix_s_);
  cmix_s_ += k_mixes_ * (cmix_target_ - cmix_s_);
  fb_s_ += k_mixes_ * (fb_target_ - fb_s_);

  float hz[kFormants];
  float level[kFormants];
  formants(vowel_s_, hz, level);
  const float kq = 1.0f / q_;
  const float pi_fs = kPi / fs_;
  for (unsigned k = 0; k < kFormants; ++k) {
    const float g = tan_pade(pi_fs * hz[k]);
    const float a1 = 1.0f / (1.0f + g * (g + kq));
    a1_[k] = a1;
    a2_[k] = g * a1;
    a3_[k] = g * g * a1;
    weight_[k] = level[k] * kMakeup * kq;
    const float s1 = ic1_[k] < 0.0f ? -ic1_[k] : ic1_[k];
    const float s2 = ic2_[k] < 0.0f ? -ic2_[k] : ic2_[k];
    if (!(s1 >= kTinyState && s1 < kHugeState)) {  // tiny, blown up or NaN (a NaN fails every comparison)
      ic1_[k] = 0.0f;
    }
    if (!(s2 >= kTinyState && s2 < kHugeState)) {
      ic2_[k] = 0.0f;
    }
  }

  constexpr float kInvPeriod = 1.0f / static_cast<float>(kControl);
  fmix_inc_ = (fmix_s_ - fmix_) * kInvPeriod;
  cmix_inc_ = (cmix_s_ - cmix_) * kInvPeriod;
  fb_inc_ = (fb_s_ - fb_) * kInvPeriod;
  delay_end_ = comb_delay(pitch_s_, fs_);
  delay_inc_ = (delay_end_ - delay_) * kInvPeriod;

  const float delta = mix_target_ - mix_;
  if (delta == 0.0f) {
    mix_end_ = mix_;
    mix_inc_ = 0.0f;
  } else {
    const float step = mix_step_ * static_cast<float>(kControl);
    const float mag = delta < 0.0f ? -delta : delta;
    mix_end_ = mag <= step ? mix_target_ : (delta < 0.0f ? mix_ - step : mix_ + step);
    mix_inc_ = (mix_end_ - mix_) * kInvPeriod;
  }
  left_ = kControl;
}

// ---- the sample loop ----------------------------------------------------------------------------------------------------------

// One piece of a control period: the mono wet path (three band-passes in parallel, then the comb) against the dry stereo input.
void Growl::render(const dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept {
  cleared_ = false;
  float ic1_0 = ic1_[0], ic1_1 = ic1_[1], ic1_2 = ic1_[2];
  float ic2_0 = ic2_[0], ic2_1 = ic2_[1], ic2_2 = ic2_[2];
  const float a1_0 = a1_[0], a1_1 = a1_[1], a1_2 = a1_[2];
  const float a2_0 = a2_[0], a2_1 = a2_[1], a2_2 = a2_[2];
  const float a3_0 = a3_[0], a3_1 = a3_[1], a3_2 = a3_[2];
  const float w0 = weight_[0], w1 = weight_[1], w2 = weight_[2];
  float fmix = fmix_, cmix = cmix_, fb = fb_, delay = delay_, m = mix_;
  const float fmix_inc = fmix_inc_, cmix_inc = cmix_inc_, fb_inc = fb_inc_, delay_inc = delay_inc_, mix_inc = mix_inc_;
  float* const line = line_;
  std::uint32_t w = w_;
  constexpr std::uint32_t kMask = kCombFrames - 1u;

  for (std::uint32_t i = from; i < to; ++i) {
    const float xl = args.in_l[i];
    const float xr = args.in_r[i];
    const float x = 0.5f * (xl + xr);

    // Three trapezoidal state-variable band-passes (v1 is the band output, peak gain 1/k at the centre; the weight has k folded in).
    const float v3_0 = x - ic2_0;
    const float v1_0 = a1_0 * ic1_0 + a2_0 * v3_0;
    const float v2_0 = ic2_0 + a2_0 * ic1_0 + a3_0 * v3_0;
    ic1_0 = 2.0f * v1_0 - ic1_0;
    ic2_0 = 2.0f * v2_0 - ic2_0;
    const float v3_1 = x - ic2_1;
    const float v1_1 = a1_1 * ic1_1 + a2_1 * v3_1;
    const float v2_1 = ic2_1 + a2_1 * ic1_1 + a3_1 * v3_1;
    ic1_1 = 2.0f * v1_1 - ic1_1;
    ic2_1 = 2.0f * v2_1 - ic2_1;
    const float v3_2 = x - ic2_2;
    const float v1_2 = a1_2 * ic1_2 + a2_2 * v3_2;
    const float v2_2 = ic2_2 + a2_2 * ic1_2 + a3_2 * v3_2;
    ic1_2 = 2.0f * v1_2 - ic1_2;
    ic2_2 = 2.0f * v2_2 - ic2_2;
    const float formant = w0 * v1_0 + w1 * v1_1 + w2 * v1_2;
    const float y = x + fmix * (formant - x);

    // The comb: y[n] = in + g y[n - D], D = di + fr read between two neighbours of the line.
    const auto di = static_cast<std::uint32_t>(delay);
    const float fr = delay - static_cast<float>(di);
    const float d0 = line[(w - di) & kMask];
    const float d1 = line[(w - di - 1u) & kMask];
    const float yd = d0 + fr * (d1 - d0);
    float yc = y + fb * yd;
    yc = (std::fabs(yc) > kTinyAudio) ? yc : 0.0f;
    line[w] = yc;
    w = (w + 1u) & kMask;
    const float norm = 1.0f - std::fabs(fb);
    const float wet = y + cmix * (norm * yc - y);

    args.out_l[i] = xl + m * (wet - xl);
    args.out_r[i] = xr + m * (wet - xr);

    fmix += fmix_inc;
    cmix += cmix_inc;
    fb += fb_inc;
    delay += delay_inc;
    m += mix_inc;
  }

  ic1_[0] = ic1_0;
  ic1_[1] = ic1_1;
  ic1_[2] = ic1_2;
  ic2_[0] = ic2_0;
  ic2_[1] = ic2_1;
  ic2_[2] = ic2_2;
  fmix_ = fmix;
  cmix_ = cmix;
  fb_ = fb;
  delay_ = delay;
  mix_ = m;
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
    const std::uint32_t n = to - from < left_ ? to - from : left_;
    render(args, from, from + n);
    left_ -= n;
    if (left_ == 0u) {
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
