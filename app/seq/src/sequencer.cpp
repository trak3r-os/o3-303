// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sequencer hook. The timing and the note rules (swing, the slide overlap, rests, the velocities 110 and 80) are Replicant 303's
// (third_party/replicant303/DSP/PatternSequencer.cpp, triggerStep and process, commit d5369be028399f5a403067b58da2936caa070c0e) by
// DanimanX, rebuilt as a clocked hook of the engine. Replicant 303 is distributed under the MIT License:
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
#include <o3_303/seq/sequencer.h>

#include <o3_303/seq/ops.h>

namespace o3_303::seq {

namespace {

namespace dsp = o3sdk::dsp;

constexpr std::uint8_t kVelocityNormal = 80;   // Replicant's: accented notes 110, the others 80
constexpr std::uint8_t kVelocityAccent = 110;
constexpr std::uint32_t kRecordKey = 0x07000000u;  // coalescing key of a motion recording (bank.cpp has the others)
constexpr int kMaxLiveTranspose = 48;
constexpr unsigned kMaxEventsLoop = 16;  // a bound on the events handled in one block (at most two steps and their points are due)

// The playhead word: one atomic, so a reader never sees half of it.
//   bit 0 playing | 1..6 pattern | 7..13 step + 1 (0: none) | 14..15 point | 16..22 queued + 1 (0: none) | 23..27 queue length | 28 sounding
std::uint32_t pack_playhead(const Playhead& h) noexcept {
  const auto u = [](int v) { return static_cast<std::uint32_t>(v); };
  return (h.playing ? 1u : 0u) | ((u(h.pattern < 0 ? 0 : h.pattern) & 0x3Fu) << 1) | ((u(h.step + 1) & 0x7Fu) << 7) |
         ((u(h.point) & 3u) << 14) | ((u(h.queued + 1) & 0x7Fu) << 16) | ((h.queue_length & 0x1Fu) << 23) | ((h.sounding ? 1u : 0u) << 28);
}

Playhead unpack_playhead(std::uint32_t w) noexcept {
  Playhead h;
  h.playing = (w & 1u) != 0u;
  h.pattern = h.playing ? static_cast<int>((w >> 1) & 0x3Fu) : -1;
  h.step = h.playing ? static_cast<int>((w >> 7) & 0x7Fu) - 1 : -1;
  h.point = static_cast<int>((w >> 14) & 3u);
  h.queued = static_cast<int>((w >> 16) & 0x7Fu) - 1;
  h.queue_length = (w >> 23) & 0x1Fu;
  h.sounding = ((w >> 28) & 1u) != 0u;
  return h;
}

unsigned length_of(const Pattern& p) noexcept { return valid_length(p.length) ? p.length : kDefaultLength; }

// The frame of this block at which `tick` is reached; 0 for a tick that already went by (the hook was late), -1 for one that is still
// ahead.
int frame_for(const dsp::Transport& t, std::uint64_t tick) noexcept {
  const int f = dsp::frame_at(t, tick);
  if (f >= 0) {
    return f;
  }
  return ((tick << 32) + t.inc) <= t.pos ? 0 : -1;
}

}  // namespace

Sequencer::Sequencer(Bank& bank) noexcept : bank_(bank) {
  for (auto& k : knob_) {
    k.store(0.0f, std::memory_order_relaxed);
  }
  for (auto& w : chain_words_) {
    w.store(0u, std::memory_order_relaxed);
  }
  for (auto& sh : shadow_) {
    clear(sh);  // until the first consistent copy of a pattern arrives the hook plays silence
  }
}

// ---- Main loop ----------------------------------------------------------------------------------------------------------------

bool Sequencer::setup(dsp::Engine& engine, dsp::UnitId voice) noexcept {
  const dsp::UnitInfo* info = engine.unit_info(voice);
  if (info == nullptr || info->param_count < kMotionLanes) {
    return false;
  }
  engine_ = &engine;
  voice_ = voice;
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    lane_desc_[lane] = engine.param_desc(voice, static_cast<std::uint16_t>(lane));
    knob_[lane].store(engine.param(voice, static_cast<std::uint16_t>(lane)), std::memory_order_relaxed);
  }
  return engine.add_hook(*this);
}

