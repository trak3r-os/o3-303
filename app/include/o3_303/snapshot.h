// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Snapshots and the morph (docs/M3B-PLAN.md R2), the pure part: eight snapshots of the sound, each `kSnapValues` numbers in the units of
// the entries they came from (pages.h snap_target()), and the position of the encoder's morph between the previous snapshot and the one
// recalled last. No engine here: the screen reads the values from the controls, asks this for the blend, and puts it on the sequencer's
// slew. RAM only, nothing is saved. Main loop only.
//
//   store(i, values)    snapshot i holds `values`
//   recall(i, live)     the morph is now from the snapshot recalled before (from `live` when there was none) to snapshot i, at its end
//   turn(detents)       moves the position along that line: 0 is the start, kMorphSteps the end (the recalled snapshot)
//   value(i, discrete)  the blend of target i at the position; a discrete target (an enum) switches at the middle instead of gliding

#include <cstdint>

namespace o3_303 {

inline constexpr unsigned kSnapSlots = 8;
inline constexpr unsigned kSnapValues = 24;  // the targets a snapshot can hold: SOUND, the distortion, the delay, the level and the send now, MODS later
inline constexpr unsigned kMorphSteps = 32;  // encoder detents from one end of the morph to the other (a coarse step is kMorphCoarse of them)
inline constexpr unsigned kMorphCoarse = 4;

class SnapBank {
 public:
  void clear() noexcept;
  bool used(unsigned slot) const noexcept { return slot < kSnapSlots && used_[slot]; }
  // Snapshot `slot` becomes `values` (kSnapValues numbers). A bad slot does nothing. The morph in progress keeps its own copies.
  void store(unsigned slot, const float* values) noexcept;
  // false (nothing changes) for an empty or bad slot. The first recall starts the morph from `live` (the sound as it stands, kSnapValues
  // numbers), the next ones from the snapshot recalled before; recalling the one that is already the end of the morph keeps its start.
  // The position is the end.
  bool recall(unsigned slot, const float* live) noexcept;
  int last() const noexcept { return last_; }          // the slot recalled last, -1 none
  int previous() const noexcept { return previous_; }  // the slot the morph starts from, -1: the sound as it was at the first recall
  bool morphing() const noexcept { return last_ >= 0; }
  unsigned position() const noexcept { return pos_; }  // 0..kMorphSteps
  // Moves the position by `detents` (a coarse step is kMorphCoarse of them), clamped; returns the new one.
  unsigned turn(int detents, bool coarse) noexcept;
  float value(unsigned target, bool discrete) const noexcept;

 private:
  bool used_[kSnapSlots] = {};
  float slot_[kSnapSlots][kSnapValues] = {};
  float from_[kSnapValues] = {};
  float to_[kSnapValues] = {};
  int last_ = -1;
  int previous_ = -1;
  unsigned pos_ = kMorphSteps;
};

}  // namespace o3_303
