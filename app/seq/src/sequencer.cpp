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

#include <o3sdk/core/audio_clock.h>
#include <o3sdk/core/param.h>
#include <o3_303/seq/ops.h>

namespace o3_303::seq {

namespace {

namespace dsp = o3sdk::dsp;
namespace core = o3sdk::core;

constexpr std::uint8_t kVelocityNormal = 80;   // Replicant's: accented notes 110, the others 80
constexpr std::uint8_t kVelocityAccent = 110;
constexpr std::uint32_t kRecordKey = 0x07000000u;  // coalescing key of a motion recording (bank.cpp has the others)
constexpr int kMaxLiveTranspose = 48;
constexpr unsigned kMaxEventsLoop = 16;  // a bound on the events handled in one block (at most two steps, their hits and points are due)
constexpr float kMaxSlewFrames = 1048576.0f;  // 23 s: a float counts samples exactly up to 2^24
constexpr unsigned kSlewExtraEvents = 4;  // a slew's last event (its goal) may go beyond kSlewEvents: the end state must land

// The playhead word: one atomic, so a reader never sees half of it.
//   bit 0 playing | 1..6 pattern | 7..13 step + 1 (0: none) | 14..15 point | 16..22 queued + 1 (0: none) | 23..27 queue length | 28 sounding
//   | 29 fill
std::uint32_t pack_playhead(const Playhead& h) noexcept {
  const auto u = [](int v) { return static_cast<std::uint32_t>(v); };
  return (h.playing ? 1u : 0u) | ((u(h.pattern < 0 ? 0 : h.pattern) & 0x3Fu) << 1) | ((u(h.step + 1) & 0x7Fu) << 7) |
         ((u(h.point) & 3u) << 14) | ((u(h.queued + 1) & 0x7Fu) << 16) | ((h.queue_length & 0x1Fu) << 23) | ((h.sounding ? 1u : 0u) << 28) |
         ((h.fill ? 1u : 0u) << 29);
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
  h.fill = ((w >> 29) & 1u) != 0u;
  return h;
}

unsigned length_of(const Pattern& p) noexcept { return valid_length(p.length) ? p.length : kDefaultLength; }

// The frame of this block at which `tick` is reached; 0 for a tick that already went by (the hook was late), -1 for one that is still
// ahead.
int frame_at_or_past(const dsp::Transport& t, std::uint64_t tick) noexcept {
  const int f = dsp::frame_at(t, tick);
  if (f >= 0) {
    return f;
  }
  return ((tick << 32) + t.inc) <= t.pos ? 0 : -1;
}

// The descriptors of kLaneSeq's two values (a motion lane on them goes through point_to_value like any other): the wobble rate as an Enum over
// the table of wobble.h, the depth as a fraction.
constexpr core::ParamDesc kSeqParams[kSeqValueCount] = {
    {"WOB RATE", "", kWobbleRateNames, nullptr, 0.0f, 10.0f, 4.0f, 10, 1, static_cast<std::uint8_t>(kWobbleRateCount), core::ParamUnit::Enum,
     core::ParamCurve::Linear, 0, 0},
    {"WOB DEPTH", "%", nullptr, nullptr, 0.0f, 1.0f, 0.5f, 100, 10, 0, core::ParamUnit::NormPct, core::ParamCurve::Linear, 0, 0},
};
static_assert(kWobbleRateCount == 11, "the wobble rate is an Enum over 11 names: kSeqParams[0].max is 10");

// The wobble word (sequencer.h): bit 0 on | 1 reset on note | 2..5 rate | 6..8 shape | 9..11 target | 12..16 id | 17..21 override rate + 1.
constexpr std::uint32_t kWobOn = 1u;
constexpr std::uint32_t kWobReset = 2u;
constexpr unsigned kWobRateShift = 2;
constexpr unsigned kWobShapeShift = 6;
constexpr unsigned kWobTargetShift = 9;
constexpr unsigned kWobIdShift = 12;
constexpr unsigned kWobOverShift = 17;
constexpr std::uint32_t kWobOverMask = 31u << kWobOverShift;
constexpr std::uint32_t kWobDefault = kWobReset | (static_cast<std::uint32_t>(kWobble1_8) << kWobRateShift) | (static_cast<std::uint32_t>(kLaneVoice) << kWobTargetShift);
constexpr std::uint32_t kWobbleSeedMix = 0x5eed0b1eu;  // the sample and hold's generator is the seed's, scrambled: it draws apart from the conditions'
constexpr unsigned kWobbleTick = 16;                   // frames between two Param events of the wobble

float default_mix(unsigned field) noexcept { return field == kMixerLevel ? 1.0f : 0.0f; }

// A mixer value as Engine::set_mix stores it (clamped; NaN is the field's default).
float clamp_mix(unsigned field, float v) noexcept {
  if (v != v) {
    return default_mix(field);
  }
  switch (field) {
    case kMixerPan:
      return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
    case kMixerMute:
      return v >= 0.5f ? 1.0f : 0.0f;
    default:
      return v < 0.0f ? 0.0f : (v > 1.5f ? 1.5f : v);
  }
}

}  // namespace

Sequencer::Sequencer(Bank& bank) noexcept : bank_(bank), seed_(kDefaultSeed), wobble_(kWobDefault), wob_rng_(kDefaultSeed ^ kWobbleSeedMix) {
  for (auto& row : knob_) {
    for (auto& k : row) {
      k.store(0.0f, std::memory_order_relaxed);
    }
  }
  for (auto& w : chain_words_) {
    w.store(0u, std::memory_order_relaxed);
  }
  for (auto& sh : shadow_) {
    clear(sh);  // until the first consistent copy of a pattern arrives the hook plays silence
  }
  clear(variant_);
  for (auto& s : slews_) {
    s = Slew{};
  }
}

// ---- Main loop ----------------------------------------------------------------------------------------------------------------

bool Sequencer::setup(dsp::Engine& engine, dsp::UnitId voice, dsp::UnitId dist, dsp::UnitId delay) noexcept {
  const dsp::UnitInfo* info = engine.unit_info(voice);
  if (info == nullptr || info->param_count < kMotionLanes) {
    return false;
  }
  engine_ = &engine;
  voice_ = voice;
  for (auto& t : tab_) {
    t = UnitTable{};
  }
  const dsp::UnitId ids[kLaneTargetCount] = {dsp::kNoUnit, voice, dist, delay, dsp::kNoUnit, dsp::kNoUnit, dsp::kNoUnit};
  for (unsigned target = kLaneVoice; target <= kLaneDelay; ++target) {
    const dsp::UnitInfo* ui = ids[target] != dsp::kNoUnit ? engine.unit_info(ids[target]) : nullptr;
    if (ui != nullptr) {
      tab_[target] = UnitTable{ids[target], ui->params, ui->param_count};
    }
  }
  tab_[kLaneSeq] = UnitTable{dsp::kNoUnit, kSeqParams, kSeqValueCount};  // set_unit() adds the growl's
  // The hook forgets what a previous run left (the app object outlives a run in the simulator's tests): setup() runs before the render.
  for (auto& s : slews_) {
    s = Slew{};
  }
  slew_rr_ = 0;
  slewing_.store(0u, std::memory_order_relaxed);
  slew_overflow_.store(0u, std::memory_order_relaxed);
  rng_ = Rng(seed_.load(std::memory_order_relaxed));
  wob_rng_ = Rng(seed_.load(std::memory_order_relaxed) ^ kWobbleSeedMix);
  wob_active_ = false;
  wob_phase_ = 0;
  wob_inc_tick_ = 0;
  wob_inc_rate_ = kWobbleRateCount;
  wob_held_ = 0.0f;
  wob_reset_count_ = 0;
  wob_base_count_ = 0;
  wob_hold_valid_ = false;
  wob_emitted_ = false;
  seq_rate_lane_ = -1;
  seq_depth_lane_ = -1.0f;
  wob_rate_now_.store(-1, std::memory_order_relaxed);
  seed_seen_ = seed_version_.load(std::memory_order_relaxed);
  fill_count_ = 0;
  pass_ = 0;
  fill_on_ = false;
  draw_valid_ = false;
  variant_valid_ = false;
  hit_n_ = 0;
  hit_k_ = 0;
  for (auto& row : unstored_) {
    for (auto& u : row) {
      u = Unstored{};
    }
  }
  update();
  return engine.add_hook(*this);
}

void Sequencer::update() noexcept {
  if (engine_ == nullptr) {
    return;
  }
  for (unsigned target = kLaneVoice; target <= kLaneGrowl; ++target) {
    const UnitTable& t = tab_[target];
    if (target == kLaneMixer || t.unit == dsp::kNoUnit) {
      continue;
    }
    const unsigned n = t.count < kKnobs ? t.count : kKnobs;
    for (unsigned id = 0; id < n; ++id) {
      knob_[target][id].store(unstored_or(target, id, engine_->param(t.unit, static_cast<std::uint16_t>(id))), std::memory_order_relaxed);
    }
  }
  for (unsigned field = 0; field < kMixerFieldCount; ++field) {
    knob_[kLaneMixer][field].store(unstored_or(kLaneMixer, field, engine_->mix(kMixerSlot, static_cast<dsp::MixField>(field))),
                                   std::memory_order_relaxed);
  }
}

float Sequencer::unstored_or(unsigned target, unsigned id, float stored) noexcept {
  Unstored& u = unstored_[target][id];
  if (u.valid && u.stored != stored) {
    u.valid = false;  // the target was written since: the stored value is the truth again
  }
  return u.valid ? u.goal : stored;
}

bool Sequencer::is_unit_target(std::uint8_t target) const noexcept {
  return target == kLaneVoice || target == kLaneDist || target == kLaneDelay || target == kLaneGrowl;
}

bool Sequencer::resolvable(std::uint8_t target, std::uint8_t id) const noexcept {
  if (target == kLaneMixer) {
    return id < kMixerFieldCount;
  }
  if (target == kLaneSeq) {
    return id < kSeqValueCount;
  }
  if (!is_unit_target(target)) {
    return false;
  }
  const UnitTable& t = tab_[target];
  return t.unit != dsp::kNoUnit && id < t.count && id < kKnobs;
}

const core::ParamDesc* Sequencer::desc_of(std::uint8_t target, std::uint8_t id) const noexcept {
  return (target != kLaneMixer && resolvable(target, id)) ? &tab_[target].params[id] : nullptr;
}

bool Sequencer::set_unit(std::uint8_t target, dsp::UnitId unit) noexcept {
  if (engine_ == nullptr || engine_->started() || (target != kLaneDist && target != kLaneDelay && target != kLaneGrowl)) {
    return false;
  }
  if (unit == dsp::kNoUnit) {
    tab_[target] = UnitTable{};
    return true;
  }
  const dsp::UnitInfo* ui = engine_->unit_info(unit);
  if (ui == nullptr) {
    return false;
  }
  tab_[target] = UnitTable{unit, ui->params, ui->param_count};
  update();
  return true;
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

void Sequencer::set_fill_variant(FillVariant v) noexcept {
  fill_variant_.store(static_cast<std::uint8_t>(static_cast<unsigned>(v) < kFillVariantCount ? static_cast<unsigned>(v) : 0u),
                      std::memory_order_relaxed);
}

FillVariant Sequencer::fill_variant() const noexcept {
  const unsigned v = fill_variant_.load(std::memory_order_relaxed);
  return static_cast<FillVariant>(v < kFillVariantCount ? v : 0u);
}

void Sequencer::set_seed(std::uint32_t seed) noexcept {
  seed_.store(seed, std::memory_order_relaxed);  // one writer: the main loop. The version, released after the seed, tells the render.
  seed_version_.store(seed_version_.load(std::memory_order_relaxed) + 1u, std::memory_order_release);
}

Playhead Sequencer::playhead() const noexcept { return unpack_playhead(playhead_.load(std::memory_order_relaxed)); }

bool Sequencer::record_motion(unsigned lane, float value) noexcept {
  if (lane >= kMotionLanes) {
    return false;
  }
  const Playhead ph = playhead();
  if (!ph.playing || ph.step < 0 || ph.pattern < 0) {
    return false;
  }
  const LaneRef ref = bank_.pattern(static_cast<unsigned>(ph.pattern)).lanes[lane];
  if (!resolvable(ref.target, ref.id)) {
    return false;
  }
  const std::uint8_t point =
      ref.target == kLaneMixer ? mixer_value_to_point(ref.id, value) : value_to_point(*desc_of(ref.target, ref.id), value);
  (void)bank_.edit(
      static_cast<unsigned>(ph.pattern),
      [&](Pattern& p) { set_motion_point(p, lane, static_cast<unsigned>(ph.step), static_cast<unsigned>(ph.point), point); },
      kRecordKey | lane);
  return true;
}

// ---- Slews (main loop) ----------------------------------------------------------------------------------------------------------

bool Sequencer::slew(std::uint8_t target, std::uint8_t id, float value, float ms, bool store) noexcept {
  if (engine_ == nullptr || target == kLaneSeq || !resolvable(target, id)) {
    return false;
  }
  const bool mixer = target == kLaneMixer;
  const core::ParamDesc* d = mixer ? nullptr : desc_of(target, id);
  const dsp::UnitId unit = tab_[target].unit;  // the mixer's entry is empty and unused
  const float goal = mixer ? clamp_mix(id, value) : core::param_clamp(*d, value);
  const float stored = mixer ? engine_->mix(kMixerSlot, static_cast<dsp::MixField>(id)) : engine_->param(unit, id);
  // After a slew that did not store its goal (a morph), the engine still holds the value from before it: start from that goal instead.
  const float from = unstored_or(target, id, stored);
  const auto write = [&]() {
    unstored_[target][id].valid = false;
    if (mixer) {
      (void)engine_->set_mix(kMixerSlot, static_cast<dsp::MixField>(id), goal);
    } else {
      (void)engine_->set_param(unit, id, goal);
    }
  };

  const float frames_f = ms * (core::kRefRateHz * 0.001f) + 0.5f;  // to the nearest sample: 30 ms are 1,323 at 44.1 kHz whatever the rounding
  const bool glides = ms > 0.0f && frames_f >= 1.0f && !(d != nullptr && d->unit == core::ParamUnit::Enum);
  if (glides) {
    const auto frames = static_cast<std::uint32_t>(frames_f > kMaxSlewFrames ? kMaxSlewFrames : frames_f);
    // The slew goes on the ring BEFORE the stored value changes. The hook starts it from `from`, at frame 0 of the block that takes it,
    // and a block takes the engine's commands first and the hook after: so the jump to the goal that write() queues never sounds,
    // whichever block the two land in (the hook's event at frame 0 comes later in the unit's list and wins).
    if (slew_commands_.push(SlewCommand{SlewOp::Start, target, id, 0, frames, goal, from})) {
      if (store) {
        write();
      } else {
        unstored_[target][id] = Unstored{goal, stored, true};
      }
      return true;
    }
  } else {
    (void)slew_commands_.push(SlewCommand{SlewOp::Stop, target, id, 0, 0, 0.0f, 0.0f});  // at once: nothing glides on this target now
  }
  write();
  return !glides;  // false: the ring was full and the value went in without its glide
}

bool Sequencer::slew_stop(std::uint8_t target, std::uint8_t id) noexcept {
  if (target < kLaneTargetCount && id < kKnobs) {
    unstored_[target][id].valid = false;  // stopped short of its goal: where it stands is not known here
  }
  return slew_commands_.push(SlewCommand{SlewOp::Stop, target, id, 0, 0, 0.0f, 0.0f});
}

bool Sequencer::slew_stop_all() noexcept {
  for (auto& row : unstored_) {
    for (auto& u : row) {
      u.valid = false;
    }
  }
  return slew_commands_.push(SlewCommand{SlewOp::StopAll, 0, 0, 0, 0, 0.0f, 0.0f});
}

// ---- Render context -----------------------------------------------------------------------------------------------------------

bool Sequencer::emit_to(dsp::BlockContext& ctx, dsp::UnitId unit, const dsp::Event& e) noexcept {
  if (ctx.emit(unit, e)) {
    return true;
  }
  dropped_.fetch_add(1u, std::memory_order_relaxed);
  return false;
}

bool Sequencer::emit(dsp::BlockContext& ctx, const dsp::Event& e) noexcept { return emit_to(ctx, voice_, e); }

void Sequencer::drain_commands(dsp::BlockContext& ctx) noexcept {
  // A new seed (set_seed()) restarts the draws; the version was released after the seed.
  const std::uint32_t version = seed_version_.load(std::memory_order_acquire);
  if (version != seed_seen_) {
    seed_seen_ = version;
    rng_ = Rng(seed_.load(std::memory_order_relaxed));
    wob_rng_ = Rng(seed_.load(std::memory_order_relaxed) ^ kWobbleSeedMix);
    fill_count_ = 0;
    variant_valid_ = false;
  }
  SlewCommand sc;
  for (unsigned n = 0; n < 64u && slew_commands_.pop(sc); ++n) {
    switch (sc.op) {
      case SlewOp::Start:
        start_slew(ctx, sc);
        break;
      case SlewOp::Stop:
        if (Slew* s = find_slew(sc.target, sc.id)) {
          s->active = false;
        }
        break;
      case SlewOp::StopAll:
        for (auto& s : slews_) {
          s.active = false;
        }
        break;
    }
  }
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
  pass_ = 0;  // a pattern change: the first pass again
  draw_valid_ = false;
  shadow_valid_ = false;
  variant_valid_ = false;
  refresh_shadow();
}

void Sequencer::release_held(dsp::BlockContext& ctx, unsigned frame) noexcept {
  if (held_ >= 0) {
    (void)emit(ctx, dsp::note_off(static_cast<std::uint8_t>(held_), static_cast<std::uint16_t>(frame)));
    held_ = -1;
  }
  off_due_ = false;
}

// ---- Lanes: whose parameter a lane drives, and what it does to it ---------------------------------------------------------------

// The value the main loop last handed over for a target (update()).
float Sequencer::knob_value(std::uint8_t target, std::uint8_t id) const noexcept {
  return knob_[target < kLaneTargetCount ? target : 0u][id < kKnobs ? id : 0u].load(std::memory_order_relaxed);
}

// One value to a lane's target: a Param event at `frame`, or the mixer field of slot 0 for the block.
bool Sequencer::write_target(dsp::BlockContext& ctx, std::uint8_t target, std::uint8_t id, float value, unsigned frame) noexcept {
  if (target == kLaneSeq) {  // the sequencer's own values: a motion lane sets the wobble's rate or depth for as long as its point holds
    if (id == kSeqWobbleRate) {
      const int r = static_cast<int>(value + 0.5f);
      seq_rate_lane_ = r < 0 ? 0 : (r >= static_cast<int>(kWobbleRateCount) ? static_cast<int>(kWobbleRateCount) - 1 : r);
    } else {
      seq_depth_lane_ = value > 0.0f ? (value < 1.0f ? value : 1.0f) : 0.0f;
    }
    return true;
  }
  if (wob_active_ && target == wob_target_ && id == wob_id_) {
    // The wobble owns this target: the write is its new base (a lock, or the knob back), which run_wobble() moves around.
    const unsigned at = wob_base_count_ < kMaxWobbleBase ? wob_base_count_++ : kMaxWobbleBase - 1u;
    wob_base_[at] = BaseEvent{static_cast<std::uint16_t>(frame), value};
    return true;
  }
  if (target == kLaneMixer) {
    return ctx.set_mix(kMixerSlot, static_cast<dsp::MixField>(id), value);
  }
  return emit_to(ctx, tab_[target].unit, dsp::param_change(id, value, static_cast<std::uint16_t>(frame)));
}

// A lock on a target ended: the knob's value comes back, or the slew that is gliding there takes it from where it has got to.
void Sequencer::restore_target(dsp::BlockContext& ctx, std::uint8_t target, std::uint8_t id, unsigned frame) noexcept {
  if (target == kLaneSeq) {  // the lane's hold ends: set_wobble()'s rate and depth apply again
    if (id == kSeqWobbleRate) {
      seq_rate_lane_ = -1;
    } else {
      seq_depth_lane_ = -1.0f;
    }
    return;
  }
  if (Slew* s = find_slew(target, id)) {
    if (!s->mixer) {  // a mixer slew writes at the end of the block, after this
      const float v = slew_value(*s, s->pos + frame);
      if (write_target(ctx, target, id, v, frame)) {
        s->last = v;
        s->emitted = true;
      }
    }
    return;
  }
  (void)write_target(ctx, target, id, knob_value(target, id), frame);
}

void Sequencer::restore_lanes(dsp::BlockContext& ctx, unsigned frame) noexcept {
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    if ((overridden_ >> lane & 1u) != 0u) {
      restore_target(ctx, lane_target_[lane], lane_id_[lane], frame);
      mark_lock(lane_target_[lane], lane_id_[lane], frame, false);
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
  hit_n_ = 0;
  hit_k_ = 0;
  fill_on_ = false;
  draw_valid_ = false;
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
  hit_n_ = 0;
  hit_k_ = 0;
  fill_on_ = false;
  off_due_ = false;
  held_ = -1;
  prev_slide_ = false;
  jump_to_ = -1;
  chain_next_ = 0;
  from_chain_ = false;
  chain_dirty_ = true;
  load_pattern(bank_.selected());  // also the first pass
  step_tick_ = (t.pos + 0xFFFFFFFFull) >> 32;  // the first whole tick the transport has not passed
  wob_phase_ = 0;  // the wobble's cycles are counted from the transport's start
  wob_held_ = wob_rng_.next_float() * 2.0f - 1.0f;
}

void Sequencer::emit_points(dsp::BlockContext& ctx, unsigned frame, unsigned quarter) noexcept {
  if (!shadow_motion_ && overridden_ == 0u) {
    return;
  }
  const Pattern& pat = shadow_[cur_];  // the lanes and the motion are the stored pattern's, whatever the fill plays
  const unsigned at = static_cast<unsigned>(cur_step_) * kMotionPointsPerStep + quarter;
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    const std::uint8_t point = pat.motion[lane][at];
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << lane);
    const LaneRef ref = pat.lanes[lane];
    if ((overridden_ & bit) != 0u && (point == kNoLock || ref.target != lane_target_[lane] || ref.id != lane_id_[lane])) {
      // The lock ended (or the lane drives another parameter now): the old target gets its knob's value back.
      restore_target(ctx, lane_target_[lane], lane_id_[lane], frame);
      mark_lock(lane_target_[lane], lane_id_[lane], frame, false);
      overridden_ = static_cast<std::uint8_t>(overridden_ & ~bit);
    }
    if (point == kNoLock || !resolvable(ref.target, ref.id)) {
      continue;
    }
    const float value = ref.target == kLaneMixer ? mixer_point_to_value(ref.id, point) : point_to_value(*desc_of(ref.target, ref.id), point);
    if ((overridden_ & bit) == 0u || value != last_value_[lane]) {
      if ((overridden_ & bit) == 0u) {
        mark_lock(ref.target, ref.id, frame, true);
      }
      (void)write_target(ctx, ref.target, ref.id, value, frame);
      last_value_[lane] = value;
      lane_target_[lane] = ref.target;
      lane_id_[lane] = ref.id;
      overridden_ = static_cast<std::uint8_t>(overridden_ | bit);
    }
  }
}

// ---- Conditions and the fill ----------------------------------------------------------------------------------------------------

// Does the condition of step `step`, on pass `pass` of the pattern, pass? A probability draws 0..99 from 24 bits (c.a of 100 pass, in the long
// run), once for a step: `ahead` is the tie look-ahead of the step before, which draws and keeps the draw for the step's own start.
bool Sequencer::cond_passes(std::uint8_t cond, unsigned step, std::uint32_t pass, bool ahead) noexcept {
  const Cond c = decode_cond(cond);
  switch (c.kind) {
    case CondKind::Always:
      return true;
    case CondKind::Probability: {
      unsigned draw;
      if (draw_valid_ && draw_step_ == step && draw_pass_ == pass) {
        draw = draw_value_;
        draw_valid_ = ahead;  // the step's own start takes the draw it was kept for
      } else {
        draw = ((rng_.next() >> 8) * 100u) >> 24;
        if (ahead) {
          draw_valid_ = true;
          draw_step_ = step;
          draw_pass_ = pass;
          draw_value_ = draw;
        }
      }
      return draw < c.a;
    }
    case CondKind::Ratio:
      return pass % c.b == static_cast<std::uint32_t>(c.a) - 1u;
    case CondKind::Fill:
      return fill_on_;
    case CondKind::NotFill:
      return !fill_on_;
    case CondKind::First:
      return pass == 0u;
    case CondKind::NotFirst:
      return pass != 0u;
  }
  return true;
}

// Is variant_ the fill of the pattern now playing, as the current choice asks? (Mutate differs from one fill to the next.)
bool Sequencer::variant_current() const noexcept {
  const std::uint8_t kind = fill_variant_.load(std::memory_order_relaxed);
  return variant_valid_ && variant_pattern_ == playing_ && variant_version_ == shadow_version_ && variant_kind_ == kind &&
         variant_fill_ == (kind == static_cast<std::uint8_t>(FillVariant::Mutate) ? fill_count_ : 0u);
}

void Sequencer::build_variant() noexcept {
  const std::uint8_t raw = fill_variant_.load(std::memory_order_relaxed);
  const std::uint8_t kind = raw < kFillVariantCount ? raw : std::uint8_t{0};
  const bool mutate = kind == static_cast<std::uint8_t>(FillVariant::Mutate);
  variant_ = shadow_[cur_];  // a copy: the Bank's pattern is never touched
  apply_fill(variant_, static_cast<FillVariant>(kind), seed_.load(std::memory_order_relaxed) ^ (fill_count_ * 0x9e3779b9u));
  variant_valid_ = true;
  variant_pattern_ = playing_;
  variant_version_ = shadow_version_;
  variant_kind_ = kind;
  variant_fill_ = mutate ? fill_count_ : 0u;
}

// ---- One step -------------------------------------------------------------------------------------------------------------------

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
    ++pass_;         // the pattern starts over ...
    advance_chain();  // ... unless the chain loads another one, whose first pass this is
  }

  // FILL is looked at here and nowhere else: the variant starts and ends on a step boundary.
  const std::uint8_t perf = perf_.load(std::memory_order_relaxed);
  const bool fill = (perf & kPerfFill) != 0u;
  if (fill && !fill_on_) {
    ++fill_count_;
  }
  fill_on_ = fill;
  if (fill_on_ && !variant_current()) {
    build_variant();
  }
  const Pattern& pat = fill_on_ ? variant_ : shadow_[cur_];
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

  cur_step_ = static_cast<int>(j);
  cur_point_ = 0;
  cur_start_ = start;
  cur_len_ = len;
  step_tick_ = following;
  pt_q_ = 1;
  pt_tick_ = start + len / kMotionPointsPerStep;
  next_step_ = j + 1u;
  hit_n_ = 0;  // the hits of the step before all came before this tick
  hit_k_ = 0;

  // The locks first: the note then starts with them.
  emit_points(ctx, frame, 0);

  const bool mute = (perf & kPerfMute) != 0u;
  const bool slide = (s.flags & kStepSlide) != 0u || (perf & kPerfSlideAll) != 0u;
  if (draw_valid_ && (draw_step_ != j || draw_pass_ != pass_)) {
    draw_valid_ = false;  // a draw kept for a step that did not come (the look-ahead of a step that was cut short)
  }
  bool rest = (s.flags & kStepRest) != 0u || mute;
  if (!rest && s.cond != kCondAlways && !cond_passes(s.cond, j, pass_, false)) {
    rest = true;  // the trig condition did not pass: the step does not play
  }
  const bool tie = (s.flags & kStepTie) != 0u && !rest;

  if (rest) {
    release_held(ctx, frame);
    prev_slide_ = false;
    return;
  }

  // Does the note go on into the next step (a slide, or a tie behind it that plays)? The look-ahead stays in this pattern; a slide
  // holds the note whatever follows, so it asks nothing. A tie step with a condition is asked now (the next pass's, past the end of
  // the pattern), and its draw is kept for its own start.
  bool tie_follows = false;
  if (!slide) {
    const unsigned after = j + 1u < n ? j + 1u : 0u;
    const Step& next = pat.steps[after];
    if ((next.flags & kStepTie) != 0u && (next.flags & kStepRest) == 0u && !mute) {
      tie_follows = next.cond == kCondAlways || cond_passes(next.cond, after, after == 0u ? pass_ + 1u : pass_, true);
    }
  }
  const bool hold = slide || tie_follows;
  if (tie) {
    if (held_ < 0) {
      prev_slide_ = false;  // nothing to go on with: silent
      return;
    }
    off_due_ = !hold;
    off_tick_ = start + (len * gate_pct + 50u) / 100u;
    prev_slide_ = slide;
    return;
  }

  // The hits: hit k starts at start + k * len / hits and lasts to the start of hit k + 1 (the step's end for the last).
  const unsigned hits = ratchet_hits(s.flags);
  const auto part0 = static_cast<std::uint32_t>(hits > 1u ? len / hits : len);
  const std::uint32_t gate_ticks = (part0 * gate_pct + 50u) / 100u;

  const int transposed = static_cast<int>(s.note) + static_cast<int>(pat.transpose) + live_transpose_.load(std::memory_order_relaxed);
  const int note = transposed < 0 ? 0 : (transposed > static_cast<int>(kMaxNote) ? static_cast<int>(kMaxNote) : transposed);
  const bool acc_all = (perf & kPerfAccAll) != 0u;
  const bool accent = (s.flags & kStepAccent) != 0u || acc_all;
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
  note_started(ctx, frame, note8);
  const bool last = hits == 1u;
  off_due_ = !(last && hold);  // only the last hit of a sliding step (or one with a tie behind it) is held into the next step
  off_tick_ = start + gate_ticks;
  prev_slide_ = last && slide;
  if (!last) {
    hit_n_ = hits;
    hit_k_ = 1;
    hit_tick_ = start + len / hits;
    hit_note_ = note8;
    hit_velocity_ = acc_all ? kVelocityAccent : kVelocityNormal;  // the step's own accent was on the first hit
    hit_note_flags_ = acc_all ? dsp::kNoteAccent : std::uint8_t{0};
    hit_gate_ = static_cast<std::uint8_t>(gate_pct);
    hit_hold_ = hold;
    hit_slide_ = slide;
  }
}

