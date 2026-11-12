// SPDX-License-Identifier: GPL-3.0-or-later
// MODDED (the Sample Loader button; docs/MODS-PLAN.md 4.1, 4.3, 5.3, 5.8, 5.10 to 5.12) and STOCK: the two pages and their keys, the Note key flips MODS, MIDI learn on the mods and "-" on
// STOCK, the locks and a recorded turn on a mod and on a switch, the Note key flipping STOCK from every screen with the toast and the chip, a turn of a mod ending STOCK (and a
// turn of DECAY, a lock or a controller not doing so), STOCK off after a boot, a load, NEW, REVERT and RESET PROJECT, the Fx key and LiveActions::toggle_latch(), and the render with every
// mod at an extreme and STOCK on being, bit for bit, a fresh project's (the A/B of mods_stock_ab).
#include <doctest/doctest.h>

#include "hands.h"

#include <o3_303/units/growl/growl.h>

#include "../app/src/frame.h"
#include "../app/src/home_map.h"
#include "../app/src/instrument_screen.h"
#include "../app/src/main_screen.h"
#include "../app/src/project_service.h"

#include <o3sdk/core/param.h>
#include <o3sdk/platform/host/dir_card.h>
#include <o3sdk/platform/host/fake_midi.h>
#include <o3_303/app_access.h>
#include <o3_303/seq/ops.h>
#include <o3_303/units/acid_voice/acid_voice.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace hands;
using namespace o3_303;
namespace host = o3sdk::platform::host;
using Port = host::FakeMidi::Port;

namespace {

constexpr unsigned kMods = AcidVoice::Stock - AcidVoice::kFirstMod;  // the eleven mods: voice ids 8..18
static_assert(kMods == 11, "eleven mods");

InstrumentScreen& screen_of(Hands& t) { return *static_cast<InstrumentScreen*>(t.h->runtime().context().screen()); }
HomeMap& home_of(Hands& t) { return static_cast<MainScreen*>(t.h->runtime().context().screen())->home_map(); }
bool is_title(Hands& t, const char* title) { return std::strcmp(t.title(), title) == 0; }

// STOCK as the voice holds it (the parameter) and as the screens show it (the chip and its words, from HOME's status; this goes to HOME, so call it last).
bool stock(Hands& t) { return t.param(kVoice, AcidVoice::Stock) >= 0.5f; }
struct Status {
  bool chip = false;
  std::string said;
};
Status status_on_home(Hands& t) {
  t.to_home();
  frame::StatusView v;
  home_of(t).status(v);
  return Status{v.stock, v.take_msg};
}

// The page's knob `k` as the screen draws it.
view::Knob knob_of(Hands& t, unsigned k) {
  InstrumentView v;
  screen_of(t).fill_view(t.h->runtime().context(), v);
  return v.knob[k];
}

// F key `k` of the page shown moves the parameter (unit, id) one way or the other.
bool key_moves(Hands& t, unsigned k, unsigned id) {
  t.tap(app::fkey(k));
  const float before = t.param(kVoice, id);
  t.turn(7);
  const float up = t.param(kVoice, id);
  t.turn(-14);
  const float down = t.param(kVoice, id);
  return up != before || down != before;
}

const AcidVoice::Param kNotesIds[7] = {AcidVoice::SoftAtk, AcidVoice::Decay, AcidVoice::AccDecay, AcidVoice::AmpDecay, AcidVoice::Slide, AcidVoice::AccSweep, AcidVoice::SwpSpeed};
const AcidVoice::Param kToneIds[7] = {AcidVoice::Tracking, AcidVoice::FilterFm, AcidVoice::Overdrive, AcidVoice::Muffler, AcidVoice::SubOsc, AcidVoice::Range, AcidVoice::Sustain};


}  // namespace

// ---- The pages -----------------------------------------------------------------------------------------------------------------------------

TEST_CASE("MODDED: the titles, the tabs and the eight keys of NOTES and TONE (docs/MODS-PLAN.md 5.3)") {
  Hands t;
  t.to_modded();
  CHECK(is_title(t, "MODDED NOTES"));
  const InstrumentGroup& g = screen_of(t).group();
  REQUIRE(g.count == 2);
  CHECK(std::strcmp(g.tabs[0], "NOTES") == 0);
  CHECK(std::strcmp(g.tabs[1], "TONE") == 0);
  const Page& notes = screen_of(t).page_table();
  for (unsigned k = 0; k < 7; ++k) {
    INFO("NOTES F" << k + 1);
    CHECK(notes.entry[k].item == Item::Voice);
    CHECK(notes.entry[k].id == kNotesIds[k]);
  }
  CHECK(notes.entry[7].item == Item::Blank);  // F8 is empty (E1b): the Note key flips MODS
  t.tap(Button::SampleLoad);
  CHECK(is_title(t, "MODDED TONE"));
  const Page& tone = screen_of(t).page_table();
  for (unsigned k = 0; k < 7; ++k) {
    INFO("TONE F" << k + 1);
    CHECK(tone.entry[k].item == Item::Voice);
    CHECK(tone.entry[k].id == kToneIds[k]);
  }
  CHECK(tone.entry[7].item == Item::Blank);
}

