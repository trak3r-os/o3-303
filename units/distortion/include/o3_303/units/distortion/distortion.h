// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// DISTORTION: a five-model distortion for an insert slot (docs/M3-PLAN.md "Contracts"). An effect unit: stereo in, stereo out, in
// place or not; a mono signal (both channels equal, as the 303's voice writes it) is computed once and copied.
//
// Origin. A port of DSP/DistortionUnit.{h,cpp} and the helpers of DSP/Common303.h of Replicant 303 by DanimanX, commit
// d5369be028399f5a403067b58da2936caa070c0e (third_party/replicant303/). Replicant 303 is MIT licensed:
//
//   MIT License. Copyright (c) 2026 Daniel Rojas Grasset (DanimanX)
//   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files
//   (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify,
//   merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions: the above copyright notice and this permission notice shall be
//   included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND
//   (the full text is in third_party/replicant303/LICENSE and in src/distortion.cpp).
//
// The models are Replicant's (names are ours): GRIT (PocoRat: 450 Hz high-pass, gain 1 + 40 drive^2, slew limiter, tanh), TUBE
// (TubeDrive: gain 1 + 24 drive, cubic soft clip, a power of 0.94 on the positive half), HARD (HardClip: gain 1 + 35 drive, clamp,
// tanh), FUZZ (GermaniumFuzz: gain 1 + 45 drive, a bias of 0.18 drive^2, diode clip above zero and tanh below), FOLD (Wavefolder:
// triangle fold with gain 1 + 5 drive, soft clip), then a one-pole low-pass (800 Hz .. 16 kHz, exponential in Tone) and the dry/wet
// mix, as the original. Different from the original, to fit the Cortex-M4F or to keep the chain clean:
//   * the power in TUBE is a degree-3 polynomial in place of pow (error under 1.2e-3 of full scale; tests/ measures it);
//   * floor in FOLD is an integer conversion;
//   * a one-pole DC blocker (about 5 Hz) sits at the output, for every model and also while the unit is inactive: FUZZ's bias leaves a
//     DC offset (+0.1 at full drive), and the 303's oscillator has some of its own (its saw is bent), which would go on into the delay
//     and the outputs; being always on, switching the unit does not click. An inactive unit is the signal through that high-pass;
//   * Drive and Tone (5 ms) and Mix and Active (a crossfade of 5 ms between the dry signal and the distorted one) are smoothed, so that
//     a jump does not click; an inactive unit with its crossfade finished computes only the blocker;
//   * FUZZ's output at zero input (its bias less its offset, up to 0.1) is subtracted, so that the unit is at rest at silence and
//     switching it on, or switching to FUZZ, over silence is silent;
//   * FUZZ's negative half is bounded: the original's fastTanh (a Pade form) saturates only up to |x| = 3 and climbs again at a slope of 1/9
//     beyond it, and GermaniumFuzz runs it up to |x| = 100, so on a hot signal the negative half of the original is a ramp (a square at
//     full Drive reaches -3.8 where the others peak at 1.6) and 48 of the 65 FUZZ presets of the vault peaked above full scale at the
//     master. The port holds the argument at -3 (identical to the original below it; tests/ checks both). GRIT and HARD, which use the
//     same fastTanh, are left as they are: HARD's argument stays inside the range; GRIT's overshoots (its wet peak is 1.2 at the default
//     Drive and 1.6 at 0.8, where its nominal bound is 0.85), which is part of the default sound, and the trim below holds the growth
//     above the default;
//   * the wet signal of each model has a gain that follows Drive (trim(); the original has none, and its five models differ by up to 12
//     dB at the default Drive and 16 dB at full Drive; Replicant's presets carry no level, only the master volume, which GEN omits as
//     well);
//   * a block that is silent in, with every state at rest, costs nothing.
//
// Parameters (the id is the index in the table: append, never insert):
//   0 Active OFF / ON (default ON)        3 Tone 0..1 (shown as %; 800 Hz .. 16 kHz), default 0.6
//   1 Type   GRIT TUBE HARD FUZZ FOLD     4 Mix  0..1 (shown as %), default 0.85
//   2 Drive  0..1 (shown as %), default 0.5
//
// Cost [est], a mono signal: 75-90 cycles a sample (a model 60-70, the scan for equal channels and for silence 7, the control period and
// the calls 8), 4.8k-5.8k a block, 1.8-2.2 % of 261,224; two different channels double the model part. Inactive: 27 a sample (the
// blocker), 0.7 %. The trim is folded into the model's output scalar (no extra multiply); trim() is about 30 cycles once per control
// period while Drive moves (about 120 a block), and FUZZ's bound about 3 cycles a sample (about 190 a block): together under 0.15 % of a
// block. Not measured on a unit.

#include <o3sdk/dsp/smooth.h>
#include <o3sdk/dsp/unit.h>

#include <cstdint>

