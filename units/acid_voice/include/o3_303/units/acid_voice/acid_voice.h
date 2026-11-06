// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// ACID: the monophonic acid voice of o3-303 (docs/DESIGN-303.md, docs/M3-PLAN.md "Contracts"). An instrument unit: notes in, a
// mono signal out (written to both channels).
//
// Origin. A port of the DSP of Replicant 303 by DanimanX, commit d5369be028399f5a403067b58da2936caa070c0e (third_party/replicant303/):
// DSP/Oscillator303.cpp (PolyBLEP saw with its bend, square with its duty and the droop high-pass), DSP/DiodeLadderFilter.cpp (four
// tanh stages, feedback high-pass, two sub-steps), DSP/Envelope303.h (filter, amplifier and accent envelopes), DSP/Common303.h (the
// Pade tanh) and the way DSP/SynthEngine.cpp combines them. Replicant 303 is MIT licensed:
//
//   MIT License. Copyright (c) 2026 Daniel Rojas Grasset (DanimanX)
//   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files
//   (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify,
//   merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions: the above copyright notice and this permission notice shall be
//   included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND
//   (the full text is in third_party/replicant303/LICENSE and in src/acid_voice.cpp).
//
// What is the same as the original: the oscillator shapes, the cutoff law base x 2^(envmod x env x 5.2 + accent x 2.2), the filter
// maths (feedback gain 17.2 x reso, input drive 1 + 0.4 x reso, the two sub-steps), the three envelopes and
// their time constants (accent 180 ms, amplifier hold 4 s, release 25 ms), the 60 ms slide, the accent lift of the amplifier.
// What is different, to fit the Cortex-M4F (docs and the oracle test in tests/ give the measured agreement): no per-sample pow, tan or
// exp (the filter coefficient is computed every kControlPeriod samples and ramped linearly between, tan by a rational fit, 2^x by
// core::exp2_fast); the tanh of each ladder state is computed once and reused; the original's four +-4 clamps of the ladder states are one
// guard on the last stage (none ever acts); Volume and the accent lift of the amplifier are smoothed (5 ms), where a jump would click; the
// envelopes are flushed to zero below 1e-9 (denormal safety); the accent is the note flag or a velocity of 96 and up (kAccentVelocity), the slide a note flag. Like the
// original the oscillator and the filter run all the time (a free-running VCO into a free-running VCF, gated by the amplifier): a note
// after a rest starts on a filter in steady state, not from zero.
// Not in the original at all: the mods, parameter ids 8 to 19 (docs/MODS-PLAN.md section 3). They are new code, written from public descriptions
// of the Devil Fish-style mods, not from the original; at their stock values the voice is the original's, as described above.
//
// Parameters (the id is the index in the table: append, never insert). Values are in the descriptor's own units:
//   0 Cutoff  Hz, 25..4500, Replicant's skew 0.35 (a table curve)      4 Accent  0..1 (shown as %), default 0.7
//   1 Reso    0..1 (shown as %), default 0.65                           5 Wave    0 SAW, 1 SQR
//   2 Env Mod 0..1 (shown as %), default 0.6                            6 Tune    semitones, -12..+12
//   3 Decay   ms, 200..2500 (the filter envelope's time constant)       7 Volume  dB, -60..+6; at or below -59.5 silence
// The mods (docs/MODS-PLAN.md section 3, appended after Volume). Each default is today's voice, bit for bit: at their stock values the
// stock loop runs, which is the loop of the voice before the mods existed.
//   8 Acc Decay  ms, 30..3000 (Log), 180  the accent's filter envelope and sweep pulse   14 Acc Sweep  0 OFF, 1 NORMAL, 2 HI RES
//   9 Tracking   %, 0..150, 0             the cutoff follows the note's pitch            15 Muffler    0 OFF, 1 SOFT, 2 HARD
//  10 Slide      ms, 60..360 (Log), 60    the pitch glide's time constant                16 Swp Speed  0 FAST, 1 NORMAL, 2 SLOW
//  11 Soft Atk   0 OFF, 1..5 = 0.3, 1, 3,                                               17 Overdrive  dB, -24..+36, 0 (the level into the ladder)
//                10, 30 ms (non-accented notes fade in)                                 18 Amp Decay  ms, 16..4000 (Log), 4000 (the amplifier's hold)
//  12 Sub Osc    0 OFF, 1 LOW, 2 MID, 3 HIGH (a square an octave down)                  19 Stock      0 OFF, 1 ON: every mod above at its stock value
//  13 Filter FM  0..1 (shown as %), 0    the output frequency-modulates the filter
// Stock is not a sound setting: it reads the stored mod values as they are, so they are heard again when it goes off. A value that is not
// a finite number reads as that parameter's stock value.
//
// Events. NoteOn: dsp::kNoteAccent is the accent; dsp::kNoteSlide, or a NoteOn while another note is held, glides (60 ms one pole) from
// the sounding pitch without retriggering the envelopes. NoteOff releases (25 ms) only when it names the note that holds the gate: a
// slide's NoteOn before the old note's NoteOff leaves the gate open. AllNotesOff releases at once. The note stack holds kHeld notes.
//
// Cost [est]: stock, about 445 cycles a sample for SAW and 465 for SQR (13 divisions of 14 cycles, 25 multiply-accumulates; 187 resp. 199
// instructions in the sample loop from the disassembly at -O2 with a cost table, 424 resp. 443 cycles, plus the control period and the
// call overhead, about 22): 28.5k-29.8k cycles a block, 10.9-11.4 % of 261,224. Not measured on a unit; the CPU % of the render there
// is the figure. The mods cost nothing at stock: the block runs the stock loop (run<Square, false>) until a mod needs the other one
// (run<Square, true>, three short passes over at most kControlPeriod samples: oscillator with the sub and the overdrive, envelopes with
// the soft attack, and the ladder with the filter FM and the muffler).
// Cost of the modded loop [est, from the disassembly at -O2 with the same cost table, no `bl` in it and no more stack traffic than the stock
// loop's]: about 510 cycles a sample for SAW and 525 for SQR, so +85 and +85..89 over the stock loop, and about +100 with the control period and
// the call overhead: 6,400 cycles a block, 2.5 % of 261,224, on top of the 11 % above. It is a fixed price: the loop computes every stage
// whenever it runs, so one mod alone costs as much as all of them (the plan's 3.4 counted each stage only when it is on). By stage, SAW: the
// loop skeleton and the guards of the saturators about 19, the sub oscillator 11 (SQR 19), the overdrive 2, the soft attack 5, the filter FM
// 18..20, the muffler 40. Not measured on a unit: the render's CPU % there, mod by mod, is the figure (docs/MODS-PLAN.md section 11).

