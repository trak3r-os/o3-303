// SPDX-License-Identifier: GPL-3.0-or-later
// The texts of HELP (app/src/help_texts.h, Shift + Config; docs/UX-303.md 5.13): every table holds to the kit's limits (o3-sdk kit/app help.h), the pads' functions fill what the state asks, and each screen,
// run in the simulator with HELP really opened over it (Shift + Config), gives the kit a Help that holds to the limits too, has a group on every pad its map uses, picks the Text of the mode it is in,
// and puts a text on no F key the screen leaves unlabelled (labelled: the soft-key row's label, or the box of the knob row, where HOME, PERFORM, INSTRUMENT, MASTER and RECORD name their keys).
#include <doctest/doctest.h>

#include "hands.h"
#include "../app/src/help_texts.h"

#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3sdk/app/help.h>
#include <o3sdk/app/help_overlay.h>
#include <o3sdk/platform/host/dir_card.h>
#include <o3sdk/sim/harness.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

using namespace hands;
using namespace o3_303;
using namespace o3_303::help_texts;
using o3sdk::app::Help;
using o3sdk::app::HelpButton;
using o3sdk::app::HelpOverlay;
namespace host = o3sdk::platform::host;

namespace {

// ---- The limits -------------------------------------------------------------------------------------------------------------------------------

// The kit's check of a Help (o3-sdk help.h help_check(): every limit, measured with the overlay's own wrap rule, the groups, the buttons table), then o3-303's own rules on top: a summary on every
// screen, no sentence on two F keys, Shift left to the kit, no button entry without a text.
void check_help(const Help& h, const std::string& who) {
  const std::string w = who + ": ";
  app::HelpProblem problem;
  CHECK_MESSAGE(app::help_check(h, &problem), w << std::string(app::help_field_name(problem.field)) << " " << unsigned{problem.index} << ": " << std::string(problem.what));
  CHECK_MESSAGE(h.summary != nullptr, w << "summary: missing");
  for (unsigned k = 0; k < app::kFKeyCount; ++k) {
    const std::string key = w + "F" + std::to_string(k + 1);
    // The same sentence on two keys of one screen is what review took out of HELLO.
    for (unsigned j = 0; j < k; ++j) {
      if (h.fkey[k] != nullptr && h.fkey[j] != nullptr) {
        CHECK_MESSAGE(std::strcmp(h.fkey[k], h.fkey[j]) != 0, key << ": the same text as F" << j + 1);
      }
    }
  }
  REQUIRE(h.button_count <= HelpOverlay::kEntries - 1u);
  for (unsigned b = 0; b < h.button_count; ++b) {
    const HelpButton& hb = h.buttons[b];
    const unsigned id = static_cast<unsigned>(hb.button);
    CHECK_MESSAGE(hb.button != app::Button::Shift, w << "Shift is the kit's");
    CHECK_MESSAGE((hb.text != nullptr || hb.shift != nullptr), w << "button " << id << " has no text at all (leave it out: it is drawn dimmed)");
  }
}

// Every pad of `used` (bit n: pad n) is in a group of the Help.
void check_pads_grouped(const Help& h, std::uint64_t used, const std::string& who) {
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    if ((used >> pad & 1u) != 0u) {
      CHECK_MESSAGE(h.pad_group[pad] != 0u, who << ": pad " << pad << " has no group");
    }
  }
}

constexpr std::uint64_t kAllPads = (std::uint64_t{1} << app::kPadCount) - 1u;
constexpr std::uint64_t kTractPads = std::uint64_t{0x1F} << 36;  // TRACT: the live vowels A E I O U, the bottom row's first five; the rest of the grid is dark and has no group
constexpr std::uint64_t kRefusedKeyPads = (std::uint64_t{1} << 23) | (std::uint64_t{1} << 33);  // '+' and '@' on the name keyboard

}  // namespace

// ---- The tables --------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("help texts: every table keeps the kit's limits") {
  for (unsigned i = 0; i < kIdCount; ++i) {
    const Text& t = text(static_cast<Id>(i));
    Help h;
    fill(h, t);
    CHECK(h.summary == t.summary);
    CHECK(h.buttons == t.buttons);
    check_help(h, t.name);
    // Holding Pattern turns the pads into the patterns on HOME and PATTERN SETUP only: the other screens' Pattern press goes to HOME.
    const bool says_hold = t.pads != nullptr && std::strstr(t.pads, "HOLD PATTERN") != nullptr;
    CHECK_MESSAGE(says_hold == (static_cast<Id>(i) == Id::Home || static_cast<Id>(i) == Id::Setup), t.name << ": the hold-Pattern clause");
  }
}

TEST_CASE("help texts: MODDED's summary says what ends STOCK and what does not (MODS-PLAN 5.11)") {
  for (Id id : {Id::ModdedNotes, Id::ModdedTone}) {
    const std::string s = text(id).summary;
    CHECK_MESSAGE(s.find("A TURN ENDS STOCK") != std::string::npos, text(id).name);
    CHECK_MESSAGE(s.find("A CC, LANE, LOCK OR SNAPSHOT MOVES A MOD UNDER STOCK") != std::string::npos, text(id).name);
  }
}

