// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The sequencer: a clocked hook of the engine (o3sdk dsp/engine.h) that plays the Bank's patterns on the voice unit.
//
// Origin. The timing and the note rules follow Replicant 303 by DanimanX (third_party/replicant303/DSP/PatternSequencer.cpp,
// triggerStep and process, commit d5369be028399f5a403067b58da2936caa070c0e): swing lengthening the even steps and shortening the odd
// ones by half the swing, the slide as a note-on before the held note's note-off at the same sample, a rest releasing the held note,
// the velocities 110 (accent) and 80. Replicant 303 is distributed under the MIT License:
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
// Timing (Replicant 303's, and the ties and gate of DESIGN-303 section 6). A
// step is a sixteenth (240 ticks). Swing lengthens the even steps and shortens the odd ones by half the swing of a step: the odd
// step starts `swing` x 120 ticks late and the pair still adds up to two sixteenths. The note of a step lasts `gate` percent of the
// step's own length, or the whole step when it slides. Events are placed at their frame inside the block, so any tempo of the
// transport (20..300 BPM) is sample-accurate.
//
//   note step   note-on (accent: dsp::kNoteAccent), note-off at the end of the gate. The note is the step's note + the pattern's
//               transpose + the live transpose, clamped to 0..127.
//   slide       the step's note is held to the end of the step and the NEXT note-on carries dsp::kNoteSlide; it is emitted before
//               the held note's note-off, at the same frame (Replicant's legato overlap).
//   tie         no new note: the held note goes on through this step; it ends with the gate of the last tied step. With nothing
//               held a tie is silent. A slide flag on a tie step holds the note into the next step.
//   rest        the held note is released at the step's start.
//   motion      the lane points of the step (four, at the quarters of the step) go to the voice as Param events; a point of
//               kNoLock puts the knob's value back (see update()).
//
// Contexts. Everything not marked "render" is the main loop. The main loop never touches the render's state: it queues commands on a
// lock-free ring (the same way the engine takes its commands; a full ring refuses and returns false), sets a few atomics, and
// reads the playhead from one atomic word. The patterns are shared through the Bank's seqlock (bank.h).

#include <o3sdk/core/param.h>
#include <o3sdk/core/ring.h>
#include <o3sdk/dsp/engine.h>
#include <o3_303/pattern.h>
#include <o3_303/seq/bank.h>

#include <atomic>
#include <cstdint>

namespace o3_303::seq {

static_assert(std::atomic<float>::is_always_lock_free && std::atomic<std::uint32_t>::is_always_lock_free,
              "the hook and the main loop share these atomics: they must be plain loads and stores (no lock, no libatomic)");

inline constexpr unsigned kMaxQueue = 16;  // entries of the chain

// Performance flags (momentary or latched by the screen): the hook honours them at the next step.
inline constexpr std::uint8_t kPerfMute = 1u << 0;      // no new notes (the playhead goes on; a held note is released)
inline constexpr std::uint8_t kPerfAccAll = 1u << 1;    // every note is accented
inline constexpr std::uint8_t kPerfSlideAll = 1u << 2;  // every step slides

// What the screens read: the playhead as of the end of the last block.
struct Playhead {
  bool playing = false;
  int pattern = -1;        // the pattern being played (0..kPatternCount-1), -1 when stopped
  int step = -1;           // the step that started last (0..63), -1 when stopped
  int point = 0;           // its quarter (0..3) now: where a motion recording lands
  int queued = -1;         // the pattern that starts when this one ends (the next of the chain, or its first when a looping chain
                           // is at its end), -1 if the playing pattern simply repeats
  unsigned queue_length = 0;  // chain entries still to come after the playing one (a loop's wrap not counted), 0..kMaxQueue
  bool sounding = false;   // a note is held now
};

// The chain as the sequencer holds it (the patterns that follow the playing one, in order), read-only for the screens.
struct ChainView {
  unsigned length = 0;                 // entries, 0..kMaxQueue
  std::uint8_t pattern[kMaxQueue] = {};  // the patterns, in play order
  int position = -1;                   // the entry playing now; -1: the playing pattern is not from the chain (or stopped)
  int next_entry = -1;                 // the entry that starts when the playing pattern ends (the first one when a loop is at its end),
                                       // -1 if the playing pattern simply repeats; it is position + 1 unless a jump moved the playing pattern
  int next = -1;                       // its pattern, as Playhead::queued
  bool loop = false;                   // the chain starts again after its last entry
};

class Sequencer final : public o3sdk::dsp::Clocked {
 public:
  explicit Sequencer(Bank& bank) noexcept;

