// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The obsidian skin's RECORD (docs/SKIN-PLAN.md 2.4 and section 8, SK6): the header, the glass, the F row (the view's knobs: F1 ARM / STOP, lit red while the take records, F2 SOURCE and F3 BARS
// action keys with their value as the hint, F4 GAIN and F5 MONITOR knobs, F6 SAVE and F7 DISCARD action keys, dim while there is no finished take, F8 an empty box). In the glass: on the left
// TAKE (the state and the clock in two windows, the bar of a BARS take as a chip, the sentence under them, SOURCE and BARS as readouts, the file name); on the right LEVELS (IN and OUT, left and
// right, as LED-segment bars, the IN pair dark while the source is the master); under them the ENVELOPE of the take, in 48 columns (the take's 96 peaks two to a column: a line of the screen
// has at most 100 primitives), red while it records and green once it is done, with a mark at each bar of a BARS take. The view says what is shown (record_screen.h); this file only places and draws
// it. Main loop only.

#include "header.h"
#include "../record_screen.h"

#include <o3sdk/ui/renderer.h>

namespace o3_303::skin {

// The LEVELS bars: 24 segments of 12 px; IN L, IN R, OUT L, OUT R. The bar's window (`group` 0 IN, 1 OUT; `c` 0 L, 1 R).
inline constexpr unsigned kRecordSegments = 24;
ui::Rect record_bar_window(unsigned group, unsigned c) noexcept;

// The envelope's columns: the take's 96 peaks two to a column (the larger of the two). Column `c` of `env`, 0..kEnvelopeCols - 1.
inline constexpr unsigned kEnvelopeCols = 48;
unsigned envelope_column(const Envelope& env, unsigned c) noexcept;
// How many columns the take has filled when `scanned` of its envelope's span are in (the last one partly): column c is drawn when c <= this.
unsigned envelope_reached(std::uint32_t scanned, std::uint32_t span) noexcept;
// The column `c`'s rect for a peak of `norm` (0..kMeterMax, the dB scale of meter_norm()): the columns share the envelope's width (736 px), a pixel of gap each, mirrored about the centre line, at least
// one pixel high each way. And the x of the mark at the start of bar `bar` (1-based, 2 or more) of a take of `bars`.
ui::Rect envelope_column_box(unsigned c, unsigned norm) noexcept;
int envelope_mark_x(unsigned bar, unsigned bars) noexcept;

// One frame of the skinned RECORD: queues the frame, draws it, then the glass's PADS line `pads_line` and the console's ballast. `v` must stay valid for the call.
void draw_record(ui::Renderer& r, const RecordView& v, const frame::StatusView& status, const HeaderInputs& in, const char* pads_line) noexcept;

}  // namespace o3_303::skin
