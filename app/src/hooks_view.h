// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What HOOKS shows (docs/UX-303.md 5.11, docs/SKIN-PLAN.md 2.4, SK6), apart from the screen: the line under the grid, the PADS line and the F row, which has no key. One HooksView, drawn by the classic function
// of hooks_screen.cpp and by the obsidian skin's skin/hooks.cpp. The 4 x 12 grid of empty cells is the same in both and needs no data.

#include "knob_strip.h"

namespace o3_303 {

struct HooksView {
  const char* line = "HOOKS ARRIVES WITH M4 . SAMPLE RECORDER WORKS NOW";
  const char* pads_line = "ALL PADS DARK . HOOKS ARRIVES WITH M4";
  view::Knob knob[view::kKnobs];  // eight blank keys: the classic row draws no box for them, the skin an empty one with its F-number

  HooksView() noexcept {
    static constexpr const char* kFKeys[view::kKnobs] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8"};
    for (unsigned i = 0; i < view::kKnobs; ++i) {
      knob[i].blank = true;
      knob[i].fkey = kFKeys[i];
    }
  }
};

}  // namespace o3_303
