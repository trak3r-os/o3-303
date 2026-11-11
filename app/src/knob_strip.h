// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The knob strip's eight boxes (docs/M3B-PLAN.md S2): in each a small F-number and the name, dim, the value large and bright, and the
// value itself as a fill of the box's background from the left, not a bar. Pure drawing of what the screen hands it; HOME, INSTRUMENT, PERFORM and MASTER
// use it. The top line of the area (kKnobStripTopH) is the PADS line's (frame.h); there is no line of page tabs.

#include <o3_303/key_kind.h>
#include <o3sdk/ui/renderer.h>

namespace o3_303::view {

namespace ui = o3sdk::ui;

inline constexpr int kKnobs = 8;
inline constexpr int kKnobStripTopH = 16;  // the line over the boxes: the screen draws its PADS line there (frame::kPadsLineH)

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
  // For the obsidian skin's F row (skin/frow.h); the classic strip ignores them, except `dim`.
  bool toggle = false;      // a two-place list (WAVE SAW / SQR): a switch, `off_name` and `on_name` its two places, `norm` 0 or 1 which one it is on
  const char* off_name = "";
  const char* on_name = "";
  bool dim = false;         // an action key (norm below 0) with nothing to do now: the skin's key is dimmed, the classic strip fades its texts (the layout does not move)
  bool lit = false;         // an action key that is on (MODDED's F8 STOCK, WOBBLE's F8 WOB): the classic box is filled whole, the skin's key is lit red (skin/frow.h)
};

// The one rule that says what kind of key a box is (U6), for both looks of the F row: a blank box is Blank, a negative `norm` an Action, `toggle` a Switch, anything else a Knob.
// Controls::kind() says the same of an entry (tests/pages_test.cpp checks that the two agree).
constexpr KeyKind key_kind(const Knob& k) noexcept {
  return k.blank ? KeyKind::Blank : (k.norm < 0.0f ? KeyKind::Action : (k.toggle ? KeyKind::Switch : KeyKind::Knob));
}

// The faces of the two kinds that are not a knob, drawn inside the box `b` by both classic painters (the strip's and INSTRUMENT's big one). `face_top` and `face_h` are the lines
// where a knob's value would be. A Switch: its value (`value`, as the screen gave it) in a chip, filled when it is on (`norm` at 0.5 or more), an outline when it is off. An Action: its
// name, large, centred in a frame, the hint (`value` and `unit`) small under it; dim fades them. Nothing else of the box is drawn by the caller for these kinds.
void draw_switch_face(ui::Renderer& r, const ui::Rect& b, int face_top, int face_h, const Knob& k) noexcept;
void draw_action_face(ui::Renderer& r, const ui::Rect& b, int face_top, int face_h, const Knob& k) noexcept;

// The boxes under the top line of `area` (the strip's rectangle: kKnobStripTopH of PADS line, then the boxes).
void draw_knobs(ui::Renderer& r, const ui::Rect& area, const Knob (&knobs)[kKnobs]) noexcept;

}  // namespace o3_303::view
