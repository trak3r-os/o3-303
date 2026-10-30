// SPDX-License-Identifier: GPL-3.0-or-later
// M3c's ui slice (docs/M3C-PLAN.md): the growl wired on slot 0's insert 1 and given to the sequencer, the WOBBLE action (a pad of the bottom row
// picks the rate, the LEDs show it, the release gives the pattern's rate back) and the ROLL, STOP and REV actions on the beat freeze (Mode, then
// Active; the release clears Active and leaves the Mode). The pure parts are in perform_test.cpp, the pages in pages_test.cpp.
#include <doctest/doctest.h>

#include "hands.h"

#include <o3sdk/dsp/engine.h>
#include <o3_303/app_access.h>
#include <o3_303/seq/sequencer.h>
#include <o3_303/units/freeze/freeze.h>
#include <o3_303/units/growl/growl.h>
#include <o3_303/units/acid_voice/acid_voice.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace hands;
using o3_303::Freeze;
using o3_303::Growl;
using o3_303::LaneRef;
using o3_303::PerfAction;
using o3_303::app_sequencer;
namespace seq = o3_303::seq;

namespace {

unsigned bottom(unsigned col) { return app::pad_at(app::kPadRows - 1u, col); }

float freeze_mode(Hands& t) { return t.param(kFreeze, Freeze::Mode); }
float freeze_active(Hands& t) { return t.param(kFreeze, Freeze::Active); }

// A run of the factory line at 125 BPM, 1.2 s, with the growl's Mix at `mix`: the interleaved samples.
std::vector<std::int16_t> play(float mix) {
  Hands t;
  t.h->record(true);
  (void)t.h->engine().set_param(kGrowl, Growl::Mix, mix);
  (void)t.h->engine().set_param(kGrowl, Growl::Vowel, 0.5f);
  t.tap(Button::Play);
  t.wait(1200);
  return t.h->audio();
}

}  // namespace

TEST_CASE("the wiring: the first four unit ids are as they were, the growl is the fifth, in the signal path and given to the sequencer") {
  Hands t;
  const dsp::Engine& e = t.h->engine();
  REQUIRE(e.unit_info(kFreeze) != nullptr);
  REQUIRE(e.unit_info(kGrowl) != nullptr);
  CHECK(std::strcmp(e.unit_info(kFreeze)->name, "FREEZE") == 0);
  CHECK(std::strcmp(e.unit_info(kGrowl)->name, "GROWL") == 0);
  CHECK(e.unit_info(kGrowl)->param_count == Growl::kParamCount);
  CHECK(e.setup_errors() == 0);  // slot 0's insert 1 is in range and the growl attached once
  CHECK(t.param(kGrowl, Growl::Mix) == doctest::Approx(0.0f));  // the default is the bypass

  // set_unit(kLaneGrowl, id): a wobble on the growl's Vowel is accepted (a target whose unit was not given is refused), and so is one on the voice.
  seq::Sequencer& s = app_sequencer();
  CHECK(s.set_wobble(true, seq::kWobble1_8, seq::kWobbleSine, 0.5f, LaneRef{o3_303::kLaneGrowl, Growl::Vowel}));
  CHECK(s.wobble().target.target == o3_303::kLaneGrowl);
  CHECK(s.set_wobble(false, 0, 0, 0.0f));

  // On insert 1 of slot 0 it is in the path: with its Mix open the sound changes, and at 0 it is the bypass it says it is (the same bytes twice).
  const std::vector<std::int16_t> a = play(0.0f);
  const std::vector<std::int16_t> b = play(0.0f);
  const std::vector<std::int16_t> c = play(1.0f);
  REQUIRE(a.size() > 40000);
  CHECK(a == b);
  REQUIRE(c.size() == a.size());
  double diff = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    diff += std::abs(static_cast<double>(a[i]) - static_cast<double>(c[i]));
  }
  CHECK(diff / static_cast<double>(a.size()) > 20.0);
}

TEST_CASE("WOBBLE: held it runs at the page's settings and the release ends it; latched with Shift it stays until the next Shift + tap; HOME's pads stay keys") {
  Hands t;
  seq::Sequencer& s = app_sequencer();
  t.assign_perf(Button::Fx, PerfAction::Wobble);
  CHECK_FALSE(s.wobble().on);
  CHECK(s.wobble().override_rate == seq::kWobbleRateNone);

  t.press(Button::Fx);
  t.wait(30);
  CHECK(s.wobble().on);  // its set rate for the hold (the rate picker is PERFORM's row 3, perform_screen_test.cpp)
  CHECK(s.wobble().rate == seq::kWobble1_8);
  CHECK(s.wobble().target.target == o3_303::kLaneVoice);
  CHECK(s.wobble().target.id == o3_303::AcidVoice::Cutoff);
  CHECK(s.wobble_rate_now() == seq::kWobble1_8);
  CHECK(t.led(bottom(2)) == app::kLedDim);  // the keyboard's: no picker over it
  t.pad(bottom(2));                          // a key here: no rate is picked
  t.wait(30);
  CHECK(s.wobble().override_rate == seq::kWobbleRateNone);
  CHECK(s.wobble_rate_now() == seq::kWobble1_8);

  t.release(Button::Fx);
  t.wait(30);
  CHECK_FALSE(s.wobble().on);
  CHECK(s.wobble_rate_now() == -1);

  // Latched with Shift: it stays after the release, and the next Shift + tap ends it.
  t.shift_tap(Button::Fx);
  t.wait(30);
  CHECK(s.wobble().on);
  CHECK(s.wobble_rate_now() == seq::kWobble1_8);
  t.shift_tap(Button::Fx);
  t.wait(30);
  CHECK_FALSE(s.wobble().on);
  CHECK(s.wobble().override_rate == seq::kWobbleRateNone);
}

