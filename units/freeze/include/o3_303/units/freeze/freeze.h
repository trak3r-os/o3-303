// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// FREEZE: the beat freeze / stutter, an effect for master insert 0 (docs/M3B-PLAN.md, R6), with the roll, the tape stop and the reverse of
// M3c (docs/M3C-PLAN.md, R: "Modes" below). Stereo in, stereo out, in place or not.
//
// What it does. The unit records its input into a ring in the SDRAM all the time. While Active is ON it plays the last Length of the
// signal as a loop (a plain loop: no resampling, so the pitch is kept) and the input is heard only through Mix. Entry, the loop point and
// exit are crossfades of about 5 ms, so none of them clicks (an exit asked for sooner than 5 ms after the entry waits for it to be complete). While Active is OFF and the exit has finished, the output is the input,
// bit for bit (no arithmetic touches the samples), whatever Length and Mix are.
//
// Length. 1/4, 1/8 or 1/16 OF A BAR (four beats): one beat, an eighth note, a sixteenth. In frames: 4, 2 or 1 sixteenths at the
// transport's tempo (dsp::samples_per_sixteenth), rounded; at least kMinLoop frames, and at most the ring less kHeadroom (a 131,072 frame
// ring, 1 MiB for both sides, holds a whole beat down to 20.3 BPM, the slowest tempo the engine allows being 20).
//
// Tempo changes while frozen. The loop length is latched at entry, together with the loop itself: a tempo change or a new Length while
// frozen does not touch the loop (its pitch and period stay), and the next entry uses the new tempo and Length. A loop that was taken at
// 125 BPM goes on at 125 BPM's beat while the song moves to 140; it does not follow.
//
// How the loop is held (the design choice worth knowing). The ring is the loop's only memory: while frozen, the unit writes the loop's own
// output where the input would have been recorded, so the loop (a delay line of exactly Length with feedback 1) cannot be overwritten, can
// last as long as the player holds it, and needs no second buffer. The cost is that the master is not recorded during a freeze: the next
// freeze, if it comes before a whole Length of new input has been recorded, takes the loop (and the exit crossfade) for its older part.
// On exit the ring continues with the crossfade and then the input, with no gap.
//
// The loop point is a crossfade made once, at entry: the last kFade frames of the loop are replaced, in the ring, by a fade from the loop's
// own tail to the material that preceded the loop's start, so the tail runs on into the head as the recording itself did (the samples are
// consecutive in the original at the join). That needs Length + kFade of history. The replacement is paced: half a frame a frame (at most
// 32 frames in a block of 64) from the block after the entry, through an exit if one comes first, and it ends within 640 frames (14 ms) of the
// entry (the first loop plays the head and the middle before it reaches the tail), so no block carries more than its share. An entry
// asked for before the previous one's baking is over waits for it, a few frames after the shortest freeze (5 ms in, 5 ms out).
//
// Parameters (the id is the index in the table: append, never insert):
//   0 Active      OFF / ON, default OFF
//   1 Length      1/4, 1/8, 1/16 (of a bar), default 1/8: the loop of FREEZE
//   2 Mix         0..1 (shown as %), default 1: the effect against the input while it is on (smoothed, 5 ms)
//   3 Mode        FREEZE, ROLL, STOP, REVERSE, default FREEZE; latched at the entry (a change while on waits for the next one)
//   4 Roll Rate   1/8, 1/16, 1/32, 1/64 (note values), default 1/16: the loop of ROLL
//   5 Accelerate  OFF, BEAT, BAR, default OFF: ROLL halves its rate each beat / bar
//   6 Stop Time   1/8, 1/4, 1/2, 1, 2 (bars), default 1/2: the slow-down of STOP
//
// Modes. FREEZE (the default) is what is described above, and is bit for bit what the unit did before the others were added.
//   ROLL     A freeze whose loop is the Roll Rate (1/8 note = 2 sixteenths ... 1/64 = a quarter of one; at least kMinRoll frames, so the
//            fastest roll at 300 BPM is 551 frames), taken, crossfaded and held exactly as FREEZE does it. The rate is read at the entry and is
//            changed live, at the next loop boundary (the end of a repeat), by Roll Rate or by Accelerate:
//              shorter  the new loop is the HEAD of the old one (its first n frames), so it goes on from the loop point without a join; its own
//                       tail is blended, in the first short repeat, with the end of the old loop (which is what precedes the head in the
//                       loop), so the new loop point is a join of consecutive samples. No crossfade is needed and none is heard.
//              longer   the roll leaves (the usual exit crossfade) and enters again at once with the new rate. The ring then holds the
//                       roll's own repeats, so the longer loop is copies of the short one plus about 5 ms of input (review 2026-10-26).
//            Accelerate steps the requested rate one down the table (1/8, 1/16, 1/32, 1/64) every beat (4 sixteenths) or bar (16) after the
//            entry, at the tempo of the entry, until 1/64; Roll Rate set while rolling becomes the request and restarts the count. The
//            steps are counted in frames, so they do not depend on the block size.
//   STOP     A tape stop. The ring goes on recording the input (it is a true record in every mode but FREEZE and ROLL, so a freeze right after
//            a stop or a reverse loops real material). The output is the ring read behind the input by a lag that grows by (1 - speed) a
//            frame, with the speed falling linearly from 1 to 0 over Stop Time (a bar is 16 sixteenths; at most the ring's room, see
//            stop_frames()), the read linearly interpolated between two frames, and its level following the speed (4 x speed up to 1) so that
//            the stop ends in silence, not in a held sample. Held at speed 0 it is silent (Mix 1) and the lag keeps growing up to the ring's
//            room (kHeadroom less than the ring). Released, the speed rises linearly back to 1 over a quarter of Stop Time (at least
//            kMinSpin frames, scaled by how far it had fallen), then the read at that lag is crossfaded into the input over the usual fade (5 ms)
//            and the output is the input, bit for bit, again. A new press during the spin-up goes down again from the speed it had; one during
//            the final crossfade waits for it.
//   REVERSE  The ring played backwards from the entry: output k frames after the entry is the input k frames before it. The entry and the
//            exit are crossfades of the usual fade (the exit may come at any time, from the share it had reached). The ring keeps recording
//            the input. The reversal can be as long as reverse_span() ((ring - kHeadroom) / 2, 1.47 s in the real 131,072 frame ring): when
//            it is used up the reversal leaves, and if Active is still on enters again from the new "now".
//
// Memory. The ring is the app's: two arrays of `capacity` floats in the SDRAM (O3SDK_BIG, FRAMEWORK.md "Units"), handed to the
// constructor; a capacity that is not a power of two is used down to the one below. The unit itself is about 100 bytes of state.
// prepare() clears the ring (the simulator re-runs setup; on the unit about 17 ms of SDRAM stores at boot, before the engine starts).
//
// Cost [est], not measured on a unit: see src/freeze.cpp ("Cost") for the count.

