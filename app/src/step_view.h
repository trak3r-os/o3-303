// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The drawing of a pattern: the step view (the hero of the 303 screen, the drawer of UX-303 5.1: the NOTE lane and the GATE, PLAYS and LOCK rows), the
// motion lane under it, the pattern grid and the small helpers they share. Pure drawing: a function of a Pattern and a few marks, no state, no engine. The
// geometry is fixed on the 800 x 480 panel: 16 columns of kCellW pixels to the right of a gutter, so a page of 16 steps is the 16 step
// pads (DESIGN-303 section 5).

#include <o3sdk/ui/components.h>
#include <o3sdk/ui/renderer.h>
#include <o3_303/pattern.h>
#include <o3_303/step_model.h>

#include <cstdint>

namespace o3_303::view {

namespace ui = o3sdk::ui;

inline constexpr int kGutterW = 52;   // note names (or the piano keys) left of the columns
inline constexpr int kCellW = 46;     // one step
inline constexpr int kGridX = kGutterW;
inline constexpr int kGridW = kCellW * 16;
inline constexpr int kNumbersH = 18;  // the row of step numbers
inline constexpr int kRowH = 14;      // one of the drawer's rows under the NOTE lane: GATE, PLAYS, LOCK
inline constexpr int kRows = 3;
inline constexpr unsigned kLockChars = 8;  // a lock's text in its cell (with the terminator)
inline constexpr int kMotionH = 46;   // the motion lane with its caption
inline constexpr int kPointW = kCellW / 4;

// What the view marks besides the pattern.
struct Marks {
  int page = 0;                 // the page of 16 steps shown
  int cursor = -1;              // global step of the cursor, -1 none
  int playhead = -1;            // global step the playhead is on (only when this pattern is playing), -1 none
  int playpoint = 0;            // the playhead's quarter of its step, for the motion lane
  std::uint32_t held = 0;       // bit k: step pad k of the page is held
  std::uint32_t emphasis = 0;   // bit k: step k of the page is outlined (a held modifier's steps, motion steps)
  DrawerRow row = DrawerRow::None;  // the row the encoder owns: its caption is highlighted
  char lock[16][kLockChars] = {};   // the LOCK row: the latched knob's lock on each step of the page ("" none), as the home map formats them
  PitchWindow window;
};

// The height the pitch lane gets inside a hero area of height `h` (the step view keeps room for the three rows).
int lane_height(int h) noexcept;

// The hero inside `area`: the step numbers, the NOTE lane with its gutter, and the GATE (one glyph a step), PLAYS (the ratchet or the condition) and
// LOCK rows under it. A ratchet is also the bar in its hits. The caption of the row `m.row` is highlighted.
void draw_steps(ui::Renderer& r, const ui::Rect& area, const Pattern& p, const Marks& m) noexcept;

// The lane of motion points of `lane` under the same 16 columns. `knob_norm` (0..1) is where the knob stands, drawn as a guide;
// `name` and `value` caption the lane. `focus_on_sound`: false when the focused page is not the SOUND page (the lane is the last one).
void draw_motion(ui::Renderer& r, const ui::Rect& area, const Pattern& p, const Marks& m, unsigned lane, const char* name,
                 float knob_norm, bool focus_on_sound) noexcept;

// A fixed-width unsigned into `out` (no leading zeros); returns the length.
unsigned fmt_uint(unsigned v, char* out) noexcept;

// A translucent block: `fn()` draws with the renderer's own calls at the given alpha (0..255).
template <typename Fn>
void translucent(ui::Renderer& r, std::uint8_t alpha, Fn&& fn) noexcept {
  r.begin_raw();
  r.raw(o3sdk::eve::dl::color_a(alpha));
  fn();
  r.end_raw();
}

}  // namespace o3_303::view