TEST_CASE("MODDED: every key of NOTES and TONE reaches its parameter, and every label fits the strip's nine characters") {
  Hands t;
  t.to_modded();
  for (unsigned k = 0; k < 7; ++k) {
    INFO("NOTES F" << k + 1);
    CHECK(key_moves(t, k, kNotesIds[k]));
    CHECK(std::strlen(knob_of(t, k).label) <= 9);
  }
  CHECK(std::strlen(knob_of(t, 7).label) <= 9);
  t.tap(Button::SampleLoad);
  for (unsigned k = 0; k < 7; ++k) {
    INFO("TONE F" << k + 1);
    CHECK(key_moves(t, k, kToneIds[k]));
    CHECK(std::strlen(knob_of(t, k).label) <= 9);
  }
  CHECK_FALSE(stock(t));  // none of the above ended or began STOCK
}

TEST_CASE("MODDED: each page keeps its own latch, and DECAY (F2 of NOTES) is the VOICE page's own knob") {
  Hands t;
  t.to_modded();
  t.tap(Button::F3);  // ACC DECAY latched on NOTES
  t.tap(Button::SampleLoad);
  t.tap(Button::F4);  // MUFFLER latched on TONE
  CHECK(screen_of(t).focus() == 3);
  t.tap(Button::SampleLoad);
  CHECK(screen_of(t).focus() == 2);  // NOTES: ACC DECAY is still the latched key
  const float acc0 = t.param(kVoice, AcidVoice::AccDecay);
  t.turn(5);
  CHECK(t.param(kVoice, AcidVoice::AccDecay) != acc0);
  // DECAY is the one parameter VOICE F4 also drives.
  t.tap(Button::F2);
  const float decay0 = t.param(kVoice, AcidVoice::Decay);
  t.turn(5);
  CHECK(t.param(kVoice, AcidVoice::Decay) != decay0);
  t.to_instrument(kInstVoice);
  CHECK(t.param(kVoice, AcidVoice::Decay) != decay0);
}

// ---- MIDI learn ---------------------------------------------------------------------------------------------------------------------------

TEST_CASE("MODDED: the Shift row is LEARN on every mod and DECAY, and '-' on STOCK and the empty keys; Shift + F8 does not arm") {
  host::FakeMidi midi;
  Hands t(midi);
  app::Context& ctx = t.h->runtime().context();
  const auto row = [&](unsigned k) {
    app::SoftKeys keys;
    screen_of(t).soft_keys(ctx, keys);
    return std::string(keys.label[k] != nullptr ? keys.label[k] : "");
  };
  t.to_modded();
  t.press(Button::Shift);
  for (unsigned k = 0; k < 7; ++k) {
    INFO("NOTES F" << k + 1);
    CHECK(row(k) == "LEARN");
  }
  CHECK(row(7) == "-");
  t.release(Button::Shift);
  t.shift_tap(Button::F8);
  CHECK_FALSE(ctx.midi().learning());
  t.tap(Button::SampleLoad);
  t.press(Button::Shift);
  const char* tone[8] = {"LEARN", "LEARN", "LEARN", "LEARN", "LEARN", "LEARN", "LEARN", "-"};
  for (unsigned k = 0; k < 8; ++k) {
    INFO("TONE F" << k + 1);
    CHECK(row(k) == tone[k]);
  }
  t.release(Button::Shift);
  t.shift_tap(Button::F8);
  CHECK_FALSE(ctx.midi().learning());
  t.shift_tap(Button::F5);  // SUB OSC: a unit parameter
  CHECK(ctx.midi().learning());
  t.to_home();
  CHECK_FALSE(ctx.midi().learning());  // leaving drops it
  CHECK_FALSE(stock(t));
}