void Sequencer::update() noexcept {
  if (engine_ == nullptr) {
    return;
  }
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    knob_[lane].store(engine_->param(voice_, static_cast<std::uint16_t>(lane)), std::memory_order_relaxed);
  }
}

// true: the command is on the ring. The hook may still refuse the entry (a full chain with the loop on): chain() is the truth.
bool Sequencer::queue(unsigned pattern) noexcept {
  if (pattern >= kPatternCount) {
    return false;
  }
  return commands_.push(Command{Op::Queue, static_cast<std::uint8_t>(pattern), 0});
}

bool Sequencer::clear_queue() noexcept { return commands_.push(Command{Op::ClearQueue, 0, 0}); }

bool Sequencer::set_chain_loop(bool loop) noexcept { return commands_.push(Command{Op::ChainLoop, loop ? std::uint8_t{1} : std::uint8_t{0}, 0}); }

bool Sequencer::jump(unsigned pattern) noexcept {
  if (pattern >= kPatternCount) {
    return false;
  }
  if (engine_ == nullptr || !engine_->playing()) {
    bank_.select(pattern);  // stopped: only selects
    return true;
  }
  if (!commands_.push(Command{Op::Jump, static_cast<std::uint8_t>(pattern), 0})) {
    return false;  // the ring is full: nothing happened, the selection stays
  }
  bank_.select(pattern);
  return true;
}

void Sequencer::set_live_transpose(int semitones) noexcept {
  live_transpose_.store(semitones < -kMaxLiveTranspose ? -kMaxLiveTranspose : (semitones > kMaxLiveTranspose ? kMaxLiveTranspose : semitones),
                        std::memory_order_relaxed);
}

void Sequencer::set_perf_flag(std::uint8_t flag, bool on) noexcept {
  const std::uint8_t cur = perf_.load(std::memory_order_relaxed);  // one writer: the main loop
  perf_.store(on ? static_cast<std::uint8_t>(cur | flag) : static_cast<std::uint8_t>(cur & ~flag), std::memory_order_relaxed);
}

Playhead Sequencer::playhead() const noexcept { return unpack_playhead(playhead_.load(std::memory_order_relaxed)); }

bool Sequencer::record_motion(unsigned lane, float value) noexcept {
  if (lane >= kMotionLanes || lane_desc_[lane] == nullptr) {
    return false;
  }
  const Playhead ph = playhead();
  if (!ph.playing || ph.step < 0 || ph.pattern < 0) {
    return false;
  }
  const std::uint8_t point = value_to_point(*lane_desc_[lane], value);
  (void)bank_.edit(
      static_cast<unsigned>(ph.pattern),
      [&](Pattern& p) { set_motion_point(p, lane, static_cast<unsigned>(ph.step), static_cast<unsigned>(ph.point), point); },
      kRecordKey | lane);
  return true;
}

// ---- Render context -----------------------------------------------------------------------------------------------------------

bool Sequencer::emit(dsp::BlockContext& ctx, const dsp::Event& e) noexcept {
  if (ctx.emit(voice_, e)) {
    return true;
  }
  dropped_.fetch_add(1u, std::memory_order_relaxed);
  return false;
}

void Sequencer::drain_commands() noexcept {
  Command c;
  for (unsigned n = 0; n < 32u && commands_.pop(c); ++n) {
    switch (c.op) {
      case Op::Queue:
        append_chain(c.arg);
        break;
      case Op::ClearQueue:
        chain_len_ = 0;
        chain_next_ = 0;
        from_chain_ = false;
        chain_dirty_ = true;
        break;
      case Op::ChainLoop:
        chain_loop_ = c.arg != 0u;
        chain_dirty_ = true;
        break;
      case Op::Jump:
        if (c.arg < kPatternCount) {
          jump_to_ = c.arg;
        }
        break;
    }
  }
}

