// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What INSTRUMENT, GROWL and WOBBLE show (docs/UX-303.md 5.5, docs/MODS-PLAN.md 5.1, docs/SKIN-PLAN.md 2.4, SK5), apart from the logic that fills it: one InstrumentView, drawn by the classic function of instrument_screen.cpp and by
// the obsidian skin's skin/instrument.cpp, so neither decides what a knob, a read-out or a step says. It holds texts, flags and numbers only. The screen fills it with
// InstrumentScreen::fill_view() (and the pure functions of instrument_screen.h); the text pointers it holds (a knob's label, a section's title) are literals or members of the view itself.

#include "knob_strip.h"
#include "step_view.h"

#include <cstdint>

namespace o3_303 {

// A read-out section: a title and up to four "NAME value" lines.
struct InstrumentSection {
  static constexpr unsigned kRows = 4;
  const char* title = "";
  unsigned count = 0;
  const char* label[kRows] = {};
  char value[kRows][12] = {};
};

// One step of the drawer's two lines (and, on the skin, of its LEDs and key).
struct InstrumentCell {
  char note[5] = "";  // "C2", "D#3", "--" a rest, "~" a tie
  char gate[3] = "";  // ".", "-", "A", "S", "AS", "~"
  bool accent = false;  // the gate glyph says A: the skin's ACCENT LED
  bool slide = false;   // ... S: its SLIDE LED
  bool on = false;      // a note or a tie (not a rest): its STEP LED
};

// What the Shift row says of a knob (MIDI learn, UX-303 5.5): nothing while Shift is up; "-" for a knob that is not a unit parameter (and for an empty box); LEARN; LEARN waiting for its controller.
// Tool: a Shift key the page itself uses (the TRACT line's LEN and CLEAR, A2): the knob's label already holds its name.
enum class Learn : std::uint8_t { None, No, Can, Armed, Tool };

struct InstrumentView {
  static constexpr unsigned kMaxSections = 4;
  view::Knob knob[view::kKnobs];  // the page's eight knobs (a blank one has no box)
  InstrumentSection section[kMaxSections];
  unsigned sections = 0;
  InstrumentCell cell[16];  // the page of 16 steps shown
  unsigned cells = 0;       // how many of them are inside the pattern
  view::Marks marks;        // the cursor, the playhead and the held steps of the page
  char lock_label[16] = "";  // "LOCK 5": the label of the knob the encoder locks on a held step
  unsigned page = 0;         // the page of the group shown (INSTRUMENT: VOICE, FX; MODDED NOTES, TONE; GROWL; WOBBLE)
  unsigned tab_count = 0;    // the group's pages: the skin's tabs (a group of one page has one tab)
  const char* const* tabs = nullptr;  // their names ("VOICE", "FX"): literals of the group
  const char* hint = "";     // the line after the tabs: what the buttons do on this group's pages (a literal)
  bool heard = false;         // `hint` is the latched mod's "heard on" line (BACKLOG 14.3), not the group's: the classic drawing shows it too
  bool heard_accent = false;  // the latched mod is heard on accented steps: the drawer's A lights
  bool heard_slide = false;   // ... on slides: the S lights
  Learn learn[view::kKnobs] = {};  // the Shift row, per knob
};

}  // namespace o3_303
