// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_303/seq/history.h>

#include <cstring>

namespace o3_303::seq {

bool History::joins(unsigned index, std::uint32_t key) noexcept {
  if (key != 0u && !join_barrier_ && count_ > 0u && cursor_ == count_) {
    const Entry& last = at(count_ - 1u);
    return last.key == key && last.index == index;  // the same gesture goes on: it stays one undo step
  }
  return false;
}

void History::keep_scratch() noexcept {
  count_ = cursor_;  // a new edit ends the redo line
  if (count_ == kHistoryDepth) {
    start_ = (start_ + 1u) % kHistoryDepth;
    --count_;
  }
  at(count_) = scratch_;
  ++count_;
  cursor_ = count_;
  join_barrier_ = false;
}

bool History::stage(unsigned index, const Pattern& live, std::uint32_t key) noexcept {
  if (joins(index, key)) {
    return false;
  }
  scratch_.state = live;
  scratch_.key = key;
  scratch_.index = static_cast<std::uint8_t>(index);
  return true;
}

bool History::commit(const Pattern& live) noexcept {
  if (std::memcmp(&scratch_.state, &live, sizeof(Pattern)) == 0) {
    return false;
  }
  keep_scratch();
  return true;
}

bool History::stage_song(const Song& live, std::uint32_t key) noexcept {
  if (joins(kSongEntry, key)) {
    return false;
  }
  scratch_.song = live;
  scratch_.key = key;
  scratch_.index = kSongEntry;
  return true;
}

bool History::commit_song(const Song& live) noexcept {
  if (std::memcmp(&scratch_.song, &live, sizeof(Song)) == 0) {
    return false;
  }
  keep_scratch();
  return true;
}

History::Entry* History::take_undo() noexcept {
  if (cursor_ == 0u) {
    return nullptr;
  }
  --cursor_;
  join_barrier_ = true;
  return &at(cursor_);
}

History::Entry* History::take_redo() noexcept {
  if (cursor_ >= count_) {
    return nullptr;
  }
  Entry* e = &at(cursor_);
  ++cursor_;
  join_barrier_ = true;
  return e;
}

}  // namespace o3_303::seq