// Appends to the chain. Full: the entries before the playing one are done with and go (not in a loop, where they are still needed).
void Sequencer::append_chain(unsigned pattern) noexcept {
  if (pattern >= kPatternCount) {
    return;
  }
  if (chain_len_ >= kMaxQueue && !chain_loop_) {
    const unsigned done = from_chain_ && chain_next_ > 0u ? chain_next_ - 1u : chain_next_;
    if (done > 0u) {
      for (unsigned i = done; i < chain_len_; ++i) {
        chain_[i - done] = chain_[i];
      }
      chain_len_ -= done;
      chain_next_ -= done;
    }
  }
  if (chain_len_ < kMaxQueue) {
    chain_[chain_len_++] = static_cast<std::uint8_t>(pattern);
    chain_dirty_ = true;
  }
}

// The playing pattern ended: the next entry of the chain starts; used up, the playing pattern repeats (a loop starts the chain again).
void Sequencer::advance_chain() noexcept {
  if (chain_next_ >= chain_len_ && chain_loop_ && chain_len_ > 0u) {
    chain_next_ = 0;
  }
  if (chain_next_ < chain_len_) {
    load_pattern(chain_[chain_next_]);
    ++chain_next_;
    from_chain_ = true;
    chain_dirty_ = true;
  }
}

int Sequencer::chain_position() const noexcept { return running_ && from_chain_ && chain_next_ > 0u ? static_cast<int>(chain_next_) - 1 : -1; }

int Sequencer::chain_queued() const noexcept {
  if (chain_next_ < chain_len_) {
    return chain_[chain_next_];
  }
  return chain_loop_ && chain_len_ > 0u ? static_cast<int>(chain_[0]) : -1;
}

// The copy of the playing pattern the hook plays from. The Bank's snapshot is taken into the other buffer and becomes the shadow only
// when no write overlapped it (a pointer-sized swap, cur_); otherwise the hook goes on with the shadow it has and tries again at the
// next block. The hook never plays a half-written pattern, and never waits for the main loop.
void Sequencer::refresh_shadow() noexcept {
  const std::uint32_t v = bank_.version(playing_);
  if (shadow_valid_ && v == shadow_version_) {
    return;
  }
  std::uint32_t seen = 0;
  bool motion = false;
  if (bank_.snapshot(playing_, shadow_[cur_ ^ 1u], &seen, &motion)) {
    cur_ ^= 1u;
    shadow_version_ = seen;
    shadow_valid_ = true;
    shadow_motion_ = motion;
  }
}

void Sequencer::load_pattern(unsigned index) noexcept {
  playing_ = index < kPatternCount ? index : 0u;
  shadow_valid_ = false;
  refresh_shadow();
}

void Sequencer::release_held(dsp::BlockContext& ctx, unsigned frame) noexcept {
  if (held_ >= 0) {
    (void)emit(ctx, dsp::note_off(static_cast<std::uint8_t>(held_), static_cast<std::uint16_t>(frame)));
    held_ = -1;
  }
  off_due_ = false;
}

void Sequencer::restore_lanes(dsp::BlockContext& ctx, unsigned frame) noexcept {
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    if ((overridden_ >> lane & 1u) != 0u) {
      (void)emit(ctx, dsp::param_change(static_cast<std::uint16_t>(lane), knob_[lane].load(std::memory_order_relaxed),
                                        static_cast<std::uint16_t>(frame)));
    }
  }
  overridden_ = 0;
}

void Sequencer::end_run(dsp::BlockContext& ctx) noexcept {
  release_held(ctx, 0);
  restore_lanes(ctx, 0);
  running_ = false;
  cur_step_ = -1;
  cur_point_ = 0;
  pt_q_ = 4;
  prev_slide_ = false;
  jump_to_ = -1;
  chain_next_ = 0;  // a stopped sequencer starts the chain from its first entry
  from_chain_ = false;
  chain_dirty_ = true;
}