TEST_CASE("help texts: the F keys' texts are where there is something to add") {
  // HOME, PERFORM and INSTRUMENT VOICE name the eight sound knobs; PATTERN SETUP, PERFORM SETUP and the other pages their own eight. FILE LIBRARY's keys are the banks. MASTER has five keys, RECORD four (ARM BARS SAVE DISCARD),
  // GROWL seven, WOBBLE six (RATE .. RESET and F8 WOB) and MODDED TONE six (F1..F5 and F8 STOCK); MODDED NOTES names all eight (the rest are empty wells).
  for (Id id : {Id::Home, Id::Perform, Id::Voice, Id::Setup, Id::PerformSetup, Id::Fx, Id::ModdedNotes, Id::Library}) {
    for (unsigned k = 0; k < app::kFKeyCount; ++k) {
      CHECK_MESSAGE(text(id).fkey[k] != nullptr, text(id).name << " F" << k + 1);
    }
  }
  for (unsigned k = 0; k < app::kFKeyCount; ++k) {
    CHECK_MESSAGE((text(Id::Tract).fkey[k] != nullptr) == (k < 8), "TRACT F" << k + 1);
    CHECK_MESSAGE((text(Id::Growl).fkey[k] != nullptr) == (k < 8), "GROWL F" << k + 1);
    CHECK_MESSAGE((text(Id::Wobble).fkey[k] != nullptr) == (k < 5 || k == 7), "WOBBLE F" << k + 1);
    CHECK_MESSAGE((text(Id::ModdedTone).fkey[k] != nullptr) == (k < 5 || k == 7), "MODDED TONE F" << k + 1);
  }
  CHECK(text(Id::Master).fkey[3] != nullptr);
  CHECK(text(Id::Master).fkey[4] != nullptr);  // DLY MODE (S0 of docs/MODS-PLAN.md)
  CHECK(text(Id::Master).fkey[5] == nullptr);
  CHECK(text(Id::Record).fkey[3] != nullptr);
  CHECK(text(Id::Record).fkey[4] == nullptr);
  CHECK(text(Id::Song).fkey[3] != nullptr);  // CLEAR moved from F7 to F4 (U7)
  CHECK(text(Id::Song).fkey[7] == nullptr);  // and plain F8 is no UNDO any more (U4)
  CHECK(text(Id::File).fkey[6] == nullptr);  // F7 is empty on FILE
}

// ---- The pads --------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("help pads: the home map has a group on all 48 pads and a name on every key") {
  KeyNames names = {};
  Help h;
  fill(h, text(Id::Home));
  HomePads s;
  fill_home_pads(h, s, names);
  check_help(h, "home map");
  check_pads_grouped(h, kAllPads, "home map");
  CHECK(h.group_count == 3);
  // Steps 1..16 on the first two rows (columns 1..8), the modifier block on columns 9..12, the keys on rows 3 and 4: C2 bottom left, C3 above it.
  CHECK(std::strcmp(h.pad_label[0], "1") == 0);
  CHECK(std::strcmp(h.pad_label[7], "8") == 0);
  CHECK(std::strcmp(h.pad_label[12], "9") == 0);
  CHECK(std::strcmp(h.pad_label[19], "16") == 0);
  CHECK(std::strcmp(h.pad_label[8], "ACC") == 0);
  CHECK(std::strcmp(h.pad_label[11], "REST") == 0);
  CHECK(std::strcmp(h.pad_label[22], "TRNSP") == 0);
  CHECK(std::strcmp(h.pad_label[23], "FILT") == 0);
  CHECK(std::strcmp(h.pad_label[36], "C2") == 0);
  CHECK(std::strcmp(h.pad_label[37], "C#2") == 0);
  CHECK(std::strcmp(h.pad_label[47], "B2") == 0);
  CHECK(std::strcmp(h.pad_label[24], "C3") == 0);
  CHECK(h.pad_group[0] == 1);
  CHECK(h.pad_group[8] == 2);
  CHECK(h.pad_group[24] == 3);
}

TEST_CASE("help pads: the key names follow the octave over its whole range, and stay within five characters") {
  for (int octave = kMinOctave; octave <= kMaxOctave; ++octave) {
    KeyNames names = {};
    Help h;
    fill(h, text(Id::Home));
    HomePads s;
    s.octave = octave;
    fill_home_pads(h, s, names);
    check_help(h, "octave " + std::to_string(octave));
    for (unsigned pad = 24; pad < app::kPadCount; ++pad) {
      const int note = key_note(pad, octave);
      REQUIRE(note >= 0);
      REQUIRE(h.pad_label[pad] != nullptr);
      const std::string want = std::string(&"C C#D D#E F F#G G#A A#B "[(note % 12) * 2], note % 12 == 1 || note % 12 == 3 || note % 12 == 6 || note % 12 == 8 || note % 12 == 10 ? 2u : 1u) +
                               std::to_string(note / 12 - 1);
      CHECK_MESSAGE(want == h.pad_label[pad], "pad " << pad << " at octave " << octave);
    }
  }
  CHECK(std::strlen("C#-1") < kKeyNameChars);
}

