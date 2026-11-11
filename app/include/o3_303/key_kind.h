// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The three kinds of F key and the empty one (docs/TRACT-KNOBS-PLAN.md 10.4, sentence 4): a key says what kind it is. A knob shows its value, a switch shows ON or OFF
// in a filled chip, an action shows its name in a frame with no value. Controls::kind() (pages.h) says which an entry is; view::key_kind() (knob_strip.h) says which
// the box a screen filled is, and both looks of the F row draw by it.

#include <cstdint>

namespace o3_303 {

enum class KeyKind : std::uint8_t {
  Blank,   // an unused key
  Knob,    // a value that turns: the encoder drives it
  Switch,  // a two-place list (WAVE SAW / SQR, an ON / OFF): the encoder flips it
  Action,  // a key that runs at the press (TAP, ARM, SAVE, STOCK, a Shift tool)
};

}  // namespace o3_303