namespace o3_303 {

class Distortion final : public o3sdk::dsp::Unit {
 public:
  enum Param : std::uint16_t { Active, Type, Drive, Tone, Mix, kParamCount };
  enum Model : std::uint8_t { Grit, Tube, Hard, Fuzz, Fold, kModelCount };

  // The output trim of each model: a gain on its wet signal (before the tone low-pass and the Mix) that follows Drive. It is make-up and
  // trim, not a limiter, so Drive keeps its character. trim() interpolates, in the gain, a table of kTrimKnots values (Drive 0, 0.05 ..
  // 1; src/distortion.cpp, with how it was measured). It is fitted on one line, the reference 303 line of tests/acid_voice_chain_test.cpp
  // played SAW and SQR: the level of a model is the mean in dB of the two, and the trim makes it GRIT's at the same Drive, to a tenth of
  // a dB from Drive 0.3 up (Replicant's untrimmed models spread over 12 dB at the default Drive and 16 dB at full Drive). Each wave alone
  // is further off, since relative to GRIT a square runs 2 to 3 dB hotter than a saw through TUBE, HARD, FUZZ and FOLD: up to 2.6 dB at
  // Drive 0.25, 1.6 dB from the default up. Below Drive 0.25 the correction fades out to M3's trims at Drive 0, where the models stay
  // above GRIT (up to 7.5 dB, FOLD; GRIT's 450 Hz high-pass makes it the quiet one). Another voice reads differently: on a resonant,
  // accented line (Reso, Env Mod and Accent at 100 %) the models sit +0.5 .. +4.2 dB above GRIT (the chain test bounds it), while their
  // wet peaks, which are what the headroom is about, stay where they are on the reference line. GRIT's own trim holds its wet peak at
  // its default-Drive value (GRIT's peak grows by 2.5 dB above it); on a voice twice as loud that peak is still 1.7 (the chain test
  // records it).
  static constexpr unsigned kTrimKnots = 21;
  static float trim(unsigned model, float drive) noexcept;

  static constexpr int kBypass = kModelCount;     // the sample loop of an inactive unit: the DC blocker only
  static constexpr unsigned kControlPeriod = 16;  // samples between two steps of the Drive and Tone smoothers

  Distortion() noexcept = default;

  const o3sdk::dsp::UnitInfo& info() const noexcept override;
  void prepare(float sample_rate) noexcept override;
  void process(const o3sdk::dsp::ProcessArgs& args) noexcept override;

 private:
  struct Smoothed {
    o3sdk::dsp::OnePole lp;
    float target = 0.0f;
    bool moving = false;
  };
  // The state of one channel: the model's filters, the tone low-pass and the DC blocker.
  struct Channel {
    float pre_hp = 0.0f;   // the 450 Hz high-pass of GRIT
    float slew = 0.0f;     // GRIT's slew limiter
    float post_lp = 0.0f;  // the tone low-pass
    float dc_x = 0.0f;     // the DC blocker: previous input and output
    float dc_y = 0.0f;
    bool at_rest() const noexcept { return pre_hp == 0.0f && slew == 0.0f && post_lp == 0.0f && dc_x == 0.0f && dc_y == 0.0f; }
  };

  void apply(const o3sdk::dsp::Event& e) noexcept;
  void set(std::uint16_t id, float v) noexcept;
  void snap() noexcept;
  void tick() noexcept;         // one control period: the smoothers, the coefficients of the model, the flush of denormals
  void derive() noexcept;       // the model's coefficients from the drive in use
  void tone_coefficient(float tone) noexcept;
  void run(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;
  template <int M>
  void chunk(Channel& c, const float* in, float* out, std::uint32_t n, float& mix) noexcept;
  template <int M>
  void chunk_model(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t n, bool mono) noexcept;
  static void flush(Channel& c) noexcept;

  float fs_ = 44100.0f;
  float inv_fs_ = 1.0f / 44100.0f;
  float smooth_k_ = 0.07f;      // per control period
  float mix_k_ = 0.005f;        // per sample
  float pre_hp_alpha_ = 0.94f;  // 450 Hz
  float post_lp_alpha_ = 0.5f;
  float dc_r_ = 0.9993f;

  bool active_ = true;
  std::uint8_t model_ = Grit;
  float mix_param_ = 0.85f;
  float mix_target_ = 0.85f;    // mix_param_ while active, else 0
  float mix_ = 0.85f;           // smoothed, per sample
  Smoothed drive_;
  Smoothed tone_;
  // The model's coefficients for the drive in use: gain, and a second and third value (slew limit; bias and offset of FUZZ).
  float k_gain_ = 1.0f;
  float k_a_ = 0.0f;
  float k_b_ = 0.0f;
  float k_out_ = 0.85f;         // the model's output scalar (the original's) times its trim at the drive in use
  float k_rest_ = 0.0f;         // the model's output at zero input (FUZZ only), taken off it
  Channel ch_[2];
  std::uint32_t ctl_ = 0;       // samples left until the next tick()
};

}  // namespace o3_303
