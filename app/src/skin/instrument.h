// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The obsidian skin's INSTRUMENT (docs/SKIN-PLAN.md 2.4 and section 8, SK5; mockup 06): the panel at full height, no glass. From the top: the header; the page's eight knobs as `knob58`
// (the F row of this screen: "Fn NAME" over each, the latch LED, the readout under it); the sections of the other pages as boxes of read-outs (whatever InstrumentSection gives on each page); the step strip of the page of 16: the note text, an ACCENT, a SLIDE and a STEP LED and a `key` sprite for each step (the cursor
// ringed in white, a held step in amber, groups of four tinted alternately), the step numbers; last the page tabs (the group's: VOICE FX; GROWL; WOBBLE) and the PADS line. While Shift is held the knobs stay
// up, their labels read LEARN (lit while armed) or "-" (dim), the F-number tabs turn amber (the PAGES tabs stay red): the coarse value of a Shift + turn is in the readout, as it is with Shift up. The view says what is
// shown (instrument_view.h); this file only places and draws it, and its pure rules (which LED a step lights, the key's tint, where each part sits) are tested apart
// (tests/skin_instrument_test.cpp). Main loop only.

#include "header.h"
#include "../instrument_view.h"

#include <o3sdk/ui/renderer.h>

namespace o3_303::skin {

// The step strip's grid: step `k` (0..15 of the page) is a column 45 px wide, its centre `step_cx(k)`.
int step_cx(unsigned k) noexcept;
// The `key` sprite's box of step k (42 x 36), and the ring round it: `held` the amber one of a held step (a pixel wider than the cursor's white one, so both show on a held cursor).
ui::Rect key_box(unsigned k) noexcept;
ui::Rect ring_box(unsigned k, bool held) noexcept;

// The three LED rows of the strip: the ACCENT row, the SLIDE row, the STEP row. The centre of step k's LED in a row.
enum class StripRow : std::uint8_t { Accent, Slide, Step };
struct Pt {
  int x;
  int y;
};
Pt led_centre(StripRow row, unsigned k) noexcept;

// What a step lights: ACCENT and SLIDE are on or off with the gate glyph; STEP is dim on a note or a tie, bright on the playhead (whatever the step holds), off on a rest.
enum class StripLed : std::uint8_t { Off, Dim, Lit };
struct StepLeds {
  bool accent;
  bool slide;
  StripLed step;
};
StepLeds step_leds(const InstrumentCell& c, bool playhead) noexcept;

// The TRACT page's strip says the mouth in text, with no LED: the ACCENT row is the step's vowel glyph (`vowel_glyph`), the SLIDE row the TONGUE lock's value and the STEP row the LIPS lock's ("+40", "-100").
// The glyph of the vowel row: "-" for a step that follows the knob, else A E I O U (a literal, valid for the program) and its tint: the lock's teal (a VOWEL lock of the step) over GLD's amber over the plain text.
const char* vowel_glyph(const InstrumentCell& c) noexcept;
ui::Color vowel_ink(const InstrumentCell& c) noexcept;

// The colour of the LED of `row` for these lights: amber, green and red when lit, dim red for a STEP on a note, the socket's own dark red when off.
ui::Rgb led_tint(StripRow row, const StepLeds& leds) noexcept;

// The step keys' two tints: groups of four steps alternate (steps 1 to 4 and 9 to 12, then 5 to 8 and 13 to 16).
ui::Rgb key_tint(unsigned k) noexcept;

// The colour of a step's note text and number: the playhead's green over the cursor's blue; else the text, dim on a rest.
ui::Color note_ink(bool playhead, bool cursor, bool rest) noexcept;

// The face a value of `n` characters takes in a window `w` px wide: the 10x20 Data face, else the 8x16 Label, else the 6x12 Micro, each when it fits with a 3 px margin.
ui::Face value_face(unsigned n, int w) noexcept;

// One frame of the skinned INSTRUMENT: queues the frame (panel, header from `status` and `in`, the knobs, the sections, the strip, the tabs), draws it, then the PADS line `pads_line` and the
// console's ballast. `v` must stay valid for the call.
void draw_instrument(ui::Renderer& r, const InstrumentView& v, const frame::StatusView& status, const HeaderInputs& in, const char* pads_line) noexcept;

}  // namespace o3_303::skin
