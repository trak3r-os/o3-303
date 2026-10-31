// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The obsidian skin's SONG (docs/SKIN-PLAN.md 2.4 and section 8, SG2; mockup 07): the panel, the header (PAGE "SONG"), the F row of five action keys (F1 PLAYS, F2 LOOP, F3 CHAIN, F7 CLEAR, F8
// UNDO; the Shift row has REDO and UNDO), and in the glass the list of the song (the shared draw_song_list(): the column chips in the colours of Note, Instr, Vol and Fx, the cursor row
// banded, the playing row with its green bar) beside a column of readout windows (SONG, LENGTH, LOOP, PLAYS, NOW) and a few lines that say what the pads and the buttons do here, and the
// PADS line under it all. The view says what is shown (song_view.h); this file only places and draws it. Main loop only.

#include "header.h"
#include "../song_view.h"

#include <o3sdk/ui/renderer.h>

namespace o3_303::skin {

// One frame of the skinned SONG: queues the frame (panel, header from `status` and `in`, the F row from `knobs`, the windows), draws it, then the glass's list and texts with `r`, the PADS
// line `pads_line`, and the console's ballast. `knobs` are the F row's eight boxes (action keys); `v.shift` says the row is the Shift row.
void draw_song(ui::Renderer& r, const SongView& v, const frame::StatusView& status, const HeaderInputs& in, const view::Knob (&knobs)[view::kKnobs], const char* pads_line) noexcept;

}  // namespace o3_303::skin
