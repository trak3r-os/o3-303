// SPDX-License-Identifier: GPL-3.0-or-later
// The snapshots (PERFORM's row 2) against the real app (docs/M3B-PLAN.md R2; the RANDOM popup went with H4): a tap recalls, Shift + a pad stores, the encoder morphs
// from the previous snapshot to the last one recalled through the sequencer's slew, and the blend ends up stored in the engine. The bank itself is tested alone (snapshot_test.cpp).
#include <doctest/doctest.h>

#include "hands.h"

#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3_303/units/growl/growl.h>

using namespace hands;
using namespace o3_303;

namespace {

float cutoff(Hands& t) { return t.param(kVoice, AcidVoice::Cutoff); }

}  // namespace

namespace {

constexpr unsigned kSnapRow = 1;  // PERFORM's row 2: snapshots 1..8
unsigned snap_pad(unsigned slot) { return app::pad_at(kSnapRow, slot); }

// Shift + a pad: the store.
void store(Hands& t, unsigned slot) {
  t.press(Button::Shift);
  t.pad(snap_pad(slot), 10);
  t.release(Button::Shift);
}

}  // namespace

TEST_CASE("snapshots: store two sounds, recall them, morph between them; the engine ends on the blend") {
  Hands t;
  const float c0 = cutoff(t);
  t.to_perform();  // the snapshots are PERFORM's row 2
  store(t, 0);     // store this sound on pad 1
  t.turn(30);      // CUTOFF is the latched knob
  const float c1 = cutoff(t);
  const float drive1 = t.param(kDist, Distortion::Drive);
  REQUIRE(c1 > c0 * 1.5f);
  store(t, 1);  // and this one on pad 2
  t.wait(50);

  // An empty pad recalls nothing and says so.
  t.pad(snap_pad(4), 10);
  t.wait(200);
  CHECK(cutoff(t) == doctest::Approx(c1));

  t.pad(snap_pad(0), 10);  // snapshot 1: the cutoff glides back and the engine stores it
  t.wait(300);
  CHECK(cutoff(t) == doctest::Approx(c0).epsilon(0.001));
  t.pad(snap_pad(1), 10);  // snapshot 2; the morph now runs from snapshot 1 to snapshot 2
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

  // An F key gives the encoder back to the knob.
  t.tap(Button::F1);
  const float before = cutoff(t);
  t.turn(3);
  CHECK(cutoff(t) != before);
}

TEST_CASE("snapshots: the growl's VOWEL, FMT Q, GRL MIX and CMB MIX are stored and recalled with the rest (D29)") {
  Hands t;
  const Growl::Param ids[4] = {Growl::Vowel, Growl::FormantQ, Growl::Mix, Growl::CombMix};  // INSTRUMENT MODS' F1..F4 are this order
  const auto knobs = [&](int detents) {  // the four MODS knobs, each turned by `detents`
    t.to_instrument(kInstMods);
    for (unsigned k = 0; k < 4; ++k) {
      t.tap(app::fkey(k));
      t.turn(detents);
    }
  };
  const auto now = [&](float (&v)[4]) {
    for (unsigned k = 0; k < 4; ++k) {
      v[k] = t.param(kGrowl, ids[k]);
    }
  };
  knobs(20);
  float stored[4];
  now(stored);
  t.to_perform();
  store(t, 0);
  knobs(-40);
  float changed[4];
  now(changed);
  for (unsigned k = 0; k < 4; ++k) {
    REQUIRE(changed[k] != doctest::Approx(stored[k]));
  }
  t.to_perform();
  t.pad(snap_pad(0), 10);  // the recall glides all four back
  t.wait(300);
  float back[4];
  now(back);
  for (unsigned k = 0; k < 4; ++k) {
    CHECK(back[k] == doctest::Approx(stored[k]).epsilon(0.01));
  }
}

TEST_CASE("snapshots: before a recall the encoder is the knob's, and leaving PERFORM stores the blend") {
  Hands t;
  const float c0 = cutoff(t);
  t.to_perform();
  t.turn(30);  // nothing recalled: the encoder moves the latched knob
  const float c1 = cutoff(t);
  CHECK(c1 > c0);
  store(t, 2);
  t.turn(-30);
  t.wait(50);
  t.pad(snap_pad(2), 10);  // recall it at once, then leave before the slew has settled
  t.to_home();
  t.wait(300);
  CHECK(cutoff(t) == doctest::Approx(c1).epsilon(0.001));
}