TEST_CASE("help pads: steps past the pattern are dark, a scale takes its out-of-scale keys, FILT swaps the keys for the ladders") {
  KeyNames names = {};
  Help h;
  fill(h, text(Id::Home));
  HomePads s;
  s.length = 8;
  s.view_page = 0;
  fill_home_pads(h, s, names);
  CHECK(h.pad_group[7] == 1);   // step 8
  CHECK(h.pad_group[12] == 0);  // step 9
  CHECK(h.pad_group[19] == 0);  // step 16
  s.length = 32;
  s.view_page = 1;  // steps 17..32
  fill_home_pads(h, s, names);
  CHECK(h.pad_group[0] == 1);
  CHECK(std::strcmp(h.pad_label[0], "17") == 0);
  CHECK(std::strcmp(h.pad_label[19], "32") == 0);
  s.length = 64;
  s.view_page = 3;
  fill_home_pads(h, s, names);
  CHECK(std::strcmp(h.pad_label[19], "64") == 0);

  s = HomePads{};
  s.out_of_scale = 1u << 13;  // pad 37, C#2
  fill_home_pads(h, s, names);
  CHECK(h.pad_group[37] == 0);
  CHECK(h.pad_label[37] == nullptr);
  CHECK(h.pad_group[36] == 3);
  CHECK(h.pad_group[38] == 3);

  s = HomePads{};
  s.filt = true;
  fill(h, text(Id::Home));
  fill_home_pads(h, s, names);
  check_help(h, "FILT on");
  CHECK(h.group_count == 3);
  CHECK(std::strcmp(h.groups[2].name, "LADDERS") == 0);
  CHECK(std::strcmp(h.pad_label[24], "ROW3") == 0);
  CHECK(std::strcmp(h.pad_label[36], "ROW4") == 0);
  check_pads_grouped(h, kAllPads, "FILT on");
}

TEST_CASE("help pads: PERFORM's rows, FILE LIBRARY's slots, the list's rows and the name keyboard") {
  {
    Help h;
    fill(h, text(Id::Perform));
    fill_perform_pads(h);
    check_help(h, "perform pads");
    std::uint64_t used = 0;
    for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
      if (perform_pad(pad).role != PerformRole::None) {
        used |= std::uint64_t{1} << pad;
      }
    }
    CHECK(used == kAllPads);
    check_pads_grouped(h, used, "perform pads");
    CHECK(h.group_count == 6);
    // Row 1 follows the order of the pads (perform.h): MUTE first, WOBBLE eleventh, ALL OFF last.
    for (unsigned col = 0; col < kPerfActionCount; ++col) {
      CHECK(h.pad_group[col] == 1);
      CHECK(h.pad_label[col] != nullptr);
    }
    CHECK(std::strcmp(h.pad_label[0], "MUTE") == 0);
    CHECK(std::strcmp(h.pad_label[10], "WOB") == 0);
    CHECK(h.pad_group[11] == 2);
    CHECK(std::strcmp(h.pad_label[11], "OFF") == 0);
    CHECK(std::strcmp(h.pad_label[12], "1") == 0);  // the snapshots
    CHECK(std::strcmp(h.pad_label[19], "8") == 0);
    CHECK(h.pad_group[19] == 3);
    CHECK(std::strcmp(h.pad_label[20], "NEW") == 0);
    CHECK(h.pad_group[23] == 4);
    CHECK(h.pad_group[24] == 5);
    CHECK(h.pad_group[36] == 6);
    CHECK(std::strcmp(h.pad_label[36], "C") == 0);
    CHECK(std::strcmp(h.pad_label[47], "B") == 0);
  }
  {
    Help h;
    fill(h, text(Id::Library));
    fill_library_pads(h);
    check_help(h, "library pads");
    check_pads_grouped(h, kAllPads, "library pads");
    CHECK(std::strcmp(h.pad_label[0], "00") == 0);
    CHECK(std::strcmp(h.pad_label[47], "47") == 0);
  }
  {
    Help h;
    fill(h, text(Id::File));
    fill_list_pads(h, 5);
    check_help(h, "list pads");
    check_pads_grouped(h, 0x1Fu, "list pads");
    CHECK(h.pad_group[5] == 0);
    fill_list_pads(h, 99);  // never past the panel
    CHECK(h.pad_group[47] == 1);
  }
  {
    Help h;
    fill(h, text(Id::FileNaming));
    fill_keyboard_pads(h);
    check_help(h, "keyboard pads");
    // Every pad types a key but the two a file name refuses ('@' on pad 33, '+' on pad 23: dark), and carries its label.
    check_pads_grouped(h, kAllPads & ~kRefusedKeyPads, "keyboard pads");
    CHECK((h.pad_group[23] == 0u && h.pad_group[33] == 0u));
    CHECK(std::strcmp(h.pad_label[0], "1") == 0);
    CHECK(std::strcmp(h.pad_label[10], "DEL") == 0);
    CHECK(std::strcmp(h.pad_label[11], "DEL") == 0);
    CHECK(std::strcmp(h.pad_label[12], "Q") == 0);
    CHECK(std::strcmp(h.pad_label[34], "CAPS") == 0);
    CHECK(std::strcmp(h.pad_label[43], "SPACE") == 0);
    CHECK(std::strcmp(h.pad_label[47], "SPACE") == 0);
  }
}

