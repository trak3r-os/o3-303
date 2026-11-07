// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Test support: the friend the voice's header names, defined once for every test of the executable (two different bodies of one class in
// two translation units would be an ODR violation).
#include <o3_303/units/acid_voice/acid_voice.h>

#include <algorithm>
#include <cmath>

// The voice's state, read through the friend the header names. Limits (3.5): the ladder's four states inside 8.5 (the coupled
// form is unclamped, as Open303's; 8.00 measured at worst, which is 4 x kLadderBound) and the tanh of each inside +-1; the feedback high-pass inside 200 (165 measured), the signal before the output ceiling (Filter FM's input, the muffler) inside 36; the
// envelopes and the soft attack in 0..1; the output of the last sample, the muffler's low-pass and the blocker's input inside the output
// bound (2.0 before the volume: 1.4 at the gesture's -3 dB); each mod's own state inside its range.
namespace o3_303 {
struct AcidVoiceTestPeaks {
  float ladder = 0.0f, g = 0.0f, k = 0.0f, fb = 0.0f, pre = 0.0f, out = 0.0f;
};
struct AcidVoiceTestAccess {
  static constexpr float kLadder = 8.5f;       // each ladder state: the coupled TB_303 form has no clamp (Open303 neither); 8.00 is the worst of the 576 corners at the 10.4 kHz top (= 4 x kLadderBound, so the top does not move it; 7.84 at v0.5.1), +6 % margin
  static constexpr float kTanh = 1.0001f;      // the tanh of a state
  static constexpr float kOut = 2.0f;          // the output bound before the volume
  static constexpr float kPre = 36.0f;         // the signal before the output ceiling (the last output Filter FM reads, the muffler's low-pass): 28.11 at worst in the 576 corners at the 10.4 kHz top (OVERDRIVE +36 dB; 31.6 at 16 kHz, 31.9 at v0.5.1), +28 % margin; the ceiling bounds what leaves
  static constexpr float kPre2 = 2.0f * kPre;  // Filter FM's blocker output
  static constexpr float kSubMax = 0.8f;       // Sub Osc at its highest step
  static constexpr float kFmMax = 2.0f;        // Filter FM's depth k at 100 %
  static constexpr float kOdMin = 0.0f;        // -24 dB is exactly no oscillator: the gain is 0 there (od_gain)
  static constexpr float kOdMax = 64.5f;       // +36 dB, a little over
  static constexpr float kSgMax = 9.2f;        // the ladder's g at Reso 1 and the top cutoff: 4 k17 / 17 with k17 = 38.58 at the ladder's top (10.4 kHz; v0.5.1's top was 4.8 kHz); 9.0769 measured and by the formula, +1.4 %
  static constexpr float kKMax = 78.0f;        // k17 x the feedback, doubled by HI RES: 38.58 x 2 = 77.15 (measured 77.1533), +1.1 %
  static constexpr float kFb = 200.0f;        // the feedback high-pass sees k x y4 (the ladder's feedback times a state): 165.1 measured at the top, +21 % (k x the state bound would be 663, never reached)
  static constexpr float kSwpMax = 2.0f;       // the follower of SLOW is twice the accent pulse

  // The largest value each state reached over every call of violation() in this executable (the corners test prints them under O3_303_PROBE=1).
  static inline AcidVoiceTestPeaks peaks{};