  // ---- Setup (main loop, before engine.start()) ---------------------------------------------------------------------------
  // `voice` is the instrument unit the notes go to; its parameter ids 0..7 are the motion lanes (their descriptors are read here).
  // Adds this hook to the engine: the app does not call engine.add_hook(). false: the engine refused the hook (full, started), or
  // `voice` has fewer than 8 parameters.
  bool setup(o3sdk::dsp::Engine& engine, o3sdk::dsp::UnitId voice) noexcept;

  // ---- Every UI frame (main loop) -----------------------------------------------------------------------------------------
  // Hands the hook the knob values (the engine's stored values of the voice's ids 0..7), which it puts back when a p-lock or a
  // motion lane ends. Call it once per UI frame; a missing call only leaves the old knob value after a lock.
  void update() noexcept;

  Bank& bank() noexcept { return bank_; }
  const Bank& bank() const noexcept { return bank_; }

  // ---- Playing patterns (main loop) ---------------------------------------------------------------------------------------
  // Play (engine.play()) starts the selected pattern, bank().selected(), from step 0.
  // The chain: a list of up to kMaxQueue patterns with a cursor. When the playing pattern ends (at the end of its length) the next
  // entry of the chain starts; when the chain is used up the playing pattern repeats (or, with the loop on, the chain starts again
  // from its first entry). Play starts at the selected pattern, outside the chain, and the cursor at the chain's first entry.
  // queue: appends the pattern to the chain (a tap while playing queues one; several taps make a chain). When the chain is full, the
  // entries already played are dropped to make room (not with the loop on: the chain is then refused). The call is a command: its
  // effect shows in chain() within a block. A pattern index >= kPatternCount is refused.
  // Return values: true = the command is on the ring (a full ring, or a bad index, gives false and changes nothing). The hook may
  // still refuse the entry later (the chain is full and looping): chain() is the truth, read it instead of counting calls.
  bool queue(unsigned pattern) noexcept;
  bool clear_queue() noexcept;  // empties the chain; the playing pattern repeats
  bool set_chain_loop(bool loop) noexcept;
  // The pattern starts at the next step, from its step 0; the chain's cursor stays where it was, so the chain goes on after it.
  // Stopped: only selects the pattern. Playing: selects it too, but only if the command went on the ring (false: nothing happened).
  bool jump(unsigned pattern) noexcept;

  // ---- Live control (main loop; atomics, effective at the next step) ----------------------------------------------------
  void set_live_transpose(int semitones) noexcept;  // added to the notes, -48..+48
  int live_transpose() const noexcept { return live_transpose_.load(std::memory_order_relaxed); }
  void set_perf(std::uint8_t flags) noexcept { perf_.store(flags, std::memory_order_relaxed); }
  void set_perf_flag(std::uint8_t flag, bool on) noexcept;
  std::uint8_t perf() const noexcept { return perf_.load(std::memory_order_relaxed); }

  // ---- Readback (main loop) -----------------------------------------------------------------------------------------------
  Playhead playhead() const noexcept;
  // The chain, its cursor and the loop flag, as of the end of the last block (a consistent copy; the render publishes it when it
  // changes). Read-only: change it with queue(), clear_queue(), set_chain_loop().
  ChainView chain() const noexcept;
  // Writes `value` (the voice parameter `lane`'s own units) as the motion point under the playhead of the playing pattern: the
  // live recording of an encoder move. An undoable edit, joined per lane. false: not playing, bad lane.
  bool record_motion(unsigned lane, float value) noexcept;
  // Events the hook could not place because the voice's list for a block was full.
  std::uint32_t dropped_events() const noexcept { return dropped_.load(std::memory_order_relaxed); }

  // ---- Render context -----------------------------------------------------------------------------------------------------
  void block(o3sdk::dsp::BlockContext& ctx) noexcept override;

 private:
  enum class Op : std::uint8_t { Queue, ClearQueue, ChainLoop, Jump };
  struct Command {
    Op op;
    std::uint8_t arg;
    std::uint16_t pad;
  };
  static_assert(sizeof(Command) == 4, "a command is one word");