// ---- The screens, with HELP open over them ---------------------------------------------------------------------------------------------

namespace {

void tap(sim::Harness& h, app::Button b, unsigned hold = 40) {
  h.press(b);
  h.run_ms(2);
  h.run_ms(hold);
  h.release(b);
  h.run_ms(2);
}

void shift_tap(sim::Harness& h, app::Button b) {
  h.press(app::Button::Shift);
  h.run_ms(2);
  tap(h, b);
  h.release(app::Button::Shift);
  h.run_ms(2);
}

// Shift + Config, as the scenario does it, then the overlay's Help: what the screen told the kit.
const Help& open_help(sim::Harness& h) {
  REQUIRE_FALSE(h.runtime().help_open());
  h.press(app::Button::Shift);
  h.run_ms(2);
  tap(h, app::Button::Config);
  h.release(app::Button::Shift);
  h.run_ms(50);
  REQUIRE(h.runtime().help_open());
  return h.runtime().help_overlay().help();
}

void close_help(sim::Harness& h) {
  tap(h, app::Button::Config);
  h.run_ms(50);
  CHECK_FALSE(h.runtime().help_open());
}

// The F keys the screen labels: the soft-key row's (asked as the kit asks it, with Shift let go) and `knob_row`'s, the boxes of the knob row that HOME and the screens that share its drawing use.
unsigned labelled(sim::Harness& h, unsigned knob_row) {
  app::Context& ctx = h.runtime().context();
  app::SoftKeys keys;
  ctx.screen()->soft_keys(ctx, keys);
  unsigned mask = knob_row;
  for (unsigned k = 0; k < app::kFKeyCount; ++k) {
    if (keys.label[k] != nullptr && keys.label[k][0] != '\0') {
      mask |= 1u << k;
    }
  }
  return mask;
}

unsigned page_mask(const Page& p) {
  unsigned mask = 0;
  for (unsigned k = 0; k < kKeys; ++k) {
    if (p.entry[k].item != Item::Blank) {
      mask |= 1u << k;
    }
  }
  return mask;
}

// What a screen must say when HELP is open over it: the Text of its mode (the summary is that Text's), the limits, a text on no key it leaves unlabelled, and (`pads`) a group on every pad it uses.
void check_screen(sim::Harness& h, Id id, unsigned knob_row, std::uint64_t pads, const char* title) {
  CAPTURE(title);
  REQUIRE(std::strcmp(h.runtime().context().screen()->title(), title) == 0);
  const unsigned labels = labelled(h, knob_row);
  const Help& help = open_help(h);
  CHECK(help.summary == text(id).summary);
  check_help(help, text(id).name);
  check_pads_grouped(help, pads, text(id).name);
  for (unsigned k = 0; k < app::kFKeyCount; ++k) {
    if (help.fkey[k] != nullptr) {
      CHECK_MESSAGE((labels >> k & 1u) != 0u, text(id).name << ": F" << k + 1 << " has a text and no label on the screen");
    }
  }
  // The kit takes the Text without cutting: the BUTTONS page shows a row of every entry (it scrolls past what fits).
  CHECK(h.runtime().help_overlay().rows() > 0u);
  close_help(h);
}

}  // namespace