// The next hit of a ratcheted step: a plain note-on (the earlier hit's gate ended at or before this tick; a hit never glides), then
// the gate of its part of the step. The last hit takes over the step's hold and slide.
void Sequencer::fire_hit(dsp::BlockContext& ctx, unsigned frame) noexcept {
  const unsigned k = hit_k_;
  const auto f = static_cast<std::uint16_t>(frame);
  if (held_ >= 0) {
    (void)emit(ctx, dsp::note_off(static_cast<std::uint8_t>(held_), f));
  }
  (void)emit(ctx, dsp::note_on(hit_note_, hit_velocity_, hit_note_flags_, f));
  held_ = hit_note_;
  note_started(ctx, frame, hit_note_);
  const std::uint64_t begin = cur_start_ + (static_cast<std::uint64_t>(k) * cur_len_) / hit_n_;
  const std::uint64_t end = cur_start_ + (static_cast<std::uint64_t>(k + 1u) * cur_len_) / hit_n_;
  const auto part = static_cast<std::uint32_t>(end - begin);
  const bool last = k + 1u >= hit_n_;
  off_due_ = !(last && hit_hold_);
  off_tick_ = begin + (part * hit_gate_ + 50u) / 100u;
  if (last) {
    prev_slide_ = hit_slide_;
    hit_k_ = hit_n_;
  } else {
    hit_k_ = k + 1u;
    hit_tick_ = end;
  }
}