#include <o3sdk/dsp/smooth.h>
#include <o3sdk/dsp/unit.h>

#include <cstdint>

namespace o3_303 {

class AcidVoice final : public o3sdk::dsp::Unit {
 public:
  // Ids 0..7 are the 303's own; 8..19 are the mods (docs/MODS-PLAN.md 3.1), appended in M3-PLAN's reserved order and then four more.
  enum Param : std::uint16_t {
    Cutoff, Reso, EnvMod, Decay, Accent, Wave, Tune, Volume,
    AccDecay, Tracking, Slide, SoftAtk, SubOsc, FilterFm, AccSweep, Muffler, SwpSpeed, Overdrive, AmpDecay, Stock,
    kParamCount
  };
  static constexpr unsigned kFirstMod = AccDecay;                       // the mods that keep a raw value: ids 8..18 (Stock is a switch)
  static constexpr unsigned kModValues = Stock - AccDecay;

  static constexpr unsigned kControlPeriod = 16;  // samples between two recomputations of the filter coefficient (a power of two)
  static constexpr unsigned kHeld = 8;           // notes the stack remembers (the oldest is dropped past that)

  AcidVoice() noexcept = default;

  const o3sdk::dsp::UnitInfo& info() const noexcept override;
  void prepare(float sample_rate) noexcept override;
  void process(const o3sdk::dsp::ProcessArgs& args) noexcept override;

  // True while a note is held or its amplifier has not released (Replicant's Envelope303::isActive). For a test, on the thread that calls
  // process(): it reads the render's state without synchronisation (a screen reads the engine's statistics instead).
  bool sounding() const noexcept;

  friend struct AcidVoiceTestAccess;  // tests read the state (the 576-corner sweep asserts it stays inside its limits); no code in the unit

 private:
  // A parameter glided by a one pole at the control rate (dsp/smooth.h); `moving` is false once it has settled on the target.
  struct Smoothed {
    o3sdk::dsp::OnePole lp;
    float target = 0.0f;
    bool moving = false;
  };

