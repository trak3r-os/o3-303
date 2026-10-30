// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The bank: the project's kPatternCount patterns, the selected one, the undo history and the clipboards. One object, big (about
// 186 KB), so it lives in the SDRAM: the app declares it `O3SDK_BIG` (or uses project_bank() below) and calls init() at setup.
//
// Threads. The main loop is the only writer of the patterns. The render context (the Sequencer's hook, sequencer.h) reads them and
// never takes a lock: every pattern has a version counter used as a seqlock. An edit makes it odd while the pattern changes and
// even again after; the hook keeps its own copy of the pattern it plays and refreshes it when the version moved (a copy that raced a
// write is repeated at the next block). So an edit is heard within a block, and a half-written pattern is never played for longer
// than one block. Patterns are changed only through edit() (or the named wrappers, which call it); pattern() is for reading.
//
// Every edit through edit() is undoable (undo() / redo()), except select() and the clipboards.

#include <o3_303/pattern.h>
#include <o3_303/seq/history.h>
#include <o3_303/seq/ops.h>

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace o3_303::seq {

class Bank {
 public:
  Bank() noexcept = default;  // zero members only: a static / O3SDK_BIG instance is zero already
  Bank(const Bank&) = delete;
  Bank& operator=(const Bank&) = delete;

  // Every pattern becomes o3_303::clear() (the fresh pattern: silent, 16 steps), the history and clipboards are emptied, pattern 0 is
  // selected. Main loop, setup (or with the engine stopped: it is not an undoable edit).
  void init() noexcept;

  // ---- Read (main loop) ---------------------------------------------------------------------------------------------------
  const Pattern& pattern(unsigned index) const noexcept { return patterns_[index < kPatternCount ? index : kPatternCount - 1u]; }

  // The pattern the screens edit, and the one Play starts. select() does not change what is playing (Sequencer::queue / jump do).
  unsigned selected() const noexcept { return selected_.load(std::memory_order_relaxed); }
  void select(unsigned index) noexcept;

  // ---- Edit (main loop) ---------------------------------------------------------------------------------------------------
  // Runs `fn(Pattern&)` on pattern `index` as one undoable edit. `fn` may return void or bool (false: it changed nothing; the edit is
  // then not recorded). `key` != 0 joins consecutive edits with the same key into one undo step (see history.h). Returns true when
  // the pattern changed.
  template <typename Fn>
  bool edit(unsigned index, Fn&& fn, std::uint32_t key = 0) noexcept {
    if (index >= kPatternCount) {
      return false;
    }
    Pattern& p = patterns_[index];
    const bool staged = history_.stage(index, p, key);
    begin_write(index);
    bool changed = true;
    if constexpr (std::is_same<decltype(fn(p)), bool>::value) {
      changed = fn(p);
    } else {
      fn(p);
    }
    end_write(index);
    if (!staged) {
      return changed;
    }
    return history_.commit(p);
  }

  bool can_undo() const noexcept { return history_.can_undo(); }
  bool can_redo() const noexcept { return history_.can_redo(); }
  // The index of the pattern that changed, -1 when there was nothing to undo / redo.
  int undo() noexcept;
  int redo() noexcept;
  // Forgets every undo and redo step; the patterns stay as they are. For the edits that are not the user's (the factory preset loaded
  // at first boot): call it right after them, so the first undo does not undo the setup. Main loop only, never from inside an edit()
  // function. The history is main-loop state that the render never reads (it sees the patterns through their versions), so this
  // needs no handshake with the hook and changes no pattern, version or flag.
  void clear_history() noexcept { history_.clear(); }

  // ---- Named edits (each is one edit() with a coalescing key where it helps) ----------------------------------------------
  bool set_note(unsigned index, unsigned step, unsigned note) noexcept;
  bool toggle_flag(unsigned index, unsigned step, std::uint8_t flag) noexcept;
  bool set_flag(unsigned index, unsigned step, std::uint8_t flag, bool on) noexcept;
  // REC entry; returns the next cursor (0 at the end of the length).
  unsigned write_advance(unsigned index, unsigned step, unsigned note, std::uint8_t flags) noexcept;
  bool insert_step(unsigned index, unsigned at) noexcept;
  bool delete_step(unsigned index, unsigned at) noexcept;
  bool rotate(unsigned index, int steps) noexcept;
  bool rotate_layer(unsigned index, Layer layer, int steps) noexcept;  // one layer only (ops.h)
  // A step's ratchet (0..3) and trig condition: consecutive turns on one step are one undo step. get_* read the stored pattern.
  bool set_ratchet(unsigned index, unsigned step, unsigned ratchet) noexcept;
  bool set_cond(unsigned index, unsigned step, std::uint8_t cond) noexcept;
  unsigned get_ratchet(unsigned index, unsigned step) const noexcept { return seq::get_ratchet(pattern(index), step); }
  std::uint8_t get_cond(unsigned index, unsigned step) const noexcept { return seq::get_cond(pattern(index), step); }
  bool transpose_notes(unsigned index, int semitones) noexcept;  // false: a note would leave 0..127, nothing changed
  bool reverse(unsigned index) noexcept;
  bool invert(unsigned index) noexcept;
  bool double_length(unsigned index) noexcept;
  bool clear_pattern(unsigned index) noexcept;
  bool set_length(unsigned index, unsigned length) noexcept;
  bool set_swing(unsigned index, int percent) noexcept;
  bool set_gate(unsigned index, int percent) noexcept;
  bool set_transpose(unsigned index, int semitones) noexcept;
  bool random_fill(unsigned index, const GenParams& g, std::uint32_t seed) noexcept;
  bool mutate(unsigned index, const GenParams& g, std::uint32_t seed, unsigned amount_pct) noexcept;
  bool set_motion_point(unsigned index, unsigned lane, unsigned step, unsigned point, std::uint8_t value) noexcept;
  bool set_plock(unsigned index, unsigned lane, unsigned step, std::uint8_t value) noexcept;
  bool clear_plock(unsigned index, unsigned lane, unsigned step) noexcept;
  bool clear_step_motion(unsigned index, unsigned step) noexcept;
  bool clear_lane_motion(unsigned index, unsigned lane) noexcept;
  bool clear_motion(unsigned index) noexcept;
  // The UX operations (ops.h), one undoable edit each. regen_rhythm / regen_notes: RHYTHM and NOTES of HOME's Shift row. resize: the LENGTH
  // of PATTERN SETUP (a step up copies the steps and their motion into the new part, a step down truncates; one undo step).
  bool regen_rhythm(unsigned index, const GenParams& g, std::uint32_t seed) noexcept;
  bool regen_notes(unsigned index, const GenParams& g, std::uint32_t seed) noexcept;
  bool resize(unsigned index, unsigned length) noexcept;
  // The step's GATE and PLAYS rows (an entry of the list in ops.h): consecutive turns on one step's row are one undo step.
  bool set_gate_list(unsigned index, unsigned step, unsigned entry) noexcept;
  bool set_plays(unsigned index, unsigned step, unsigned entry) noexcept;
  // D2: the lane that drives (target, id), claiming one if none does (ops.h claim_lane); returns it, -1 when all eight are in use. The
  // claim is one undoable edit (nothing is recorded when the lane already existed).
  int claim_lane(unsigned index, std::uint8_t target, std::uint8_t id) noexcept;
  // The LOCK row: the parameter lock of (target, id) on `step`, its lane claimed first, all in ONE edit (one undo removes both). Returns
  // the lane, -1 when no lane is free (nothing changes). Consecutive turns on one step and parameter are one undo step. A value of kNoLock
  // ("--") is unlock_step() and claims nothing (it returns the lane that drives the target, -1 if none).
  int lock_step(unsigned index, std::uint8_t target, std::uint8_t id, unsigned step, std::uint8_t value) noexcept;
  // Removes the lock of (target, id) on `step`; the lane keeps its target (it is free once its last lock goes). true: a lock was removed.
  bool unlock_step(unsigned index, std::uint8_t target, std::uint8_t id, unsigned step) noexcept;
  // Replaces the pattern with `src` (a preset from the generator, a pasted pattern).
  bool replace(unsigned index, const Pattern& src) noexcept;

  // ---- Clipboards (not undoable) ------------------------------------------------------------------------------------------
  void copy_step(unsigned index, unsigned step) noexcept;
  bool has_step_clip() const noexcept { return has_step_clip_; }
  bool paste_step(unsigned index, unsigned step) noexcept;  // false: nothing copied
  void copy_pattern(unsigned index) noexcept;
  bool has_pattern_clip() const noexcept { return has_pattern_clip_; }
  bool paste_pattern(unsigned index) noexcept;  // false: nothing copied

  // ---- Render side (the Sequencer's hook; no lock, no wait) ---------------------------------------------------------------
  // The version of pattern `index`: odd while the main loop is writing it. Changes with every edit of it.
  std::uint32_t version(unsigned index) const noexcept { return versions_[index].load(std::memory_order_acquire); }
  // Copies pattern `index` into `out` (word by word). true: the copy is consistent (no write overlapped it), `*version_out` is its
  // version and `*has_motion_out` says whether any of its motion points is a lock (the Bank keeps that flag with every edit, so the
  // reader need not scan 2 KB). false: a write overlapped (or is going on): `out` is NOT to be used (it may be torn or untouched);
  // keep the copy you have and try again later. Never waits.
  bool snapshot(unsigned index, Pattern& out, std::uint32_t* version_out, bool* has_motion_out = nullptr) const noexcept;
  // The flag snapshot() reports, for the main loop (a pattern with any p-lock or motion point).
  bool has_motion(unsigned index) const noexcept {
    return has_motion_[index < kPatternCount ? index : kPatternCount - 1u].load(std::memory_order_relaxed) != 0u;
  }

 private:
  void begin_write(unsigned index) noexcept;
  void end_write(unsigned index) noexcept;

  alignas(8) Pattern patterns_[kPatternCount];
  std::atomic<std::uint32_t> versions_[kPatternCount];  // seqlock: odd while patterns_[i] is being written (single writer: the main loop)
  std::atomic<std::uint8_t> has_motion_[kPatternCount];  // 1: patterns_[i] has a motion point; rewritten in every write window, so it is part of the seqlock
  std::atomic<std::uint8_t> selected_;
  History history_;
  StepClip step_clip_;
  alignas(4) Pattern pattern_clip_;
  bool has_step_clip_;
  bool has_pattern_clip_;
};

static_assert(sizeof(Pattern) % 4 == 0, "patterns are copied with word accesses");
static_assert(std::is_trivially_default_constructible<Bank>::value,
              "an O3SDK_BIG Bank is zero-initialised SDRAM that no constructor may touch before init(): it must stay trivially constructible");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free && std::atomic<std::uint8_t>::is_always_lock_free,
              "the versions are shared with the render context: plain loads and stores, no lock");

// The project's bank, in the SDRAM (defined in bank.cpp; the app calls project_bank().init() at setup). Tests build their own Bank.
Bank& project_bank() noexcept;

}  // namespace o3_303::seq
