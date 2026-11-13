// SPDX-License-Identifier: GPL-3.0-or-later
// The colour keys as the four sounds' switches (E1b, docs/PADS-TRACT-PLAN.md E): Note flips MODS, Instr GROWL, Vol (FX1) TRACT, Fx (FX2) WOBBLE, on every screen; with a step held they are the
// drawer's rows instead; Shift changes nothing (PF1: the PERFORM actions the keys once ran with Shift are gone). The sound bits the header chips read (E1c) follow.
#include <doctest/doctest.h>

#include "hands.h"

#include "../app/src/frame.h"
#include "../app/src/home_map.h"
#include "../app/src/main_screen.h"

#include <o3sdk/core/param.h>
#include <o3_303/app_access.h>
#include <o3_303/seq/sequencer.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3_303/units/growl/growl.h>
#include <o3_303/units/tract/tract.h>

#include <cstring>
#include <string>

using namespace hands;
using namespace o3_303;

namespace {

bool mods(Hands& t) { return t.param(kVoice, AcidVoice::Stock) < 0.5f; }
bool growl(Hands& t) { return t.param(kGrowl, Growl::Mix) > 0.0f; }
bool tract(Hands& t) { return t.param(kTract, TractFx::Mix) > 0.0f; }
bool wobble() { return app_sequencer().wobble().on; }
HomeMap& home_of(Hands& t) { return static_cast<MainScreen*>(t.h->runtime().context().screen())->home_map(); }

// The four sounds as bits: Note MODS, Instr GROWL, Vol TRACT, Fx WOBBLE.
unsigned bits(Hands& t) { return (mods(t) ? 1u : 0u) | (growl(t) ? 2u : 0u) | (tract(t) ? 4u : 0u) | (wobble() ? 8u : 0u); }

// The status line's words (HOME's; the sound keys say their news there from every screen).
std::string said(Hands& t) {
  t.to_home();
  if (std::strcmp(t.title(), "PATTERN SETUP") == 0) {
    t.to_home();
  }
  frame::StatusView v;
  home_of(t).status(v);
  return v.take_msg;
}

// On the screen `go` puts a fresh player on, every key flips its own sound and no other, and flips it back.
template <typename Go>
void each_key_flips_its_sound(Go go) {
  Hands t;
  go(t);
  const std::string where = t.title();
  const unsigned start = bits(t);
  const Button keys[4] = {Button::Note, Button::Instr, Button::Vol, Button::Fx};
  for (unsigned k = 0; k < 4; ++k) {
    t.tap(keys[k]);
    t.wait(30);
    CHECK_MESSAGE(bits(t) == (start ^ (1u << k)), "key " << k << " on " << where);
    CHECK_MESSAGE(where == t.title(), "key " << k << " leaves the screen " << where);
    t.tap(keys[k]);
    t.wait(30);
    CHECK_MESSAGE(bits(t) == start, "key " << k << " back on " << where);
  }
}

}  // namespace

TEST_CASE("sound buttons: at boot MODS is on, GROWL, TRACT and WOBBLE are off") {
  Hands t;
  CHECK(bits(t) == 1u);
}

TEST_CASE("sound buttons: Note, Instr, Vol and Fx flip MODS, GROWL, TRACT and WOBBLE on every screen") {
  each_key_flips_its_sound([](Hands&) {});  // HOME
  each_key_flips_its_sound([](Hands& t) { t.to_perform(); });
  each_key_flips_its_sound([](Hands& t) { t.to_setup(); });
  each_key_flips_its_sound([](Hands& t) { t.tap(Button::Pattern); });  // PATTERN SETUP
  each_key_flips_its_sound([](Hands& t) { t.to_instrument(); });
  each_key_flips_its_sound([](Hands& t) { t.to_modded(); });
  each_key_flips_its_sound([](Hands& t) { t.to_growl(); });
  each_key_flips_its_sound([](Hands& t) { t.to_tract(); });
  each_key_flips_its_sound([](Hands& t) { t.to_wobble(); });
  each_key_flips_its_sound([](Hands& t) { t.tap(Button::Song); });
  each_key_flips_its_sound([](Hands& t) { t.to_record(); });
}

TEST_CASE("sound buttons: the flip is at the press, the release does nothing, and the status line says which way") {
  Hands t;
  t.press(Button::Instr);
  CHECK(growl(t));
  t.wait(400);  // a hold is no different from a tap
  CHECK(growl(t));
  t.release(Button::Instr);
  CHECK(growl(t));
  CHECK(said(t) == "GROWL ON");
  t.tap(Button::Vol);
  CHECK(said(t) == "TRACT ON");
  t.tap(Button::Vol);
  CHECK(said(t) == "TRACT OFF");
  t.tap(Button::Note);
  CHECK(said(t) == "MODS OFF");
  t.tap(Button::Note);
  CHECK(said(t) == "MODS ON");
}

TEST_CASE("sound buttons: GROWL comes back at the mix it had") {
  Hands t;
  t.tap(Button::Instr);
  const float mix = t.param(kGrowl, Growl::Mix);
  REQUIRE(mix > 0.0f);
  t.tap(Button::Instr);
  CHECK(t.param(kGrowl, Growl::Mix) == 0.0f);
  t.tap(Button::Instr);
  CHECK(t.param(kGrowl, Growl::Mix) == doctest::Approx(mix));
}

TEST_CASE("sound buttons: with a step held the colour keys are the drawer's rows and flip nothing") {
  Hands t;
  const unsigned start = bits(t);
  for (const Button b : {Button::Note, Button::Instr, Button::Vol, Button::Fx}) {
    t.row_key(b);
    CHECK(bits(t) == start);
  }
}

TEST_CASE("sound buttons: Shift + the key flips the sound as a plain press does and runs no PERFORM action (PF1)") {
  Hands t;
  seq::Sequencer& s = app_sequencer();
  const unsigned start = bits(t);
  const Button keys[4] = {Button::Note, Button::Instr, Button::Vol, Button::Fx};
  for (unsigned k = 0; k < 4; ++k) {
    t.shift_tap(keys[k]);
    t.wait(30);
    CHECK_MESSAGE(bits(t) == (start ^ (1u << k)), "key " << k);
    CHECK(s.perf() == 0);  // MUTE, ACC, FILL and FRZ are on the pads only
    t.shift_tap(keys[k]);
    t.wait(30);
    CHECK_MESSAGE(bits(t) == start, "key " << k << " back");
  }
  // A key held down is no momentary action either: it flips at the press and the release does nothing.
  t.press(Button::Shift);
  t.press(Button::Note);
  t.release(Button::Shift);
  t.wait(400);
  CHECK(bits(t) == (start ^ 1u));
  CHECK(s.perf() == 0);
  t.release(Button::Note);
  CHECK(bits(t) == (start ^ 1u));
}

TEST_CASE("sound buttons: WOBBLE's key is the WOB action's latch: it stays across a screen change and the key lets it go") {
  Hands t;
  t.to_wobble();
  t.tap(Button::Fx);
  t.wait(30);
  CHECK(wobble());
  t.to_home();
  CHECK(wobble());
  t.tap(Button::Fx);
  t.wait(30);
  CHECK_FALSE(wobble());
}
