// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The knob strip's eight boxes (docs/M3B-PLAN.md S2): in each a small F-number and the name, dim, the value large and bright, and the
// value itself as a fill of the box's background from the left, not a bar. Pure drawing of what the screen hands it; the pages' strip, the
// SNAP page and the RANDOM popup all use it.

#include <o3sdk/ui/renderer.h>

namespace o3_303::view {

namespace ui = o3sdk::ui;

inline constexpr int kKnobs = 8;
inline constexpr int kKnobTabsH = 16;  // the line of page tabs over the boxes

struct Knob {
  const char* fkey = "";    // "F1"
  const char* label = "";
  char value[24] = "";      // the value's text without its unit
  const char* unit = "";    // drawn small after the value when there is room
  float norm = -1.0f;       // 0..1: how far the background is filled from the left; below 0 none
  bool blank = false;       // a key that does nothing here: no box
  bool focus = false;       // the key the encoder drives: a strong fill and a frame
  bool held = false;        // the key is down: an Accent frame
  bool locked = false;      // the value is a p-lock of the held step: drawn in Accent
  bool previous = false;    // (SNAP) the start of the morph: a quiet frame
};

// The boxes under the tab line of `area` (the strip's rectangle: kKnobTabsH of tabs, then the boxes).
void draw_knobs(ui::Renderer& r, const ui::Rect& area, const Knob (&knobs)[kKnobs]) noexcept;

}  // namespace o3_303::view