TEST_CASE("learn_target: the eleven mods and DECAY are unit parameters; STOCK and WOB are not") {
  Wiring w;
  w.voice = 0;
  dsp::UnitId unit = dsp::kNoUnit;
  std::uint16_t param = 99;
  for (unsigned id = AcidVoice::kFirstMod; id < AcidVoice::Stock; ++id) {
    CHECK(learn_target(w, Entry{Item::Voice, static_cast<std::uint16_t>(id), nullptr}, unit, param));
    CHECK(param == id);
  }
  CHECK(learn_target(w, Entry{Item::Voice, AcidVoice::Decay, nullptr}, unit, param));
  CHECK_FALSE(learn_target(w, Entry{Item::Mods, 0, nullptr}, unit, param));
  CHECK_FALSE(learn_target(w, Entry{Item::WobOn, 0, nullptr}, unit, param));
}

// ---- F8 STOCK, and a mod's turn --------------------------------------------------------------------------------------------------------

TEST_CASE("The Note key flips MODS on MODDED (E1b): STOCK flips at the press, F8 is empty, and the encoder and the latch stay where they were") {
  Hands t;
  t.to_modded();
  t.tap(Button::F3);  // ACC DECAY latched
  REQUIRE(screen_of(t).focus() == 2);
  CHECK_FALSE(stock(t));
  t.press(Button::Note);
  CHECK(stock(t));  // at the press, not at the release
  t.release(Button::Note);
  CHECK(stock(t));
  CHECK(screen_of(t).focus() == 2);
  CHECK_FALSE(knob_of(t, 2).lit);
  t.tap(Button::Note);
  CHECK_FALSE(stock(t));
  // The same key on TONE; F8 is nothing there, and Shift + Note is the PERFORM action (no STOCK).
  t.tap(Button::SampleLoad);
  t.tap(Button::Note);
  CHECK(stock(t));
  t.tap(Button::F8);
  CHECK(stock(t));
  t.shift_tap(Button::Note);
  CHECK(stock(t));
}

TEST_CASE("a turn of a mod ends STOCK with the words MODS ON; DECAY, a lock and a controller do not end it") {
  host::FakeMidi midi;
  Hands t(midi);
  app::Context& ctx = t.h->runtime().context();
  t.to_modded();
  t.tap(Button::Note);
  REQUIRE(stock(t));

  // DECAY is not a mod.
  t.tap(Button::F2);
  const float decay0 = t.param(kVoice, AcidVoice::Decay);
  t.turn(5);
  CHECK(t.param(kVoice, AcidVoice::Decay) != decay0);
  CHECK(stock(t));

  // A lock on a mod (hold a step + the encoder) is the step's: STOCK stays, the knob stays.
  t.tap(Button::F3);  // ACC DECAY
  const float acc0 = t.param(kVoice, AcidVoice::AccDecay);
  t.down(step_pad_of(2));
  t.turn(3);
  t.up(step_pad_of(2));
  CHECK(stock(t));
  CHECK(t.param(kVoice, AcidVoice::AccDecay) == acc0);
  CHECK(seq::find_lane(t.pat(), kLaneVoice, AcidVoice::AccDecay) >= 0);

  // A controller bound to a mod writes the stored value and leaves STOCK on.
  t.tap(Button::SampleLoad);  // TONE
  t.shift_tap(Button::F2);  // FILTER FM
  REQUIRE(ctx.midi().learning());
  midi.in(Port::Usb, 0xB0, 20, 0x7F);
  t.wait(30);
  midi.in(Port::Usb, 0xB0, 20, 0);
  t.wait(30);
  midi.in(Port::Usb, 0xB0, 20, 0x7F);
  t.wait(30);
  CHECK(t.param(kVoice, AcidVoice::FilterFm) > 0.9f);
  CHECK(stock(t));

  // The turn of a mod: STOCK goes first and the value is the turn's.
  t.tap(Button::F4);  // MUFFLER
  t.turn(1);
  CHECK_FALSE(stock(t));
  CHECK(t.param(kVoice, AcidVoice::Muffler) > 0.0f);
  const Status s = status_on_home(t);
  CHECK_FALSE(s.chip);
  CHECK(s.said == "MODS ON");
}

TEST_CASE("a turn of RANGE or SUSTAIN ends STOCK too: under STOCK they would be silent") {
  for (const Button key : {Button::F6, Button::F7}) {
    Hands t;
    t.tap(Button::SampleLoad);  // MODDED
    t.tap(Button::SampleLoad);  // TONE
    t.tap(Button::Note);        // STOCK on
    REQUIRE(stock(t));
    t.tap(key);
    t.turn(1);
    CHECK_FALSE(stock(t));
    CHECK(t.param(kVoice, key == Button::F6 ? AcidVoice::Range : AcidVoice::Sustain) > 0.0f);
  }
}

