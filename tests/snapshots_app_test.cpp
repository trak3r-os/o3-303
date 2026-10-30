// SPDX-License-Identifier: GPL-3.0-or-later
// The SNAP page and the RANDOM popup against the real app (docs/M3B-PLAN.md R2, C3): a tap recalls, Shift + a key stores, the encoder morphs
// from the previous snapshot to the last one recalled through the sequencer's slew, and the blend ends up stored in the engine; the popup has
// the encoder while it is open and gives it back. The bank itself is tested alone (snapshot_test.cpp).
#include <doctest/doctest.h>

#include "hands.h"

#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/acid_voice/acid_voice.h>

using namespace hands;
using namespace o3_303;

namespace {

float cutoff(Hands& t) { return t.param(kVoice, AcidVoice::Cutoff); }

}  // namespace

TEST_CASE("snapshots: store two sounds, recall them, morph between them; the engine ends on the blend") {
  Hands t;
  const float c0 = cutoff(t);
  t.shift_tap(Button::Params);  // the SNAP page
  t.shift_tap(Button::F1);      // store this sound on key 1
  t.shift_tap(Button::Params);  // back on SOUND, CUTOFF latched
  t.turn(30);
  const float c1 = cutoff(t);
  const float drive1 = t.param(kDist, Distortion::Drive);
  REQUIRE(c1 > c0 * 1.5f);
  t.shift_tap(Button::Params);
  t.shift_tap(Button::F2);  // and this one on key 2
  t.wait(50);

  // An empty key recalls nothing and says so.
  t.tap(Button::F5);
  t.wait(200);
  CHECK(cutoff(t) == doctest::Approx(c1));

  t.tap(Button::F1);  // snapshot 1: the cutoff glides back and the engine stores it
  t.wait(300);
  CHECK(cutoff(t) == doctest::Approx(c0).epsilon(0.001));
  t.tap(Button::F2);  // snapshot 2; the morph now runs from snapshot 1 to snapshot 2
  t.wait(300);
  CHECK(cutoff(t) == doctest::Approx(c1).epsilon(0.001));
  CHECK(t.param(kDist, Distortion::Drive) == doctest::Approx(drive1));
  t.turn(-16);  // half way back
  t.wait(300);
  CHECK(cutoff(t) == doctest::Approx((c0 + c1) * 0.5f).epsilon(0.002));
  t.turn(-16);
  t.wait(300);
  CHECK(cutoff(t) == doctest::Approx(c0).epsilon(0.001));
  t.turn(-5);  // at the start: it stops
  t.wait(200);
  CHECK(cutoff(t) == doctest::Approx(c0).epsilon(0.001));
  t.press(Button::Shift);  // coarse: four detents a step, so 4 turns = 16 of the 32
  t.turn(4);
  t.release(Button::Shift);
  t.wait(300);
  CHECK(cutoff(t) == doctest::Approx(c0 + (c1 - c0) * 16.0f / 32.0f).epsilon(0.002));

  // Back on the page it came from, the encoder is the page's key again.
  t.shift_tap(Button::Params);
  const float before = cutoff(t);
  t.turn(3);
  CHECK(cutoff(t) != before);
}

TEST_CASE("snapshots: the morph needs a snapshot recalled first, and Params leaves the page with the blend stored") {
  Hands t;
  const float c0 = cutoff(t);
  t.shift_tap(Button::Params);  // SNAP, nothing recalled: the encoder does nothing to the sound
  t.turn(5);
  t.wait(100);
  CHECK(cutoff(t) == doctest::Approx(c0));
  t.shift_tap(Button::Params);
  t.turn(30);
  const float c1 = cutoff(t);
  t.shift_tap(Button::Params);
  t.shift_tap(Button::F3);
  t.tap(Button::F3);  // recall it at once, then leave through Params before the slew has settled
  t.tap(Button::Params);
  t.tap(Button::Params);
  t.tap(Button::Params);
  t.tap(Button::Params);
  t.wait(300);
  CHECK(cutoff(t) == doctest::Approx(c1).epsilon(0.001));
}

TEST_CASE("RANDOM popup: it has the encoder while open, F7 and Params close it, F8 generates") {
  Hands t;
  const float c0 = cutoff(t);
  const Pattern before = t.pat();
  t.random_popup();
  t.tap(Button::F2);  // DENSITY
  t.turn(-4);
  CHECK(cutoff(t) == doctest::Approx(c0));  // the popup's setting moved, the page's did not
  t.tap(Button::F8);
  bool differs = false;
  for (unsigned i = 0; i < 16; ++i) {
    differs = differs || t.pat().steps[i].note != before.steps[i].note || t.pat().steps[i].flags != before.steps[i].flags;
  }
  CHECK(differs);
  t.tap(Button::F7);  // closed: the encoder is the page's
  t.turn(3);
  CHECK(cutoff(t) != doctest::Approx(c0));
  const float c1 = cutoff(t);
  t.random_popup();
  t.tap(Button::Params);  // Params only closes it: still the SOUND page
  t.turn(3);
  CHECK(cutoff(t) != doctest::Approx(c1));
}