// ---- Slews (render) -------------------------------------------------------------------------------------------------------------

Sequencer::Slew* Sequencer::find_slew(std::uint8_t target, std::uint8_t id) noexcept {
  for (auto& s : slews_) {
    if (s.active && s.target == target && s.id == id) {
      return &s;
    }
  }
  return nullptr;
}

// Where a slew stands `pos` samples into its time.
float Sequencer::slew_value(const Slew& s, std::uint32_t pos) const noexcept {
  if (s.snap || pos >= s.total) {
    return s.goal;
  }
  const float x = s.a + (s.b - s.a) * (static_cast<float>(pos) * s.inv_total);
  return s.mixer ? x : core::param_denorm(*s.desc, x);
}

void Sequencer::start_slew(dsp::BlockContext& ctx, const SlewCommand& c) noexcept {
  if (!resolvable(c.target, c.id)) {
    return;
  }
  Slew* s = find_slew(c.target, c.id);
  float from = c.from;
  if (s != nullptr) {
    from = slew_value(*s, s->pos);  // a slew still gliding: the new one goes on from where it has got to
  } else {
    for (auto& slot : slews_) {
      if (!slot.active) {
        s = &slot;
        break;
      }
    }
    if (s == nullptr) {
      // No room: the target jumps to its goal now, so it ends where it should even when the main loop did not write it (store = false).
      slew_overflow_.fetch_add(1u, std::memory_order_relaxed);
      (void)write_target(ctx, c.target, c.id, c.to, 0);
      return;
    }
  }
  const bool mixer = c.target == kLaneMixer;
  const core::ParamDesc* d = mixer ? nullptr : desc_of(c.target, c.id);
  *s = Slew{};
  s->active = true;
  s->mixer = mixer;
  s->snap = d != nullptr && d->unit == core::ParamUnit::Enum;
  s->target = c.target;
  s->id = c.id;
  s->desc = d;
  s->goal = c.to;
  s->a = mixer ? from : core::param_norm(*d, from);
  s->b = mixer ? c.to : core::param_norm(*d, c.to);
  s->total = s->snap ? 0u : (c.frames < 1u ? 1u : c.frames);
  s->inv_total = s->total != 0u ? 1.0f / static_cast<float>(s->total) : 0.0f;
}