TEST_CASE("WOBBLE: the WOBBLE page's target and rate apply while it runs, and the Vowel is a growl target") {
  Hands t;
  seq::Sequencer& s = app_sequencer();
  t.assign_perf(Button::Fx, PerfAction::Wobble);
  t.press(Button::Fx);
  t.to_page(o3_303::kPageWobble);
  t.tap(Button::F5);  // TARGET: the voice's Cutoff, the growl's Vowel
  t.turn(1);
  t.wait(10);
  CHECK(s.wobble().on);
  CHECK(s.wobble().target.target == o3_303::kLaneGrowl);
  CHECK(s.wobble().target.id == Growl::Vowel);
  t.tap(Button::F1);  // RATE
  t.turn(2);          // 1/8 -> 1/16
  t.tap(Button::F4);  // RESET off
  t.turn(-1);
  t.wait(10);
  CHECK(s.wobble().rate == seq::kWobble1_16);
  CHECK_FALSE(s.wobble().reset_on_note);
  t.release(Button::Fx);
  t.wait(10);
  CHECK_FALSE(s.wobble().on);
  // Changed with the wobble off: the settings wait for the next hold.
  t.tap(Button::F1);
  t.turn(-2);
  CHECK_FALSE(s.wobble().on);
  t.press(Button::Fx);
  t.wait(10);
  CHECK(s.wobble().rate == seq::kWobble1_8);
  t.release(Button::Fx);
  t.back_to_sound(o3_303::kPageWobble);
  // A fresh run starts with the defaults (setup() resets the wiring and the sequencer's wobble).
  Hands again;
  CHECK_FALSE(app_sequencer().wobble().on);
  CHECK(app_sequencer().wobble().reset_on_note);
}

TEST_CASE("ROLL, STOP and REV: each writes the freeze's Mode, then Active, while held; the release clears Active and leaves the Mode") {
  Hands t;
  t.assign_perf(Button::Note, PerfAction::Roll);
  t.assign_perf(Button::Instr, PerfAction::Stop);
  t.assign_perf(Button::Vol, PerfAction::Reverse);
  CHECK(freeze_mode(t) == doctest::Approx(0.0f));
  CHECK(freeze_active(t) == 0.0f);

  t.press(Button::Note);
  CHECK(freeze_mode(t) == doctest::Approx(1.0f));
  CHECK(freeze_active(t) == 1.0f);
  t.release(Button::Note);
  CHECK(freeze_active(t) == 0.0f);
  CHECK(freeze_mode(t) == doctest::Approx(1.0f));  // left as it was: nothing is written while the unit leaves

  t.press(Button::Instr);
  CHECK(freeze_mode(t) == doctest::Approx(2.0f));
  CHECK(freeze_active(t) == 1.0f);
  t.release(Button::Instr);
  CHECK(freeze_active(t) == 0.0f);

  t.press(Button::Vol);
  CHECK(freeze_mode(t) == doctest::Approx(3.0f));
  CHECK(freeze_active(t) == 1.0f);
  t.release(Button::Vol);
  CHECK(freeze_active(t) == 0.0f);

  // The one that went on first has the unit: a second press does nothing, and the first one's release ends it; the second needs a new press.
  t.press(Button::Note);
  t.press(Button::Instr);
  CHECK(freeze_mode(t) == doctest::Approx(1.0f));
  CHECK(freeze_active(t) == 1.0f);
  t.release(Button::Instr);
  CHECK(freeze_active(t) == 1.0f);  // the second one's release is nothing either
  t.press(Button::Instr);
  t.release(Button::Note);
  CHECK(freeze_active(t) == 0.0f);
  CHECK(freeze_mode(t) == doctest::Approx(1.0f));
  t.release(Button::Instr);
  CHECK(freeze_active(t) == 0.0f);

  // Shift + tap latches ROLL and again lets it go.
  t.shift_tap(Button::Note);
  CHECK(freeze_active(t) == 1.0f);
  CHECK(freeze_mode(t) == doctest::Approx(1.0f));
  t.shift_tap(Button::Note);
  CHECK(freeze_active(t) == 0.0f);

  // Leaving the screen lets go of a held one.
  t.press(Button::Vol);
  CHECK(freeze_active(t) == 1.0f);
  t.tap(Button::File);
  t.wait(10);
  CHECK(freeze_active(t) == 0.0f);
  t.release(Button::Vol);
  t.tap(Button::Pattern);
}

TEST_CASE("FREEZE after a ROLL is a FREEZE again: it writes its own Mode at the entry") {
  Hands t;
  t.assign_perf(Button::Note, PerfAction::Roll);
  t.assign_perf(Button::Vol, PerfAction::Freeze);
  t.press(Button::Note);
  t.release(Button::Note);
  CHECK(freeze_mode(t) == doctest::Approx(1.0f));
  t.press(Button::Vol);
  CHECK(freeze_mode(t) == doctest::Approx(0.0f));
  CHECK(freeze_active(t) == 1.0f);
  t.release(Button::Vol);
  CHECK(freeze_active(t) == 0.0f);
  // The two buttons carry the same action: it is on while either is (as before).
  t.assign_perf(Button::Instr, PerfAction::Freeze);
  t.press(Button::Instr);
  t.press(Button::Vol);
  t.release(Button::Instr);
  CHECK(freeze_active(t) == 1.0f);
  t.release(Button::Vol);
  CHECK(freeze_active(t) == 0.0f);
}
