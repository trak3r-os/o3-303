// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// ACID: the monophonic acid voice of o3-303 (docs/DESIGN-303.md, docs/M3-PLAN.md "Contracts"). An instrument unit: notes in, a mono signal out
// (written to both channels). At its stock values it is as close to a Roland TB-303 as the published measurements allow; the mods are the
// Devil Fish's, with the sub oscillator of the TD-3-MO.
//
// Origin (the licences, in full, are in src/acid_voice.cpp and in third_party/):
//   * Replicant 303 by DanimanX (MIT), commit d5369be028399f5a403067b58da2936caa070c0e: the PolyBLEP oscillator, the Pade tanh, the note stack
//     and the way a note starts and ends.
//   * Open303 by Robin Schmidt (MIT): the coupled four-pole ladder of TeeBeeFilter (TB_303 mode) with its fitted step, feedback and gain, the
//     150 Hz high-pass in the feedback, the measured law of the cutoff against Cutoff, Env Mod and the envelope, the high-passes, all-pass and
//     notch around the filter, the amplifier's structure (the filter envelope in it, through a 200 Hz de-clicker).
//   * One recording of one TB-303 (Din Sync, "TB-303 reference recordings for x0xb0x builders", http://dinsync.info, 2010), measured, nothing
//     copied: the amplifier's 4.4 ms start, the filter envelope's share in it (0.10), the accent's share (max(0, 2.6 Accent - 0.6)), the accent
//     decay of 87 ms, the amplifier decay of 1,430 ms, the square's level, the cutoff's range and the law's offset. docs/VOICE-303.md section 2.
//   * Robin Whittle's published text on the TB-303 and the Devil Fish: the accent sweep circuit, with C13 keeping its charge between accents; a
//     slide of 60 ms being the audible time, not the time constant; the 8 ms hold after the gate; the mods' ranges.
// The difference from Open303, to fit the Cortex-M4F: two sub-steps a sample where it has four (the peak moves under 3 % against sixteen), no
// per-sample pow or exp, coefficients every kControlPeriod samples and ramped, a soft bound on the ladder's input (Open303's ladder is linear: the
// bound is nearly silent at stock, and it is what holds HI RES and Overdrive), float, and the sweep circuit in place of a 15 ms lag.
// Like the instrument the oscillator and the filter run all the time (a free-running VCO into a free-running VCF, gated by the amplifier).
// A stock voice never self-oscillates, at any Cutoff or Reso; HI RES doubles the feedback, and then it does, at mid and high frequencies.
//
// Parameters (the id is the index in the table: append, never insert). Values are in the descriptor's own units:
//   0 Cutoff  Hz, 25..4500, a table curve (the TB-303's knob is about 314..2394) 4 Accent  0..1 (shown as %), default 0.7
//   1 Reso    0..1 (shown as %), default 0.65                                     5 Wave    0 SAW, 1 SQR
//   2 Env Mod 0..1 (shown as %), default 0.6                                      6 Tune    semitones, -12..+12
//   3 Decay   ms, 30..3000 (the filter envelope's time constant; a TB-303 200..2000) 7 Volume  dB, -60..+6; at or below -59.5 silence
// The mods (docs/MODS-PLAN.md section 3, appended after Volume). Each default is the TB-303: at their stock values the stock loop runs.
//   8 Acc Decay  ms, 30..3000 (Log), 87   the filter envelope's decay on an accent        14 Acc Sweep  0 OFF, 1 NORMAL, 2 HI RES
//   9 Tracking   %, 0..150, 0             the cutoff follows the note's pitch            15 Muffler    0 OFF, 1 SOFT, 2 HARD
//  10 Slide      ms, 60..360 (Log), 60    the glide's audible time                       16 Swp Speed  0 FAST, 1 NORMAL, 2 SLOW
//  11 Soft Atk   0 OFF, 1..5 = 0.3, 1, 3,                                               17 Overdrive  dB, -24..+36, 0; the bottom is none at all
//                10, 30 ms (non-accented notes fade in)                                 18 Amp Decay  ms, 16..4000 (Log), 1430 (the amplifier's decay)
//  12 Sub Osc    0 OFF, 1 LOW, 2 MID, 3 HIGH (a square an octave down)                  19 Stock      0 OFF, 1 ON: every mod at its stock value
//  13 Filter FM  0..1 (shown as %), 0    the output frequency-modulates the filter      20 Range      0 303, 1 FISH: Env Mod from zero to three times the
//  21 Sustain    0..1 (shown as %), 0    the level the amplifier decays to                            top, the bass of the Devil Fish, a note's end at the gate
// Stock is not a sound setting: it reads the stored mod values as they are, so they are heard again when it goes off. A value that is not
// a finite number reads as that parameter's stock value.
//
// Events. NoteOn: dsp::kNoteAccent is the accent; dsp::kNoteSlide, or a NoteOn while another note is held, glides from the sounding pitch
// without retriggering the envelopes. NoteOff ends the note, 8 ms later and in about 8 ms (an accent in 50 ms), only when it names the note that
// holds the gate: a slide's NoteOn before the old note's NoteOff leaves the gate open. AllNotesOff releases at once. The note stack holds
// kHeld notes. A NoteOn that starts the voice from quiet (not legato or a slide) holds the amplifier's main input down for 4.4 ms (kAmpDelay,
// measured on one recording of one unit; Whittle gives 4 ms and then a 3 ms attack): the filter envelope and the accent's share start at the gate.
// The rule is "from quiet": on a tail that still sounds (a NoteOn after a NoteOff, or in the same frame as one, which is how the sequencer
// writes a full gate) the input is held at the de-clicker's level and waits only the part of the delay still to climb, so nothing dips: the
// louder the tail, the shorter the delay, and none on a tail at full level. Soft Atk on a value replaces the delay with that attack; RANGE FISH
// has neither. AllNotesOff cancels a start under way.
//
// Cost: the CPU % of the render on a unit is the figure. The unit figures of this build (the amplifier's start, the cutoff's floor and top, the square)
// are in docs/hw/2026-11-07-de6704f.md, those of the v0.5.1 voice in docs/hw/2026-11-06-v051.md. An estimate from the cross build's disassembly: see
// docs/VOICE-303.md section 4.

