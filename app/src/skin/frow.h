// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The obsidian skin's F row (docs/SKIN-PLAN.md 2.3, mockups 01 and 03): y 384 to 480, eight slots of 100 px, one for each F key. A slot is one of four kinds, picked by a pure
// function of the `view::Knob` the screen fills (slot_kind), and every kind has the label line "Fn NAME" above it (a red tab with the F-number, then the name):
//
//   Empty   a key that does nothing here: a dim box with the F-number.
//   Action  an action key (NEW, TAP, LOAD ...: `norm` below 0): a raised key with its name in it, its hint (`value`) in a window under it. Dim when it has nothing to do now.
//   Switch  a two-place list (WAVE SAW / SQR: `toggle`): a slide switch with both names, the value in the window under it.
//   Knob    everything else: scale, skirt and cap, the pointer at `norm` over 300 degrees, the latch LED, the value and unit in the window under it.
//
// The states, on Knob and Switch: `focus` (the encoder's knob) lights the LED and fills the readout red; `held` (the key is down) puts an amber rim round the readout; `locked` (the
// value is a p-lock of the held step) draws it in the accent colour and lights the LED amber, with no fill. While Shift is held the row is the screen's Shift row, the same slots filled
// with action keys (FRowView::shift): the tabs turn amber, the other cue besides the keys being keys.
//
// fill_frow() is pure (tests/skin_frow_test.cpp): the slot kinds and the readout texts. draw_frow() queues shapes and texts into the Panel; the FRowView must stay valid until the
// Panel's flush_text().

#include "panel.h"

#include "../knob_strip.h"

#include <cstdint>

namespace o3_303::skin {

inline constexpr int kFRowTop = 384;
inline constexpr int kSlotW = 100;
inline constexpr int kSlots = view::kKnobs;

enum class SlotKind : std::uint8_t { Empty, Action, Switch, Knob };

// One rule, one place (view::key_kind, which the classic strip draws by too): a blank key is an empty box, a negative `norm` an action, `toggle` a switch, anything else a knob.
constexpr SlotKind slot_kind(const view::Knob& k) noexcept {
  switch (view::key_kind(k)) {
    case KeyKind::Blank:
      return SlotKind::Empty;
    case KeyKind::Action:
      return SlotKind::Action;
    case KeyKind::Switch:
      return SlotKind::Switch;
    case KeyKind::Knob:
      break;
  }
  return SlotKind::Knob;
}

inline constexpr unsigned kNameChars = 8;      // the label line's name: a tab and eight 8x16 characters fit a slot
inline constexpr unsigned kReadoutChars = 14;  // the readout window's most (6x12 characters in 92 px)
inline constexpr unsigned kCaptionChars = 6;   // a switch's two names (CUTOFF / VOWEL fits a slot)

// The face a readout of `n` characters is drawn in: the 10x20 Data face to eight characters, the 8x16 Label to ten, the 6x12 Micro after that.
constexpr ui::Face readout_face(unsigned n) noexcept { return n <= 8u ? ui::Face::Data : (n <= 10u ? ui::Face::Label : ui::Face::Micro); }

// A hint under an action key is the quieter 8x16 Label face to ten characters, the 6x12 Micro after that.
constexpr ui::Face hint_face(unsigned n) noexcept { return n <= 10u ? ui::Face::Label : ui::Face::Micro; }

struct FSlot {
  SlotKind kind = SlotKind::Empty;
  char fkey[3] = "";                        // "F1"
  char name[kNameChars + 1] = "";           // the label line's name
  char readout[kReadoutChars + 1] = "";     // value and unit ("350Hz"), or an action's hint; "" none (no window)
  char off_name[kCaptionChars + 1] = "";    // Switch: the two places
  char on_name[kCaptionChars + 1] = "";
  float norm = 0.0f;                        // Knob: the pointer's value; Switch: 0 or 1 (the lever's place)
  bool focus = false;
  bool held = false;
  bool locked = false;
  bool dim = false;
  bool lit = false;                         // Action: the key is lit red (RECORD's ARM / STOP while it records, MODDED's STOCK, WOBBLE's WOB while on): fill_frow() takes Knob::lit, and a screen may set it after
};

struct FRowView {
  FSlot slot[kSlots];
  bool shift = false;  // the Shift row: action keys, amber tabs
};

// The row from the screen's knobs (the page's, or the Shift row's built as actions). `shift` says which it is.
void fill_frow(FRowView& out, const view::Knob (&knobs)[kSlots], bool shift) noexcept;

// Queues the row's shapes and texts (the panel under it is queue_panel()'s).
void draw_frow(Panel& p, const FRowView& v) noexcept;

// The parts of a slot that INSTRUMENT (skin/instrument.h) draws at its own places, so that both screens' knobs are one look. `cx` is the slot's centre, `y` the top of the part.
// A label line "Fn NAME": a tab of colour `tab` with the F-number in `tab_ink`, then the name in `name_ink` in `face` (Label or Micro), the pair centred on `cx`.
void queue_label(Panel& p, int cx, int y, const char* fkey, const char* name, ui::Color name_ink, ui::Color tab, ui::Color tab_ink, ui::Face face) noexcept;
// The readout window of `w` px (22 high) with the slot's state: the held rim, the focused knob's fill, a lock in the accent colour, the text. The hint under an action key is quiet; with
// `state_hint` it is a state (STOCK's ON), read at full strength while the key is lit.
void queue_readout(Panel& p, int cx, int y, int w, const FSlot& s, bool state_hint = false) noexcept;
// The slide switch of a two-place list, its track centred on (`cx`, `cy`), both names either side.
void queue_switch(Panel& p, int cx, int cy, const FSlot& s) noexcept;
// The cap's face of an action key (the bitmap is 40 high with its lower lip).
inline constexpr int kKeyH = 36;
// The raised key of an action slot, its cap's top at `y` (the F row's is at kKeyY; INSTRUMENT's knob row puts it where a knob would be).
void queue_action_key(Panel& p, int cx, int y, const FSlot& s) noexcept;

}  // namespace o3_303::skin