TEST_CASE("help screens: HOME, PATTERN SETUP, PERFORM, PERFORM SETUP, INSTRUMENT, MODDED, GROWL, WOBBLE, MASTER and RECORD each give the kit their own Text") {
  Hands t;
  sim::Harness& h = *t.h;
  const unsigned sound = page_mask(page_at(kPageSound));
  check_screen(h, Id::Home, sound, kAllPads, "STEPS");
  tap(h, app::Button::Pattern);
  check_screen(h, Id::Setup, page_mask(setup_page()), kAllPads, "PATTERN SETUP");
  tap(h, app::Button::Pattern);
  check_screen(h, Id::Home, sound, kAllPads, "STEPS");
  tap(h, app::Button::Performance);
  check_screen(h, Id::Perform, sound, kAllPads, "PERFORM");
  tap(h, app::Button::Performance);
  check_screen(h, Id::Knobs, sound, kAllPads, "PERFORM KNOBS");
  shift_tap(h, app::Button::Performance);  // R8 (the review's S1): Shift + Performance is PERFORM SETUP
  check_screen(h, Id::PerformSetup, page_mask(page_at(kPagePerfSetup)), kAllPads, "PERFORM SETUP");
  tap(h, app::Button::Params);
  {
    app::Context& ctx = h.runtime().context();
    const auto* inst = static_cast<InstrumentScreen*>(ctx.screen());
    check_screen(h, Id::Fx, page_mask(inst->page_table()), kAllPads, "INSTRUMENT FX");
  }
  tap(h, app::Button::Params);
  check_screen(h, Id::Voice, sound, kAllPads, "INSTRUMENT VOICE");
  tap(h, app::Button::SampleLoad);
  {
    app::Context& ctx = h.runtime().context();
    const auto* inst = static_cast<InstrumentScreen*>(ctx.screen());
    check_screen(h, Id::ModdedNotes, page_mask(inst->page_table()), kAllPads, "MODDED NOTES");
  }
  tap(h, app::Button::SampleLoad);
  {
    app::Context& ctx = h.runtime().context();
    const auto* inst = static_cast<InstrumentScreen*>(ctx.screen());
    check_screen(h, Id::ModdedTone, page_mask(inst->page_table()), kAllPads, "MODDED TONE");
  }
  tap(h, app::Button::SamplePlay);
  {
    app::Context& ctx = h.runtime().context();
    const auto* inst = static_cast<InstrumentScreen*>(ctx.screen());
    check_screen(h, Id::Tract, page_mask(inst->page_table()), kTractPads, "TRACT");  // Sample Playback opens TRACT (A2): F1 to F7 the knobs, F8 the on / off box
  }
  tap(h, app::Button::SamplePlay);  // again: GROWL
  {
    app::Context& ctx = h.runtime().context();
    const auto* inst = static_cast<InstrumentScreen*>(ctx.screen());
    check_screen(h, Id::Growl, page_mask(inst->page_table()), kAllPads, "GROWL");
  }
  tap(h, app::Button::SampleEdit);
  {
    app::Context& ctx = h.runtime().context();
    const auto* inst = static_cast<InstrumentScreen*>(ctx.screen());
    check_screen(h, Id::Wobble, page_mask(inst->page_table()), kAllPads, "WOBBLE");
  }
  tap(h, app::Button::Master);
  check_screen(h, Id::Master, 0x1Fu, kAllPads, "MASTER");  // the knob row of MASTER: F1 TEMPO, F2 TAP, F3 NUDGE, F4 MASTER, F5 DLY MODE (master_screen.cpp kPage and the two action boxes)
  tap(h, app::Button::SampleRec);
  check_screen(h, Id::Record, 0x0Fu, kAllPads, "RECORD");  // F1 ARM / STOP, F2 BARS, F3 SAVE, F4 DISCARD; F5 to F8 have no box (record_screen.cpp)
  tap(h, app::Button::Song);
  check_screen(h, Id::Song, 0x0Fu, kAllPads, "SONG");  // F1 PLAYS, F2 LOOP, F3 CHAIN, F4 CLEAR (song_screen.cpp song_knobs)
  tap(h, app::Button::File);
  tap(h, app::Button::File);
  check_screen(h, Id::Library, 0u, kAllPads, "FILE LIBRARY");
}

TEST_CASE("help texts: Sample Loader opens MODDED (NEXT PAGE on it) and with Shift flips STOCK on every screen; Sample Playback opens TRACT (NEXT PAGE on TRACT and GROWL) and Sample Editor opens WOBBLE from every screen but its own") {
  for (unsigned i = 0; i < kIdCount; ++i) {
    const Text& t = text(static_cast<Id>(i));
    const HelpButton* play = nullptr;
    const HelpButton* edit = nullptr;
    const HelpButton* load = nullptr;
    for (unsigned b = 0; b < t.button_count; ++b) {
      if (t.buttons[b].button == app::Button::SamplePlay) {
        play = &t.buttons[b];
      }
      if (t.buttons[b].button == app::Button::SampleEdit) {
        edit = &t.buttons[b];
      }
      if (t.buttons[b].button == app::Button::SampleLoad) {
        load = &t.buttons[b];
      }
    }
    REQUIRE_MESSAGE(load != nullptr, t.name << ": Sample Loader is not listed");
    const bool on_modded = static_cast<Id>(i) == Id::ModdedNotes || static_cast<Id>(i) == Id::ModdedTone;
    CHECK_MESSAGE(std::strcmp(load->text, on_modded ? "NEXT PAGE: NOTES, TONE" : "OPEN MODDED") == 0, t.name);
    CHECK_MESSAGE(std::strcmp(load->shift, "MODS: HEARD / PLAIN 303") == 0, t.name << ": Shift + Sample Loader");
    REQUIRE_MESSAGE(play != nullptr, t.name);
    const bool on_play = static_cast<Id>(i) == Id::Tract || static_cast<Id>(i) == Id::Growl;
    CHECK_MESSAGE(std::strcmp(play->text, on_play ? "NEXT PAGE: TRACT, GROWL" : "OPEN TRACT") == 0, t.name);
    REQUIRE_MESSAGE(play->shift != nullptr, t.name << ": Shift + Sample Playback does the same, and HELP says so (U8)");
    CHECK_MESSAGE(std::strcmp(play->shift, on_play ? "SAME: NEXT PAGE" : "SAME: OPEN TRACT") == 0, t.name);
    if (static_cast<Id>(i) == Id::Wobble) {
      CHECK_MESSAGE(edit == nullptr, t.name << ": Sample Editor does nothing on WOBBLE (it is drawn dimmed)");
    } else {
      REQUIRE_MESSAGE(edit != nullptr, t.name);
      CHECK_MESSAGE(std::strcmp(edit->text, "OPEN WOBBLE") == 0, t.name);
      REQUIRE_MESSAGE(edit->shift != nullptr, t.name << ": Shift + Sample Editor does the same, and HELP says so (U8)");
      CHECK_MESSAGE(std::strcmp(edit->shift, "SAME: OPEN WOBBLE") == 0, t.name);
    }
  }
}