// A lane's lock is on this target right now: the lock wins, the slew only counts the time.
bool Sequencer::slew_locked(const Slew& s) const noexcept {
  for (unsigned lane = 0; lane < kMotionLanes && overridden_ != 0u; ++lane) {
    if ((overridden_ >> lane & 1u) != 0u && lane_target_[lane] == s.target && lane_id_[lane] == s.id) {
      return true;
    }
  }
  return false;
}

void Sequencer::mark_lock(std::uint8_t target, std::uint8_t id, unsigned frame, bool on) noexcept {
  const auto f = static_cast<std::uint16_t>(frame);
  if (on) {
    if (mark_count_ < kMaxMarks) {
      marks_[mark_count_++] = LockMark{target, id, f, kOpen};
    }
    return;
  }
  for (unsigned i = mark_count_; i > 0u; --i) {
    LockMark& m = marks_[i - 1u];
    if (m.target == target && m.id == id && m.to == kOpen) {
      m.to = f;  // a lock that began in this block ends in it
      return;
    }
  }
  if (mark_count_ < kMaxMarks) {
    marks_[mark_count_++] = LockMark{target, id, 0, f};  // one that began before this block
  }
}

// Is the slew's target locked at `frame` of this block? The marks say where a lock began or ended in the block; with none for the
// target the state is the one the block ended in.
bool Sequencer::slew_locked_at(const Slew& s, unsigned frame) const noexcept {
  bool marked = false;
  for (unsigned i = 0; i < mark_count_; ++i) {
    const LockMark& m = marks_[i];
    if (m.target == s.target && m.id == s.id) {
      marked = true;
      if (frame >= m.from && (m.to == kOpen || frame < m.to)) {
        return true;
      }
    }
  }
  return marked ? false : slew_locked(s);
}

