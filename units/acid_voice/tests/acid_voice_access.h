// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Test support: the friend the voice's header names, defined once for every test of the executable (two different bodies of one class in
// two translation units would be an ODR violation).
#include <o3_303/units/acid_voice/acid_voice.h>

#include <algorithm>
#include <cmath>

// The voice's state, read through the friend the header names. Limits (3.5): the ladder's four states inside 16 (the coupled
// form is unclamped, as Open303's; 7.84 measured at worst) and the tanh of each inside +-1; the feedback high-pass inside the largest k times the ladder clamp, the signal before the output ceiling (Filter FM's input, the muffler) inside 64; the
// envelopes and the soft attack in 0..1; the output of the last sample, the muffler's low-pass and the blocker's input inside the output
// bound (2.0 before the volume: 1.4 at the gesture's -3 dB); each mod's own state inside its range.
namespace o3_303 {
struct AcidVoiceTestAccess {
  static constexpr float kLadder = 16.0f;      // each ladder state: the coupled TB_303 form has no clamp (Open303 neither); 7.84 is the worst of the 576 corners, a bit over twice that here
  static constexpr float kTanh = 1.0001f;      // the tanh of a state
  static constexpr float kOut = 2.0f;          // the output bound before the volume
  static constexpr float kPre = 64.0f;         // the signal before the output ceiling (the last output Filter FM reads, the muffler's low-pass): 31.9 at worst in the 576 corners (OVERDRIVE +36 dB), twice that here; the ceiling bounds what leaves
  static constexpr float kPre2 = 2.0f * kPre;  // Filter FM's blocker output
  static constexpr float kSubMax = 0.8f;       // Sub Osc at its highest step
  static constexpr float kFmMax = 2.0f;        // Filter FM's depth k at 100 %
  static constexpr float kOdMin = 0.0f;        // -24 dB is exactly no oscillator: the gain is 0 there (od_gain)
  static constexpr float kOdMax = 64.5f;       // +36 dB, a little over
  static constexpr float kSgMax = 12.6f;       // the ladder's g at Reso 1 and the top cutoff: 2 (k17 / 17) 2 with k17 = 53.2 at fx = 0.131 (Filter FM clamps there)
  static constexpr float kKMax = 107.0f;       // k17 x the feedback, doubled by HI RES: 53.2 x 2
  static constexpr float kFb = kKMax * kLadder;  // the feedback high-pass sees k x y4 (the ladder's feedback times a state)
  static constexpr float kSwpMax = 2.0f;       // the follower of SLOW is twice the accent pulse

  static bool modded(const AcidVoice& v) { return v.modded_; }
  static float ladder_peak(const AcidVoice& v) { return std::max(std::max(std::fabs(v.s_[0]), std::fabs(v.s_[1])), std::max(std::fabs(v.s_[2]), std::fabs(v.s_[3]))); }
  static float env_scale(const AcidVoice& v) { return v.env_scale_; }  // the cutoff law: octaves per unit of envelope
  static float env_off(const AcidVoice& v) { return v.env_off_; }      // and the envelope's value at which the cutoff is the knob's

  // Returns null when every state is inside its limit, else the name of the first that is not; `worst` takes the largest |s| over the ladder.
  static const char* violation(const AcidVoice& v, float& worst) {
    const auto bad = [](float x, float lo, float hi) { return !(x >= lo && x <= hi) || std::fpclassify(x) == FP_SUBNORMAL; };
    for (unsigned i = 0; i < 4; ++i) {
      worst = std::max(worst, std::fabs(v.s_[i]));
      if (bad(v.s_[i], -kLadder, kLadder)) return "ladder state";
    }
    if (bad(v.fb_x_, -kFb, kFb) || bad(v.fb_y_, -kFb, kFb)) return "feedback high-pass";
    if (bad(v.lad_g_, 0.0f, kSgMax)) return "ladder gain";
    if (bad(v.lad_k_, 0.0f, kKMax)) return "ladder feedback";
    if (bad(v.vcf_, 0.0f, 1.0f)) return "filter envelope";
    if (bad(v.vca_, 0.0f, 1.0f)) return "amplifier envelope";
    if (bad(v.cap_, 0.0f, kSwpMax)) return "accent sweep capacitor";
    if (bad(v.att_, 0.0f, 1.0f)) return "soft attack";
    if (bad(v.fm_xm1_, -kPre, kPre)) return "FM blocker input";
    if (bad(v.fm_ym1_, -kPre2, kPre2)) return "FM blocker output";
    if (bad(v.fm_y_, -kPre, kPre)) return "FM last output";
    if (bad(v.muf_lp_, -kPre, kPre)) return "muffler low-pass";
    if (bad(v.last_out_, -kOut * 2.0f, kOut * 2.0f)) return "last output";  // after the volume: up to +6 dB
    if (bad(v.sub_.v, 0.0f, kSubMax)) return "sub level";
    if (bad(v.subq_, -1.0f, 1.0f)) return "sub sign";
    if (bad(v.fm_.v, 0.0f, kFmMax)) return "FM depth";
    if (bad(v.mix_.v, 0.0f, 1.0f)) return "muffler mix";
    if (bad(v.od_g_, kOdMin, kOdMax)) return "overdrive gain";
    if (bad(v.swp_m_, 0.0f, kSwpMax)) return "sweep follower (slow)";
    if (bad(v.phase_, 0.0f, 1.0f)) return "oscillator phase";
    if (!std::isfinite(v.pitch_hz_) || v.pitch_hz_ <= 0.0f) return "pitch";
    return nullptr;
  }
};
}  // namespace o3_303