#include <o3sdk/dsp/smooth.h>
#include <o3sdk/dsp/unit.h>

#include <cstdint>

namespace o3_303 {

class AcidVoice final : public o3sdk::dsp::Unit {
 public:
  // Ids 0..7 are the 303's own; the rest are the mods (docs/MODS-PLAN.md 3.1), appended in M3-PLAN's reserved order and then more.
  enum Param : std::uint16_t {
    Cutoff, Reso, EnvMod, Decay, Accent, Wave, Tune, Volume,
    AccDecay, Tracking, Slide, SoftAtk, SubOsc, FilterFm, AccSweep, Muffler, SwpSpeed, Overdrive, AmpDecay, Stock, Range, Sustain,
    kParamCount
  };
  static constexpr unsigned kFirstMod = AccDecay;                       // the mods that keep a raw value: ids 8.. (Stock is a switch and keeps none)
  static constexpr unsigned kModValues = kParamCount - AccDecay;

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
  void ladder_coeffs(float x, float& b, float& k, float& g) const noexcept;
  float accent_sweep(float m, float periods) noexcept;
  void update_law() noexcept;
  void update_reso() noexcept;
  void update_range() noexcept;
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
  void clamp_slew(std::uint16_t elapsed) noexcept;
  void start_pass(float* amp, std::uint32_t n) noexcept;