// Once per block, after the sequencer (stopped or playing). A parameter slew: an event at frame 0 and one every 16 frames (fewer when
// many slews share a unit's list), the goal at the frame its time runs out. A mixer slew: one write, the value at the block's end.
void Sequencer::run_slews(dsp::BlockContext& ctx) noexcept {
  const std::uint32_t frames = ctx.transport().frames;
  unsigned active = 0;
  unsigned per_unit[kLaneTargetCount] = {};
  for (const Slew& s : slews_) {
    if (s.active) {
      ++active;
      per_unit[s.mixer ? 0u : s.target] += 1u;
    }
  }
  if (active == 0u || frames == 0u) {
    slewing_.store(active, std::memory_order_relaxed);
    return;
  }
  // Ticks per slew in this block: 4 (every 16 frames) while the unit's budget holds them all, else 2 or 1.
  unsigned ticks[kLaneTargetCount];
  unsigned budget[kLaneTargetCount];
  for (unsigned t = 0; t < kLaneTargetCount; ++t) {
    const unsigned n = per_unit[t];
    ticks[t] = n * 4u <= kSlewEvents ? 4u : (n * 2u <= kSlewEvents ? 2u : 1u);
    budget[t] = kSlewEvents + kSlewExtraEvents;
  }

  for (unsigned i = 0; i < kMaxSlews; ++i) {
    Slew& s = slews_[(slew_rr_ + i) % kMaxSlews];
    if (!s.active) {
      continue;
    }
    const std::uint32_t remaining = s.pos < s.total ? s.total - s.pos : 0u;  // samples to the end, from the start of this block
    const bool ends_here = remaining < frames;

    if (s.mixer) {
      const bool locked = slew_locked(s) || wob_owns(s);
      // The mixer ramps across the block to its target, so the value that belongs at the block's end is the one to write.
      if (!locked) {
        const bool final = ends_here;
        const float v = final ? s.goal : slew_value(s, s.pos + frames);
        if (ctx.set_mix(kMixerSlot, static_cast<dsp::MixField>(s.id), v)) {
          s.last = v;
          s.emitted = true;
          if (final) {
            s.active = false;
            continue;
          }
        }
      }
      s.pos = (s.pos + frames < s.total) ? s.pos + frames : s.total;
      continue;
    }

    bool done = false;
    const dsp::UnitId unit = tab_[s.target].unit;
    const unsigned t = s.target;
    const unsigned stride = 64u / ticks[t];
    for (unsigned k = 0; k < ticks[t]; ++k) {
      const unsigned f = k * stride;
      if (f >= frames || f >= remaining) {
        break;  // past the block, or past the end of the slew (the goal below)
      }
      if (budget[t] <= kSlewExtraEvents) {
        break;  // the rest of the unit's share is for the slews' last events
      }
      if (slew_locked_at(s, f) || wob_owns(s)) {
        continue;
      }
      const float v = slew_value(s, s.pos + f);
      if (k != 0u && s.emitted && v == s.last) {
        continue;  // unchanged since the last event (frame 0 always speaks: the slew owns its target while it runs)
      }
      if (!emit_to(ctx, unit, dsp::param_change(s.id, v, static_cast<std::uint16_t>(f)))) {
        break;  // the unit's list is full: the next block goes on
      }
      --budget[t];
      s.last = v;
      s.emitted = true;
    }
    if (ends_here && budget[t] > 0u && !slew_locked_at(s, remaining) && !wob_owns(s)) {
      // The goal, at the frame the time runs out. A slew that cannot say it now (a full share, a full list, a lock) says it at frame 0
      // of the next block.
      if (emit_to(ctx, unit, dsp::param_change(s.id, s.goal, static_cast<std::uint16_t>(remaining)))) {
        --budget[t];
        s.last = s.goal;
        s.emitted = true;
        done = true;
      }
    }
    if (done) {
      s.active = false;
    } else {
      s.pos = (s.pos + frames < s.total) ? s.pos + frames : s.total;
    }
  }
  slew_rr_ = (slew_rr_ + 1u) % kMaxSlews;
  unsigned left = 0;
  for (const Slew& s : slews_) {
    left += s.active ? 1u : 0u;
  }
  slewing_.store(left, std::memory_order_relaxed);
}

