// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_303/seq/bank.h>

#include <o3sdk/core/placement.h>

#include <cstring>

namespace o3_303::seq {

namespace {

// Coalescing keys: a kind in the top byte, what the gesture is about below.
constexpr std::uint32_t kKeyNote = 0x01000000u;
constexpr std::uint32_t kKeyLock = 0x02000000u;
constexpr std::uint32_t kKeySwing = 0x03000000u;
constexpr std::uint32_t kKeyGate = 0x04000000u;
constexpr std::uint32_t kKeyTranspose = 0x05000000u;
constexpr std::uint32_t kKeyRatchet = 0x08000000u;  // (0x07 is the sequencer's motion recording)
constexpr std::uint32_t kKeyCond = 0x09000000u;
constexpr std::uint32_t kKeyGateList = 0x0A000000u;
constexpr std::uint32_t kKeyPlays = 0x0B000000u;
constexpr std::uint32_t kKeyLockTarget = 0x0C000000u;  // a lock named by (target, id) rather than by a lane: the lane is claimed in the edit

// Word accesses, not bytes: the patterns are 4-byte aligned and a multiple of 4 long (bank.h), and a write window over the SDRAM stays
// as short as it can. (memcpy of one word compiles to a load and a store.)
static_assert(sizeof(Pattern) % 4 == 0 && alignof(History::Entry) >= 4, "the patterns move in words");

void swap_words(void* a, void* b, std::size_t n) noexcept {
  auto* x = static_cast<std::uint8_t*>(a);
  auto* y = static_cast<std::uint8_t*>(b);
  for (std::size_t i = 0; i < n; i += 4) {
    std::uint32_t u;
    std::uint32_t v;
    std::memcpy(&u, x + i, 4);
    std::memcpy(&v, y + i, 4);
    std::memcpy(x + i, &v, 4);
    std::memcpy(y + i, &u, 4);
  }
}

void copy_words(void* dst, const void* src, std::size_t n) noexcept {
  auto* d = static_cast<std::uint8_t*>(dst);
  const auto* s = static_cast<const std::uint8_t*>(src);
  for (std::size_t i = 0; i < n; i += 4) {
    std::uint32_t w;
    std::memcpy(&w, s + i, 4);
    std::memcpy(d + i, &w, 4);
  }
}

// Does any motion point hold a lock? (Each lane is 256 bytes, read as 64 words.)
bool any_motion(const Pattern& p) noexcept {
  static_assert(sizeof(p.motion[0]) % 4 == 0, "a lane is scanned in words");
  for (const auto& lane : p.motion) {
    for (std::size_t i = 0; i < sizeof(lane); i += 4) {
      std::uint32_t w;
      std::memcpy(&w, lane + i, 4);
      if (w != 0xFFFFFFFFu) {  // four kNoLock points
        return true;
      }
    }
  }
  return false;
}

// The project's bank: zero-initialised SDRAM (placement.h); init() gives it its content.
O3SDK_BIG Bank g_project_bank;

}  // namespace

Bank& project_bank() noexcept { return g_project_bank; }

void Bank::init() noexcept {
  for (unsigned i = 0; i < kPatternCount; ++i) {
    clear(patterns_[i]);  // the fresh pattern: silent, length 16, default gate, no swing, no motion
    versions_[i].store(0u, std::memory_order_relaxed);
    has_motion_[i].store(0u, std::memory_order_relaxed);
  }
  selected_.store(0u, std::memory_order_relaxed);
  history_.clear();
  std::memset(&step_clip_, 0, sizeof(step_clip_));
  clear(pattern_clip_);
  has_step_clip_ = false;
  has_pattern_clip_ = false;
  std::atomic_thread_fence(std::memory_order_release);
}

void Bank::select(unsigned index) noexcept {
  selected_.store(static_cast<std::uint8_t>(index < kPatternCount ? index : kPatternCount - 1u), std::memory_order_relaxed);
}

void Bank::begin_write(unsigned index) noexcept {
  const std::uint32_t v = versions_[index].load(std::memory_order_relaxed);
  versions_[index].store(v + 1u, std::memory_order_relaxed);  // odd: a write is in progress
  std::atomic_thread_fence(std::memory_order_release);
}

void Bank::end_write(unsigned index) noexcept {
  // Still inside the window: the flag belongs to the contents it describes.
  has_motion_[index].store(any_motion(patterns_[index]) ? 1u : 0u, std::memory_order_relaxed);
  const std::uint32_t v = versions_[index].load(std::memory_order_relaxed);
  versions_[index].store(v + 1u, std::memory_order_release);  // even again
}

bool Bank::snapshot(unsigned index, Pattern& out, std::uint32_t* version_out, bool* has_motion_out) const noexcept {
  if (index >= kPatternCount) {
    index = kPatternCount - 1u;
  }
  const std::uint32_t v1 = versions_[index].load(std::memory_order_acquire);
  if (version_out != nullptr) {
    *version_out = v1;
  }
  if ((v1 & 1u) != 0u) {
    return false;  // a write is going on (this reader preempted it): do not even copy
  }
  copy_words(&out, &patterns_[index], sizeof(Pattern));
  const bool motion = has_motion_[index].load(std::memory_order_relaxed) != 0u;
  std::atomic_thread_fence(std::memory_order_acquire);
  const std::uint32_t v2 = versions_[index].load(std::memory_order_relaxed);
  if (has_motion_out != nullptr) {
    *has_motion_out = motion;
  }
  return v1 == v2;
}

int Bank::undo() noexcept {
  History::Entry* e = history_.take_undo();
  if (e == nullptr) {
    return -1;
  }
  begin_write(e->index);
  swap_words(&patterns_[e->index], &e->state, sizeof(Pattern));
  end_write(e->index);
  return e->index;
}

int Bank::redo() noexcept {
  History::Entry* e = history_.take_redo();
  if (e == nullptr) {
    return -1;
  }
  begin_write(e->index);
  swap_words(&patterns_[e->index], &e->state, sizeof(Pattern));
  end_write(e->index);
  return e->index;
}

// ---- Named edits --------------------------------------------------------------------------------------------------------------

bool Bank::set_note(unsigned index, unsigned step, unsigned note) noexcept {
  return edit(index, [&](Pattern& p) { seq::set_note(p, step, note); }, kKeyNote | (step & 0xFFu));
}

bool Bank::toggle_flag(unsigned index, unsigned step, std::uint8_t flag) noexcept {
  return edit(index, [&](Pattern& p) { seq::toggle_flag(p, step, flag); });
}

bool Bank::set_flag(unsigned index, unsigned step, std::uint8_t flag, bool on) noexcept {
  return edit(index, [&](Pattern& p) { seq::set_flag(p, step, flag, on); });
}

unsigned Bank::write_advance(unsigned index, unsigned step, unsigned note, std::uint8_t flags) noexcept {
  unsigned next = 0;
  (void)edit(index, [&](Pattern& p) { next = seq::write_advance(p, step, note, flags); });
  return next;
}

bool Bank::insert_step(unsigned index, unsigned at) noexcept {
  return edit(index, [&](Pattern& p) { seq::insert_step(p, at); });
}

bool Bank::delete_step(unsigned index, unsigned at) noexcept {
  return edit(index, [&](Pattern& p) { seq::delete_step(p, at); });
}

bool Bank::rotate(unsigned index, int steps) noexcept {
  return edit(index, [&](Pattern& p) { seq::rotate(p, steps); });
}

bool Bank::rotate_layer(unsigned index, Layer layer, int steps) noexcept {
  return edit(index, [&](Pattern& p) { seq::rotate_layer(p, layer, steps); });
}

bool Bank::set_ratchet(unsigned index, unsigned step, unsigned ratchet) noexcept {
  return edit(index, [&](Pattern& p) { seq::set_ratchet(p, step, ratchet); }, kKeyRatchet | (step & 0xFFu));
}

bool Bank::set_cond(unsigned index, unsigned step, std::uint8_t cond) noexcept {
  return edit(index, [&](Pattern& p) { seq::set_cond(p, step, cond); }, kKeyCond | (step & 0xFFu));
}

bool Bank::transpose_notes(unsigned index, int semitones) noexcept {
  return edit(index, [&](Pattern& p) { return seq::transpose_notes(p, semitones); });
}

bool Bank::reverse(unsigned index) noexcept {
  return edit(index, [&](Pattern& p) { seq::reverse(p); });
}

bool Bank::invert(unsigned index) noexcept {
  return edit(index, [&](Pattern& p) { seq::invert(p); });
}

bool Bank::double_length(unsigned index) noexcept {
  return edit(index, [&](Pattern& p) { return seq::double_length(p); });
}

bool Bank::clear_pattern(unsigned index) noexcept {
  return edit(index, [&](Pattern& p) { seq::clear_pattern(p); });
}

bool Bank::set_length(unsigned index, unsigned length) noexcept {
  return edit(index, [&](Pattern& p) { return seq::set_length(p, length); });
}

bool Bank::set_swing(unsigned index, int percent) noexcept {
  return edit(index, [&](Pattern& p) { seq::set_swing(p, percent); }, kKeySwing);
}

bool Bank::set_gate(unsigned index, int percent) noexcept {
  return edit(index, [&](Pattern& p) { seq::set_gate(p, percent); }, kKeyGate);
}

bool Bank::set_transpose(unsigned index, int semitones) noexcept {
  return edit(index, [&](Pattern& p) { seq::set_transpose(p, semitones); }, kKeyTranspose);
}

bool Bank::random_fill(unsigned index, const GenParams& g, std::uint32_t seed) noexcept {
  return edit(index, [&](Pattern& p) { seq::random_fill(p, g, seed); });
}

bool Bank::mutate(unsigned index, const GenParams& g, std::uint32_t seed, unsigned amount_pct) noexcept {
  return edit(index, [&](Pattern& p) { seq::mutate(p, g, seed, amount_pct); });
}

bool Bank::regen_rhythm(unsigned index, const GenParams& g, std::uint32_t seed) noexcept {
  return edit(index, [&](Pattern& p) { return seq::regen_rhythm(p, g, seed); });
}

bool Bank::regen_notes(unsigned index, const GenParams& g, std::uint32_t seed) noexcept {
  return edit(index, [&](Pattern& p) { return seq::regen_notes(p, g, seed); });
}

bool Bank::resize(unsigned index, unsigned length) noexcept {
  return edit(index, [&](Pattern& p) { return seq::resize(p, length); });
}

bool Bank::set_gate_list(unsigned index, unsigned step, unsigned entry) noexcept {
  return edit(index, [&](Pattern& p) { return seq::set_gate_list(p, step, entry); }, kKeyGateList | (step & 0xFFu));
}

bool Bank::set_plays(unsigned index, unsigned step, unsigned entry) noexcept {
  return edit(index, [&](Pattern& p) { return seq::set_plays(p, step, entry); }, kKeyPlays | (step & 0xFFu));
}

int Bank::claim_lane(unsigned index, std::uint8_t target, std::uint8_t id) noexcept {
  int lane = -1;
  edit(index, [&](Pattern& p) {
    const bool existed = seq::find_lane(p, target, id) >= 0;
    lane = seq::claim_lane(p, target, id);
    return lane >= 0 && !existed;  // only a re-pointed lane is a change
  });
  return lane;
}

int Bank::lock_step(unsigned index, std::uint8_t target, std::uint8_t id, unsigned step, std::uint8_t value) noexcept {
  if (value == kNoLock) {
    const int lane = seq::find_lane(pattern(index), target, id);
    if (lane >= 0) {
      unlock_step(index, target, id, step);
    }
    return lane;
  }
  int lane = -1;
  const std::uint32_t key = kKeyLockTarget | ((target & 0xFu) << 16) | (static_cast<std::uint32_t>(id) << 8) | (step & 0xFFu);
  edit(index,
       [&](Pattern& p) {
         lane = seq::claim_lane(p, target, id);
         if (lane < 0) {
           return false;
         }
         seq::set_plock(p, static_cast<unsigned>(lane), step, value);
         return true;
       },
       key);
  return lane;
}

bool Bank::unlock_step(unsigned index, std::uint8_t target, std::uint8_t id, unsigned step) noexcept {
  const int lane = seq::find_lane(pattern(index), target, id);
  if (lane < 0) {
    return false;
  }
  const std::uint32_t key = kKeyLockTarget | ((target & 0xFu) << 16) | (static_cast<std::uint32_t>(id) << 8) | (step & 0xFFu);
  return edit(
      index,
      [&](Pattern& p) {
        bool had = false;  // (a joined edit reports fn's answer, so say what really changed)
        for (unsigned q = 0; q < kMotionPointsPerStep; ++q) {
          had = had || p.motion[lane][(step < kMaxSteps ? step : kMaxSteps - 1u) * kMotionPointsPerStep + q] != kNoLock;
        }
        seq::clear_plock(p, static_cast<unsigned>(lane), step);
        return had;
      },
      key);
}

bool Bank::set_motion_point(unsigned index, unsigned lane, unsigned step, unsigned point, std::uint8_t value) noexcept {
  return edit(index, [&](Pattern& p) { seq::set_motion_point(p, lane, step, point, value); }, kKeyLock | ((lane & 0xFu) << 8) | (step & 0xFFu));
}

bool Bank::set_plock(unsigned index, unsigned lane, unsigned step, std::uint8_t value) noexcept {
  return edit(index, [&](Pattern& p) { seq::set_plock(p, lane, step, value); }, kKeyLock | ((lane & 0xFu) << 8) | (step & 0xFFu));
}

bool Bank::clear_plock(unsigned index, unsigned lane, unsigned step) noexcept {
  return edit(index, [&](Pattern& p) { seq::clear_plock(p, lane, step); });
}

bool Bank::clear_step_motion(unsigned index, unsigned step) noexcept {
  return edit(index, [&](Pattern& p) { seq::clear_step_motion(p, step); });
}

bool Bank::clear_lane_motion(unsigned index, unsigned lane) noexcept {
  return edit(index, [&](Pattern& p) { seq::clear_lane_motion(p, lane); });
}

bool Bank::clear_motion(unsigned index) noexcept {
  return edit(index, [&](Pattern& p) { seq::clear_motion(p); });
}

bool Bank::replace(unsigned index, const Pattern& src) noexcept {
  return edit(index, [&](Pattern& p) { p = src; });
}

bool Bank::restore(unsigned index, const Pattern& src) noexcept {
  if (index >= kPatternCount) {
    return false;
  }
  begin_write(index);
  copy_words(&patterns_[index], &src, sizeof(Pattern));
  end_write(index);
  return true;
}

// ---- Clipboards ---------------------------------------------------------------------------------------------------------------

void Bank::copy_step(unsigned index, unsigned step) noexcept {
  step_clip_ = seq::copy_step(pattern(index), step);
  has_step_clip_ = true;
}

bool Bank::paste_step(unsigned index, unsigned step) noexcept {
  if (!has_step_clip_) {
    return false;
  }
  (void)edit(index, [&](Pattern& p) { seq::paste_step(p, step, step_clip_); });
  return true;
}

void Bank::copy_pattern(unsigned index) noexcept {
  pattern_clip_ = pattern(index);
  has_pattern_clip_ = true;
}

bool Bank::paste_pattern(unsigned index) noexcept {
  if (!has_pattern_clip_) {
    return false;
  }
  (void)edit(index, [&](Pattern& p) { p = pattern_clip_; });
  return true;
}

}  // namespace o3_303::seq
