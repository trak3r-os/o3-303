// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The obsidian skin's MASTER (docs/SKIN-PLAN.md 2.4 and section 8, SK6; mockup 09): the header, the glass, the F row (the view's knobs: F1 TEMPO, F2 TAP and F3 NUDGE, F4 MASTER, the rest empty
// boxes). In the glass, two columns of boxes with their titles on the top rule: on the left TEMPO (the engine's tempo as a second seven-segment display, "BPM", the tap tempo's count as a chip) and
// TIMING (NUDGE, TAP and CLOCK, each a readout in a window with a hint beside it); on the right OUTPUT (the master's L and R as LED-segment bars, green to amber, with a held peak and the dB scale
// under them) and SYSTEM (the CPU load, "--" where the platform has no meter, and the USB audio port). The view says what is shown (master_screen.h); this file only places and draws it. Main loop
// only.

#include "header.h"
#include "../master_screen.h"

#include <o3sdk/ui/renderer.h>

namespace o3_303::skin {

// The segments of each OUTPUT bar: 30 of 10 px, 1.6 dB each over the scale's 48 dB.
inline constexpr unsigned kMasterSegments = 30;
// The OUTPUT bar's window of channel `c` (0 L, 1 R).
ui::Rect master_bar_window(unsigned c) noexcept;

// The tempo as the seven-segment display shows it: "125.0", or " 95.5" (a blank cell first: the ghost 8 alone, as the header's TEMPO display), from the view's "125.0" / "95.5".
const char* master_tempo_cells(const char* tempo, char (&out)[6]) noexcept;

// One frame of the skinned MASTER: queues the frame (panel, header from `status` and `in`, the F row, the boxes), draws it, then the glass's PADS line `pads_line` and the console's ballast. `v` must
// stay valid for the call.
void draw_master(ui::Renderer& r, const MasterView& v, const frame::StatusView& status, const HeaderInputs& in, const char* pads_line) noexcept;

}  // namespace o3_303::skin