// ---- Wobble (main loop) ----------------------------------------------------------------------------------------------------------

bool Sequencer::wobbleable(std::uint8_t target, std::uint8_t id) const noexcept {
  if (target == kLaneNone || target == kLaneSeq || !resolvable(target, id)) {
    return false;
  }
  if (target == kLaneMixer) {
    return id != kMixerMute;
  }
  return desc_of(target, id)->unit != core::ParamUnit::Enum;
}

bool Sequencer::set_wobble(bool on, unsigned rate, unsigned shape, float depth, LaneRef target) noexcept {
  const std::uint32_t old = wobble_.load(std::memory_order_relaxed);
  if (!on) {
    wobble_.store(old & ~kWobOn, std::memory_order_relaxed);
    return true;
  }
  if (rate >= kWobbleRateCount || shape >= kWobbleShapeCount || !wobbleable(target.target, target.id)) {
    return false;
  }
  wobble_depth_.store(depth > 0.0f ? (depth < 1.0f ? depth : 1.0f) : 0.0f, std::memory_order_relaxed);  // depth first: the word publishes it
  const std::uint32_t w = (old & (kWobReset | kWobOverMask)) | kWobOn | (rate << kWobRateShift) | (shape << kWobShapeShift) |
                          (static_cast<std::uint32_t>(target.target) << kWobTargetShift) | (static_cast<std::uint32_t>(target.id) << kWobIdShift);
  wobble_.store(w, std::memory_order_relaxed);
  return true;
}

