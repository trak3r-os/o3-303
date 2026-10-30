// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The frame every screen shares (docs/UX-303.md section 4, on the kit's rendering of the first waves): the status line under the kit's header
// and the PADS line over the knob row, drawn from view structs so that the obsidian skin later replaces the drawing and not the logic. A screen fills
// a StatusView (what is shown) and the text of its PADS line in update() or draw(), and calls one function for each.

#include "knob_strip.h"

#include <o3sdk/app/context.h>
#include <o3sdk/ui/renderer.h>
#include <o3_303/pages.h>

#include <cstdint>

namespace o3_303::frame {

namespace ui = o3sdk::ui;

inline constexpr int kStatusH = 20;     // the status line, the first line of the screen's area
inline constexpr int kPadsLineH = view::kKnobStripTopH;  // the PADS line takes the place of the knob row's tab line

// The status line: the encoder's owner, the keyboard's mode and octave, REC, a chip for each live action that is on, a toast, a failed autosave.
struct StatusView {
  char enc[32] = "";          // what follows "ENC > ": "F1 CUTOFF", "MORPH 1>2 50%"
  bool keys = true;           // false: no KEYS / TRNSP chip and no octave (a screen whose pads are not a keyboard: FILE)
  bool trnsp = false;         // the keyboard transposes the pattern (TRNSP), else it plays the voice (KEYS)
  int octave = 0;             // the keyboard's octave shift: 0 is "OCT 2-3"
  bool rec = false;           // write / record mode is on
  const char* take = "";      // an audio take is armed or under way ("TAKE ARMED", "TAKE REC 0:05", Rig::take), "" none (UX-303 2.2, 5.10)
  const char* take_msg = "";  // what the take just said ("8 BARS TOO LONG AT 55.0", Rig::take_msg), from every screen; "" none
  bool take_msg_off = false;  // the message says something did not happen: an outline
  std::uint16_t actions = 0;  // a bit per PerfAction that is on: one chip each (kPerfActionChips)
  const char* toast = "";     // a short message, "" none
  bool toast_off = false;     // the message says something did not happen: drawn as an outline
  bool save_failed = false;   // the last autosave failed (UX-303 D23): "SAVE FAILED"
};

// A chip for each action in `actions` (a bit per PerfAction, the lowest first) on the baseline `baseline`, from `x` and never past `x_limit`: as many as
// fit with room for one "+N" chip that stands for the N that did not. Returns the x after the last chip (`x` when nothing fits). The status line and
// the 303 screen's strip use this one rule, so a latched action is never lost without a word and no chip overlaps another field.
int draw_action_chips(ui::Renderer& r, int x, int x_limit, int baseline, std::uint16_t actions) noexcept;
// The width draw_action_chips takes of `room` pixels.
int action_chips_width(ui::Renderer& r, std::uint16_t actions, int room) noexcept;

// The line, in `line` (kStatusH high).
void draw_status(ui::Renderer& r, const ui::Rect& line, const StatusView& view) noexcept;

// One sentence for the four rows of pads, in `line` (kPadsLineH high), the strip's tab line.
void draw_pads_line(ui::Renderer& r, const ui::Rect& line, const char* text) noexcept;

// The knob of `entry` as the page shows it: its name, value and unit, the value as the fill. `k` is the F key, 0..7.
void fill_knob(o3sdk::app::Context& ctx, Controls& controls, unsigned k, const Entry& entry, view::Knob& out) noexcept;

}  // namespace o3_303::frame
