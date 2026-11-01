// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The obsidian skin's LED-segment bars (docs/SKIN-PLAN.md 2.4: MASTER's OUTPUT L and R, RECORD's IN and OUT): a recess with a row of small rects in it, the lit ones green and then amber, the dark
// ones the same colours very dim, and, past the level, one lit segment at the held peak. The rules (which segments are lit, which colour a segment is, where it sits) are pure and tested apart
// (tests/skin_master_record_test.cpp); queue_bar() queues the bar into the Panel. One rect a segment, no bitmap, so a bar costs `segments + 2` shapes and its row on a scan line `segments + 2`
// primitives. Main loop only.

#include "panel.h"

#include "../master_screen.h"  // kMeterMax: the scale's full level

namespace o3_303::skin {

// How many of `n` segments a level lit: 0 for 0, `n` for kMeterMax, and one more for each n-th of the scale the level has passed (segment i is lit when level > i * kMeterMax / n).
unsigned segments_lit(unsigned level, unsigned n) noexcept;

// The last quarter of the segments (the scale's top 12 dB, from -12 dB up) are amber, the rest green.
bool segment_amber(unsigned i, unsigned n) noexcept;

// The segment i's rect in a bar whose window is `win`: a margin of 3 px, a pitch of (win.w - 6) / n, a gap of 2 px, as high as the window less the margin.
ui::Rect segment_box(const ui::Rect& win, unsigned n, unsigned i) noexcept;

// The colours: lit and dark, for the zone of a segment.
ui::Rgb segment_rgb(bool amber, bool lit) noexcept;

// Whether segment i is lit for `level` and the held `peak`: the level's own segments, and the one segment of the peak when it is past the level's.
bool segment_lit(unsigned i, unsigned n, unsigned level, unsigned peak) noexcept;

// The bar: the window `win` (the recess and its lit lip) and its `n` segments. `live` false: a bar with no signal to show (RECORD's IN while the source is the master): all dark.
void queue_bar(Panel& p, const ui::Rect& win, unsigned n, unsigned level, unsigned peak, bool live) noexcept;

}  // namespace o3_303::skin
