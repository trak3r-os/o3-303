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
  char gate[4] = "";  // ".", "-", "A", "S", "AS", "~"; on the TRACT page the step's vowel letter ("-" none) and "~" for GLD, "*" for a VOWEL lock ("A~*")
  bool accent = false;  // the gate glyph says A: the skin's ACCENT LED
  bool slide = false;   // ... S: its SLIDE LED
  bool on = false;      // a note or a tie (not a rest): its STEP LED
  // The TRACT page's strip (E2b; filled by fill_tract_cells(), zero elsewhere): the mouth of the step.
  std::uint8_t vowel = 0;   // the line's vowel under the step: 0 "-" (follows the knob), 1..5 = A E I O U
  bool glide = false;       // GLD on a vowel step (the mouth glides to the next one)
  bool vowel_lock = false;  // a VOWEL lock of the step (the lane lock of F1 on a held step)
  bool tongue = false;      // a TONGUE lock of the step
  bool lips = false;        // a LIPS lock of the step
  char tongue_txt[5] = "";  // the lock's value as the signed percent ("+40", "-100"); "" without a lock: the strip prints it under the vowel
  char lips_txt[5] = "";
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
  bool tract = false;       // TRACT: the strip shows the mouth (vowel glyph, TONGUE and LIPS values as text) where the others show ACCENT, SLIDE and STEP LEDs (E2b)
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