void Sequencer::begin_run(const dsp::Transport& t) noexcept {
  running_ = true;
  next_step_ = 0;
  cur_step_ = -1;
  cur_point_ = 0;
  pt_q_ = 4;
  off_due_ = false;
  held_ = -1;
  prev_slide_ = false;
  jump_to_ = -1;
  chain_next_ = 0;
  from_chain_ = false;
  chain_dirty_ = true;
  load_pattern(bank_.selected());
  step_tick_ = (t.pos + 0xFFFFFFFFull) >> 32;  // the first whole tick the transport has not passed
}

void Sequencer::emit_points(dsp::BlockContext& ctx, unsigned frame, unsigned quarter) noexcept {
  if (!shadow_motion_ && overridden_ == 0u) {
    return;
  }
  const unsigned at = static_cast<unsigned>(cur_step_) * kMotionPointsPerStep + quarter;
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    const o3sdk::core::ParamDesc* d = lane_desc_[lane];
    if (d == nullptr) {
      continue;
    }
    const std::uint8_t point = shadow_[cur_].motion[lane][at];
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << lane);
    if (point == kNoLock) {
      if ((overridden_ & bit) != 0u) {  // the lock ended: the knob's value comes back
        (void)emit(ctx, dsp::param_change(static_cast<std::uint16_t>(lane), knob_[lane].load(std::memory_order_relaxed),
                                          static_cast<std::uint16_t>(frame)));
        overridden_ = static_cast<std::uint8_t>(overridden_ & ~bit);
      }
      continue;
    }
    const float value = point_to_value(*d, point);
    if ((overridden_ & bit) == 0u || value != last_value_[lane]) {
      (void)emit(ctx, dsp::param_change(static_cast<std::uint16_t>(lane), value, static_cast<std::uint16_t>(frame)));
      last_value_[lane] = value;
      overridden_ = static_cast<std::uint8_t>(overridden_ | bit);
    }
  }
}