TEST_CASE("help texts: the Song button opens SONG from every screen but SONG itself") {
  for (unsigned i = 0; i < kIdCount; ++i) {
    const Text& t = text(static_cast<Id>(i));
    const HelpButton* song = nullptr;
    for (unsigned b = 0; b < t.button_count; ++b) {
      if (t.buttons[b].button == app::Button::Song) {
        song = &t.buttons[b];
      }
    }
    if (static_cast<Id>(i) == Id::Song) {
      CHECK_MESSAGE(song == nullptr, t.name << ": the Song button does nothing on SONG (it is drawn dimmed)");
    } else {
      REQUIRE_MESSAGE(song != nullptr, t.name << ": the Song button is not listed");
      CHECK_MESSAGE(std::strcmp(song->text, "OPEN SONG") == 0, t.name);
    }
  }
}

TEST_CASE("help texts: SONG says what its buttons, F keys and pads do") {
  const Text& t = text(Id::Song);
  const auto says = [&](app::Button b, const char* what) {
    for (unsigned i = 0; i < t.button_count; ++i) {
      if (t.buttons[i].button == b) {
        return t.buttons[i].text != nullptr && std::strstr(t.buttons[i].text, what) != nullptr;
      }
    }
    return false;
  };
  CHECK(says(app::Button::Up, "ROW"));
  CHECK(says(app::Button::Left, "PAGE"));
  CHECK(says(app::Button::Insert, "ADD"));
  CHECK(says(app::Button::Delete, "REMOVE"));
  CHECK(says(app::Button::Note, "PATTERN"));
  CHECK(says(app::Button::Instr, "REPEAT"));
  CHECK(says(app::Button::Vol, "TRANSPOSE"));
  CHECK(says(app::Button::Fx, "MUTE"));
  CHECK(t.fkey[0] != nullptr);
  CHECK(std::strcmp(t.fkey[3], "PRESS TWICE") == 0);  // CLEAR
  CHECK(t.fkey[4] == nullptr);
  CHECK(t.fkey[7] == nullptr);  // the plain F8 UNDO is gone
  CHECK(std::strcmp(t.fkey_shift[6], "REDO") == 0);
  CHECK(std::strcmp(t.fkey_shift[7], "UNDO") == 0);
  Help h;
  fill(h, t);
  fill_song_pads(h);
  CHECK(std::strcmp(h.pad_label[0], "A01") == 0);
  CHECK(std::strcmp(h.pad_label[11], "A12") == 0);
  CHECK(std::strcmp(h.pad_label[12], "B01") == 0);
  CHECK(std::strcmp(h.pad_label[47], "D12") == 0);
  check_pads_grouped(h, kAllPads, "SONG");
  check_help(h, "SONG");
}

