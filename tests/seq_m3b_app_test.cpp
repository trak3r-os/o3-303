// SPDX-License-Identifier: GPL-3.0-or-later
// The M3b sequencer features as the app wires them (docs/M3B-PLAN.md; app/seq/tests/seq_m3b_test.cpp has the engine-level tests): the
// lanes and slews reach the distortion, the delay and the mixer, and the host-only stand-in controls of app/src/o3_303_app.cpp
// (O3_303_TEST_HOOKS) load the demo line, hold FILL and pick its variant. The checks read the sequencer's readback and the engine's values.
#include <doctest/doctest.h>

#include <o3sdk/app/app.h>
#include <o3sdk/sim/harness.h>
#include <o3sdk/units/delay/delay.h>
#include <o3_303/app_access.h>
#include <o3_303/pattern.h>
#include <o3_303/seq/bank.h>
#include <o3_303/seq/ops.h>
#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/tb303/tb303.h>

#include <memory>

using namespace o3sdk;
using namespace o3_303;
using app::Button;

namespace {

constexpr dsp::UnitId kVoice = 0;  // the units are attached in this order (o3_303_app.cpp)
constexpr dsp::UnitId kDist = 1;
constexpr dsp::UnitId kDelay = 2;

struct Hands {
  std::unique_ptr<sim::Harness> h = std::make_unique<sim::Harness>(o3sdk_app());
  Hands() {
    REQUIRE(h->start());
    h->run_ms(100);
  }
  void press(Button b) {
    h->press(b);
    h->run_ms(2);
  }
  void release(Button b) {
    h->release(b);
    h->run_ms(2);
  }
  void tap(Button b) {
    press(b);
    h->run_ms(40);
    release(b);
  }
};

}  // namespace

TEST_CASE("the app gives the sequencer the distortion, the delay and the mixer: they are slew and lane targets") {
  Hands hands;
  seq::Sequencer& s = app_sequencer();
  CHECK(s.slew(kLaneDist, Distortion::Drive, 0.9f, 30.0f));
  CHECK(s.slew(kLaneDelay, units::Delay::Feedback, 20.0f, 30.0f));
  CHECK(s.slew(kLaneMixer, kMixerSendA, 0.5f, 30.0f));
  hands.h->run_ms(100);
  const dsp::Engine& e = hands.h->engine();
  CHECK(e.param(kDist, Distortion::Drive) == doctest::Approx(0.9f));
  CHECK(e.param(kDelay, units::Delay::Feedback) == doctest::Approx(20.0f));
  CHECK(e.mix(0, dsp::MixField::SendA) == doctest::Approx(0.5f));
  CHECK(s.slewing() == 0);
  // The voice is still the default lanes' target.
  CHECK(s.slew(kLaneVoice, Tb303::Cutoff, 1000.0f, 30.0f));
  hands.h->run_ms(100);
  CHECK(e.param(kVoice, Tb303::Cutoff) == doctest::Approx(1000.0f));
}

TEST_CASE("host-only stand-ins: SampleLoad loads the demo line; SamplePlay held is FILL; Shift + SampleEdit picks the variant") {
  Hands hands;
  seq::Sequencer& s = app_sequencer();
  hands.tap(Button::SampleLoad);
  const Pattern& p = seq::project_bank().pattern(seq::project_bank().selected());
  CHECK(seq::get_ratchet(p, 2) == 1);
  CHECK(seq::get_ratchet(p, 6) == 2);
  CHECK(seq::get_ratchet(p, 10) == 3);
  CHECK(seq::get_cond(p, 3) == cond_ratio(1, 2));
  CHECK(seq::get_cond(p, 7) == cond_probability(50));
  CHECK(seq::get_cond(p, 11) == kCondFill);
  CHECK(seq::get_cond(p, 15) == kCondNotFirst);

  CHECK(s.fill_variant() == seq::FillVariant::Mutate);
  hands.press(Button::Shift);
  hands.tap(Button::SampleEdit);
  hands.release(Button::Shift);
  CHECK(s.fill_variant() == seq::FillVariant::OctUp);
  hands.tap(Button::SampleEdit);  // without Shift: nothing
  CHECK(s.fill_variant() == seq::FillVariant::OctUp);

  hands.tap(Button::Play);
  hands.h->run_ms(300);
  CHECK_FALSE(s.playhead().fill);
  hands.press(Button::SamplePlay);
  CHECK(s.fill_held());
  hands.h->run_ms(400);  // a step is 120 ms at 125 BPM: the variant starts at the next one
  CHECK(s.playhead().fill);
  hands.release(Button::SamplePlay);
  CHECK_FALSE(s.fill_held());
  hands.h->run_ms(400);
  CHECK_FALSE(s.playhead().fill);
  hands.tap(Button::Play);
}