TEST_CASE("a turn of a mod with STOCK off leaves it off and says nothing; a turn with a step held writes a lock and ends nothing") {
  Hands t;
  t.to_modded();
  t.tap(Button::F3);  // ACC DECAY
  const float acc0 = t.param(kVoice, AcidVoice::AccDecay);
  t.turn(5);
  CHECK_FALSE(stock(t));
  CHECK(t.param(kVoice, AcidVoice::AccDecay) != acc0);
  CHECK(status_on_home(t).said.empty());  // (this goes to HOME: back to NOTES for the rest)
  t.to_modded();
  t.tap(Button::F3);

  // A step held: the turn is the step's lock on the mod, the knob stays, and STOCK is as it was, off and then on.
  const float acc1 = t.param(kVoice, AcidVoice::AccDecay);
  t.down(step_pad_of(3));
  t.turn(2);
  t.up(step_pad_of(3));
  CHECK_FALSE(stock(t));
  CHECK(t.param(kVoice, AcidVoice::AccDecay) == acc1);
  const int lane = seq::find_lane(t.pat(), kLaneVoice, AcidVoice::AccDecay);
  REQUIRE(lane >= 0);
  CHECK(step_lock_point(t.pat(), static_cast<unsigned>(lane), 3) != kNoLock);
  t.tap(Button::Note);  // STOCK on
  REQUIRE(stock(t));
  t.down(step_pad_of(4));
  t.turn(2);
  t.up(step_pad_of(4));
  CHECK(stock(t));
  CHECK(step_lock_point(t.pat(), static_cast<unsigned>(lane), 4) != kNoLock);
  CHECK(status_on_home(t).said != "MODS ON");
}

// ---- Lanes ----------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("MODDED: a lock on a mod and on a switch claims a lane and plays; the mods and the growl's seven are lane targets, STOCK is not") {
  Hands t;
  const core::ParamDesc* acc = t.h->engine().param_desc(kVoice, AcidVoice::AccDecay);
  const core::ParamDesc* sub = t.h->engine().param_desc(kVoice, AcidVoice::SubOsc);
  REQUIRE(acc != nullptr);
  REQUIRE(sub != nullptr);
  const float acc0 = t.param(kVoice, AcidVoice::AccDecay);
  t.to_modded();
  t.tap(Button::F3);  // ACC DECAY
  t.down(step_pad_of(2));
  t.turn(3);
  t.up(step_pad_of(2));
  int lane = seq::find_lane(t.pat(), kLaneVoice, AcidVoice::AccDecay);
  REQUIRE(lane >= 0);
  CHECK(step_lock_point(t.pat(), static_cast<unsigned>(lane), 2) == seq::value_to_point(*acc, core::param_step(*acc, acc0, 3)));
  CHECK(t.param(kVoice, AcidVoice::AccDecay) == acc0);  // a lock is the step's

  // A switch locks like WAVE: SUB OSC three detents up is HIGH.
  t.tap(Button::SampleLoad);
  t.tap(Button::F5);  // SUB OSC
  t.down(step_pad_of(4));
  t.turn(3);
  t.up(step_pad_of(4));
  lane = seq::find_lane(t.pat(), kLaneVoice, AcidVoice::SubOsc);
  REQUIRE(lane >= 0);
  CHECK(step_lock_point(t.pat(), static_cast<unsigned>(lane), 4) == seq::value_to_point(*sub, 3.0f));
  CHECK(t.param(kVoice, AcidVoice::SubOsc) == 0.0f);

  // Every mod has a lane target (the SOUND page's eight are lanes 0..7; the others claim one), and STOCK has none.
  Rig rig;
  for (unsigned id = AcidVoice::kFirstMod; id < AcidVoice::Stock; ++id) {
    LaneRef ref{kLaneNone, 0};
    CHECK(rig.controls.lane_ref(Entry{Item::Voice, static_cast<std::uint16_t>(id), nullptr}, ref));
    CHECK(ref.target == kLaneVoice);
    CHECK(ref.id == id);
  }
  LaneRef none{kLaneNone, 0};
  CHECK_FALSE(rig.controls.lane_ref(Entry{Item::Mods, 0, nullptr}, none));
  CHECK_FALSE(rig.controls.lane_ref(Entry{Item::WobOn, 0, nullptr}, none));
  CHECK_FALSE(rig.controls.lane_ref(Entry{Item::Voice, AcidVoice::Stock, nullptr}, none));  // the lane guard of 5.8
}

