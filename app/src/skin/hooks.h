// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The obsidian skin's HOOKS (docs/SKIN-PLAN.md 2.4 and section 8, SK6): the header, the glass with the 4 x 12 grid of the pads as dark `lamp` sprites (the pads are dark here), the one line under it, the PADS
// line, and the F row of eight empty boxes. The view (hooks_view.h) holds the two sentences. Main loop only.

#include "header.h"
#include "../hooks_view.h"

#include <o3sdk/ui/renderer.h>

namespace o3_303::skin {

// The lamp of the pad in `row` (0..3) and `col` (0..11), a `lamp` sprite's box (58 x 40) in screen pixels: the twelve 59 px apart, centred on the glass.
ui::Rect hooks_lamp_box(unsigned row, unsigned col) noexcept;

// One frame of the skinned HOOKS: queues the frame, draws it, then the glass's PADS line and the console's ballast.
void draw_hooks(ui::Renderer& r, const HooksView& v, const frame::StatusView& status, const HeaderInputs& in) noexcept;

}  // namespace o3_303::skin
