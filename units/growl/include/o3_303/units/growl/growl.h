// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// GROWL: a vowel formant filter and a tuned comb, an effect for slot 0's insert 1 after the distortion (docs/M3C-PLAN.md, G). Stereo in,
// stereo out, in place or not. Written from scratch; the formant frequencies are the male averages of Peterson and Barney (1952, J. Acoust.
// Soc. Am. 24), a published measurement, and the relative levels are our own choice.
//
// What it does. The wet signal is the mono sum of the input (a mono input, as the 303's voice writes it, comes out exactly as it went in
// through the wet path; a stereo one keeps its image in the dry part only):
//   1. FORMANTS. Three band-pass filters (trapezoidal state-variable, peak gain 1 at the centre) in parallel, with centres and levels
//      interpolated linearly across the vowel table A E I O U as Vowel goes 0..1 (A at 0, E at 0.25, I at 0.5, O at 0.75, U at 1):
//             F1 / F2 / F3 [Hz]    level of F1 / F2 / F3
//        A    730 / 1090 / 2440    1.00 / 0.63 / 0.32
//        E    530 / 1840 / 2480    1.00 / 0.50 / 0.32
//        I    270 / 2290 / 3010    1.00 / 0.32 / 0.25
//        O    570 /  840 / 2410    1.00 / 0.50 / 0.16
//        U    300 /  870 / 2240    1.00 / 0.32 / 0.10
//      The sum is made up by kMakeup (2) and crossfaded with the unfiltered signal by Formant Mix. Formant Q is the Q of all three.
//   2. COMB. A feedback comb (y[n] = x[n] + g y[n - D], D fractional by linear interpolation) whose delay follows the pitch of
//      Note + Comb Tune (semitones): D = sample rate / frequency, so it resonates at the note and its harmonics (a negative Feedback
//      resonates at the odd multiples of half the note instead). A delay longer than the buffer (a note below about 22 Hz) is halved until it
//      fits, an octave up, and one shorter than kMinDelay frames is held there. The comb is peak-normalised (times 1 - |g|), so the level at the
//      resonances is the input's whatever Feedback is. Comb Mix crossfades it with its input.
//   3. MIX. The whole wet signal against the dry input.
//
// Mix 0 is the default: a unit that is wired and untouched passes the input bit for bit (no arithmetic touches the samples, in place or
// not) and costs a memcpy at most. Its state is cleared when it goes idle, so Mix going up starts from silence.
//
// Smoothing (the engine does not ramp parameters: FRAMEWORK.md "Units"). The control period is kControl (16) frames, counted across blocks,
// so the result does not depend on the block size. Each period: Vowel (3 ms) and the pitch of Note + Tune (4 ms) are one-pole smoothed and
// the filter coefficients follow; Formant Mix, Comb Mix, Feedback and the delay are smoothed (5 ms) and ramped linearly across the period,
// and the Mix is a linear ramp of 5 ms that lands exactly on its target. The wobble drives Vowel every 16 frames: the one-pole takes the
// staircase out. A Note jump is a glide of the comb's pitch of a few ms, not a step of its delay line.
// The smoothed pitch lands on its target once it is within 1e-3 semitone (a tenth of a cent); the other values run their one-pole to where float
// stops it. When none moves any more the control period has nothing to do, and a settled unit renders a whole run in one piece (the cost
// of each case is in the .cpp).
//
// Parameters (the id is the index in the table: append, never insert):
//   0 Vowel          0..1 (shown as %), default 0 (A)
//   1 Formant Q      1..20 (log), default 5
//   2 Formant Mix    0..1 (shown as %), default 1
//   3 Comb Tune      -24..24 semitones, default 0
//   4 Comb Feedback  -0.95..0.95, default 0.6
//   5 Comb Mix       0..1 (shown as %), default 0.5
//   6 Note           0..127, a MIDI note, default 36: the sequencer sets it on each note start (seq::kGrowlNoteParam)
//   7 Mix            0..1 (shown as %), default 0
//
// Memory. The comb's line is 2048 floats inside the unit (8 KiB), no SDRAM. The longest delay it needs (22 Hz) fits at 44.1 kHz.
//
// Cost [est], not measured on a unit: see src/growl.cpp ("Cost").

#include <o3sdk/dsp/unit.h>

#include <cstdint>

namespace o3_303 {

class Growl final : public o3sdk::dsp::Unit {
 public:
  enum Param : std::uint16_t { Vowel, FormantQ, FormantMix, CombTune, CombFeedback, CombMix, Note, Mix, kParamCount };

  static constexpr unsigned kVowels = 5;               // A E I O U
  static constexpr unsigned kFormants = 3;
  static constexpr std::uint32_t kControl = 16;        // frames between two control updates
  static constexpr std::uint32_t kCombFrames = 2048;   // the comb's line, a power of two
  static constexpr float kMinDelay = 4.0f;             // frames: the highest comb pitch (11 kHz at 44.1 kHz)
  static constexpr float kMaxDelay = 2000.0f;          // frames: the longest delay the line holds with room for the interpolation
  static constexpr float kMakeup = 2.0f;               // gain of the formant sum

