// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_303/units/tract/tract.h>

#include <cmath>
#include <cstring>

// Cost [est]: the processor's, from extras/tract (an instruction trace of its cross build: about 256 cycles a frame at Full including the
// control step, 220 at Lite), plus this wrapper: the mono sum (2 loads, 1 add, 1 multiply, 1 store: ~7), the mix per frame (a ramp step while
// it moves, a multiply-add pair a channel: ~10). About 270 a frame at Full is 6.6 % of a 64-frame block (40.8 cycles a frame are 1 %).
// Idle (Mix 0, settled): a memcpy of the block when the buffers differ, nothing in place. The measurement is `diag cpu` with the tract at
// Mix 100 % and a pattern playing; it is owed.
namespace o3_303 {

namespace {

namespace dsp = o3sdk::dsp;
using o3sdk::core::ParamCurve;
using o3sdk::core::ParamUnit;

constexpr std::uint8_t kSmooth = o3sdk::core::kParamSmooth;

constexpr const char* kSizeNames[] = {"BIG", "LARGE", "NORMAL", "SMALL", "TINY"};

// name, unit text, names, knots, min, max, default, detents, coarse, list count, display, curve, flags, page
constexpr o3sdk::core::ParamDesc kParams[TractFx::kParamCount] = {
    {"VOWEL", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, kSmooth, 0},
    {"TONGUE", "%", nullptr, nullptr, -100.0f, 100.0f, 0.0f, 200, 10, 0, ParamUnit::Int, ParamCurve::Linear, kSmooth, 0},
    {"LIPS", "%", nullptr, nullptr, -100.0f, 100.0f, 0.0f, 200, 10, 0, ParamUnit::Int, ParamCurve::Linear, kSmooth, 0},
    {"RES", "%", nullptr, nullptr, 0.0f, 1.0f, 0.5f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, kSmooth, 0},
    {"GLIDE", "ms", nullptr, nullptr, 5.0f, 500.0f, 30.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Log, 0, 0},
    {"SIZE", "", kSizeNames, nullptr, 0.0f, 4.0f, 2.0f, 4, 1, 5, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"MIX", "%", nullptr, nullptr, 0.0f, 1.0f, 0.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, kSmooth, 0},
    {"GAIN", "dB", nullptr, nullptr, -12.0f, 18.0f, kTractGainDefaultDb, 60, 6, 0, ParamUnit::Fixed1, ParamCurve::Linear, kSmooth, 0},
};

constexpr dsp::UnitInfo kInfo{"TRACT", dsp::UnitKind::Effect, kParams, TractFx::kParamCount, nullptr, 0};

constexpr float kMixRampSeconds = 0.005f;
constexpr float kMixOff = 1.0e-3f;  // a Mix below this is 0: the unit is bypassed (a tenth of the 1 % the page shows)
constexpr float kLn10Over20 = 0.115129255f;  // dB to a ratio: exp(dB * ln(10) / 20)

}  // namespace

TractFx::TractFx() noexcept { prepare(44100.0f); }

const dsp::UnitInfo& TractFx::info() const noexcept { return kInfo; }

void TractFx::prepare(float sample_rate) noexcept {
  const float fs = sample_rate > 1000.0f ? sample_rate : 44100.0f;
  mix_step_ = 1.0f / (kMixRampSeconds * fs);
  proc_.prepare(fs);
  proc_.set_mix(1.0f);  // the processor always runs fully wet: the mix is ours (a stereo dry path)
  for (unsigned p = 0; p < kParamCount; ++p) {
    set(static_cast<std::uint16_t>(p), kParams[p].def);
  }
  mix_ = 0.0f;
  mix_target_ = 0.0f;
  gain_ = gain_target_;
  gain_step_ = 0.0f;
  proc_.reset();  // lands every smoother on the defaults just set
}

void TractFx::set(std::uint16_t id, float v) noexcept {
  if (id >= kParamCount) {
    return;
  }
  const float x = o3sdk::core::param_clamp(kParams[id], v);
  switch (id) {
    case Vowel:
      proc_.set_vowel(x);
      break;
    case Tongue:
      tongue_ = x * 0.01f;
      proc_.set_bend(tongue_, 0.0f, lips_);
      break;
    case Lips:
      lips_ = x * 0.01f;
      proc_.set_bend(tongue_, 0.0f, lips_);
      break;
    case Res:
      proc_.set_res(x);
      break;
    case Glide:
      proc_.set_glide_ms(x);
      break;
    case Size:
      proc_.set_size(static_cast<int>(x + 0.5f) - 2);  // BIG is the longest tube (-2), TINY the shortest (+2)
      break;
    case Mix: {
      const bool was_idle = idle();
      mix_target_ = x < kMixOff ? 0.0f : x;
      if (was_idle && mix_target_ > 0.0f) {
        proc_.reset();  // the tube ran last at some earlier signal: start from silence at the shape asked for
        gain_ = gain_target_;  // the gain does not ramp while idle: it is where it was asked to be
        gain_step_ = 0.0f;
      }
      break;
    }
    case Gain:
      // Exactly 1.0f at 0 dB (the multiply is then skipped: the run is bit for bit the one without the stage), else a ratio.
      gain_target_ = x == 0.0f ? 1.0f : std::exp(x * kLn10Over20);
      if (idle()) {
        gain_ = gain_target_;  // nothing is heard: no ramp
        gain_step_ = 0.0f;
      } else {
        gain_step_ = (gain_target_ - gain_) * mix_step_;  // 5 ms, as the Mix ramp, from where the gain is now
      }
      break;
    default:
      break;
  }
}

// One run of frames between two events: the mono sum through the processor, then the crossfade with the stereo dry signal.
void TractFx::run(const dsp::ProcessArgs& a, std::uint32_t from, std::uint32_t to) noexcept {
  const std::uint32_t n = to - from;
  if (idle()) {
    if (a.out_l != a.in_l) {
      std::memcpy(a.out_l + from, a.in_l + from, static_cast<std::size_t>(n) * sizeof(float));
    }
    if (a.out_r != a.in_r) {
      std::memcpy(a.out_r + from, a.in_r + from, static_cast<std::size_t>(n) * sizeof(float));
    }
    return;
  }
  const float* const il = a.in_l + from;
  const float* const ir = a.in_r + from;
  float* const ol = a.out_l + from;
  float* const orr = a.out_r + from;
  for (std::uint32_t i = 0; i < n; ++i) {
    mono_[i] = 0.5f * (il[i] + ir[i]);
  }
  proc_.process(mono_, mono_, n);
  // GAIN scales the wet signal, after the processor (its own level trim would cancel a gain before it) and before the crossfade.
  if (gain_ != 1.0f || gain_target_ != 1.0f) {
    float g = gain_;
    const float gt = gain_target_;
    const float gs = gain_step_;
    for (std::uint32_t i = 0; i < n; ++i) {
      if (g != gt) {
        g += gs;
        if (gs > 0.0f ? g >= gt : g <= gt) {
          g = gt;
        }
      }
      mono_[i] *= g;
    }
    gain_ = g;
  }
  float m = mix_;
  const float t = mix_target_;
  const float step = mix_step_;
  for (std::uint32_t i = 0; i < n; ++i) {
    if (m != t) {
      m = m < t ? (t - m < step ? t : m + step) : (m - t < step ? t : m - step);
    }
    const float w = mono_[i];
    if (m >= 1.0f) {
      ol[i] = w;
      orr[i] = w;
    } else {
      ol[i] = il[i] + (w - il[i]) * m;
      orr[i] = ir[i] + (w - ir[i]) * m;
    }
  }
  mix_ = m;
}

void TractFx::process(const dsp::ProcessArgs& args) noexcept {
  dsp::for_each_run(
      args, [&](std::uint32_t from, std::uint32_t to) { run(args, from, to); },
      [&](const dsp::Event& e) {
        if (e.kind == dsp::EventKind::Param) {
          set(e.param, e.value);
        }
      });
}

}  // namespace o3_303