TEST_CASE("MODDED: a recorded turn of a mod goes to the lane that drives it") {
  Hands t;
  const core::ParamDesc* fm = t.h->engine().param_desc(kVoice, AcidVoice::FilterFm);
  REQUIRE(fm != nullptr);
  t.tap(Button::Play);
  t.wait(200);
  t.to_modded(1);
  t.tap(Button::F2);  // FILTER FM
  t.tap(Button::Rec);
  t.wait(40);
  t.turn(5);
  t.wait(300);
  t.turn(3);
  t.wait(300);
  const float v = t.param(kVoice, AcidVoice::FilterFm);
  const int lane = seq::find_lane(t.pat(), kLaneVoice, AcidVoice::FilterFm);
  REQUIRE(lane >= 0);
  CHECK(seq::lane_has_motion(t.pat(), static_cast<unsigned>(lane)));
  bool found = false;
  for (unsigned i = 0; i < 16 * kMotionPointsPerStep; ++i) {
    found = found || t.pat().motion[lane][i] == seq::value_to_point(*fm, v);
  }
  CHECK(found);
  t.tap(Button::Play);
}

// ---- The Note key --------------------------------------------------------------------------------------------------------------------

TEST_CASE("The Note key flips STOCK (MODS) from every screen with no change of screen, with the words and the chip") {
  Hands t;
  const auto go = [&](const char* title, auto enter) {
    t.to_home();
    enter();
    REQUIRE_MESSAGE(is_title(t, title), title);
    t.tap(Button::Note);
    CHECK_MESSAGE(is_title(t, title), title << ": the screen stays");
    CHECK_MESSAGE(stock(t), title);
    const Status on = status_on_home(t);  // the chip and the words are every screen's (the status line of the home map is one of them)
    CHECK_MESSAGE(on.chip, title);
    CHECK_MESSAGE(on.said == "MODS OFF", title);
    t.tap(Button::Note);  // HOME: and back off
    CHECK_FALSE(stock(t));
    const Status off = status_on_home(t);
    CHECK_FALSE(off.chip);
    CHECK_MESSAGE(off.said == "MODS ON", title);
  };
  go("PERFORM", [&] { t.tap(Button::Performance); });
  go("MASTER", [&] { t.tap(Button::Master); });
  go("FILE PROJECTS", [&] { t.tap(Button::File); });
  go("SONG", [&] { t.tap(Button::Song); });
  go("RECORD", [&] { t.to_record(); });
  go("INSTRUMENT FX", [&] { t.to_instrument(kInstFx); });
  go("GROWL", [&] { t.to_growl(); });
  go("MODDED TONE", [&] { t.to_modded(1); });
  go("PATTERN SETUP", [&] { t.tap(Button::Pattern); });
  // From HOME itself.
  t.to_home();
  if (is_title(t, "PATTERN SETUP")) {
    t.tap(Button::Pattern);
  }
  REQUIRE(is_title(t, "STEPS"));
  t.tap(Button::Note);
  CHECK(is_title(t, "STEPS"));
  CHECK(stock(t));
}

// ---- STOCK is off at boot, a load, NEW, REVERT and RESET PROJECT ------------------------------------------------------------------------------------

namespace {

// The app on a scratch card, started, with `project_settled()` to wait for the project service.
struct Session {
  host::ScratchDir dir{"o3303-mods"};
  host::DirCard card{dir.path()};
  std::unique_ptr<sim::Harness> h;
  Session() {
    REQUIRE(dir.ok());
    h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, &card, "o3_303");
    REQUIRE(h->start());
    h->run_ms(100);
    app_project().set_autosave_period(1'000);
  }
  void wait(unsigned ms) { h->run_ms(ms); }
  bool settled(unsigned limit_ms = 6000) {
    for (unsigned i = 0; i < limit_ms; ++i) {
      if (app_project().idle() && !h->runtime().context().files().busy()) {
        return true;
      }
      h->run_ms(1);
    }
    return false;
  }
  void tap(app::Button b) {
    h->press(b);
    h->run_ms(40);
    h->release(b);
    h->run_ms(2);
  }
  void shift_tap(app::Button b) {
    h->press(app::Button::Shift);
    h->run_ms(2);
    tap(b);
    h->release(app::Button::Shift);
    h->run_ms(2);
  }
  float param(unsigned id) { return h->engine().param(kVoice, static_cast<std::uint16_t>(id)); }
  bool stock() { return param(AcidVoice::Stock) >= 0.5f; }
  // The chip the screens draw: the home map's status (the app's screens fill theirs from the same rig).
  bool chip() {
    frame::StatusView v;
    static_cast<MainScreen*>(h->runtime().context().screen())->home_map().status(v);
    return v.stock;
  }
};

}  // namespace