  float fs_ = 44100.0f;
  float inv_fs_ = 1.0f / 44100.0f;
  float smooth_k_ = 0.02f;      // the smoothing coefficient per control period
  // The ladder's step b at the top of the cutoff (10.4 kHz, 0.2507 at 44.1 kHz): the ceiling of Filter FM's modulated step. It sits on the top's own
  // step, so the cap never changes the stock loop's step (which has none) and MODS on and off agree up to the top.
  static constexpr float kB0Max = 0.2508f;
  float x_max_ = 13.3443f;        // log2 of the highest (10.4 kHz) and lowest cutoff, and of the floor of the sweep
  float x_min_ = 4.6f;
  float x_floor_ = 7.6f;
  float fx_scale_ = 0.0f;       // Hz to Open303's fx at the sub-step's rate

  // Parameters as the user set them (targets) and as the voice uses them (smoothed).
  // Taken at once, as the original does (a jump of these changes the response, not the signal path: the filter states stay continuous):
  float cutoff_l2_ = 8.0f;      // log2 of the base cutoff in Hz
  float clamp_lo_ = 7.6438562f; // the ends the knob is clamped between (log2 Hz), and where RANGE sends them: they slew there (clamp_slew()) and
  float clamp_hi_ = 11.2255f;   // are set at once by a note that does not slide. prepare() sets them to RANGE 303's
  float clamp_lo_t_ = 7.6438562f;
  float clamp_hi_t_ = 11.2255f;
  float env_mod_ = 0.0f;        // Env Mod, 0..1
  float env_scale_ = 0.0f;      // the law from the two knobs: octaves per unit of envelope, and the envelope's value at which the cutoff is the knob's
  float env_off_ = 0.0f;
  float accent_amt_ = 0.0f;     // Accent, 0..1, as the sweep reads it
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

  // The ladder (TeeBeeFilter, TB_303 mode): the four states, the feedback high-pass, and the step, feedback and gain of this period, ramped.
  float s_[4] = {};
  float fb_x_ = 0.0f, fb_y_ = 0.0f;
  float fb_a_ = 0.99f, fb_b_ = 0.99f;
  float lad_b_ = 0.0f, lad_k_ = 0.0f, lad_g_ = 2.0f;
  float lad_b_step_ = 0.0f, lad_k_step_ = 0.0f, lad_g_step_ = 0.0f;
  float res_ = 0.0f;            // Reso through Open303's skew
  float fb_r_ = 0.0f;           // and in the feedback: doubled by HI RES
  // Around it: the high-pass before, and the all-pass, high-pass and notch after.
  float pre_x_ = 0.0f, pre_y_ = 0.0f, pre_a_ = 0.99f, pre_b_ = 0.99f;
  float ap_x_ = 0.0f, ap_y_ = 0.0f, ap_c_ = 0.0f;
  float post_x_ = 0.0f, post_y_ = 0.0f, post_a_ = 0.99f, post_b_ = 0.99f;
  float notch_s1_ = 0.0f, notch_s2_ = 0.0f, notch_g_ = 0.0f, notch_two_r_ = 0.0f;

  // The filter envelope (the MEG) and the amplifier's; coefficients per sample. released_: the gate fell and its hold is over.
  float vcf_ = 0.0f;
  float vca_ = 0.0f;
  float decay_coeff_ = 0.9999f;
  float accent_coeff_ = 0.9999f;
  float decay_coeff_n_ = 0.9999f;   // the coefficients raised to kControlPeriod: the envelope at the end of a period
  float accent_coeff_n_ = 0.9999f;
  float hold_coeff_ = 0.99999f;
  float release_c_ = 0.999f;
  float release_acc_c_ = 0.999f;
  float release_fish_c_ = 0.999f;
  float amp_lift_ = 0.0f;           // the filter envelope's share of the amplifier for this period
  float dk_lp_ = 0.0f, dk_bp_ = 0.0f, declick_f_ = 0.03f;  // the amplifier's de-clicker
  std::uint16_t gate_hold_ = 353;      // kGateHold, in samples
  std::uint16_t gate_wait_ = 0;        // samples left of it
  std::uint16_t since_control_ = 0;    // samples since control() last ran: it runs early when a parameter moves
  // The start of a note that starts the voice (not legato or a slide, not RANGE FISH, and for a plain note Soft Atk OFF): the amplifier's main
  // input is held at start_g_ (0 on a silent voice) for start_wait_ samples (kAmpDelay), then steps to 1; the accent's term is not held. The
  // de-clicker is the rise. A restart on a voice that still sounds holds the input at the de-clicker's level and waits less, so nothing jumps:
  // the delay applies from quiet, on a tail only what avoids a dip. all_notes_off() clears it.
  // Counted in samples inside the loops, never by control(), which may run early. Written by note_on() and start_pass() only, on the one audio
  // context.
  float start_g_ = 1.0f;
  std::uint16_t start_wait_ = 0;
  std::uint16_t start_wait_n_ = 194;   // kAmpDelay, in samples at 44.1 kHz (prepare() sets it)
  bool gate_ = false;
  bool released_ = true;
  bool has_accent_ = false;