void Sequencer::start_step(dsp::BlockContext& ctx, unsigned frame) noexcept {
  // Which step: a pending jump starts its pattern here; the end of the length is the end of the pattern (queue, chain).
  if (jump_to_ >= 0) {
    load_pattern(static_cast<unsigned>(jump_to_));
    jump_to_ = -1;
    next_step_ = 0;
    from_chain_ = false;  // the chain's cursor stays: the chain goes on after this pattern
    chain_dirty_ = true;
  } else if (next_step_ >= length_of(shadow_[cur_])) {
    next_step_ = 0;
    advance_chain();
  }
  const Pattern& pat = shadow_[cur_];
  const unsigned n = length_of(pat);
  const unsigned j = next_step_ < n ? next_step_ : 0u;
  const Step s = pat.steps[j];

  // Swing: the even steps are longer and the odd ones shorter by half the swing of a step (a percent of 240 ticks, halved).
  // The pairs sit on the grid: an even step starts a pair (pair_tick_) and lasts a sixteenth plus the swing; the odd step then starts
  // late by the swing and ends where the next pair begins. So a swing changed while playing only moves the odd steps from the next
  // pair on, and the even steps never leave the grid.
  const unsigned swing = pat.swing > kMaxSwing ? kMaxSwing : pat.swing;
  const std::uint32_t sw = (swing * 6u + 2u) / 5u;
  const std::uint64_t start = step_tick_;
  std::uint64_t following;  // the tick the next step starts at
  if ((j & 1u) == 0u) {
    pair_tick_ = start;
    following = start + dsp::kTicksPerSixteenth + sw;
  } else {
    following = pair_tick_ + 2u * dsp::kTicksPerSixteenth;
    if (following <= start) {  // not after an even step of this pair (cannot happen: step 0 is even): keep going on the grid
      following = start + dsp::kTicksPerSixteenth;
    }
  }
  const auto len = static_cast<std::uint32_t>(following - start);
  const unsigned gate_pct = pat.gate < 5u ? 5u : (pat.gate > 100u ? 100u : pat.gate);
  const std::uint32_t gate_ticks = (len * gate_pct + 50u) / 100u;

  cur_step_ = static_cast<int>(j);
  cur_point_ = 0;
  cur_start_ = start;
  cur_len_ = len;
  step_tick_ = following;
  pt_q_ = 1;
  pt_tick_ = start + len / kMotionPointsPerStep;
  next_step_ = j + 1u;

  // The locks first: the note then starts with them.
  emit_points(ctx, frame, 0);

  const std::uint8_t perf = perf_.load(std::memory_order_relaxed);
  const bool mute = (perf & kPerfMute) != 0u;
  const bool slide = (s.flags & kStepSlide) != 0u || (perf & kPerfSlideAll) != 0u;
  const bool rest = (s.flags & kStepRest) != 0u || mute;
  const bool tie = (s.flags & kStepTie) != 0u && !rest;

  // Does the note go on into the next step (a slide, or a tie behind it)? The look-ahead stays in this pattern.
  const unsigned after = j + 1u < n ? j + 1u : 0u;
  const std::uint8_t after_flags = pat.steps[after].flags;
  const bool tie_follows = (after_flags & kStepTie) != 0u && (after_flags & kStepRest) == 0u && !mute;
  const bool hold = slide || tie_follows;

  if (rest) {
    release_held(ctx, frame);
    prev_slide_ = false;
    return;
  }
  if (tie) {
    if (held_ < 0) {
      prev_slide_ = false;  // nothing to go on with: silent
      return;
    }
    off_due_ = !hold;
    off_tick_ = start + gate_ticks;
    prev_slide_ = slide;
    return;
  }

  const int transposed = static_cast<int>(s.note) + static_cast<int>(pat.transpose) + live_transpose_.load(std::memory_order_relaxed);
  const int note = transposed < 0 ? 0 : (transposed > static_cast<int>(kMaxNote) ? static_cast<int>(kMaxNote) : transposed);
  const bool accent = (s.flags & kStepAccent) != 0u || (perf & kPerfAccAll) != 0u;
  const std::uint8_t velocity = accent ? kVelocityAccent : kVelocityNormal;
  const std::uint8_t accent_flag = accent ? dsp::kNoteAccent : std::uint8_t{0};
  const auto f = static_cast<std::uint16_t>(frame);
  const auto note8 = static_cast<std::uint8_t>(note);

  if (held_ >= 0 && prev_slide_) {
    // The legato overlap: the new note starts while the old one is still held, so the voice glides. A glide to the same note needs
    // no note-off (it would release the very note that goes on).
    (void)emit(ctx, dsp::note_on(note8, velocity, static_cast<std::uint8_t>(accent_flag | dsp::kNoteSlide), f));
    if (held_ != note) {
      (void)emit(ctx, dsp::note_off(static_cast<std::uint8_t>(held_), f));
    }
  } else {
    if (held_ >= 0) {
      (void)emit(ctx, dsp::note_off(static_cast<std::uint8_t>(held_), f));
    }
    (void)emit(ctx, dsp::note_on(note8, velocity, accent_flag, f));
  }
  held_ = note;
  off_due_ = !hold;
  off_tick_ = start + gate_ticks;
  prev_slide_ = slide;
}

void Sequencer::publish() noexcept {
  Playhead h;
  h.playing = running_;
  h.pattern = running_ ? static_cast<int>(playing_) : -1;
  h.step = running_ ? cur_step_ : -1;
  h.point = cur_point_;
  h.queued = chain_queued();
  h.queue_length = chain_len_ > chain_next_ ? chain_len_ - chain_next_ : 0u;
  h.sounding = held_ >= 0;
  playhead_.store(pack_playhead(h), std::memory_order_relaxed);
  if (chain_dirty_) {
    publish_chain();
    chain_dirty_ = false;
  }
}