#include <o3sdk/dsp/unit.h>

#include <cstdint>

namespace o3_303 {

class Freeze final : public o3sdk::dsp::Unit {
 public:
  enum Param : std::uint16_t { Active, Length, Mix, Mode, RollRate, Accelerate, StopTime, kParamCount };
  enum LengthValue : std::uint8_t { Quarter, Eighth, Sixteenth, kLengthCount };  // of a bar: 4, 2, 1 sixteenths
  enum ModeValue : std::uint8_t { kModeFreeze, kModeRoll, kModeStop, kModeReverse, kModeCount };
  enum RollRateValue : std::uint8_t { kRoll8, kRoll16, kRoll32, kRoll64, kRollRateCount };  // note values: 2, 1, 1/2, 1/4 sixteenths
  enum AccelerateValue : std::uint8_t { kAccelOff, kAccelBeat, kAccelBar, kAccelCount };
  enum StopTimeValue : std::uint8_t { kStop8th, kStop4th, kStopHalf, kStopOne, kStopTwo, kStopTimeCount };  // bars: 1/8 ... 2

  static constexpr std::uint32_t kMaxFade = 256;      // the longest crossfade, frames
  static constexpr std::uint32_t kMinLoop = 1024;     // the shortest loop, frames (23 ms); a 1/16 bar at 300 BPM is 2,205
  static constexpr std::uint32_t kHeadroom = 1024;    // ring frames a loop leaves unused: the fade's history and the baking window
  static constexpr std::uint32_t kMinCapacity = 4096; // a ring below this only passes the signal through
  static constexpr std::uint32_t kMinRoll = 544;      // the shortest ROLL loop, frames (551 is 1/64 note at 300 BPM): its fade is a quarter of it
  static constexpr std::uint32_t kMinSpin = 2048;     // the shortest STOP spin-up, frames