  // The accent sweep circuit: C13's charge, the conductances of its node at this Resonance, and the lift of the last period.
  float cap_ = 0.0f;
  float sw_ga_ = 1.0f, sw_g2_ = 1.0f, sw_gt_ = 0.25f, sw_off_ = 0.5f;
  float sw_rate_ = 0.0f, sw_rate_normal_ = 0.0f, sw_rate_slow_ = 0.0f;
  float sweep_oct_ = 0.0f;

  // Engine (SynthEngine): the held notes (the last is the one sounding) and the control period.
  std::uint8_t held_[kHeld] = {};
  std::uint8_t held_count_ = 0;
  float accent_boost_ = 0.0f;   // accent_to_amp(Accent), smoothed: the filter envelope's extra share of the amplifier on an accent
  float gain_ = 0.0f;
  std::uint32_t ctl_ = 0;       // samples left until the next control()
  bool fresh_ = true;           // a note started: the next control() sets the coefficient to its value at once instead of ramping

  // ---- The mods (docs/MODS-PLAN.md 3). All of it is written and read on the render's thread (set() arrives as an event).
  float raw_[kModValues] = {};          // each mod's value as set() received it, clamped (a non-finite value is its stock): Stock reads these back
  bool stock_ = false;                  // Stock: every mod uses its stock value
  bool modded_ = false;                 // a mod needs run<Square, true> for the coming period (decided in control())
  bool ramp_pending_ = false;           // set() moved a target: the next control() starts the ramp

  float stock_accent_c_ = 0.9999f;      // the coefficients prepare() computes from the constants: assigned back at the stock value, never recomputed
  float stock_accent_cn_ = 0.9999f;
  float stock_slide_c_ = 0.0004f;
  float stock_hold_c_ = 0.99999f;
  float reso_ = 0.0f;                   // Reso as set: the pot's position
  bool fish_ = false;                   // Range FISH
  float sustain_ = 0.0f;                // Sustain: the level the amplifier decays to
  float last_out_ = 0.0f;               // the last sample of the last run: seeds the states a mod left stale while it was off

  float track_ = 0.0f;                  // Tracking: octaves of cutoff per octave of pitch; track_on_ is false at 0 %, where no term is added
  bool track_on_ = false;
  std::uint8_t sweep_mode_ = 1;         // Acc Sweep: 0 OFF, 1 NORMAL, 2 HI RES
  std::uint8_t swp_mode_ = 1;           // Swp Speed: 0 FAST, 1 NORMAL, 2 SLOW
  bool hires_ = false;
  float swp_m_ = 0.0f;                  // Swp Speed FAST: the slow follower taken away from the pulse
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
  Ramp mix_;                            // Muffler: the wet/dry mix, a per-sample ramp; muf_type_ is the type the loop computes, muf_want_ the one asked for
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
  float b0_min_ = 0.0f;                 // the ladder's step at the lowest cutoff: the floor of Filter FM's modulated step
  float muf_hard_a_ = 0.0f;
};

}  // namespace o3_303