  // ---- render side
  void drain_commands() noexcept;
  void begin_run(const o3sdk::dsp::Transport& t) noexcept;
  void end_run(o3sdk::dsp::BlockContext& ctx) noexcept;
  void start_step(o3sdk::dsp::BlockContext& ctx, unsigned frame) noexcept;
  void emit_points(o3sdk::dsp::BlockContext& ctx, unsigned frame, unsigned quarter) noexcept;
  void release_held(o3sdk::dsp::BlockContext& ctx, unsigned frame) noexcept;
  void restore_lanes(o3sdk::dsp::BlockContext& ctx, unsigned frame) noexcept;
  void load_pattern(unsigned index) noexcept;
  void refresh_shadow() noexcept;
  void append_chain(unsigned pattern) noexcept;
  void advance_chain() noexcept;
  int chain_position() const noexcept;
  int chain_queued() const noexcept;
  void publish() noexcept;
  void publish_chain() noexcept;
  bool emit(o3sdk::dsp::BlockContext& ctx, const o3sdk::dsp::Event& e) noexcept;

  Bank& bank_;
  o3sdk::dsp::Engine* engine_ = nullptr;
  o3sdk::dsp::UnitId voice_ = o3sdk::dsp::kNoUnit;
  const o3sdk::core::ParamDesc* lane_desc_[kMotionLanes] = {};

  // ---- main -> render
  o3sdk::core::SpscRing<Command, 32> commands_;
  std::atomic<int> live_transpose_{0};
  std::atomic<std::uint8_t> perf_{0};
  std::atomic<float> knob_[kMotionLanes];  // the voice's stored values, see update()

  // ---- render -> main
  std::atomic<std::uint32_t> playhead_{0};  // packed (sequencer.cpp)
  std::atomic<std::uint32_t> dropped_{0};
  // The chain, published by the render when it changes: a seqlock (single writer: the render; odd while writing) over 4 words of
  // entries (4 bytes each) and a word of length, position and loop. chain() retries when a write overlapped its read.
  std::atomic<std::uint32_t> chain_seq_{0};
  std::atomic<std::uint32_t> chain_words_[kMaxQueue / 4];
  std::atomic<std::uint32_t> chain_meta_{0};
  mutable ChainView last_chain_;  // main loop: the last consistent view chain() read

  // ---- render state (only the render context touches it after setup)
  bool running_ = false;
  bool chain_loop_ = false;
  unsigned playing_ = 0;            // the pattern being played
  unsigned next_step_ = 0;          // index of the step that starts next
  int cur_step_ = -1;               // the step that started last
  int cur_point_ = 0;               // its quarter whose motion points went out last
  unsigned pt_q_ = 4;               // the next motion quarter of the current step (4: none left)
  int jump_to_ = -1;                // a pending jump
  std::uint64_t step_tick_ = 0;     // the tick the next step starts at
  std::uint64_t off_tick_ = 0;      // the tick the held note's gate ends at
  bool off_due_ = false;
  std::uint64_t pt_tick_ = 0;       // the tick of the next motion quarter
  std::uint64_t cur_start_ = 0;     // the current step's start and length in ticks
  std::uint32_t cur_len_ = 0;
  int held_ = -1;                   // the note on the voice now (0..127), -1 none
  bool prev_slide_ = false;         // the last step held its note into the next one with a slide
  std::uint8_t chain_[kMaxQueue] = {};
  unsigned chain_len_ = 0;          // entries
  unsigned chain_next_ = 0;         // the entry that starts when the playing pattern ends (== chain_len_: none left)
  bool from_chain_ = false;         // the playing pattern is the entry before chain_next_ (not the selected one, not a jump)
  bool chain_dirty_ = true;         // the published chain is out of date
  std::uint8_t overridden_ = 0;     // motion lanes whose value the hook set (bit per lane)
  float last_value_[kMotionLanes] = {};
  // The playing pattern, as of shadow_version_: two buffers, shadow_[cur_] is the one played from; the other receives the next
  // snapshot and becomes current only when the copy is consistent (refresh_shadow()).
  alignas(4) Pattern shadow_[2];
  unsigned cur_ = 0;
  std::uint32_t shadow_version_ = 0;
  bool shadow_valid_ = false;       // shadow_[cur_] is a consistent copy of the playing pattern at shadow_version_
  bool shadow_motion_ = false;      // it has at least one motion point (the Bank keeps the flag; the hook never scans)
  std::uint64_t pair_tick_ = 0;     // the tick the current pair (an even step and its odd step) began at
};

}  // namespace o3_303::seq
