// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the SONG screen shows (docs/UX-303.md 5.7, docs/SKIN-PLAN.md 2.4, mockup 07), apart from the logic that fills it: one SongView, drawn by the classic function of
// song_screen.cpp and by the obsidian skin's skin/song.cpp. It holds texts and flags only, so both drawings are a function of it and neither decides what the screen says.
// fill_song_view() is pure (tests/song_screen_test.cpp): the song, the cursor, where the song plays now and a few facts of the session in, the view out.
//
// The list is a page of kSongPageRows lines: the rows of the song and, after the last, the END line (the place a pad appends at). A page is cursor / kSongPageRows.

#include <o3_303/pattern.h>
#include <o3_303/seq/song.h>

#include <o3sdk/ui/renderer.h>

#include <cstdint>

namespace o3_303 {

inline constexpr unsigned kSongPageRows = 10;  // lines a page of the list holds (Left / Right move by this many)
inline constexpr unsigned kSongColumns = 4;    // PATTERN, REPEAT, TRANSP, MUTE: the Note, Instr, Vol and Fx buttons pick one

// A line of the list. A row of the song has its cells as text; the END line has `end` set and no cells.
struct SongLine {
  unsigned row = 0;         // the song row (the song's length on the END line)
  char number[3] = "";      // "1".."64"
  char pattern[4] = "";     // "A03"
  char repeat[6] = "";      // "x4", or "2/4" (the play of its repeats now) on the row that plays
  char transpose[4] = "";   // "+0", "-5", "+12"
  bool muted = false;       // the row is skipped (its cells are dim, MUTE says so)
  bool cursor = false;
  bool playing = false;     // the song plays this row now
  bool end = false;         // the line after the last row
};

// One of the eight F keys. `label` is the key's name (at most 8 characters: the obsidian skin's action key and the classic box's label); `hint` is the quieter text under the skin's key
// (at most 10 characters), `value` the classic box's bright text (at most 8). A key with no label is an empty key.
struct SongKey {
  const char* label = "";
  char hint[12] = "";
  char value[10] = "";
  bool dim = false;  // nothing to do now
  bool hot = false;  // waits for its second press (CLEAR SONG)
};

struct SongView {
  SongLine line[kSongPageRows];
  unsigned lines = 0;        // lines of the page filled
  unsigned page = 0;         // 0-based
  unsigned pages = 1;
  unsigned length = 0;       // rows in the song
  unsigned cursor = 0;       // the cursor's row: the length is the END line
  unsigned column = 0;       // 0 PATTERN, 1 REPEAT, 2 TRANSP, 3 MUTE: the one the encoder edits
  char song[12] = "";        // "4 ROWS", "1 ROW", "EMPTY"
  char duration[24] = "";    // "12 BARS . 0:24" at the tempo the transport has, the rows that are not muted
  char loop[4] = "";         // "ON", "OFF"
  char plays[8] = "";        // "SONG", "PATTERN": what Play plays
  char now[24] = "";         // "ROW 3 . PLAY 2 OF 4" while the song plays, "" when it does not
  bool plays_song = false;
  bool playing = false;      // the transport runs
  bool song_playing = false; // ... and what runs is the song
  bool loop_on = true;
  bool empty = true;         // no rows: the list says how to begin
  bool shift = false;        // the Shift row is the F row
  SongKey key[8];
  char enc[12] = "";         // "PATTERN", "REPEAT", "TRANSP", "MUTE": what the encoder moves
};

// What fill_song_view() needs of the session.
struct SongInputs {
  const seq::Song* song = nullptr;
  unsigned cursor = 0;
  unsigned column = 0;
  int playing_row = -1;            // Sequencer::song_position().row, -1 none
  unsigned playing_repeat = 0;     // its repeat, from 0
  bool transport = false;          // the transport runs
  bool song_active = false;        // the run plays a song (song_position().active)
  bool plays_song = false;         // Play plays the song (Sequencer::play_song())
  bool loop = true;                // Sequencer::song_loop()
  std::uint32_t tempo_mbpm = 125'000;
  const std::uint8_t* pattern_steps = nullptr;  // kPatternCount lengths in steps (0: the default 16); null: every pattern is 16 steps
  unsigned chain_length = 0;       // entries in the chain made on the fly (Sequencer::chain().length): FROM CHAIN has something to copy
  bool can_undo = false;
  bool can_redo = false;
  bool clear_armed = false;        // F7 waits for its second press
  bool shift = false;
};

// The view of `in`. The cursor is clamped into the song (the END line when it is past the last row; none when the song is full and the cursor is on the 64th).
void fill_song_view(SongView& out, const SongInputs& in) noexcept;

// The pieces the view is made of (the tests hold them).
// "A03": a pattern's name on the pads.
unsigned song_pattern_text(unsigned pattern, char* out, unsigned cap) noexcept;
// "x4".
unsigned song_repeat_text(unsigned repeat, char* out, unsigned cap) noexcept;
// "+0", "-5", "+12".
unsigned song_transpose_text(int semitones, char* out, unsigned cap) noexcept;
// The steps the song plays in one lap: the rows that are not muted, each pattern's length times the repeats (a pattern of length 0 is the default 16).
std::uint32_t song_steps(const seq::Song& song, const std::uint8_t* pattern_steps) noexcept;
// "12 BARS . 0:24" for `steps` sixteenth notes at `tempo_mbpm`: the bars to a tenth when they are not whole, the time to the second. "0 BARS" for no steps.
unsigned song_duration_text(std::uint32_t steps, std::uint32_t tempo_mbpm, char* out, unsigned cap) noexcept;

// The list in `area` (both drawings use it): the column chips in the colour of the button that picks each column (the picked one filled), then the page's lines, `row_h` pixels each, the cursor
// row banded, the playing row with a green bar, the picked cell filled; what to do with an empty song or the page under it.
void draw_song_list(o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area, const SongView& v, int row_h) noexcept;

}  // namespace o3_303
