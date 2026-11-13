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
#include <o3_303/perform.h>

#include <cstdint>

namespace o3_303::help_texts {

// The screens (and the modes of a screen that has two): one Text each.
enum class Id : std::uint8_t {
  Home,          // HOME: the steps (MainScreen)
  Setup,         // PATTERN SETUP (MainScreen, its second page)
  Perform,       // PERFORM
  Knobs,         // PERFORM KNOBS (PERFORM's second page: four ladder rows)
  PerformSetup,  // PERFORM SETUP
  Voice,         // INSTRUMENT, page VOICE
  Fx,            // INSTRUMENT, page FX
  ModdedNotes,   // MODDED, page NOTES (the Sample Loader button)
  ModdedTone,    // MODDED, page TONE
  Tract,         // TRACT (the Sample Playback button; again: GROWL and back)
  Growl,         // GROWL (Sample Playback, again)
  Wobble,        // WOBBLE (the Sample Editor button)
  Master,        // MASTER
  File,          // FILE PROJECTS, the list
  FileNaming,    // FILE PROJECTS with the name keyboard open (SAVE AS, NEW)
  Library,       // FILE LIBRARY
  Record,        // RECORD
  Song,          // SONG
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

// The "heard on" line of a mod (docs/BACKLOG.md 14.3): when the mod is heard, from the conditions in units/acid_voice. `voice_id` is the unit's parameter id (8..18, the mods). `line` is null for a mod
// that has no condition (it is heard on every note); `accent` and `slide` say which of the drawer's A and S lights the mod belongs to; `needs_accent` marks the mods that ACCENT at 0 silences
// (the accent's cutoff lift, AcidVoice::sweep_term). An id that is not a mod gives the empty entry.
struct HeardOn {
  const char* line = nullptr;
  bool accent = false;
  bool slide = false;
  bool needs_accent = false;
};
HeardOn heard_on(unsigned voice_id) noexcept;
// The widest line, in characters, that heard_on() and heard_off_accent() give (the strips are tested against it).
inline constexpr unsigned kHeardMax = 30;
// The text for a latched mod: its line, or "ACCENT IS 0: NOT HEARD" for an accent mod while ACCENT is at 0; empty when the mod has none.
const char* heard_line(unsigned voice_id, bool accent_zero) noexcept;

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

// The home map (HOME, PATTERN SETUP, INSTRUMENT, MODDED, GROWL, WOBBLE, MASTER and RECORD share it): groups STEPS, MODIFIERS and KEYS (FILT on: LADDERS in place of KEYS), a label on every pad, and `names` for the
// keys' note names (the labels point into it). `h` already has the screen's Text (fill()).
void fill_home_pads(o3sdk::app::Help& h, const HomePads& s, KeyNames& names) noexcept;
// TRACT: the pads are not the home map's: rows 1 and 2 the pattern's TRACT line, row 3 the palette (A E I O U "-", GLD, LINE), row 4 the live vowels A E I O U. `h` already has the screen's Text (fill()).
void fill_tract_pads(o3sdk::app::Help& h) noexcept;
// TRACT's STEPS layer (E2a): rows 1 and 2 are the pattern's steps (columns 1-8; the rest do nothing), rows 3 and 4 as fill_tract_pads(); sets the group texts too.
void fill_tract_steps_pads(o3sdk::app::Help& h) noexcept;
// The WOBBLE page (E2c): call after the home map's fill_home_pads(): row 3 becomes the wobble's rates (PERFORM's), row 4 keeps the keys, and the groups are STEPS, MODIFIERS, WOBBLE and KEYS.
void fill_wobble_pads(o3sdk::app::Help& h) noexcept;
// PERFORM: row 1 the line and tone actions and ALL OFF, row 2 the beat freeze's rates and the selectors SEL3 and SEL4 (pads 11 and 12), rows 3 and 4 what `row3` and `row4` say (kContentWobRates,
// kContentKeys or kContentKnobBase + k: the wobble's rates, the keys, a knob's ladder). `setup` (PERFORM SETUP): row 2 is the snapshots and the variations, no selectors (the groups come with the
// screen's Text). `knobs` (PERFORM KNOBS): four ladders, MIN .. MAX.
void fill_perform_pads(o3sdk::app::Help& h, bool knobs = false, bool setup = false, unsigned row3 = kContentWobRates, unsigned row4 = kContentKeys) noexcept;
// FILE LIBRARY: the 48 pads are slots 00..47 of the bank.
void fill_library_pads(o3sdk::app::Help& h) noexcept;
// FILE PROJECTS: the pads of the visible list rows (the first `rows` pads).
void fill_list_pads(o3sdk::app::Help& h, unsigned rows) noexcept;
// FILE PROJECTS with the name keyboard: all 48 pads are its keys.
void fill_keyboard_pads(o3sdk::app::Help& h) noexcept;
// SONG: the 48 pads are the 48 patterns, labelled A01 to D12 (the names the pattern grid and the song's list use).
void fill_song_pads(o3sdk::app::Help& h) noexcept;

}  // namespace o3_303::help_texts
