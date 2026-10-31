// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The obsidian skin's PERFORM and PERFORM SETUP (docs/SKIN-PLAN.md 2.4 and section 8, SK4; mockup 05): the panel, the header, the F row (the sound knobs, PERFORM SETUP's eight, or with Shift
// held the Shift row of two action keys) and in the glass the four rows of twelve lamps, one over each pad: a `lamp` sprite tinted by the pad's LED level (a dark cap, a grey one, the blue one,
// the green one) with an accent frame round a latched action, and its label over it. Each row has its name in the silkscreen colour and, under it, what it is now (the ladder's knob, the keys'
// mode). Under the lamps a line of four chips in the colour buttons' colours, with the action each does, filled while it is held and framed while it is latched; then the PADS line.
// PERFORM SETUP keeps row 1 as a reminder, the chips, and the panel's text. The view says what is shown (perform_view.h); this file only places and draws it. Main loop only.

#include "header.h"
#include "../perform_view.h"

#include <o3sdk/ui/renderer.h>

namespace o3_303::skin {

// The lamp's cap (a `lamp` sprite is 58 x 40) of the pad in `row` (0..3) and `col` (0..11), in screen pixels. The twelve caps of a row sit 59 px apart, left of the glass's edge.
ui::Rect lamp_box(unsigned row, unsigned col) noexcept;

// The colour a lamp of this tone is tinted, and the colour token its label is drawn in on it.
ui::Rgb lamp_tint(LampTone tone) noexcept;
ui::Color lamp_ink(LampTone tone) noexcept;

// One frame of the skinned PERFORM (`setup`: PERFORM SETUP): queues the frame (panel, header from `status` and `in`, the F row from `knobs`, the lamps, the chips), draws it, then the glass's PADS
// line `pads_line` and the console's ballast. `knobs` are the F row's eight boxes; `shift_row` says it is the Shift row. `v` must stay valid for the call.
void draw_perform(ui::Renderer& r, const PerformView& v, bool setup, const frame::StatusView& status, const HeaderInputs& in, const view::Knob (&knobs)[view::kKnobs], bool shift_row,
                  const char* pads_line) noexcept;

}  // namespace o3_303::skin
