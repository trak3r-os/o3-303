// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// FREEZE: the beat freeze / stutter, an effect for master insert 0 (docs/M3B-PLAN.md, R6). Stereo in, stereo out, in place or not.
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
//   0 Active  OFF / ON, default OFF
//   1 Length  1/4, 1/8, 1/16 (of a bar), default 1/8
//   2 Mix     0..1 (shown as %), default 1: the loop against the input while frozen (smoothed, 5 ms)
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
  enum Param : std::uint16_t { Active, Length, Mix, kParamCount };
  enum LengthValue : std::uint8_t { Quarter, Eighth, Sixteenth, kLengthCount };  // of a bar: 4, 2, 1 sixteenths

  static constexpr std::uint32_t kMaxFade = 256;      // the longest crossfade, frames
  static constexpr std::uint32_t kMinLoop = 1024;     // the shortest loop, frames (23 ms); a 1/16 bar at 300 BPM is 2,205
  static constexpr std::uint32_t kHeadroom = 1024;    // ring frames a loop leaves unused: the fade's history and the baking window
  static constexpr std::uint32_t kMinCapacity = 4096; // a ring below this only passes the signal through

  // `left` and `right` hold `capacity` floats each (the SDRAM, zero at start), and stay valid for the unit's life.
  Freeze(float* left, float* right, std::uint32_t capacity) noexcept;

  // Pure helpers (the tests use them as the contract). `capacity` is the ring's size as the unit uses it (a power of two).
  // The loop in frames for a Length (LengthValue) at a tempo (milli-BPM), kMinLoop <= n <= capacity - kHeadroom.
  static std::uint32_t loop_frames(unsigned length, std::uint32_t tempo_mbpm, std::uint32_t capacity) noexcept;
  // The crossfade in frames for a loop of `loop` frames: 5 ms, at most kMaxFade and a quarter of the loop.
  static std::uint32_t fade_frames(float sample_rate, std::uint32_t loop) noexcept;

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

 private:
  enum class Phase : std::uint8_t { Idle, Hold, Leave };

  void set(std::uint16_t id, float v) noexcept;
  void enter(std::uint32_t tempo_mbpm) noexcept;
  void bake(std::uint32_t count) noexcept;
  void passthrough(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;
  void idle(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;
  template <bool kRamp>
  void hold(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;
  void leave(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;
  void run(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;

  float* l_;
  float* r_;
  std::uint32_t mask_;     // capacity - 1 (a power of two); the ring is unusable when usable_ is false
  bool usable_;
  std::uint32_t w_ = 0;    // the next ring index to write

  // Parameters, as the events set them.
  bool want_ = false;      // Active
  std::uint8_t length_ = Eighth;
  float mix_target_ = 1.0f;

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
};

}  // namespace o3_303
