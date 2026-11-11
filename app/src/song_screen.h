// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SONG (docs/UX-303.md 5.7, docs/SKIN-PLAN.md 2.4, SG2): the saved chain. A list of up to 64 rows, each a pattern played REPEAT times with its own TRANSPOSE, or skipped (MUTE); Play plays the
// song instead of the selected pattern when F1 says so. The song is the Bank's (seq::Song, one undo stack with the patterns [D9]) and travels with the project (project.h, the SONG chunk); the
// Sequencer plays its own copy (sync_song() after every edit) and keeps its place in it when rows come and go (sequencer.h, "SG2").
//
//   Up / Down  the row           Left / Right  a page of rows (kSongPageRows)       Note / Instr / Vol / Fx  the column (PATTERN, REPEAT, TRANSP, MUTE)
//   encoder    the cell (Shift: pattern by 12, repeat by 4, transpose by an octave)
//   a pad      sets the row's pattern (pad n is pattern n); on the END line it adds a row with that pattern, and the cursor stays on END, so a song is built by tapping pads
//   Insert     a row above the cursor, a copy of it (on END: of the last row)           Delete  removes the row
//   F1 PLAYS: PATTERN or SONG (Play starts at the cursor row)   F2 LOOP   F3 CHAIN (the chain made with Pattern held becomes the song)   F4 CLEAR (twice within 5 s)   F8: the toast "UNDO IS SHIFT + F8"
//   Shift row: F7 REDO, F8 UNDO (as on every screen that edits).
//
// What is shown is a SongView (song_view.h), filled by the pure fill_song_view(); the classic drawing is in song_screen.cpp, the obsidian skin's in skin/song.cpp. Main loop only.

#include "rig.h"
#include "song_view.h"

#include <o3sdk/app/screen.h>

#include <cstdint>

namespace o3_303 {

class SongScreen final : public o3sdk::app::Screen {
 public:
  void init(Rig& rig) noexcept;

  // Every pass of the main loop whatever screen is up (o3_303_app.cpp): a song that ended with its loop off stops the transport, which the render leaves running (song_position().done).
  void service(o3sdk::app::Context& ctx) noexcept;

  const char* title() const noexcept override { return "SONG"; }
  // The obsidian skin (skin/skin.h): no kit chrome and the obsidian theme while it is active; otherwise the kit's.
  o3sdk::app::Chrome chrome(const o3sdk::app::Context& ctx) const noexcept override;
  const o3sdk::ui::Theme* theme(const o3sdk::app::Context& ctx) const noexcept override;
  void enter(o3sdk::app::Context& ctx) noexcept override;
  void leave(o3sdk::app::Context& ctx) noexcept override;
  void input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept override;
  void update(o3sdk::app::Context& ctx) noexcept override;
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  void soft_keys(o3sdk::app::Context& ctx, o3sdk::app::SoftKeys& keys) noexcept override;
  void leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept override;
  void help(const o3sdk::app::Context& ctx, o3sdk::app::Help& h) const noexcept override;

  // For the tests and the console.
  unsigned cursor() const noexcept { return cursor_; }
  unsigned column() const noexcept { return column_; }
  bool plays_song() const noexcept { return plays_song_; }
  bool clear_armed(const o3sdk::app::Context& ctx) const noexcept;
  const SongView& last_view() const noexcept { return view_; }
  const char* toast() const noexcept { return toast_; }

 private:
  void button(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void fkey(o3sdk::app::Context& ctx, unsigned k, bool shift) noexcept;
  void turn(o3sdk::app::Context& ctx, int delta, bool shift) noexcept;
  void pad(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void insert_row(o3sdk::app::Context& ctx) noexcept;
  void delete_row(o3sdk::app::Context& ctx) noexcept;
  void from_chain(o3sdk::app::Context& ctx) noexcept;
  void clear_song(o3sdk::app::Context& ctx) noexcept;
  void undo(o3sdk::app::Context& ctx, bool redo) noexcept;
  void move_cursor(o3sdk::app::Context& ctx, int rows) noexcept;
  // The cursor kept in the song (its length may have changed under it: an undo, a load): the END line when it is past the last row, the last row when the song is full.
  void clamp() noexcept;
  // The song as the Sequencer plays it: sync_song() after an edit, and Play's start row and mode.
  void hand_over() noexcept;
  void apply_play_mode() noexcept;
  void say(o3sdk::app::Context& ctx, const char* text, bool off = false) noexcept;
  void fill_view(o3sdk::app::Context& ctx, SongView& view) noexcept;
  void draw_classic(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;
  void draw_skinned(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r) noexcept;

  Rig* rig_ = nullptr;
  unsigned cursor_ = 0;      // the row; the song's length is the END line
  unsigned column_ = 0;      // 0 PATTERN, 1 REPEAT, 2 TRANSP, 3 MUTE
  bool plays_song_ = false;  // F1: Play plays the song
  std::uint64_t clear_until_ms_ = 0;  // F4 was pressed: a second press before this clears
  char toast_[40] = "";
  bool toast_off_ = false;
  std::uint64_t toast_until_ms_ = 0;
  std::uint32_t seen_pos_ = 0;  // the song position, the tempo and the transport as the last frame drew them: a change asks for a redraw
  SongView view_;
};

}  // namespace o3_303
