// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_303/seq/song.h>

#include <cstring>

namespace o3_303::seq {

namespace {

unsigned clamp_pattern(unsigned p) noexcept { return p < kPatternCount ? p : kPatternCount - 1u; }
unsigned clamp_repeat(unsigned r) noexcept { return r < 1u ? 1u : (r > kSongMaxRepeat ? kSongMaxRepeat : r); }
int clamp_transpose(int t) noexcept { return t < -kSongMaxTranspose ? -kSongMaxTranspose : (t > kSongMaxTranspose ? kSongMaxTranspose : t); }

bool same(const Song& a, const Song& b) noexcept { return std::memcmp(&a, &b, sizeof(Song)) == 0; }

}  // namespace

void clear_song(Song& s) noexcept {
  std::memset(&s, 0, sizeof(Song));
  for (auto& r : s.rows) {
    r = default_row();
  }
}

SongRow song_clean_row(const SongRow& r) noexcept {
  SongRow c;
  c.pattern = static_cast<std::uint8_t>(clamp_pattern(r.pattern));
  c.repeat = static_cast<std::uint8_t>(clamp_repeat(r.repeat));
  c.transpose = static_cast<std::int8_t>(clamp_transpose(r.transpose));
  c.flags = static_cast<std::uint8_t>(r.flags & kSongRowMute);
  return c;
}

void song_normalize(Song& s) noexcept {
  const unsigned n = s.length < kSongRows ? s.length : kSongRows;
  for (unsigned i = 0; i < kSongRows; ++i) {
    s.rows[i] = i < n ? song_clean_row(s.rows[i]) : default_row();
  }
  s.length = static_cast<std::uint8_t>(n);
  std::memset(s.reserved, 0, sizeof(s.reserved));
}

bool song_set_pattern(Song& s, unsigned row, unsigned pattern) noexcept {
  if (row >= s.length) {
    return false;
  }
  const auto v = static_cast<std::uint8_t>(clamp_pattern(pattern));
  const bool changed = s.rows[row].pattern != v;
  s.rows[row].pattern = v;
  return changed;
}

bool song_set_repeat(Song& s, unsigned row, unsigned repeat) noexcept {
  if (row >= s.length) {
    return false;
  }
  const auto v = static_cast<std::uint8_t>(clamp_repeat(repeat));
  const bool changed = s.rows[row].repeat != v;
  s.rows[row].repeat = v;
  return changed;
}

bool song_set_transpose(Song& s, unsigned row, int semitones) noexcept {
  if (row >= s.length) {
    return false;
  }
  const auto v = static_cast<std::int8_t>(clamp_transpose(semitones));
  const bool changed = s.rows[row].transpose != v;
  s.rows[row].transpose = v;
  return changed;
}

bool song_set_mute(Song& s, unsigned row, bool mute) noexcept {
  if (row >= s.length) {
    return false;
  }
  const auto v = static_cast<std::uint8_t>(mute ? kSongRowMute : 0u);
  const bool changed = s.rows[row].flags != v;
  s.rows[row].flags = v;
  return changed;
}

bool song_insert(Song& s, unsigned at, const SongRow& r) noexcept {
  if (s.length >= kSongRows) {
    return false;
  }
  if (at > s.length) {
    at = s.length;
  }
  for (unsigned i = s.length; i > at; --i) {
    s.rows[i] = s.rows[i - 1u];
  }
  s.rows[at] = song_clean_row(r);
  ++s.length;
  return true;
}

bool song_append(Song& s, const SongRow& r) noexcept { return song_insert(s, s.length, r); }

bool song_delete(Song& s, unsigned at) noexcept {
  if (at >= s.length) {
    return false;
  }
  for (unsigned i = at; i + 1u < s.length; ++i) {
    s.rows[i] = s.rows[i + 1u];
  }
  --s.length;
  s.rows[s.length] = default_row();
  return true;
}

bool song_clear(Song& s) noexcept {
  if (s.length == 0u) {
    return false;
  }
  clear_song(s);
  return true;
}

bool song_from_chain(Song& s, int first, const std::uint8_t* patterns, unsigned count) noexcept {
  Song n;
  clear_song(n);
  if (first >= 0) {
    SongRow r = default_row();
    r.pattern = static_cast<std::uint8_t>(clamp_pattern(static_cast<unsigned>(first)));
    (void)song_append(n, r);
  }
  for (unsigned i = 0; i < count && patterns != nullptr; ++i) {
    SongRow r = default_row();
    r.pattern = patterns[i];
    if (!song_append(n, r)) {
      break;
    }
  }
  if (same(s, n)) {
    return false;
  }
  s = n;
  return true;
}

}  // namespace o3_303::seq
