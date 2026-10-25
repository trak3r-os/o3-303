// SPDX-License-Identifier: GPL-3.0-or-later
//
// DISTORTION: the five-model distortion of trak-303. A port of DSP/DistortionUnit.{h,cpp} and DSP/Common303.h of Replicant 303 by
// DanimanX, commit d5369be028399f5a403067b58da2936caa070c0e (third_party/replicant303/DSP/). Replicant 303 is distributed under the
// MIT License:
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
// The sample loop is one function per model (a template), so that the model is not tested per sample. Per sample: the model, the tone
// low-pass, the mix (smoothed per sample), the DC blocker. Once per control period: the smoothers of Drive and Tone, the coefficients
// of the model, the flush of states near zero. Real-time: no allocation, no double, no libm in the loop.

#include <o3_303/units/distortion/distortion.h>

#include <o3sdk/core/fast_math.h>

#include <cmath>
#include <cstring>

namespace o3_303 {

namespace {

using o3sdk::core::ParamCurve;
using o3sdk::core::ParamUnit;
namespace dsp = o3sdk::dsp;

constexpr const char* kActiveNames[] = {"OFF", "ON"};
constexpr const char* kTypeNames[] = {"GRIT", "TUBE", "HARD", "FUZZ", "FOLD"};

constexpr std::uint8_t kSmooth = o3sdk::core::kParamSmooth;

// name, unit text, names, knots, min, max, default, detents, coarse, list count, display, curve, flags, page
constexpr o3sdk::core::ParamDesc kParams[Distortion::kParamCount] = {
    {"ACTIVE", "", kActiveNames, nullptr, 0.0f, 1.0f, 1.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"TYPE", "", kTypeNames, nullptr, 0.0f, 4.0f, 0.0f, 4, 1, 5, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"DRIVE", "%", nullptr, nullptr, 0.0f, 1.0f, 0.5f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, kSmooth, 0},
    {"TONE", "%", nullptr, nullptr, 0.0f, 1.0f, 0.6f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, kSmooth, 0},
    {"MIX", "%", nullptr, nullptr, 0.0f, 1.0f, 0.85f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, kSmooth, 0},
};

constexpr dsp::UnitInfo kInfo{"DISTORTION", dsp::UnitKind::Effect, kParams, Distortion::kParamCount, nullptr, 0};

// ---- constants of the original ----------------------------------------------------------------------------------------------
constexpr float kPi = 3.14159265f;
constexpr float kPreHpHz = 450.0f;   // the Rat's high-pass before the gain stage
constexpr float kToneBaseHz = 800.0f;
constexpr float kLog2Twenty = 4.32192809f;  // tone cutoff = 800 x 20^tone

// The original's output scalar of each model (0.85, 0.90, 0.85, 0.80, 0.80); the model's level trim (Distortion::trim) is folded into it
// with the drive in use (derive()), so that the trim costs nothing in the sample loop.
constexpr float kScale[Distortion::kModelCount] = {0.85f, 0.90f, 0.85f, 0.80f, 0.80f};

// The trim table (Distortion::trim): the gain of each model at Drive 0, 0.05 .. 1, and the same in dB above each row. Measured offline on
// the 303 line of units/tb303/tests/tb303_chain_test.cpp (the voice at Replicant's defaults, Volume 0 dB, SAW and SQR) through the unit at
// Tone 0.6 and Mix 1; the level of a model is the mean in dB of the RMS of the two. GRIT, the reference: 0 dB up to the default Drive;
// above it the gain that holds its wet peak (the larger of the two waves) at its value at 0.5, which falls to -2.7 dB by Drive 0.85
// (its RMS rises by only 1 to 2 dB there, its peak by 2.5 dB). The others: the gain that makes their level GRIT's, trimmed as it is, at
// the same Drive, faded in over Drive 0 .. 0.25 (at 0 it is M3's trim; GRIT's 450 Hz high-pass makes it far quieter than the others
// at low Drive, where they are near-transparent). At Drive 0.5 TUBE, HARD and FOLD are M3's trims and so the default sound is
// unchanged; FUZZ is re-measured after its negative half was bounded (tanh_pade_floor). Measure again when a model, the voice or the
// line changes: the "distortion levels" cases of tb303_chain_test.cpp check this table against GRIT's level and peaks, on the line and
// on two other voice settings.
constexpr float kTrimTable[Distortion::kModelCount][Distortion::kTrimKnots] = {
    // GRIT, dB: +0.0 +0.0 +0.0 +0.0 +0.0 +0.0 +0.0 +0.0 +0.0 +0.0 +0.0 -0.6 -1.0 -1.5 -1.9 -2.2 -2.4 -2.7 -2.7 -2.7 -2.7
    {1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 0.9351f, 0.8883f, 0.8434f, 0.8061f, 0.7730f, 0.7560f, 0.7341f, 0.7341f, 0.7341f, 0.7341f},
    // TUBE, dB: -3.3 -4.5 -5.7 -6.2 -6.2 -5.9 -5.0 -4.4 -3.9 -3.6 -3.3 -3.6 -3.9 -4.2 -4.4 -4.7 -4.9 -5.1 -5.1 -5.1 -5.1
    {0.6839f, 0.5943f, 0.5172f, 0.4875f, 0.4889f, 0.5089f, 0.5606f, 0.6028f, 0.6363f, 0.6634f, 0.6839f, 0.6593f, 0.6401f, 0.6186f, 0.5994f, 0.5806f, 0.5718f, 0.5572f, 0.5577f, 0.5563f, 0.5529f},
    // HARD, dB: -1.6 -3.0 -4.2 -4.7 -4.7 -4.4 -3.5 -2.8 -2.3 -1.9 -1.6 -2.0 -2.2 -2.5 -2.7 -3.0 -3.1 -3.3 -3.3 -3.3 -3.3
    {0.8318f, 0.7078f, 0.6162f, 0.5807f, 0.5823f, 0.6059f, 0.6699f, 0.7225f, 0.7648f, 0.7996f, 0.8318f, 0.7985f, 0.7772f, 0.7527f, 0.7309f, 0.7093f, 0.6997f, 0.6830f, 0.6847f, 0.6840f, 0.6807f},
    // FUZZ, dB: -5.7 -6.6 -7.3 -7.3 -6.7 -5.8 -4.9 -4.2 -3.7 -3.3 -2.9 -3.2 -3.4 -3.7 -3.9 -4.2 -4.3 -4.5 -4.5 -4.5 -4.5
    {0.5188f, 0.4665f, 0.4316f, 0.4324f, 0.4615f, 0.5121f, 0.5687f, 0.6158f, 0.6544f, 0.6865f, 0.7138f, 0.6895f, 0.6727f, 0.6529f, 0.6352f, 0.6175f, 0.6101f, 0.5964f, 0.5987f, 0.5988f, 0.5966f},
    // FOLD, dB: +3.9 +2.2 +0.6 -0.4 -0.7 -0.6 +0.6 +1.7 +2.6 +3.3 +3.9 +3.7 +3.4 +3.0 +2.6 +2.2 +2.0 +1.8 +1.8 +1.8 +1.7
    {1.5668f, 1.2873f, 1.0747f, 0.9592f, 0.9196f, 0.9352f, 1.0744f, 1.2141f, 1.3475f, 1.4671f, 1.5668f, 1.5224f, 1.4755f, 1.4102f, 1.3476f, 1.2896f, 1.2600f, 1.2236f, 1.2247f, 1.2244f, 1.2208f},
};

// ---- ours --------------------------------------------------------------------------------------------------------------------
constexpr float kSmoothTau = 0.005f;   // seconds
constexpr float kDcHz = 5.0f;          // the DC blocker's corner
constexpr float kTiny = 1.0e-9f;       // states below this (-180 dB) are flushed to zero: no state lingers where a product of three is subnormal
constexpr float kHuge = 1.0e15f;       // states above this (or not a number) are reset

inline float clampf(float x, float lo, float hi) noexcept { return x < lo ? lo : (x > hi ? hi : x); }
// For a parameter value that comes from outside: a NaN is the lower end.
inline float clamp_param(float x, float lo, float hi) noexcept { return !(x >= lo) ? lo : (x > hi ? hi : x); }

// Common303.h fastTanh.
inline float tanh_pade(float x) noexcept {
  const float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// FUZZ's negative branch: Common303.h fastTanh of x, with x bounded below at -3. The Pade form is a saturator only up to |x| = 3, where it
// reaches 1 with a zero slope; beyond that it climbs again at a slope of 1/9 and does not saturate. The original's GermaniumFuzz feeds it
// biased x 2.1 with a gain of up to 46, so on a hot signal its negative half is a ramp ten times higher than the positive half (a square
// through FUZZ at full Drive reaches -3.8 where the others peak at 1.6; 48 of the 65 FUZZ presets of the vault peaked above full scale at
// the master). Bounded here, it saturates at -1 as a tanh does and is identical to the original below |x| = 3.
inline float tanh_pade_floor(float x) noexcept { return tanh_pade(x < -3.0f ? -3.0f : x); }

// Common303.h softClip: cubic, exactly +-1 at +-1.5. Below |x| of 1e-6 the cube is under 1e-12 of x, which a float cannot see, and the
// product would be subnormal for |x| under 2e-13 (a decaying tail passes through there for thousands of samples): x is returned as it is.
inline float soft_clip(float x) noexcept {
  if (x <= -1.5f) {
    return -1.0f;
  }
  if (x >= 1.5f) {
    return 1.0f;
  }
  if (x > -1.0e-6f && x < 1.0e-6f) {
    return x;
  }
  return x - (4.0f / 27.0f) * (x * x * x);
}

// Common303.h diodeClip: x / sqrt(1 + x^2).
inline float diode_clip(float x) noexcept { return x / std::sqrt(1.0f + x * x); }

// s^0.94 for s in [0, 1] (TubeDrive's asymmetry; the original calls pow): s + s (1 - s) q(s), q a cubic fitted to the difference.
// Absolute error under 1.2e-3 (tests/distortion_oracle_test.cpp measures it), exact at 0 and 1.
inline float pow094(float s) noexcept {
  return s + s * (1.0f - s) * (0.23418564f + s * (-0.72560041f + s * (1.17682079f + s * -0.65911563f)));
}

// floor for the folder: an integer conversion (the argument is clamped first, so the conversion is defined; NaN goes to the lower end).
inline float floor_fast(float t) noexcept {
  t = !(t > -1.0e6f) ? -1.0e6f : (t > 1.0e6f ? 1.0e6f : t);
  const auto i = static_cast<int>(t);
  const float f = static_cast<float>(i);
  return f > t ? f - 1.0f : f;
}

// Common303.h wavefold: `gain` is 1 + 5 x fold amount.
inline float wavefold(float x, float gain) noexcept {
  const float scaled = x * gain;
  const float folded = 4.0f * std::fabs(0.25f * scaled - floor_fast(0.25f * scaled + 0.75f) + 0.25f) - 1.0f;
  return soft_clip(folded);
}

}  // namespace

const dsp::UnitInfo& Distortion::info() const noexcept { return kInfo; }

// The model's trim at a Drive: linear interpolation in the gain between the knots of the table (a few instructions; derive() calls it
// once per control period while Drive moves, and when the model changes).
float Distortion::trim(unsigned model, float drive) noexcept {
  const unsigned m = model < kModelCount ? model : kModelCount - 1u;
  const float pos = clamp_param(drive, 0.0f, 1.0f) * static_cast<float>(kTrimKnots - 1u);
  unsigned i = static_cast<unsigned>(pos);
  if (i > kTrimKnots - 2u) {
    i = kTrimKnots - 2u;
  }
  const float f = pos - static_cast<float>(i);
  return kTrimTable[m][i] + f * (kTrimTable[m][i + 1u] - kTrimTable[m][i]);
}

// ---- parameters ---------------------------------------------------------------------------------------------------------------

void Distortion::derive() noexcept {
  const float d = drive_.lp.value();
  switch (model_) {
    case Grit:
      k_gain_ = 1.0f + d * d * 40.0f;
      k_a_ = 0.18f + (1.0f - d) * 0.40f;  // the slew limit per sample
      break;
    case Tube:
      k_gain_ = 1.0f + d * 24.0f;
      break;
    case Hard:
      k_gain_ = 1.0f + d * 35.0f;
      break;
    case Fuzz:
      k_gain_ = 1.0f + d * 45.0f;
      k_a_ = 0.18f * (d * d);  // the bias
      k_b_ = 0.12f * d;        // the offset taken off
      break;
    default:  // Fold
      k_gain_ = 1.0f + d * 5.0f;
      break;
  }
  k_out_ = kScale[model_] * trim(model_, d);
  // What the model gives at zero input, which is taken off its output so that the unit is at rest at silence and switching it on (or
  // switching to FUZZ) over silence is silent: only FUZZ has one (its bias less the offset), the other models give zero. The value
  // is recomputed here with the drive, so it follows a glide of Drive. The DC blocker is still there for the part of the offset that
  // depends on the signal.
  k_rest_ = model_ == Fuzz ? (diode_clip(k_a_ * 1.4f) - k_b_) * k_out_ : 0.0f;
}

// DistortionUnit::setTone: the post filter is a one pole from 800 Hz (0) to 16 kHz (1), exponential.
void Distortion::tone_coefficient(float tone) noexcept {
  const float cutoff = kToneBaseHz * o3sdk::core::exp2_fast(tone * kLog2Twenty);
  const float x = 2.0f * kPi * cutoff * inv_fs_;
  post_lp_alpha_ = x / (1.0f + x);  // dt / (rc + dt) with rc = 1 / (2 pi fc)
}

void Distortion::set(std::uint16_t id, float v) noexcept {
  switch (id) {
    case Active:
      active_ = v >= 0.5f;
      mix_target_ = active_ ? mix_param_ : 0.0f;
      break;
    case Type:
      model_ = static_cast<std::uint8_t>(clamp_param(v + 0.5f, 0.0f, static_cast<float>(kModelCount - 1)));
      derive();
      break;
    case Drive:
      drive_.target = clamp_param(v, 0.0f, 1.0f);
      drive_.moving = true;
      break;
    case Tone:
      tone_.target = clamp_param(v, 0.0f, 1.0f);
      tone_.moving = true;
      break;
    case Mix:
      mix_param_ = clamp_param(v, 0.0f, 1.0f);
      mix_target_ = active_ ? mix_param_ : 0.0f;
      break;
    default:
      break;
  }
}

// The smoothed parameters at their targets: at prepare, on the engine's delivery of the stored values (a process() of zero frames), and
// while the unit is bypassed (nothing is heard, so there is nothing to glide).
void Distortion::snap() noexcept {
  drive_.lp.jump(drive_.target);
  drive_.moving = false;
  tone_.lp.jump(tone_.target);
  tone_.moving = false;
  mix_ = mix_target_;
  derive();
  tone_coefficient(tone_.target);
}

void Distortion::prepare(float sample_rate) noexcept {
  fs_ = sample_rate < 1000.0f ? 1000.0f : sample_rate;
  inv_fs_ = 1.0f / fs_;
  const float rc = 1.0f / (2.0f * kPi * kPreHpHz);
  pre_hp_alpha_ = rc / (rc + inv_fs_);
  smooth_k_ = 1.0f - std::exp(-static_cast<float>(kControlPeriod) * inv_fs_ / kSmoothTau);
  mix_k_ = 1.0f - std::exp(-inv_fs_ / kSmoothTau);
  dc_r_ = std::exp(-2.0f * kPi * kDcHz * inv_fs_);
  drive_.lp.set_coeff(smooth_k_);
  tone_.lp.set_coeff(smooth_k_);
  ch_[0] = Channel{};
  ch_[1] = Channel{};
  ctl_ = 0;
  for (unsigned p = 0; p < kParamCount; ++p) {
    set(static_cast<std::uint16_t>(p), kParams[p].def);
  }
  snap();
}

void Distortion::apply(const dsp::Event& e) noexcept {
  if (e.kind == dsp::EventKind::Param) {
    set(e.param, e.value);
  }
}

// ---- audio --------------------------------------------------------------------------------------------------------------------

void Distortion::flush(Channel& c) noexcept {
  float* const states[] = {&c.pre_hp, &c.slew, &c.post_lp, &c.dc_x, &c.dc_y};
  for (float* s : states) {
    const float a = std::fabs(*s);
    if (a < kTiny || !(a < kHuge)) {
      *s = 0.0f;
    }
  }
}

void Distortion::tick() noexcept {
  bool changed = false;
  if (drive_.moving) {
    const float v = drive_.lp.next(drive_.target);
    const float d = v - drive_.target;
    if ((d < 0.0f ? -d : d) <= 1.0e-4f) {
      drive_.lp.jump(drive_.target);
      drive_.moving = false;
    }
    changed = true;
  }
  if (changed) {
    derive();
  }
  if (tone_.moving) {
    const float v = tone_.lp.next(tone_.target);
    const float d = v - tone_.target;
    if ((d < 0.0f ? -d : d) <= 1.0e-4f) {
      tone_.lp.jump(tone_.target);
      tone_.moving = false;
    }
    tone_coefficient(tone_.lp.value());
  }
  const float dm = mix_ - mix_target_;
  if ((dm < 0.0f ? -dm : dm) < 1.0e-5f) {
    mix_ = mix_target_;
  }
  flush(ch_[0]);
  flush(ch_[1]);
}

// One channel, n samples, one model. `mix` is the smoothed mix at the start of the chunk and is advanced.
template <int M>
void Distortion::chunk(Channel& c, const float* in, float* out, std::uint32_t n, float& mix_io) noexcept {
  float pre_hp = c.pre_hp;
  float slew = c.slew;
  float post = c.post_lp;
  float dcx = c.dc_x;
  float dcy = c.dc_y;
  float mix = mix_io;
  const float mix_t = mix_target_;
  const float mix_k = mix_k_;
  const float pa = post_lp_alpha_;
  const float hpa = pre_hp_alpha_;
  const float gain = k_gain_;
  const float ka = k_a_;
  const float kb = k_b_;
  const float rest = k_rest_;
  const float out_gain = k_out_;
  const float dcr = dc_r_;

  for (std::uint32_t i = 0; i < n; ++i) {
    const float x = in[i];
    float y = x;  // inactive (kBypass): the signal goes through the DC blocker only
    if constexpr (M != kBypass) {
      float wet;
      if constexpr (M == Grit) {
        // PocoRat: the 450 Hz high-pass, the gain stage, the LM308 slew limit, the symmetric tanh.
        const float hp = hpa * (pre_hp + x);
        pre_hp = hp - x;
        const float diff = hp * gain - slew;
        slew += clampf(diff, -ka, ka);
        wet = tanh_pade(slew * 1.6f) * out_gain;
      } else if constexpr (M == Tube) {
        // TubeDrive: cubic soft clip, a power of 0.94 on the positive half.
        float sat = soft_clip(x * gain);
        if (sat > 0.0f) {
          sat = pow094(sat);
        }
        wet = sat * out_gain;
      } else if constexpr (M == Hard) {
        // HardClip: clamp at 0.95 of 0.8 x the driven signal, then tanh.
        wet = tanh_pade(clampf(x * gain * 0.8f, -0.95f, 0.95f) * 1.2f) * out_gain;
      } else if constexpr (M == Fuzz) {
        // GermaniumFuzz: a bias, a diode clip above zero and tanh below.
        const float biased = x * gain + ka;
        const float clipped = biased > 0.0f ? diode_clip(biased * 1.4f) : tanh_pade_floor(biased * 2.1f);
        wet = (clipped - kb) * out_gain - rest;
      } else {
        // Wavefolder.
        wet = wavefold(x, gain) * out_gain;
      }
      // The tone low-pass, the dry/wet mix (the mix glides per sample).
      post += pa * (wet - post);
      mix += mix_k * (mix_t - mix);
      y = (1.0f - mix) * x + mix * post;
    }
    // The DC blocker, on the unit's output: it runs whether or not the unit is active, so that switching does not click.
    dcy = y - dcx + dcr * dcy;
    dcx = y;
    out[i] = dcy;
  }

  c.pre_hp = pre_hp;
  c.slew = slew;
  c.post_lp = post;
  c.dc_x = dcx;
  c.dc_y = dcy;
  mix_io = mix;
}

template <int M>
void Distortion::chunk_model(const dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t n, bool mono) noexcept {
  float mix_l = mix_;
  chunk<M>(ch_[0], args.in_l + from, args.out_l + from, n, mix_l);
  if (mono) {
    for (std::uint32_t i = 0; i < n; ++i) {
      args.out_r[from + i] = args.out_l[from + i];
    }
    ch_[1] = ch_[0];  // the mono fast path: the right channel's state is the left's, which is what it would have computed
  } else {
    float mix_r = mix_;
    chunk<M>(ch_[1], args.in_r + from, args.out_r + from, n, mix_r);
  }
  mix_ = mix_l;
}

void Distortion::run(const dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept {
  std::uint32_t n = to - from;
  // Inactive (or mix 0) with the crossfade finished: only the DC blocker runs, and nothing of the model is computed.
  const bool bypass = mix_target_ == 0.0f && mix_ == 0.0f;
  if (bypass) {
    if (drive_.moving || tone_.moving) {
      snap();
    }
    for (Channel& c : ch_) {
      c.pre_hp = 0.0f;
      c.slew = 0.0f;
      c.post_lp = 0.0f;
    }
  }

  // Both channels equal (the voice is mono): compute one and copy. Silence in with every state at rest gives silence out (every model's
  // rest is zero: FUZZ's bias is taken off, see derive()); only a unit at rest looks for silence.
  const bool at_rest = ch_[0].at_rest() && ch_[1].at_rest();
  // Compared as bit patterns (a load and an or a sample): a mono signal costs a few cycles a sample to find out; -0.0 against 0.0 counts as
  // different (the stereo path then, which is right for any input), and bit-equal NaNs as equal (the same NaN on both sides).
  std::uint32_t differ = 0;
  std::uint32_t nonzero = 0;
#pragma GCC unroll 4
  for (std::uint32_t i = from; i < to; ++i) {
    std::uint32_t l;
    std::uint32_t r;
    std::memcpy(&l, args.in_l + i, sizeof(l));
    std::memcpy(&r, args.in_r + i, sizeof(r));
    differ |= l ^ r;
    nonzero |= l << 1;  // the sign bit out: -0.0 is silent too
  }
  const bool mono = differ == 0u;
  const bool silent = at_rest && nonzero == 0u;
  if (mono && silent) {
    for (std::uint32_t i = from; i < to; ++i) {
      args.out_l[i] = 0.0f;
      args.out_r[i] = 0.0f;
    }
    return;
  }

  while (n != 0u) {
    if (ctl_ == 0u) {
      tick();
      ctl_ = kControlPeriod;
    }
    const std::uint32_t m = n < ctl_ ? n : ctl_;
    switch (bypass ? static_cast<std::uint8_t>(kBypass) : model_) {
      case Grit:
        chunk_model<Grit>(args, from, m, mono);
        break;
      case Tube:
        chunk_model<Tube>(args, from, m, mono);
        break;
      case Hard:
        chunk_model<Hard>(args, from, m, mono);
        break;
      case Fuzz:
        chunk_model<Fuzz>(args, from, m, mono);
        break;
      case kBypass:
        chunk_model<kBypass>(args, from, m, mono);
        break;
      default:
        chunk_model<Fold>(args, from, m, mono);
        break;
    }
    ctl_ -= m;
    from += m;
    n -= m;
  }
}

void Distortion::process(const dsp::ProcessArgs& args) noexcept {
  dsp::for_each_run(
      args, [&](std::uint32_t from, std::uint32_t to) { run(args, from, to); }, [&](const dsp::Event& e) { apply(e); });
  if (args.frames == 0u) {
    snap();  // the engine delivering the stored values at start()
  }
}

}  // namespace o3_303