// The chain for the main loop: seqlock, single writer (this context). Entries are 4 to a word.
void Sequencer::publish_chain() noexcept {
  const std::uint32_t seq = chain_seq_.load(std::memory_order_relaxed);
  chain_seq_.store(seq + 1u, std::memory_order_relaxed);  // odd: writing
  std::atomic_thread_fence(std::memory_order_release);
  for (unsigned w = 0; w < kMaxQueue / 4u; ++w) {
    std::uint32_t word = 0;
    for (unsigned b = 0; b < 4u; ++b) {
      const unsigned i = w * 4u + b;
      word |= static_cast<std::uint32_t>(i < chain_len_ ? chain_[i] : 0u) << (8u * b);
    }
    chain_words_[w].store(word, std::memory_order_relaxed);
  }
  const auto position = static_cast<std::uint32_t>(chain_position() + 1);  // 0: none
  const std::uint32_t upcoming = chain_next_ < chain_len_ ? chain_next_ + 1u : (chain_loop_ && chain_len_ > 0u ? 1u : 0u);  // entry + 1; 0: none
  chain_meta_.store(chain_len_ | (position << 8) | (upcoming << 16) | ((chain_loop_ ? 1u : 0u) << 24), std::memory_order_relaxed);
  chain_seq_.store(seq + 2u, std::memory_order_release);
}

ChainView Sequencer::chain() const noexcept {
  ChainView v;
  for (unsigned attempt = 0; attempt < 8u; ++attempt) {
    const std::uint32_t before = chain_seq_.load(std::memory_order_acquire);
    if ((before & 1u) != 0u) {
      continue;  // the render is writing: try again
    }
    std::uint32_t words[kMaxQueue / 4u];
    for (unsigned w = 0; w < kMaxQueue / 4u; ++w) {
      words[w] = chain_words_[w].load(std::memory_order_relaxed);
    }
    const std::uint32_t meta = chain_meta_.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    if (chain_seq_.load(std::memory_order_relaxed) != before) {
      continue;
    }
    v.length = meta & 0xFFu;
    v.length = v.length > kMaxQueue ? kMaxQueue : v.length;
    for (unsigned i = 0; i < v.length; ++i) {
      v.pattern[i] = static_cast<std::uint8_t>(words[i / 4u] >> (8u * (i % 4u)));
    }
    v.position = static_cast<int>((meta >> 8) & 0xFFu) - 1;
    v.next_entry = static_cast<int>((meta >> 16) & 0xFFu) - 1;
    v.loop = ((meta >> 24) & 1u) != 0u;
    v.next = v.next_entry >= 0 && static_cast<unsigned>(v.next_entry) < v.length ? v.pattern[v.next_entry] : -1;
    last_chain_ = v;
    return v;
  }
  return last_chain_;  // the render kept writing (cannot be, on one core): the last consistent view
}

void Sequencer::block(dsp::BlockContext& ctx) noexcept {
  drain_commands();
  const dsp::Transport& t = ctx.transport();

  if (t.stopped && running_) {
    end_run(ctx);
  }
  if (!t.playing) {
    if (running_) {
      end_run(ctx);
    }
    publish();
    return;
  }
  if (t.started && running_) {
    end_run(ctx);  // Play while playing: the notes end, the pattern starts again
  }
  if (!running_) {
    begin_run(t);
  }
  refresh_shadow();

  for (unsigned n = 0; n < kMaxEventsLoop; ++n) {
    // The earliest thing due: the gate end of the held note, a motion quarter, or the next step. At the same tick the gate end goes
    // first (so a full-length gate does not glide into the next note), and a quarter before a step cannot tie (it is inside its step).
    enum class Due : std::uint8_t { Step, Point, Off };
    Due due = Due::Step;
    std::uint64_t tick = step_tick_;
    if (pt_q_ < kMotionPointsPerStep && pt_tick_ <= tick) {
      due = Due::Point;
      tick = pt_tick_;
    }
    if (off_due_ && off_tick_ <= tick) {
      due = Due::Off;
      tick = off_tick_;
    }
    const int f = frame_for(t, tick);
    if (f < 0) {
      break;
    }
    const auto frame = static_cast<unsigned>(f);
    switch (due) {
      case Due::Off:
        release_held(ctx, frame);
        break;
      case Due::Point:
        cur_point_ = static_cast<int>(pt_q_);
        emit_points(ctx, frame, pt_q_);
        ++pt_q_;
        pt_tick_ = cur_start_ + (static_cast<std::uint64_t>(pt_q_) * cur_len_) / kMotionPointsPerStep;
        break;
      case Due::Step:
        start_step(ctx, frame);
        break;
    }
  }
  publish();
}

}  // namespace o3_303::seq