TEST_CASE("STOCK is off at boot, after a NEW, REVERT and RESET PROJECT, and a LOAD gives the project as it was saved (MODS on or off, F13a)") {
  Session s;
  CHECK_FALSE(s.stock());
  CHECK_FALSE(s.chip());
  (void)s.h->engine().set_param(kVoice, AcidVoice::AccDecay, 900.0f);  // a mod the saved project holds
  s.wait(50);
  REQUIRE(app_project().save_as("MODA"));
  REQUIRE(s.settled());
  app_project().want_list(true);
  app_project().refresh_list();
  s.wait(300);
  REQUIRE(s.settled());
  REQUIRE(app_project().list_valid());

  const auto stock_on = [&] {
    s.tap(app::Button::Note);
    REQUIRE(s.stock());
    REQUIRE(s.chip());
  };

  // NEW: a fresh project (the mods at their defaults, STOCK off). The session was MODA, which keeps its mod.
  stock_on();
  REQUIRE(app_project().new_project("MODB"));
  REQUIRE(s.settled());
  CHECK_FALSE(s.stock());
  CHECK_FALSE(s.chip());
  CHECK(s.param(AcidVoice::AccDecay) == doctest::Approx(87.0f));

  // LOAD (the session is MODB now, so MODA is read from the card and not the session's own): MODA was left with the mods out (NEW above saved it so, F13a), and the project
  // is heard as saved: STOCK on, and the mod is its stored value (not stock's 87 ms).
  (void)s.h->engine().set_param(kVoice, AcidVoice::AccDecay, 300.0f);
  REQUIRE(app_project().load("MODA"));
  REQUIRE(s.settled());
  CHECK(s.stock());
  CHECK(s.chip());
  CHECK(s.param(AcidVoice::AccDecay) == doctest::Approx(900.0f));
  s.tap(app::Button::Note);  // the mods heard again, and saved so
  CHECK_FALSE(s.stock());
  app_project().save();
  REQUIRE(s.settled());

  // REVERT (the last save of the session's project).
  stock_on();
  REQUIRE(app_project().revert());
  REQUIRE(s.settled());
  CHECK_FALSE(s.stock());
  CHECK_FALSE(s.chip());

  // RESET PROJECT.
  (void)s.h->engine().set_param(kVoice, AcidVoice::AccDecay, 900.0f);
  stock_on();
  o3sdk_app().config_run(s.h->runtime().context(), config::kResetProject);
  REQUIRE(s.settled());
  s.wait(600);  // the reset waits for the transport to stop
  CHECK_FALSE(s.stock());
  CHECK_FALSE(s.chip());
  CHECK(s.param(AcidVoice::AccDecay) == doctest::Approx(87.0f));
}

TEST_CASE("a new project starts with eight factory snapshots: a boot, NEW and RESET PROJECT give them, a LOAD of a project saved without any keeps its empty slots (REVIEW I4)") {
  Session s;
  const auto all_used = [] {
    for (unsigned i = 0; i < kSnapSlots; ++i) {
      if (!app_snapshots().used(i)) {
        return false;
      }
    }
    return true;
  };
  const auto none_used = [] {
    for (unsigned i = 0; i < kSnapSlots; ++i) {
      if (app_snapshots().used(i)) {
        return false;
      }
    }
    return true;
  };
  CHECK(all_used());  // the first boot

  // An old project: saved with no snapshot at all.
  app_snapshots().clear();
  REQUIRE(app_project().save_as("OLDS"));
  REQUIRE(s.settled());
  app_project().want_list(true);
  app_project().refresh_list();
  s.wait(300);
  REQUIRE(s.settled());
  REQUIRE(app_project().list_valid());

  // NEW: the eight are there again (the outgoing project OLDS was saved as it was).
  REQUIRE(app_project().new_project("NEWS"));
  REQUIRE(s.settled());
  CHECK(all_used());
  CHECK(app_snapshots().stored(0, 0) == doctest::Approx(s.param(AcidVoice::Cutoff)));  // slot 1 is the sound the project starts with

  // LOAD of the old project: its empty slots stay empty, nothing is installed over them.
  REQUIRE(app_project().load("OLDS"));
  REQUIRE(s.settled());
  CHECK(none_used());

  // RESET PROJECT installs them again, even over a project that has none.
  o3sdk_app().config_run(s.h->runtime().context(), config::kResetProject);
  REQUIRE(s.settled());
  s.wait(600);  // the reset waits for the transport to stop
  CHECK(all_used());
}