  void apply(const o3sdk::dsp::Event& e) noexcept;
  void set(std::uint16_t id, float v) noexcept;
  void note_on(std::uint8_t note, bool accent, bool slide) noexcept;
  void note_off(std::uint8_t note) noexcept;
  void all_notes_off() noexcept;
  void render(float* out, std::uint32_t from, std::uint32_t to) noexcept;
  void control() noexcept;
  float ladder_g(float x) const noexcept;
  void snap() noexcept;
  void retune_filter() noexcept;
  template <bool Square, bool Modded>
  void run(float* out, std::uint32_t n) noexcept;
  static void jump(Smoothed& s, float v) noexcept;
  float step(Smoothed& s, float eps) noexcept;

  // The mods. A value ramped by `step` a sample inside a run (a per-sample linear ramp set at the control rate) or, for the two that only
  // multiply once a sample, by a stair at the control rate; `target` is where it is going and it lands on it exactly.
  struct Ramp {
    float v = 0.0f;
    float target = 0.0f;
    float step = 0.0f;
  };
  void set_mod(std::uint16_t id, float v) noexcept;
  void apply_mod(unsigned id) noexcept;
  void refresh_mods() noexcept;
  float eff(unsigned id) const noexcept;
  void advance_mods() noexcept;
  void load_muffler() noexcept;
  float tail_estimate() const noexcept;
  bool mods_needed() const noexcept;
  float cutoff_base() const noexcept;
  float sweep_term(float acc_now, float swp_now) const noexcept;

  float fs_ = 44100.0f;
  float inv_fs_ = 1.0f / 44100.0f;
  float smooth_k_ = 0.02f;      // the smoothing coefficient per control period
  float x_max_ = 13.0f;         // log2 of the highest cutoff the filter coefficient can express (g = 0.95)
  float pi_over_fs_ = 0.0f;

  // Parameters as the user set them (targets) and as the voice uses them (smoothed).
  // Taken at once, as the original does (a jump of these changes the response, not the signal path: the filter states stay continuous):
  float cutoff_l2_ = 8.0f;      // log2 of the base cutoff in Hz
  float env_mod_oct_ = 0.0f;    // Env Mod x 5.2: octaves of sweep at full envelope
  float accent_oct_ = 0.0f;     // Accent x 2.2: octaves of lift at full accent envelope
  // Smoothed (5 ms), because they multiply the output: a jump would click.
  Smoothed accent_;             // the accent amount in the amplifier's lift (linear 0..1)
  Smoothed volume_;             // linear gain
  bool square_ = false;

  // Oscillator (Oscillator303): phase, the droop high-pass, the pitch glide.
  float phase_ = 0.0f;
  float sqr_hp_ = 0.0f;
  float sqr_hp_coeff_ = 0.992f;
  float pitch_hz_ = 110.0f;     // gliding towards target_hz_ (60 ms one pole)
  float target_hz_ = 110.0f;
  float slide_coeff_ = 0.0004f;
  float tune_scale_ = 0.0f;     // 2^(tune / 12) / fs
  float inc_lo_ = 0.0f;         // phase increment limits: 10 Hz .. 0.45 fs
  float inc_hi_ = 0.45f;

  // Diode ladder (DiodeLadderFilter): states, the cached tanh of each, the feedback high-pass, the coefficients of this period.
  float s_[4] = {};
  float t_[4] = {};
  float hp_state_ = 0.0f;
  float hp_alpha_ = 0.995f;
  float sub_g_ = 0.05f;         // g / 2 for each of the two sub-steps; ramps by sub_g_step_ a sample towards the period's end value
  float sub_g_step_ = 0.0f;
  float fb_k_ = 0.0f;           // 17.2 x reso
  float in_drive_ = 1.0f;       // 1 + 0.4 x reso

  // Envelopes (Envelope303): filter, amplifier and accent; coefficients per sample.
  float vcf_ = 0.0f;
  float vca_ = 0.0f;
  float acc_ = 0.0f;
  float decay_coeff_ = 0.9999f;
  float accent_coeff_ = 0.9999f;
  float decay_coeff_n_ = 0.9999f;   // the coefficients raised to kControlPeriod: the envelope at the end of a period
  float accent_coeff_n_ = 0.9999f;
  float hold_coeff_ = 0.99999f;
  float release_coeff_ = 0.999f;
  bool gate_ = false;
  bool has_accent_ = false;

