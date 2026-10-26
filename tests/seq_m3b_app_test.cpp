// SPDX-License-Identifier: GPL-3.0-or-later
// The M3b sequencer features as the app wires them (docs/M3B-PLAN.md; app/seq/tests/seq_m3b_test.cpp has the engine-level tests): the
// lanes and slews reach the distortion, the delay and the mixer, and FILL (the Vol button) plays the variant the PERF page chose. The checks
// read the sequencer's readback and the engine's values.
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

// The M3b demo line: ratchets x2, x3, x4 (one accented), a slide into a ratcheted step and a ratcheted slide step, a condition of each kind
// (1:2, 50 %, FILL, NOT FILL, FIRST, NOT FIRST), over a C minor line. (app/scenarios/m3b_seq.txt enters it with the real gestures.)
void load_demo_line() {
  static constexpr std::uint8_t kNotes[16] = {36, 36, 39, 43, 36, 41, 43, 46, 36, 36, 39, 43, 48, 46, 43, 41};
  Pattern p;
  clear(p);
  for (unsigned j = 0; j < 16; ++j) {
    p.steps[j].note = kNotes[j];
    p.steps[j].flags = 0;
  }
  p.steps[0].flags = kStepAccent;
  p.steps[2].flags = with_ratchet(0, 1);
  p.steps[3].cond = cond_ratio(1, 2);
  p.steps[5].flags = kStepSlide;
  p.steps[6].flags = with_ratchet(0, 2);
  p.steps[7].cond = cond_probability(50);
  p.steps[8].flags = kStepAccent;
  p.steps[9].flags = kStepRest;
  p.steps[10].flags = static_cast<std::uint8_t>(with_ratchet(0, 3) | kStepAccent);
  p.steps[11].cond = kCondFill;
  p.steps[12].cond = kCondNotFill;
  p.steps[13].flags = static_cast<std::uint8_t>(with_ratchet(0, 1) | kStepSlide);
  p.steps[14].cond = kCondFirst;
  p.steps[15].cond = kCondNotFirst;
  p.length = 16;
  seq::project_bank().replace(seq::project_bank().selected(), p);
}

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

TEST_CASE("FILL is the Vol button (C4's default) and its variant is the PERF page's F5; the demo line's FILL conditions follow it") {
  Hands hands;
  seq::Sequencer& s = app_sequencer();
  load_demo_line();
  const Pattern& p = seq::project_bank().pattern(seq::project_bank().selected());
  CHECK(seq::get_ratchet(p, 2) == 1);
  CHECK(seq::get_cond(p, 11) == kCondFill);

  // The variant: SEQ is two pages on from SOUND; F6 is FILL's variant, MUTATE, OCT UP, RATCHET, ACC ROT.
  CHECK(s.fill_variant() == seq::FillVariant::Mutate);
  hands.tap(Button::Params);
  hands.tap(Button::Params);
  hands.tap(Button::F6);
  hands.h->turn(1);
  hands.h->run_ms(2);
  CHECK(s.fill_variant() == seq::FillVariant::OctUp);
  hands.h->turn(2);
  hands.h->run_ms(2);
  CHECK(s.fill_variant() == seq::FillVariant::AccRotate);
  hands.h->turn(-3);
  hands.h->run_ms(2);
  CHECK(s.fill_variant() == seq::FillVariant::Mutate);
  for (int i = 0; i < 3; ++i) {
    hands.tap(Button::Params);  // back to SOUND
  }

  hands.tap(Button::Play);
  hands.h->run_ms(300);
  CHECK_FALSE(s.playhead().fill);
  hands.press(Button::Vol);
  CHECK(s.fill_held());
  hands.h->run_ms(400);  // a step is 120 ms at 125 BPM: the variant starts at the next one
  CHECK(s.playhead().fill);
  hands.release(Button::Vol);
  CHECK_FALSE(s.fill_held());
  hands.h->run_ms(400);
  CHECK_FALSE(s.playhead().fill);
  // Shift latches it; Shift again lets it go.
  hands.press(Button::Shift);
  hands.tap(Button::Vol);
  hands.release(Button::Shift);
  CHECK(s.fill_held());
  hands.h->run_ms(400);
  CHECK(s.playhead().fill);
  hands.press(Button::Shift);
  hands.tap(Button::Vol);
  hands.release(Button::Shift);
  CHECK_FALSE(s.fill_held());
  hands.tap(Button::Play);
}