// ---- the Fx key and LiveActions::toggle_latch() -----------------------------------------------------------------------------------------

TEST_CASE("the Fx key flips WOB (PERFORM's WOB action, latched): a screen change keeps it, a Shift + Fx hold ends with the release, ALL OFF lets it go; F8 of the page is the same latch") {
  Hands t;
  seq::Sequencer& s = app_sequencer();
  t.assign_perf(Button::Fx, PerfAction::Wobble);
  t.to_wobble();
  t.tap(Button::Fx);  // on, whatever the screen
  t.wait(30);
  CHECK(s.wobble().on);
  t.to_home();
  t.wait(30);
  CHECK(s.wobble().on);  // it stays across a screen change (D4)
  t.to_wobble();
  t.tap(Button::Fx);  // off
  t.wait(30);
  CHECK_FALSE(s.wobble().on);
  t.action_press(Button::Fx);  // Shift + the key held does what a hold does, and nothing stays after it
  t.wait(30);
  CHECK(s.wobble().on);
  t.action_release(Button::Fx);
  t.wait(30);
  CHECK_FALSE(s.wobble().on);

  // ALL OFF lets it go (D27): PERFORM's last pad of row 1.
  t.tap(Button::Fx);
  t.wait(30);
  REQUIRE(s.wobble().on);
  t.to_perform();
  t.pad(app::pad_at(0, 11));
  t.wait(30);
  CHECK_FALSE(s.wobble().on);

  // F8 of the page is the same latch (the MIDI learn well) and never latches the encoder.
  t.to_wobble();
  t.tap(Button::F1);
  t.tap(Button::F8);
  t.wait(30);
  CHECK(screen_of(t).focus() == 0);
  CHECK(s.wobble().on);
  t.tap(Button::F8);
  t.wait(30);
  CHECK_FALSE(s.wobble().on);
}

TEST_CASE("the classic status line draws the MOD chip as a filled chip while the mods are on and as an outline while STOCK has them out, on HOME, PERFORM and MODDED") {
  Hands t;
  t.skin_off();
  const auto words = [&] {
    t.wait(2700);  // the toast of the flip (2.5 s) is gone: what is left is the chip
    return t.h->display().last_frame_words();
  };
  const std::pair<const char*, std::function<void()>> screens[] = {{"HOME", [&] { t.to_home(); }}, {"PERFORM", [&] { t.to_perform(); }}, {"MODDED", [&] { t.to_modded(1); t.tap(Button::F2); }}};
  for (const auto& [name, go] : screens) {
    INFO(name);
    go();
    const std::size_t off = words();
    t.tap(Button::Note);
    CHECK(words() != off);  // the chip is an outline now, not a filled panel (MODDED latches FILTER FM, a mod with no heard-on line, so the hint is the same)
    t.tap(Button::Note);
    CHECK(words() == off);  // and filled again with it
  }
}

// ---- The A/B: every mod at an extreme and STOCK on is a fresh project's, bit for bit ---------------------------------------------------------------

namespace {

constexpr std::uint64_t kSetupMs = 400;  // every run spends this long before Play (the gestures, then waiting): one timeline
constexpr unsigned kRunMs = 1500;        // the factory line, from step 1

enum class How {
  Fresh,      // nothing touched
  StockOnly,  // STOCK by the gesture, no mod
  StockThenMods,  // STOCK by the gesture, then every mod written by the engine: the route of a MIDI controller (a CC writes the stored value and does not end STOCK)
  SameBlock,  // every mod and then STOCK, all before the first block that renders them
  ModsOnly    // every mod and no STOCK: the control that the mods are heard at all
};
enum class Extreme { Max, Min };

// The factory line from a fresh app (the audio since the start): `how` sets it up, then the wait fills kSetupMs.
std::vector<std::int16_t> render(How how, Extreme extreme) {
  Hands t;
  t.h->record(true);
  app::Context& ctx = t.h->runtime().context();
  const std::uint64_t t0 = ctx.uptime_ms();
  const auto mods = [&] {
    for (unsigned id = AcidVoice::kFirstMod; id < AcidVoice::Stock; ++id) {
      const core::ParamDesc* d = t.h->engine().param_desc(kVoice, static_cast<std::uint16_t>(id));
      REQUIRE(d != nullptr);
      REQUIRE(t.h->engine().set_param(kVoice, static_cast<std::uint16_t>(id), extreme == Extreme::Max ? d->max : d->min));
    }
  };
  switch (how) {
    case How::Fresh:
      break;
    case How::StockOnly:
      t.tap(Button::Note);
      break;
    case How::StockThenMods:
      t.tap(Button::Note);
      t.wait(10);
      mods();
      break;
    case How::SameBlock:
      mods();
      REQUIRE(t.h->engine().set_param(kVoice, AcidVoice::Stock, 1.0f));
      break;
    case How::ModsOnly:
      mods();
      break;
  }
  while (ctx.uptime_ms() - t0 < kSetupMs) {
    t.wait(1);
  }
  t.tap(Button::Play);
  t.wait(kRunMs);
  return t.h->audio();
}

}  // namespace