  // `left` and `right` hold `capacity` floats each (the SDRAM, zero at start), and stay valid for the unit's life.
  Freeze(float* left, float* right, std::uint32_t capacity) noexcept;

  // Pure helpers (the tests use them as the contract). `capacity` is the ring's size as the unit uses it (a power of two).
  // The loop in frames for a Length (LengthValue) at a tempo (milli-BPM), kMinLoop <= n <= capacity - kHeadroom.
  static std::uint32_t loop_frames(unsigned length, std::uint32_t tempo_mbpm, std::uint32_t capacity) noexcept;
  // The crossfade in frames for a loop of `loop` frames: 5 ms, at most kMaxFade and a quarter of the loop.
  static std::uint32_t fade_frames(float sample_rate, std::uint32_t loop) noexcept;
  // ROLL's loop for a Roll Rate (RollRateValue), rounded: kMinRoll <= n <= capacity - kHeadroom.
  static std::uint32_t roll_frames(unsigned rate, std::uint32_t tempo_mbpm, std::uint32_t capacity) noexcept;
  // The frames between two Accelerate steps: a beat (4 sixteenths) or a bar (16); 0 for kAccelOff.
  static std::uint32_t accel_frames(unsigned accel, std::uint32_t tempo_mbpm) noexcept;
  // STOP's slow-down in frames for a Stop Time (StopTimeValue): the bars at the tempo, at most twice the ring's room (the lag at the end of
  // a slow-down is half of it and must fit), at least kMinSpin.
  static std::uint32_t stop_frames(unsigned time, std::uint32_t tempo_mbpm, std::uint32_t capacity) noexcept;
  // The longest REVERSE before it must start over: (capacity - kHeadroom) / 2, 0 for a ring too small.
  static std::uint32_t reverse_span(std::uint32_t capacity) noexcept;

  const o3sdk::dsp::UnitInfo& info() const noexcept override;
  void prepare(float sample_rate) noexcept override;
  void process(const o3sdk::dsp::ProcessArgs& args) noexcept override;

  // For the tests and the diagnostics (render side; read them from the thread that calls process()).
  bool frozen() const noexcept { return phase_ != Phase::Idle; }
  std::uint32_t loop() const noexcept { return loop_; }  // the latched loop of the last entry, frames
  std::uint32_t capacity() const noexcept { return mask_ + 1u; }
  // Tail frames the last process() call replaced: at most (frames + 1) / 2, a block's share of the baking. 0 when none is pending.
  std::uint32_t baked_last() const noexcept { return baked_; }
  bool baking() const noexcept { return bake_next_ < fade_; }
  unsigned mode() const noexcept { return cur_mode_; }       // the mode of the freeze that is on (or the last), ModeValue
  bool rolling() const noexcept { return phase_ == Phase::Hold && cur_mode_ == kModeRoll; }
  unsigned roll_rate() const noexcept { return roll_cur_; }  // the rate of the loop now (RollRateValue)
  float stop_speed() const noexcept { return sp_; }          // STOP: the read speed now, 1 .. 0
  std::uint32_t stop_lag() const noexcept { return li_; }    // STOP: the whole frames the read is behind the input

 private:
  enum class Phase : std::uint8_t { Idle, Hold, Leave, Stop, Reverse };

