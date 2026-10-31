// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The texts of HELP (Shift + Config, o3-sdk kit/app help.h; docs/UX-303.md 5.13): what each button, each F key and each pad does on each screen of the app. The kit draws them; each
// screen's help() override (one per screen class, in its own file) picks a Text here, copies it into the Help it is given (fill()) and adds what only the screen's state knows (the pads
// of the home map, which depend on the octave, the scale lock and the pattern's length; the Shift row's labels, which the screen owns).
//
// Everything here is constexpr data with static storage, so every pointer in a Help stays valid while HELP is open. The pad labels that depend on the state (the keyboard's note
// names) are written into a buffer the caller owns: HomeMap's, one for every screen that has the home map. Main loop only, no heap.
//
// What the texts say is what the code does today (docs/UX-303.md is the plan; where they differ the code wins and the plan has a note). The limits are help.h's (kHelp*): tests/help_texts_test.cpp
// holds every table to them, and every screen as the app builds it.

#include <o3sdk/app/help.h>

#include <cstdint>

namespace o3_303::help_texts {

// The screens (and the modes of a screen that has two): one Text each.
enum class Id : std::uint8_t {
  Home,          // HOME: the steps (MainScreen)
  Setup,         // PATTERN SETUP (MainScreen, its second page)
  Perform,       // PERFORM
  PerformSetup,  // PERFORM SETUP
  Voice,         // INSTRUMENT, page VOICE
  Fx,            // INSTRUMENT, page FX
  Mods,          // INSTRUMENT, page MODS
  Master,        // MASTER
  File,          // FILE PROJECTS, the list
  FileNaming,    // FILE PROJECTS with the name keyboard open (SAVE AS, NEW)
  Library,       // FILE LIBRARY
  Record,        // RECORD
  Hooks,         // HOOKS
  kCount
};
inline constexpr unsigned kIdCount = static_cast<unsigned>(Id::kCount);

// The static part of one screen's Help: the texts that do not depend on state. `fkey` has a text only for an F key the screen labels (on the soft-key row, or in its knob row, which is
// where HOME and the screens that share its pads name their eight keys).
struct Text {
  const char* name;  // for the tests
  const char* summary;
  const char* fkey[o3sdk::app::kFKeyCount];
  const char* fkey_shift[o3sdk::app::kFKeyCount];  // the Shift row's labels where the screen's row is fixed (FILE LIBRARY); the others fill it from their own labels
  const char* encoder;
  const o3sdk::app::HelpButton* buttons;
  std::uint8_t button_count;
  const o3sdk::app::HelpPadGroup* groups;
  std::uint8_t group_count;
  const char* pads;
};

const Text& text(Id id) noexcept;

// Copies the Text's fields into `h` (the summary, the F keys' texts, the buttons, the encoder, the groups and the pads line). The pad labels and groups are the caller's next step.
void fill(o3sdk::app::Help& h, const Text& t) noexcept;

// ---- The pads --------------------------------------------------------------------------------------------------------------------------

// The home map's keys: rows 3 and 4 (pads 24..47), a name each ("C#-1" is the longest, with its terminator).
inline constexpr unsigned kHomeKeys = 24;
inline constexpr unsigned kKeyNameChars = 5;
using KeyNames = char[kHomeKeys][kKeyNameChars];

// The state the home map's pads depend on (home_map.h): the keyboard's octave shift, the step page shown and the pattern's length (steps past it are dark), FILT (rows 3 and 4 are the
// CUTOFF and RESO ladders) and the keys outside the scale the keyboard is locked to (bit n: the key on pad 24 + n; dark, they do nothing).
struct HomePads {
  int octave = 0;
  unsigned view_page = 0;
  unsigned length = 16;
  bool filt = false;
  std::uint32_t out_of_scale = 0;
};

// The home map (HOME, PATTERN SETUP, INSTRUMENT, MASTER and RECORD share it): groups STEPS, MODIFIERS and KEYS (FILT on: LADDERS in place of KEYS), a label on every pad, and `names` for the
// keys' note names (the labels point into it). `h` already has the screen's Text (fill()).
void fill_home_pads(o3sdk::app::Help& h, const HomePads& s, KeyNames& names) noexcept;
// PERFORM and PERFORM SETUP: row 1 the eleven live actions and ALL OFF, row 2 the snapshots and the variations, row 3 the ladder, row 4 the keys.
void fill_perform_pads(o3sdk::app::Help& h) noexcept;
// FILE LIBRARY: the 48 pads are slots 00..47 of the bank.
void fill_library_pads(o3sdk::app::Help& h) noexcept;
// FILE PROJECTS: the pads of the visible list rows (the first `rows` pads).
void fill_list_pads(o3sdk::app::Help& h, unsigned rows) noexcept;
// FILE PROJECTS with the name keyboard: all 48 pads are its keys.
void fill_keyboard_pads(o3sdk::app::Help& h) noexcept;

}  // namespace o3_303::help_texts