TEST_CASE("the render with every mod at an extreme and STOCK on is bit for bit a fresh project's (mods_stock_ab)") {
  for (const Extreme extreme : {Extreme::Max, Extreme::Min}) {
    INFO((extreme == Extreme::Max ? "every mod at its maximum" : "every mod at its minimum"));
    const std::vector<std::int16_t> fresh = render(How::Fresh, extreme);
    REQUIRE(fresh.size() > 40000);
    // STOCK alone changes nothing.
    CHECK(render(How::StockOnly, extreme) == fresh);
    // STOCK first and the mods after, as a controller writes them: the voice never leaves the stock loop.
    CHECK(render(How::StockThenMods, extreme) == fresh);
    // The mods and STOCK in one block (nothing rendered in between): the route that needs no ordering across blocks.
    CHECK(render(How::SameBlock, extreme) == fresh);
    // The control: the same mods without STOCK are heard (this is what makes the checks above mean something).
    CHECK(render(How::ModsOnly, extreme) != fresh);
  }
}

namespace {
std::string hint_of(Hands& t) {
  InstrumentView v;
  screen_of(t).fill_view(t.h->runtime().context(), v);
  return v.hint != nullptr ? v.hint : "";
}
}  // namespace

TEST_CASE("B5a, E1b: the Instr key toggles the growl's Mix between 0 and the last value (50 % to start), the Fx and Note keys flip WOB and MODS, and the strip says OFF: <key> while one is off") {
  Hands t;
  t.to_growl();
  CHECK(t.param(kGrowl, Growl::Mix) == doctest::Approx(0.0f));
  CHECK(hint_of(t) == "GROWL OFF: INSTR");
  t.tap(Button::Instr);  // on: the 50 % it starts with
  CHECK(t.param(kGrowl, Growl::Mix) == doctest::Approx(0.5f));
  CHECK(hint_of(t) != "GROWL OFF: INSTR");
  (void)t.h->engine().set_param(kGrowl, Growl::Mix, 0.8f);  // the key remembers what it turns off
  t.tap(Button::Instr);  // off
  CHECK(t.param(kGrowl, Growl::Mix) == doctest::Approx(0.0f));
  CHECK(hint_of(t) == "GROWL OFF: INSTR");
  t.tap(Button::Instr);  // on again: the last non-zero value the key saw going off (0.8)
  CHECK(t.param(kGrowl, Growl::Mix) == doctest::Approx(0.8f));

  // WOBBLE: Fx is the latch; the strip says so while it is off.
  t.to_wobble();
  CHECK(hint_of(t) == "WOB OFF: FX");
  t.tap(Button::Fx);
  t.wait(10);
  CHECK(app_sequencer().wobble().on);
  CHECK(hint_of(t) != "WOB OFF: FX");
  t.tap(Button::Fx);
  t.wait(10);
  CHECK_FALSE(app_sequencer().wobble().on);

  // MODS: Stock off is the mods heard; Stock on says MODS OFF: NOTE.
  t.to_modded();
  CHECK_FALSE(stock(t));
  CHECK(hint_of(t) != "MODS OFF: NOTE");
  t.tap(Button::Note);
  CHECK(stock(t));
  CHECK(hint_of(t) == "MODS OFF: NOTE");
  t.tap(Button::Note);
  CHECK_FALSE(stock(t));
}

TEST_CASE("B5a: TARGET VOWEL says GROWL IS OFF and stays on CUTOFF while GRL MIX is 0, and is taken once the growl is on") {
  Hands t;
  t.to_wobble();
  t.tap(Button::F4);
  t.turn(1);
  CHECK(app_sequencer().wobble().target.target == o3_303::kLaneVoice);
  t.to_home();
  frame::StatusView sv;
  home_of(t).status(sv);
  CHECK(std::string(sv.toast) == "GROWL IS OFF");
  (void)t.h->engine().set_param(kGrowl, Growl::Mix, 0.5f);
  t.to_wobble();
  t.tap(Button::F4);
  t.turn(1);
  CHECK(app_sequencer().wobble().target.id == Growl::Vowel);
}
