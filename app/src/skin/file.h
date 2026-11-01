// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The obsidian skin's FILE PROJECTS (docs/SKIN-PLAN.md 2.4 and section 8, SK7; mockup 08): the header, the glass and the F row (LOAD, SAVE, SAVE AS, NEW, REVERT and DELETE as action keys, dim when
// the screen would refuse them, the armed REVERT or DELETE lit red and reading "REVERT?" / "DELETE?"; F7 an empty box; F8 LIBRARY). In the glass: the info line (the folder, NOW with the current
// project, and the autosave line at the right), the list (nine rows; the cursor row banded in the selection colour with a ">", a "*" on the project the session is, a scroll mark when there are more),
// the list's empty state (NO CARD and the card's other troubles in red, NO PROJECTS YET, READING THE CARD) and the PADS line. While SAVE AS or NEW asks for a name, the glass holds the kit's own name
// keyboard (app::NameEntry::draw(), centred in the glass in the obsidian theme) and the F row is the keyboard's: TYPE, OK (dim while the name is refused) and CANCEL.
// The view says what is shown (file_screen.h); this file only places and draws it. Main loop only.

#include "frow.h"
#include "header.h"
#include "../file_screen.h"

#include <o3sdk/app/name_entry.h>
#include <o3sdk/ui/renderer.h>

namespace o3_303::skin {

// The list's window in the glass: nine rows of 28 px under the info line (the pads select them, row 0 first: FileScreen::leds()).
inline constexpr unsigned kFileRows = 9;
// The row `row` (0..kFileRows - 1) of the window; `mark` leaves room for the scroll mark at the right (more projects than rows).
ui::Rect file_row_box(unsigned row, bool mark) noexcept;
// The scroll mark's track (as high as the rows that are shown) and its thumb, for a list of `total` projects whose window starts at `first`.
ui::Rect file_scroll_track(unsigned shown) noexcept;
ui::Rect file_scroll_thumb(unsigned shown, unsigned total, unsigned first, unsigned rows) noexcept;
// The ink of the autosave line: the last write's state (AUTOSAVED green, SAVING amber, NOT SAVED red, anything else dim).
ui::Color saved_ink(const char* saved) noexcept;
// The area the name keyboard is centred in: the glass above its PADS line.
ui::Rect file_keyboard_area() noexcept;

// The F row of the view: the six keys of the classic row and LIBRARY as action keys, a key with no label an empty box; `dim` and `hot` as the view says them (hot: lit red, the hint "again?"). In
// the keyboard the labels are its own and the hints say what they do.
void fill_file_frow(FRowView& out, const FileView& v) noexcept;

// One frame of the skinned FILE PROJECTS: queues the frame (panel, header from `status` and `in`, the F row, the list), draws it, then, with `names` (the keyboard is up), the kit's keyboard in the
// glass, then the PADS line `pads_line` and the console's ballast. `v` must stay valid for the call.
void draw_file(ui::Renderer& r, const FileView& v, const frame::StatusView& status, const HeaderInputs& in, const o3sdk::app::NameEntry* names, const char* pads_line) noexcept;

}  // namespace o3_303::skin