  Growl() noexcept;

  // Pure helpers (the tests use them as the contract). The centres [Hz] and levels of F1..F3 at a Vowel (clamped to 0..1).
  static void formants(float vowel, float hz[kFormants], float level[kFormants]) noexcept;
  // The comb's delay in frames for a pitch in semitones (MIDI note + tune; 69 is 440 Hz) at a sample rate.
  static float comb_delay(float semitones, float sample_rate) noexcept;

  const o3sdk::dsp::UnitInfo& info() const noexcept override;
  void prepare(float sample_rate) noexcept override;
  void process(const o3sdk::dsp::ProcessArgs& args) noexcept override;

  // For the tests and the diagnostics (render side).
  bool idle() const noexcept { return mix_ == 0.0f && mix_target_ == 0.0f; }

 private:
  void set(std::uint16_t id, float v) noexcept;
  void snap() noexcept;
  void control() noexcept;
  void land() noexcept;
  void clear_state() noexcept;
  void coefficients() noexcept;
  void derive_comb() noexcept;
  void run(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;
  // What ramps in a control period: nothing (Steady), only the comb's delay (Glide: a Note change), or Formant Mix, Comb Mix, Feedback or the
  // Mix too (Ramps). The first two keep the comb's gains and read position in registers: the same samples as the Ramps loop with its
  // steps zero, to rounding. kFull: and the Mix is 1.
  enum Mode : std::uint8_t { Steady, Glide, Ramps };
  template <Mode kMode, bool kFull>
  void render(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;

  float fs_ = 44100.0f;
  float k_vowel_ = 0.0f;   // one-pole coefficients per control period
  float k_pitch_ = 0.0f;
  float k_mixes_ = 0.0f;
  float mix_step_ = 0.0f;  // linear Mix ramp, per frame

  // Parameters as the events set them.
  float vowel_target_ = 0.0f;
  float q_ = 5.0f;
  float kq_ = 0.2f;        // 1 / q_
  float fmix_target_ = 1.0f;
  float tune_ = 0.0f;
  float fb_target_ = 0.6f;
  float cmix_target_ = 0.5f;
  float note_ = 36.0f;
  float mix_target_ = 0.0f;

  // Smoothed at the control rate.
  float vowel_s_ = 0.0f;
  float pitch_s_ = 36.0f;
  float fmix_s_ = 1.0f;
  float cmix_s_ = 0.5f;
  float fb_s_ = 0.6f;

  // Per frame: the value now and its step across the period.
  float fmix_ = 1.0f, fmix_inc_ = 0.0f;
  float cmix_ = 0.5f;
  float fb_ = 0.6f, fb_inc_ = 0.0f;
  // The comb's output gains: wet = (1 - Comb Mix) y + Comb Mix (1 - |fb|) yc, ramped as the two products they are.
  float c1_ = 0.5f, c1_inc_ = 0.0f;
  float cn_ = 0.2f, cn_inc_ = 0.0f;
  float delay_ = 100.0f, delay_inc_ = 0.0f, delay_end_ = 100.0f;
  float mix_ = 0.0f, mix_inc_ = 0.0f, mix_end_ = 0.0f;
  std::uint32_t left_ = 0;  // frames left in the control period
  Mode mode_ = Ramps;       // this period
  bool cleared_ = true;     // the state is zero (idle)

  // What the coefficients and the comb's delay were last computed from: the work is redone only when its input moved.
  float vowel_c_ = 0.0f;
  float pitch_c_ = 36.0f;
  bool coef_dirty_ = true;
  bool moving_ = true;      // a target was set, or a smoothed value has not stopped: the period does the full work
  std::uint32_t sweep_ = 0;  // control periods since the filter states were last checked

  // The formant bank.
  float a1_[kFormants] = {};
  float a2_[kFormants] = {};
  float g2_[kFormants] = {};      // 2 g, g the prewarped integrator gain
  float weight_[kFormants] = {};  // level x kMakeup / Q: the band-pass output times this has the table level at its peak
  float ic1_[kFormants] = {};
  float ic2_[kFormants] = {};

  // The comb.
  alignas(16) float line_[kCombFrames] = {};
  std::uint32_t w_ = 0;
};

static_assert(alignof(Growl) >= 16, "the comb's line is declared aligned (alignas(16) in the class)");
static_assert(Growl::kCombFrames >= 1024 && (Growl::kCombFrames & (Growl::kCombFrames - 1u)) == 0u, "the comb's line is a power of two");
static_assert(Growl::kMaxDelay + 2.0f < static_cast<float>(Growl::kCombFrames), "the longest delay and its interpolation tap fit in the line");

}  // namespace o3_303