  static bool modded(const AcidVoice& v) { return v.modded_; }
  static float pitch_hz(const AcidVoice& v) { return v.inc_ / v.tune_scale_; }    // the oscillator's pitch now (the glide's state)
  static float target_hz(const AcidVoice& v) { return v.target_hz_; }
  static float ladder_peak(const AcidVoice& v) { return std::max(std::max(std::fabs(v.s_[0]), std::fabs(v.s_[1])), std::max(std::fabs(v.s_[2]), std::fabs(v.s_[3]))); }
  static float env_scale(const AcidVoice& v) { return v.env_scale_; }  // the cutoff law: octaves per unit of envelope
  static float env_off(const AcidVoice& v) { return v.env_off_; }      // and the envelope's value at which the cutoff is the knob's
  static float lad_b(const AcidVoice& v) { return v.lad_b_; }                    // the ladder's step, feedback and gain as set at the control rate
  static float lad_k(const AcidVoice& v) { return v.lad_k_; }
  static float lad_g(const AcidVoice& v) { return v.lad_g_; }
  static float x_max(const AcidVoice& v) { return v.x_max_; }                    // the ladder's top and floor, log2 Hz
  static float x_min(const AcidVoice& v) { return v.x_min_; }
  static float res(const AcidVoice& v) { return v.res_; }
  static float fb_r(const AcidVoice& v) { return v.fb_r_; }
  static constexpr float kB0Max = AcidVoice::kB0Max;                              // Filter FM's ceiling for the step
  // ladder_coeffs() at 2^x Hz, with the Resonance and HI RES the voice holds.
  static void coeffs(const AcidVoice& v, float x, float& b, float& k, float& g) { v.ladder_coeffs(x, b, k, g); }
  static unsigned start_wait(const AcidVoice& v) { return v.start_wait_; }    // samples of the amplifier's start delay still to wait
  static unsigned start_wait_n(const AcidVoice& v) { return v.start_wait_n_; }  // kAmpDelay in samples
  static float declick(const AcidVoice& v) { return v.dk_lp_; }                  // the de-clicker's output: the amplifier gain
  static float clamp_lo(const AcidVoice& v) { return v.clamp_lo_; }              // the knob's clamp, log2 Hz: its ends slew after a switch of RANGE
  static float clamp_hi(const AcidVoice& v) { return v.clamp_hi_; }

  // Returns null when every state is inside its limit, else the name of the first that is not; `worst` takes the largest |s| over the ladder.
  static const char* violation(const AcidVoice& v, float& worst) {
    const auto bad = [](float x, float lo, float hi) { return !(x >= lo && x <= hi) || std::fpclassify(x) == FP_SUBNORMAL; };
    peaks.g = std::max(peaks.g, v.lad_g_);
    peaks.k = std::max(peaks.k, v.lad_k_);
    peaks.fb = std::max(peaks.fb, std::max(std::fabs(v.fb_x_), std::fabs(v.fb_y_)));
    peaks.pre = std::max(peaks.pre, std::max(std::max(std::fabs(v.fm_xm1_), std::fabs(v.fm_y_)), std::fabs(v.muf_lp_)));
    peaks.out = std::max(peaks.out, std::fabs(v.last_out_));
    for (unsigned i = 0; i < 4; ++i) {
      worst = std::max(worst, std::fabs(v.s_[i]));
      peaks.ladder = std::max(peaks.ladder, std::fabs(v.s_[i]));
      if (bad(v.s_[i], -kLadder, kLadder)) return "ladder state";
    }
    if (bad(v.fb_x_, -kFb, kFb) || bad(v.fb_y_, -kFb, kFb)) return "feedback high-pass";
    if (bad(v.lad_g_, 0.0f, kSgMax)) return "ladder gain";
    if (bad(v.lad_k_, 0.0f, kKMax)) return "ladder feedback";
    if (bad(v.vcf_, 0.0f, 1.0f)) return "filter envelope";
    if (bad(v.vca_, 0.0f, 1.0f)) return "amplifier envelope";
    if (bad(v.cap_, 0.0f, kSwpMax)) return "accent sweep capacitor";
    if (bad(v.att_, 0.0f, 1.0f)) return "soft attack";
    if (bad(v.start_g_, 0.0f, 1.0f)) return "start gain";
    if (v.start_wait_ > v.start_wait_n_) return "start wait";
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
    if (!std::isfinite(v.inc_) || v.inc_ <= 0.0f || v.inc_ > 0.45f + 1.0e-6f) return "pitch";
    return nullptr;
  }
};
}  // namespace o3_303
