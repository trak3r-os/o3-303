// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The song: up to kSongRows rows, each a pattern played `repeat` times with its own transpose, or skipped when muted (UX-303 5.7). Plain
// data, one flat 260-byte block (no padding, so two songs compare with memcmp): the Bank holds the one the user edits (bank.h, undoable on
// the same stack as the patterns), the Sequencer gets a copy of it to play (sequencer.h, set_song()).
//
// Canonical form: the rows from `length` on are always default_row(), so equal songs are equal bytes and an edit that changes nothing
// leaves the same bytes behind. The edit functions below keep it; song_normalize() makes any block canonical (a song read from a card).
// Each edit returns true when it changed the song; a row out of range or a value out of range is refused or clamped as said.

#include <o3_303/pattern.h>

#include <cstdint>
#include <type_traits>

namespace o3_303::seq {

inline constexpr unsigned kSongRows = 64;
inline constexpr unsigned kSongMaxRepeat = 16;    // repeat 1..16 (UX-303: x1..x16)
inline constexpr int kSongMaxTranspose = 12;      // transpose -12..+12 semitones, on top of the pattern's and the live one
inline constexpr std::uint8_t kSongRowMute = 1u;  // flags bit 0: the row is skipped

struct SongRow {
  std::uint8_t pattern;    // 0..kPatternCount-1
  std::uint8_t repeat;     // plays of the pattern before the next row, 1..kSongMaxRepeat
  std::int8_t transpose;   // -kSongMaxTranspose..+kSongMaxTranspose
  std::uint8_t flags;      // kSongRowMute
};

struct alignas(4) Song {  // word-aligned: Bank::swap_entry swaps it a word at a time
  SongRow rows[kSongRows];
  std::uint8_t length;       // rows in use, 0..kSongRows
  std::uint8_t reserved[3];  // zero
};

static_assert(sizeof(SongRow) == 4 && sizeof(Song) == 4 * kSongRows + 4, "a row is one word; the song has no padding");
static_assert(alignof(Song) >= 4, "Bank::swap_entry swaps the song a word at a time");
static_assert(std::is_trivially_copyable<Song>::value && std::is_trivially_default_constructible<Song>::value,
              "the Bank is zero-initialised SDRAM: the song inside it must stay trivial");

// The row a new song is full of: pattern 0, played once, no transpose, not muted.
inline constexpr SongRow default_row() noexcept { return SongRow{0, 1, 0, 0}; }
inline bool row_muted(const SongRow& r) noexcept { return (r.flags & kSongRowMute) != 0u; }

// The fresh song: no rows (every row default_row()).
void clear_song(Song& s) noexcept;
// A row with its values brought into range (pattern clamped to the last, repeat to 1..16, transpose to +-12, only the mute flag kept).
SongRow song_clean_row(const SongRow& r) noexcept;
// Any block made canonical: the length clamped to kSongRows, each row cleaned, the rows beyond the length default, `reserved` zero.
void song_normalize(Song& s) noexcept;

// Cell edits of an existing row (`row` < length, else false and nothing changes). Values are clamped into range.
bool song_set_pattern(Song& s, unsigned row, unsigned pattern) noexcept;
bool song_set_repeat(Song& s, unsigned row, unsigned repeat) noexcept;
bool song_set_transpose(Song& s, unsigned row, int semitones) noexcept;
bool song_set_mute(Song& s, unsigned row, bool mute) noexcept;
// Adds `r` (cleaned) above row `at` (`at` >= length appends). false: the song is full.
bool song_insert(Song& s, unsigned at, const SongRow& r) noexcept;
bool song_append(Song& s, const SongRow& r) noexcept;
// Removes row `at`; the rows below move up. false: no such row.
bool song_delete(Song& s, unsigned at) noexcept;
// Empties the song. false: it was empty.
bool song_clear(Song& s) noexcept;
// FROM CHAIN: the song becomes `first` (a pattern, or -1 for none: the pattern a Play starts, which is outside the chain) followed by the
// `count` patterns of the chain (Sequencer::chain().pattern), each played once, no transpose, not muted; at most kSongRows rows. The old
// rows are replaced. false: the song is the same as before. With `first` set and the song loop on, each lap starts again at `first`, where
// the chain's own loop goes back to its first entry only.
bool song_from_chain(Song& s, int first, const std::uint8_t* patterns, unsigned count) noexcept;

}  // namespace o3_303::seq
