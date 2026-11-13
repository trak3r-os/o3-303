// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What PERFORM shows (docs/UX-303.md 5.3, docs/SKIN-PLAN.md 2.4, SK4), apart from the logic that fills it: one PerformView, drawn by the classic function of perform_screen.cpp and by
// the obsidian skin's skin/perform.cpp. It holds texts, flags and the pads' LED levels only, so both drawings are a function of it and neither decides what a lamp says. The screen
// fills it with PerformScreen::fill_view(); the pads' LEDs are the same levels (PerformScreen::leds()), so each lamp is in the tone of its pad's LED (lamp_tone() below).

#include <o3_303/perform.h>

#include <o3sdk/app/pad_leds.h>
#include <o3sdk/app/panel.h>

#include <cstdint>

namespace o3_303 {

// One pad as the screen shows it.
struct PadLamp {
  char label[8] = "";
  std::uint8_t level = 0;  // the pad's LED level (the kit's kLedOff .. kLedOn)
  bool latched = false;    // an action latched: drawn with a frame
};

struct PerformView {
  PadLamp lamp[o3sdk::app::kPadCount];
  // A row that is a ladder of one knob (all four on KNOBS; PERFORM's rows 3 and 4 when SEL3 / SEL4 picked a knob, PF1): `ladder[row]`. Such a row is named "ROW n" and says its knob
  // (`row_knob`) and where it stands (`row_value`, "62 %").
  bool ladder[4] = {};
  char row_knob[4][16] = {};
  char row_value[4][12] = {};
  char row_name[4][12] = {};      // the four rows' names ("LINE+TONE", "REPEAT" or "SNAPSHOTS" on PERFORM SETUP, "WOBBLE", "KEYS")
  char row_caption[4][16] = {};   // under each name what the row is now ("BEAT FREEZE", "PLAY VOICE" or "TRANSPOSE"); "" for none
};

// A lamp's brightness in four tones, from the pad's LED level: off is an outline alone (classic) or a dark cap, dim a grey one, low and mid (two levels) one blue, on the green one. One rule
// for the classic drawing and the skin's, so neither can drift from the LEDs; a lamp keeps its LED's tone, not its exact level.
enum class LampTone : std::uint8_t { Off, Dim, Mid, On };
constexpr LampTone lamp_tone(std::uint8_t level) noexcept {
  return level >= o3sdk::app::kLedOn ? LampTone::On
                                     : (level >= o3sdk::app::kLedLow ? LampTone::Mid : (level >= o3sdk::app::kLedDim ? LampTone::Dim : LampTone::Off));
}

}  // namespace o3_303