  // Engine (SynthEngine): the held notes (the last is the one sounding) and the control period.
  std::uint8_t held_[kHeld] = {};
  std::uint8_t held_count_ = 0;
  float accent_boost_ = 0.0f;   // accent amount x 0.45: the amplifier's lift by the accent envelope
  float gain_ = 0.0f;
  std::uint32_t ctl_ = 0;       // samples left until the next control()
  bool fresh_ = true;           // a note started: the next control() sets the coefficient to its value at once instead of ramping

  // ---- The mods (docs/MODS-PLAN.md 3). Appended after the stock state on purpose: the stock loop reads the members above at offsets that
  // are today's, and the objdump of run<Square, false> is compared against the voice before the mods. All of it is written and read on the
  // render's thread (set() arrives as an event).
  float raw_[kModValues] = {};          // each mod's value as set() received it, clamped (a non-finite value is its stock): Stock reads these back
  bool stock_ = false;                  // Stock: every mod uses its stock value
  bool modded_ = false;                 // a mod needs run<Square, true> for the coming period (decided in control())
  bool ramp_pending_ = false;           // set() moved a target: the next control() starts the ramp

  float stock_accent_c_ = 0.9999f;      // the coefficients prepare() computes from the constants: assigned back at the stock value, never recomputed
  float stock_accent_cn_ = 0.9999f;
  float stock_slide_c_ = 0.0004f;
  float stock_hold_c_ = 0.99999f;
  float reso_ = 0.0f;                   // Reso as set: the feedback gain is derived from it (doubled by Acc Sweep HI RES)
  float last_out_ = 0.0f;               // the last sample of the last run: seeds the states a mod left stale while it was off

  float track_ = 0.0f;                  // Tracking: octaves of cutoff per octave of pitch; track_on_ is false at 0 %, where no term is added
  bool track_on_ = false;
  std::uint8_t sweep_mode_ = 1;         // Acc Sweep: 0 OFF, 1 NORMAL, 2 HI RES
  std::uint8_t swp_mode_ = 1;           // Swp Speed: 0 FAST, 1 NORMAL, 2 SLOW
  bool hires_ = false;
  float swp_ = 0.0f;                    // the sweep follower of SLOW and FAST (control rate), and FAST's slow follower m_
  float swp_m_ = 0.0f;
  float swp_slow_c_ = 0.0f;
  float swp_fast_c_ = 0.0f;

  float att_ = 1.0f;                    // Soft Atk: the attack's charge, 1 = done; att_c_ is its coefficient a sample, 0 when Soft Atk is OFF
  float att_c_ = 0.0f;
  std::uint8_t att_idx_ = 0;

  Ramp sub_;                            // Sub Osc: the level (a ramp a sample); the square's sign flips on the main oscillator's wrap
  float subq_ = 1.0f;
  float od_db_ = 0.0f;                  // Overdrive: its dB at the end of the last period, the target, the gain now and its step a sample
  float od_db_t_ = 0.0f;
  float od_g_ = 1.0f;
  float od_step_ = 0.0f;
  Ramp fm_;                             // Filter FM: the depth k (twice the amount), a stair at the control rate
  float fm_xm1_ = 0.0f;                 // the DC blocker of the output that modulates the filter, and the output of the last sample
  float fm_ym1_ = 0.0f;
  float fm_y_ = 0.0f;
  Ramp mix_;                            // Muffler: the wet/dry mix, a stair; muf_type_ is the type the loop computes, muf_want_ the one asked for
  float muf_lp_ = 0.0f;
  float muf_a_ = 0.0f;
  float muf_t_ = 0.7f;
  float muf_inv_t_ = 1.0f / 0.7f;
  std::uint8_t muf_type_ = 0;
  std::uint8_t muf_want_ = 0;

  float ramp_sub_rate_ = 0.0f;          // per control period (about 5 ms for the whole range)
  float ramp_mix_rate_ = 0.0f;
  float ramp_fm_rate_ = 0.0f;
  float ramp_od_rate_ = 0.0f;
  float fm_dc_r_ = 0.999f;
  float g_half_min_ = 0.0f;             // half of the ladder's g at the lowest cutoff: the floor of Filter FM's modulated g
  float muf_hard_a_ = 0.0f;
};

}  // namespace o3_303
