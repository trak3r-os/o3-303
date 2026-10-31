// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The undo / redo history: whole-pattern snapshots in a fixed ring, no allocation. It lives inside the Bank (bank.h, in the SDRAM:
// kHistoryDepth + 1 patterns and as many songs, about 85 KB); the Bank is its only user.
//
// The entries form a timeline with a cursor. The entries before the cursor can be undone, the ones from the cursor on redone. An
// entry holds a pattern's state and which pattern: undo swaps the entry's state with the live pattern's (so the entry then holds
// the state to redo), redo swaps back. A new edit drops the redo entries; when the ring is full the oldest entry goes.
//
// Coalescing: an edit with a non-zero `key` equal to the last entry's key (and the same pattern, nothing to redo) joins that
// entry instead of making a new one, so turning the encoder on one step is one undo, not forty.
//
// The song (song.h) shares the timeline (UX-303 D9): an entry whose `index` is kSongEntry holds a song in `song` instead of a pattern in
// `state` (the other one is not touched), swapped with the Bank's song on undo and redo in the same way.

#include <o3_303/pattern.h>
#include <o3_303/seq/song.h>

#include <cstdint>

namespace o3_303::seq {

inline constexpr unsigned kHistoryDepth = 32;
inline constexpr std::uint8_t kSongEntry = 0xFF;  // Entry::index of an entry that holds the song (a pattern index is below kPatternCount)

class History {
 public:
  struct Entry {
    Pattern state;
    Song song;
    std::uint32_t key;
    std::uint8_t index;  // the pattern it belongs to, or kSongEntry
  };

  void clear() noexcept {
    start_ = 0;
    count_ = 0;
    cursor_ = 0;
    join_barrier_ = false;
  }
  bool can_undo() const noexcept { return cursor_ > 0u; }
  bool can_redo() const noexcept { return cursor_ < count_; }
  unsigned undo_depth() const noexcept { return cursor_; }
  unsigned redo_depth() const noexcept { return count_ - cursor_; }

  // Before an edit of pattern `index` whose live state is `live`: stages the snapshot. false: the edit joins the last entry (same
  // non-zero `key`), nothing is staged and nothing needs committing.
  bool stage(unsigned index, const Pattern& live, std::uint32_t key) noexcept;
  // After the edit (when stage() returned true): keeps the snapshot when `live` differs from it (drops the redo entries first),
  // discards it otherwise. true: the pattern changed.
  bool commit(const Pattern& live) noexcept;
  // The same for an edit of the song (an entry of kind kSongEntry); the key joins only with another song entry.
  bool stage_song(const Song& live, std::uint32_t key) noexcept;
  bool commit_song(const Song& live) noexcept;

  // The entry to swap for an undo / redo (null when there is none); the cursor has moved. Calling one ends coalescing.
  Entry* take_undo() noexcept;
  Entry* take_redo() noexcept;

 private:
  Entry& at(unsigned i) noexcept { return ring_[(start_ + i) % kHistoryDepth]; }
  bool joins(unsigned index, std::uint32_t key) noexcept;  // the edit about to be staged continues the last entry
  void keep_scratch() noexcept;                            // drops the redo line, makes room and stores scratch_ as the newest entry

  Entry ring_[kHistoryDepth];
  Entry scratch_;
  std::uint32_t start_;  // ring position of the oldest entry
  std::uint32_t count_;  // entries held (0..kHistoryDepth)
  std::uint32_t cursor_; // 0..count_
  bool join_barrier_;    // set by an undo / redo: the next edit makes a new entry whatever its key
};

}  // namespace o3_303::seq