  void set(std::uint16_t id, float v) noexcept;
  void enter(std::uint32_t tempo_mbpm) noexcept;
  void bake(std::uint32_t count) noexcept;
  void passthrough(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;
  void idle(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;
  template <bool kRamp>
  void hold(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;
  void leave(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;
  void run(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;
  // M3c modes (each returns the frame it got to; the caller loops).
  void enter_stop(std::uint32_t tempo_mbpm) noexcept;
  void enter_reverse() noexcept;
  std::uint32_t run_roll(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;
  void roll_boundary() noexcept;
  void roll_tick(std::uint32_t frames) noexcept;
  void relatch_tail(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;
  std::uint32_t run_stop(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;
  template <int K>
  std::uint32_t stop_loop(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;
  std::uint32_t run_reverse(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;
  template <int K>
  void reverse_loop(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;

  float* l_;
  float* r_;
  std::uint32_t mask_;     // capacity - 1 (a power of two); the ring is unusable when usable_ is false
  bool usable_;
  std::uint32_t w_ = 0;    // the next ring index to write

  // Parameters, as the events set them.
  bool want_ = false;      // Active
  std::uint8_t length_ = Eighth;
  float mix_target_ = 1.0f;
  std::uint8_t mode_ = kModeFreeze;
  std::uint8_t roll_rate_ = kRoll16;
  std::uint8_t accel_ = kAccelOff;
  std::uint8_t stop_time_ = kStopHalf;

  Phase phase_ = Phase::Idle;
  float fs_ = 44100.0f;
  float mix_k_ = 0.005f;   // the Mix smoother, per frame
  float mix_ = 1.0f;       // smoothed (jumps to the target at entry)

  // The latched loop of the current (or last) freeze.
  std::uint32_t loop_ = 0;        // frames
  std::uint32_t fade_ = 1;        // frames
  float inv_fade_ = 1.0f;
  std::uint32_t fp_ = 0;          // fade position 0..fade_: the loop's share of the output is fp_ / fade_ (g)
  std::uint32_t bake_origin_ = 0; // the ring index at entry; the loop's tail is [origin - fade, origin)
  std::uint32_t bake_next_ = 0;   // the next tail frame to replace; fade_ when done
  std::uint32_t baked_ = 0;       // tail frames replaced by the last process() call (the cost of the baking in that block)

  // The mode of the freeze that is on, latched at entry, and what the read of the loop is behind the write head (hold() and leave() read
  // `lag_` frames back; it is loop_ except in the first short repeat of a ROLL that went shorter).
  std::uint8_t cur_mode_ = kModeFreeze;
  std::uint32_t lag_ = 0;

  // ROLL.
  std::uint8_t roll_cur_ = kRoll16;    // the rate of loop_
  std::uint8_t roll_want_ = kRoll16;   // the rate asked for (Roll Rate or Accelerate), applied at the next loop boundary
  bool roll_dirty_ = false;            // Roll Rate was set: it is the new request
  bool roll_restart_ = false;          // the roll left to enter again with roll_want_
  bool relatch_ = false;               // the first repeat of a shorter loop is being copied (lag_ is the old loop)
  std::uint32_t roll_pos_ = 0;         // frames into the repeat
  std::uint32_t roll_tempo_ = 120'000; // the tempo of the entry (milli-BPM)
  std::uint32_t accel_period_ = 0;     // frames between Accelerate steps, 0: off
  std::uint32_t accel_left_ = 0;

  // STOP and REVERSE (their own crossfade, so that fade_ and the baking of FREEZE and ROLL are not touched).
  std::uint32_t xfade_ = 1;            // frames
  float inv_xfade_ = 1.0f;
  std::uint32_t xp_ = 0;               // share position 0..xfade_: the effect's share of the output is xp_ / xfade_
  float sp_ = 1.0f;                    // STOP: read speed
  float dec_ = 0.0f;                   // STOP: speed lost a frame while held
  float inc_ = 0.0f;                   // STOP: speed gained a frame while released
  float lf_ = 0.0f;                    // STOP: the lag's fraction, [0, 1)
  std::uint32_t li_ = 0;               // STOP: the lag's whole frames
  std::uint32_t lag_max_ = 0;
  bool fading_ = false;                // STOP: the final crossfade to the input has begun (not interruptible)
  std::uint32_t rev_org2_ = 0;         // REVERSE: twice the ring index of the entry (the read is org2 - write index, modulo the ring)
  std::uint32_t rev_k_ = 0;            // REVERSE: frames since the entry
  std::uint32_t rev_span_ = 0;
  bool rev_forced_ = false;            // REVERSE: the span is used up, it leaves whatever Active says
};

}  // namespace o3_303