void Sequencer::set_wobble_reset(bool reset_on_note) noexcept {
  const std::uint32_t old = wobble_.load(std::memory_order_relaxed);
  wobble_.store(reset_on_note ? (old | kWobReset) : (old & ~kWobReset), std::memory_order_relaxed);
}

void Sequencer::wobble_rate_override(int rate) noexcept {
  const std::uint32_t old = wobble_.load(std::memory_order_relaxed) & ~kWobOverMask;
  const std::uint32_t v = (rate >= 0 && rate < static_cast<int>(kWobbleRateCount)) ? static_cast<std::uint32_t>(rate) + 1u : 0u;
  wobble_.store(old | (v << kWobOverShift), std::memory_order_relaxed);
}

Wobble Sequencer::wobble() const noexcept {
  const std::uint32_t w = wobble_.load(std::memory_order_relaxed);
  Wobble o;
  o.on = (w & kWobOn) != 0u;
  o.reset_on_note = (w & kWobReset) != 0u;
  o.rate = (w >> kWobRateShift) & 15u;
  o.shape = (w >> kWobShapeShift) & 7u;
  o.depth = wobble_depth_.load(std::memory_order_relaxed);
  o.target = LaneRef{static_cast<std::uint8_t>((w >> kWobTargetShift) & 7u), static_cast<std::uint8_t>((w >> kWobIdShift) & 31u)};
  o.override_rate = static_cast<int>((w >> kWobOverShift) & 31u) - 1;
  return o;
}

// ---- Wobble and the growl's note (render) -------------------------------------------------------------------------------------------

// A note started at `frame`: the growl follows it (one Param event), and the wobble restarts with it when it is told to.
void Sequencer::note_started(dsp::BlockContext& ctx, unsigned frame, std::uint8_t note) noexcept {
  const dsp::UnitId growl = tab_[kLaneGrowl].unit;
  if (growl != dsp::kNoUnit) {
    (void)emit_to(ctx, growl, dsp::param_change(kGrowlNoteParam, static_cast<float>(note), static_cast<std::uint16_t>(frame)));
  }
  if ((wob_cfg_ & (kWobOn | kWobReset)) == (kWobOn | kWobReset) && wob_reset_count_ < kMaxWobbleResets) {
    wob_resets_[wob_reset_count_++] = static_cast<std::uint16_t>(frame);
  }
}

// The value a lane lock holds on (target, id) now, if one does.
bool Sequencer::lane_lock_value(std::uint8_t target, std::uint8_t id, float& value) const noexcept {
  for (unsigned lane = 0; lane < kMotionLanes && overridden_ != 0u; ++lane) {
    if ((overridden_ >> lane & 1u) != 0u && lane_target_[lane] == target && lane_id_[lane] == id) {
      value = last_value_[lane];
      return true;
    }
  }
  return false;
}

// `base` moved by `delta` (a fraction of the target's range, signed) through the target's own curve, held inside the range.
float Sequencer::wobble_value(std::uint8_t target, std::uint8_t id, float base, float delta) const noexcept {
  if (delta == 0.0f) {
    return base;
  }
  if (target == kLaneMixer) {
    const LaneRange r = kMixerRange[id];
    const float n = (base - r.min) / (r.max - r.min) + delta;
    return r.min + (n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n)) * (r.max - r.min);
  }
  const core::ParamDesc* d = desc_of(target, id);
  if (d == nullptr) {
    return base;
  }
  const float n = core::param_norm(*d, base) + delta;
  return core::param_denorm(*d, n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n));
}