TEST_CASE("help screens: the Shift row is the screen's own") {
  Hands t;
  sim::Harness& h = *t.h;
  {  // HOME: NEW VARY RHYTHM NOTES REVERSE CLEAR REDO UNDO, "COPY PAT" .. on PATTERN SETUP with its free keys left out
    const Help& help = open_help(h);
    REQUIRE(help.fkey_shift[0] != nullptr);
    CHECK(std::strcmp(help.fkey_shift[0], "NEW") == 0);
    CHECK(std::strcmp(help.fkey_shift[4], "REVERSE") == 0);
    CHECK(std::strcmp(help.fkey_shift[7], "UNDO") == 0);
    close_help(h);
  }
  tap(h, app::Button::Pattern);
  {
    const Help& help = open_help(h);
    CHECK(std::strcmp(help.fkey_shift[0], "COPY PAT") == 0);
    CHECK(std::strcmp(help.fkey_shift[2], "DUP>NEXT") == 0);
    CHECK(std::strcmp(help.fkey_shift[4], "ROOT") == 0);
    CHECK(help.fkey_shift[5] == nullptr);
    CHECK(std::strcmp(help.fkey_shift[6], "REDO") == 0);  // U4: REDO on Shift + F7 wherever you edit
    CHECK(std::strcmp(help.fkey_shift[7], "UNDO") == 0);
    close_help(h);
  }
  tap(h, app::Button::Song);  // SONG: REDO and UNDO, the two keys of the Shift row
  {
    const Help& help = open_help(h);
    CHECK(help.fkey_shift[0] == nullptr);
    CHECK(help.fkey_shift[5] == nullptr);
    CHECK(std::strcmp(help.fkey_shift[6], "REDO") == 0);
    CHECK(std::strcmp(help.fkey_shift[7], "UNDO") == 0);
    close_help(h);
  }
  tap(h, app::Button::Performance);
  {
    const Help& help = open_help(h);
    CHECK(help.fkey_shift[0] == nullptr);
    CHECK(std::strcmp(help.fkey_shift[6], "REDO") == 0);
    CHECK(std::strcmp(help.fkey_shift[7], "UNDO") == 0);
    close_help(h);
  }
  tap(h, app::Button::Performance);  // PERFORM KNOBS: FACTORY 1..4 on F1..F4, REDO and UNDO as on PERFORM
  {
    const Help& help = open_help(h);
    CHECK(std::strcmp(help.fkey_shift[0], "FACTORY 1") == 0);
    CHECK(std::strcmp(help.fkey_shift[3], "FACTORY 4") == 0);
    CHECK(help.fkey_shift[4] == nullptr);
    CHECK(std::strcmp(help.fkey_shift[6], "REDO") == 0);
    CHECK(std::strcmp(help.fkey_shift[7], "UNDO") == 0);
    close_help(h);
  }
  shift_tap(h, app::Button::Performance);  // PERFORM SETUP has no Shift row
  {
    const Help& help = open_help(h);
    for (unsigned k = 0; k < app::kFKeyCount; ++k) {
      CHECK(help.fkey_shift[k] == nullptr);
    }
    close_help(h);
  }
  tap(h, app::Button::Params);  // INSTRUMENT FX, then the next page: VOICE
  tap(h, app::Button::Params);  // INSTRUMENT VOICE: LEARN on the unit's parameters, nothing on VOLUME (F8, an app value)
  {
    const Help& help = open_help(h);
    CHECK(std::strcmp(help.fkey_shift[0], "LEARN") == 0);
    CHECK(std::strcmp(help.fkey_shift[6], "LEARN") == 0);
    CHECK(help.fkey_shift[7] == nullptr);
    close_help(h);
  }
  tap(h, app::Button::File);
  tap(h, app::Button::File);  // FILE LIBRARY: NEW SEED and LOAD LINE, the default (what_ cycles with the key)
  {
    const Help& help = open_help(h);
    CHECK(std::strcmp(help.fkey_shift[0], "NEW SEED") == 0);
    CHECK(std::strcmp(help.fkey_shift[1], "LOAD LINE") == 0);
    close_help(h);
  }
  h.press(app::Button::Shift);  // Shift + F2 cycles what a load takes: the Shift row's key and HELP's say the same
  h.run_ms(2);
  tap(h, app::Button::F2);
  h.release(app::Button::Shift);
  h.run_ms(50);
  {
    const Help& help = open_help(h);
    CHECK(std::strcmp(help.fkey_shift[1], "LOAD SOUND") == 0);
    close_help(h);
  }
}

TEST_CASE("help screens: the home map's pads follow the pattern, the octave, the scale lock and FILT") {
  Hands t;
  sim::Harness& h = *t.h;
  t.length_steps(-1);  // 16 -> 8 steps
  REQUIRE(t.pat().length == 8);
  {
    const Help& help = open_help(h);
    check_help(help, "8 steps");
    CHECK(help.pad_group[7] == 1);   // step 8
    CHECK(help.pad_group[12] == 0);  // step 9: past the end, dark
    CHECK(std::strcmp(help.pad_label[36], "C2") == 0);
    close_help(h);
  }
  t.length_steps(1);
  t.pad(role_pad(PadRole::OctUp), 10);  // the keyboard an octave up
  {
    const Help& help = open_help(h);
    CHECK(std::strcmp(help.pad_label[36], "C3") == 0);
    CHECK(std::strcmp(help.pad_label[24], "C4") == 0);
    close_help(h);
  }
  t.pad(role_pad(PadRole::OctDown), 10);
  t.choose_scale(3);  // Major: every key still plays (KEYS: ALL), so no pad is dark
  {
    const Help& help = open_help(h);
    check_help(help, "scale shown, KEYS: ALL");
    for (unsigned pad = 24; pad < app::kPadCount; ++pad) {
      CHECK_MESSAGE(help.pad_group[pad] == 3, "pad " << pad);
    }
    close_help(h);
  }
  t.config_set(o3_303::config::kKeys, 1.0f);  // KEYS: IN SCALE: the keys outside the scale are dark
  {
    const Help& help = open_help(h);
    check_help(help, "scale locked");
    unsigned dark = 0;
    for (unsigned pad = 24; pad < app::kPadCount; ++pad) {
      dark += help.pad_group[pad] == 0u ? 1u : 0u;
    }
    CHECK(dark > 0);
    CHECK(dark < 24);
    CHECK(help.pad_group[36] == 3);  // the first key (C2)
    // Major on C: only C D E F G A B have a group, the five other keys are dark.
    for (unsigned pad = 24; pad < app::kPadCount; ++pad) {
      const unsigned pc = static_cast<unsigned>(key_note(pad, 0)) % 12u;
      const bool in_scale = pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
      CHECK_MESSAGE((help.pad_group[pad] != 0u) == in_scale, "pad " << pad);
    }
    close_help(h);
  }
  t.pad(role_pad(PadRole::Filt), 10);  // rows 3 and 4 are the ladders
  {
    const Help& help = open_help(h);
    CHECK(std::strcmp(help.groups[2].name, "LADDERS") == 0);
    CHECK(std::strcmp(help.pad_label[24], "ROW3") == 0);
    close_help(h);
  }
}

TEST_CASE("help screens: PERFORM's keys outside a locked scale are dark, as its LEDs and key presses have them") {
  Hands t;
  sim::Harness& h = *t.h;
  t.choose_scale(3);  // Major on C
  t.config_set(o3_303::config::kKeys, 1.0f);  // KEYS: IN SCALE
  t.to_perform();
  REQUIRE(std::strcmp(h.runtime().context().screen()->title(), "PERFORM") == 0);
  const Help& help = open_help(h);
  check_help(help, "perform, scale locked");
  unsigned lit = 0;
  unsigned dark = 0;
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    if (perform_pad(pad).role != PerformRole::Key) {
      continue;
    }
    const unsigned pc = static_cast<unsigned>(key_note(pad, 0)) % 12u;
    const bool in_scale = pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
    CHECK_MESSAGE((help.pad_group[pad] != 0u) == in_scale, "pad " << pad);
    CHECK_MESSAGE((help.pad_label[pad] != nullptr) == in_scale, "pad " << pad);
    (in_scale ? lit : dark) += 1u;
  }
  CHECK(lit == 7);
  CHECK(dark == 5);
  close_help(h);
}

TEST_CASE("help screens: FILE PROJECTS, its list and its name keyboard (with a card), and the app is as it was after HELP closes") {
  host::ScratchDir dir{"o3303-help"};
  host::DirCard card{dir.path()};
  REQUIRE(dir.ok());
  auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, static_cast<host::HostCard*>(&card), "o3_303");
  REQUIRE(h->start());
  h->run_ms(300);
  tap(*h, app::Button::File);
  h->run_ms(100);
  check_screen(*h, Id::File, 0u, 0u, "FILE PROJECTS");  // the soft-key row labels F1..F6 and F8
  {
    const Help& help = open_help(*h);
    CHECK(help.group_count == 1);
    // Only the pads of rows that show a project have the group; with the card's list read there is at least the current project.
    unsigned grouped = 0;
    for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
      grouped += help.pad_group[pad] != 0u ? 1u : 0u;
      if (help.pad_group[pad] != 0u) {
        CHECK(pad < 12);
      }
    }
    CHECK(grouped <= 12);
    close_help(*h);
  }
  tap(*h, app::Button::F3);  // SAVE AS: the name keyboard
  h->run_ms(50);
  check_screen(*h, Id::FileNaming, 0u, kAllPads & ~kRefusedKeyPads, "FILE PROJECTS");
  // HELP closed: the keyboard is still open and takes F8 (cancel); the app is where it was.
  tap(*h, app::Button::F8);
  h->run_ms(50);
  check_screen(*h, Id::File, 0u, 0u, "FILE PROJECTS");
}

TEST_CASE("the heard-on table follows the voice's mods: a line that fits the strip, none for a mod heard on every note, ACCENT 0 named on the accent mods") {
  using o3_303::AcidVoice;
  for (unsigned id = AcidVoice::kFirstMod; id < AcidVoice::Stock; ++id) {
    const auto h = o3_303::help_texts::heard_on(id);
    if (h.line == nullptr) {
      CHECK(std::strlen(o3_303::help_texts::heard_line(id, false)) == 0);
      CHECK(std::strlen(o3_303::help_texts::heard_line(id, true)) == 0);
      continue;
    }
    CHECK(std::strlen(h.line) <= o3_303::help_texts::kHeardMax);
    CHECK(std::strlen(o3_303::help_texts::heard_line(id, true)) <= o3_303::help_texts::kHeardMax);
    CHECK(std::strcmp(o3_303::help_texts::heard_line(id, false), h.line) == 0);
    CHECK((std::strcmp(o3_303::help_texts::heard_line(id, true), "ACCENT IS 0: NOT HEARD") == 0) == h.needs_accent);
  }
  CHECK(o3_303::help_texts::heard_on(AcidVoice::SubOsc).line == nullptr);
  CHECK(o3_303::help_texts::heard_on(AcidVoice::Muffler).line == nullptr);
  CHECK(o3_303::help_texts::heard_on(AcidVoice::AccDecay).accent);
  CHECK_FALSE(o3_303::help_texts::heard_on(AcidVoice::AccDecay).needs_accent);  // vcf_c and end_g follow has_accent_, not ACCENT
  CHECK(o3_303::help_texts::heard_on(AcidVoice::AccSweep).needs_accent);
  CHECK(o3_303::help_texts::heard_on(AcidVoice::SwpSpeed).needs_accent);
  CHECK(o3_303::help_texts::heard_on(AcidVoice::Slide).slide);
  CHECK_FALSE(o3_303::help_texts::heard_on(AcidVoice::Slide).accent);
  CHECK(o3_303::help_texts::heard_on(AcidVoice::AccDecay - 1).line == nullptr);
  CHECK(o3_303::help_texts::heard_on(AcidVoice::Stock).line == nullptr);
}