// Once per block, after the sequencer and before the slews. Param events toward the target at frames 0, 16, 32 ... of the block and at the
// frame of every note that restarts the phase; a mixer field gets one write, the value at the block's end.
void Sequencer::run_wobble(dsp::BlockContext& ctx) noexcept {
  const dsp::Transport& t = ctx.transport();
  const std::uint32_t frames = t.frames;
  const std::uint32_t cfg = wob_cfg_;
  const auto tg = static_cast<std::uint8_t>((cfg >> kWobTargetShift) & 7u);
  const auto id = static_cast<std::uint8_t>((cfg >> kWobIdShift) & 31u);
  const bool want = (cfg & kWobOn) != 0u && frames != 0u && wobbleable(tg, id);

  // A wobble that stopped or moved: its target goes back to its base (the lock, the slew or the knob), then it is the wobble's no more.
  if (wob_active_ && (!want || tg != wob_target_ || id != wob_id_)) {
    const std::uint8_t old_t = wob_target_;
    const std::uint8_t old_id = wob_id_;
    wob_active_ = false;
    float base = 0.0f;
    if (!lane_lock_value(old_t, old_id, base)) {
      const Slew* s = find_slew(old_t, old_id);
      base = s != nullptr ? slew_value(*s, s->pos) : knob_value(old_t, old_id);
    }
    (void)write_target(ctx, old_t, old_id, base, 0);
  }
  if (!want) {
    wob_active_ = false;
    wob_reset_count_ = 0;
    wob_base_count_ = 0;
    wob_rate_now_.store(-1, std::memory_order_relaxed);
    return;
  }

  // The rate: the pad's override, else the lane's, else the setting. The phase carries on across a change of rate.
  const int over = static_cast<int>((cfg >> kWobOverShift) & 31u) - 1;
  unsigned rate = (cfg >> kWobRateShift) & 15u;
  if (over >= 0) {
    rate = static_cast<unsigned>(over);
  } else if (seq_rate_lane_ >= 0) {
    rate = static_cast<unsigned>(seq_rate_lane_);
  }
  rate = rate < kWobbleRateCount ? rate : kWobbleRateCount - 1u;
  const unsigned shape = (cfg >> kWobShapeShift) & 7u;
  const float depth = seq_depth_lane_ >= 0.0f ? seq_depth_lane_ : wobble_depth_.load(std::memory_order_relaxed);
  const float amplitude = 0.5f * depth;
  if (t.inc != wob_inc_tick_ || rate != wob_inc_rate_) {
    const std::uint64_t inc = t.inc < (1ull << 31) ? t.inc : (1ull << 31);  // 300 BPM is about 2^29: the shift below cannot overflow
    wob_inc_ = (inc << 32) / kWobbleTicks[rate];                              // Q0.64 turns a frame
    wob_inc_tick_ = t.inc;
    wob_inc_rate_ = rate;
  }
  const std::uint64_t inc = wob_inc_;
  wob_rate_now_.store(static_cast<int>(rate), std::memory_order_relaxed);

  const bool mixer = tg == kLaneMixer;
  const dsp::UnitId unit = mixer ? dsp::kNoUnit : tab_[tg].unit;
  const Slew* slew = find_slew(tg, id);
  const auto live = [&](unsigned frame) {
    return slew != nullptr ? slew_value(*slew, slew->pos + frame) : knob_value(tg, id);
  };
  const auto draw = [&]() { wob_held_ = wob_rng_.next_float() * 2.0f - 1.0f; };
  if (!wob_active_) {
    // Switched on: the cycle is where the transport's is (a free one from 0 while stopped), so the wobble is on the grid from the start.
    wob_active_ = true;
    wob_target_ = tg;
    wob_id_ = id;
    wob_emitted_ = false;
    wob_hold_valid_ = lane_lock_value(tg, id, wob_hold_);
    const std::uint64_t period = kWobbleTicks[rate];
    wob_phase_ = t.playing ? ((t.pos % (period << 32)) / period) << 32 : 0u;
    draw();
  }

  std::uint64_t ph = wob_phase_;  // the phase at frame `seg`
  unsigned seg = 0;
  std::uint64_t prev = wob_phase_;  // at the last point looked at: a smaller one now is a wrap, the start of a cycle
  bool base_valid = wob_hold_valid_;
  float base_now = wob_hold_;
  unsigned bi = 0;
  unsigned ri = 0;
  unsigned tick = 0;
  for (;;) {
    const unsigned f_tick = tick < frames ? tick : ~0u;
    const unsigned f_reset = ri < wob_reset_count_ ? wob_resets_[ri] : ~0u;
    if (f_tick == ~0u && f_reset == ~0u) {
      break;
    }
    const unsigned f = f_tick < f_reset ? f_tick : f_reset;
    bool restarted = false;
    while (ri < wob_reset_count_ && wob_resets_[ri] <= f) {
      restarted = true;
      ++ri;
    }
    if (restarted) {
      ph = 0;
      seg = f;
      prev = 0;
      draw();
    }
    const std::uint64_t at = ph + inc * (f - seg);
    if (!restarted && at < prev) {
      draw();
    }
    prev = at;
    if (f_tick == f) {
      tick += kWobbleTick;
    }
    if (mixer) {
      continue;  // a mixer field is written once, below
    }
    while (bi < wob_base_count_ && wob_base_[bi].frame <= f) {
      base_now = wob_base_[bi].value;
      base_valid = true;
      ++bi;
    }
    const float base = base_valid ? base_now : live(f);
    const float v = wobble_value(tg, id, base, amplitude * wobble_wave(shape, static_cast<std::uint32_t>(at >> 32), wob_held_));
    if (f == 0u || !wob_emitted_ || v != wob_last_) {  // frame 0 always speaks: the wobble owns its target, the main loop's write must not win
      (void)emit_to(ctx, unit, dsp::param_change(id, v, static_cast<std::uint16_t>(f)));
    }
    wob_last_ = v;
    wob_emitted_ = true;
  }
  const std::uint64_t end = ph + inc * (frames - seg);
  if (end < prev) {
    draw();
  }
  if (mixer) {
    while (bi < wob_base_count_) {
      base_now = wob_base_[bi++].value;
      base_valid = true;
    }
    const float base = base_valid ? base_now : live(frames);
    (void)ctx.set_mix(kMixerSlot, static_cast<dsp::MixField>(id),
                      wobble_value(tg, id, base, amplitude * wobble_wave(shape, static_cast<std::uint32_t>(end >> 32), wob_held_)));
  }
  wob_phase_ = end;

  // The base carries on into the next block when a lane lock holds it.
  float held = 0.0f;
  wob_hold_valid_ = lane_lock_value(tg, id, held);
  if (wob_hold_valid_) {
    wob_hold_ = held;
  }
  wob_reset_count_ = 0;
  wob_base_count_ = 0;
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
  h.fill = running_ && fill_on_;
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
  mark_count_ = 0;
  wob_cfg_ = wobble_.load(std::memory_order_relaxed);
  drain_commands(ctx);
  sequence(ctx);
  run_wobble(ctx);  // before the slews: a slew on the wobbled target only counts its time, and the wobble reads where it stands
  run_slews(ctx);
}

void Sequencer::sequence(dsp::BlockContext& ctx) noexcept {
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
    // The earliest thing due: the gate end of the held note, a motion quarter, a ratchet hit, or the next step. At the same tick the gate
    // end goes first (so a full-length gate does not glide into the next note), then a quarter (its lock lands before the hit), then
    // the hit; a quarter or a hit cannot tie with a step: they are inside their step.
    enum class Due : std::uint8_t { Step, Hit, Point, Off };
    Due due = Due::Step;
    std::uint64_t tick = step_tick_;
    if (hit_k_ < hit_n_ && hit_tick_ <= tick) {
      due = Due::Hit;
      tick = hit_tick_;
    }
    if (pt_q_ < kMotionPointsPerStep && pt_tick_ <= tick) {  // a lock lands before a hit at the same tick
      due = Due::Point;
      tick = pt_tick_;
    }
    if (off_due_ && off_tick_ <= tick) {
      due = Due::Off;
      tick = off_tick_;
    }
    const int f = frame_at_or_past(t, tick);
    if (f < 0) {
      break;
    }
    const auto frame = static_cast<unsigned>(f);
    switch (due) {
      case Due::Off:
        release_held(ctx, frame);
        break;
      case Due::Hit:
        fire_hit(ctx, frame);
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
