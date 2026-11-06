// SPDX-License-Identifier: GPL-3.0-or-later
// The snapshots (PERFORM's row 2) against the real app (docs/M3B-PLAN.md R2; the RANDOM popup went with H4): a tap recalls, Shift + a pad stores, the encoder morphs
// from the previous snapshot to the last one recalled through the sequencer's slew, and the blend ends up stored in the engine. The bank itself is tested alone (snapshot_test.cpp).
#include <doctest/doctest.h>

#include "hands.h"

#include <o3sdk/core/param.h>
#include <o3_303/app_access.h>
#include <o3_303/seq/sequencer.h>
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
  const Growl::Param ids[4] = {Growl::Vowel, Growl::FormantQ, Growl::Mix, Growl::CombMix};
  const unsigned keys[4] = {0, 1, 6, 5};  // GROWL's F1 VOWEL, F2 FMT Q, F7 GRL MIX and F6 CMB MIX
  const auto knobs = [&](int detents) {  // the four stored growl knobs, each turned by `detents`
    t.to_growl();
    for (unsigned k = 0; k < 4; ++k) {
      t.tap(app::fkey(keys[k]));
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

TEST_CASE("snapshots: the eleven mods and the growl's FMT MIX, CMB TUNE and CMB FB are stored and recalled, switches included; a recall while STOCK is on leaves STOCK on and writes the stored values (MODS-PLAN 5.9)") {
  Hands t;
  // The mods as they are stored: values the factory does not have, each in its own units.
  struct Mod {
    unsigned id;
    float value;
  };
  const Mod mods[] = {{AcidVoice::AccDecay, 900.0f}, {AcidVoice::Tracking, 80.0f}, {AcidVoice::Slide, 200.0f}, {AcidVoice::SoftAtk, 3.0f},   {AcidVoice::SubOsc, 2.0f},   {AcidVoice::FilterFm, 0.5f},
                      {AcidVoice::AccSweep, 2.0f},   {AcidVoice::Muffler, 2.0f}, {AcidVoice::SwpSpeed, 0.0f}, {AcidVoice::Overdrive, 12.0f}, {AcidVoice::AmpDecay, 500.0f}};
  const auto set_all = [&](bool stored) {
    for (const Mod& m : mods) {
      const core::ParamDesc* d = t.h->engine().param_desc(kVoice, static_cast<std::uint16_t>(m.id));
      REQUIRE(d != nullptr);
      REQUIRE(t.h->engine().set_param(kVoice, static_cast<std::uint16_t>(m.id), stored ? m.value : d->def));
    }
    REQUIRE(t.h->engine().set_param(kGrowl, Growl::FormantMix, stored ? 0.6f : 0.0f));
    REQUIRE(t.h->engine().set_param(kGrowl, Growl::CombTune, stored ? 7.0f : 0.0f));
    REQUIRE(t.h->engine().set_param(kGrowl, Growl::CombFeedback, stored ? 0.4f : 0.0f));
    t.wait(5);
  };
  set_all(true);
  t.to_perform();
  store(t, 0);
  set_all(false);
  t.shift_tap(Button::SampleLoad);  // STOCK on
  REQUIRE(t.param(kVoice, AcidVoice::Stock) >= 0.5f);
  t.pad(snap_pad(0), 10);
  t.wait(300);
  for (const Mod& m : mods) {
    INFO("voice id " << m.id);
    CHECK(t.param(kVoice, static_cast<unsigned>(m.id)) == doctest::Approx(m.value).epsilon(0.01));
  }
  CHECK(t.param(kGrowl, Growl::FormantMix) == doctest::Approx(0.6f).epsilon(0.01));
  CHECK(t.param(kGrowl, Growl::CombTune) == doctest::Approx(7.0f).epsilon(0.02));
  CHECK(t.param(kGrowl, Growl::CombFeedback) == doctest::Approx(0.4f).epsilon(0.01));
  CHECK(t.param(kVoice, AcidVoice::Stock) >= 0.5f);  // a recall does not end STOCK: it writes the stored values
}

TEST_CASE("snapshots: a recall does not write a mod or a new growl knob that the blend leaves where it is (the slew's round trip through the curve moves ACC DECAY 180 to 180.000015), and glides one that it moves") {
  Hands t;
  seq::Sequencer& seq = app_sequencer();
  t.to_perform();
  store(t, 0);  // the project as it is: every mod at its stock value
  t.wait(20);
  // Seventeen of the 22 older targets glide (counted on this build: the others are the four switches, WAVE, the distortion's ACTIVE and MODEL and the delay's MODE, which are written, and
  // one that no glide starts here).
  constexpr unsigned kOlderGliding = 17;
  t.pad(snap_pad(0), 10);
  t.wait(10);
  CHECK(seq.slewing() > 0u);  // the recall did glide the older ones
  CHECK(seq.slewing() <= kOlderGliding);  // and none of the fourteen new targets
  t.wait(400);
  // A snapshot with a mod and a growl knob that differ: those two glide with the older ones, and the new values land exactly.
  (void)t.h->engine().set_param(kVoice, AcidVoice::AccDecay, 900.0f);
  (void)t.h->engine().set_param(kGrowl, Growl::CombTune, 5.0f);
  t.wait(20);
  store(t, 1);
  (void)t.h->engine().set_param(kVoice, AcidVoice::AccDecay, 180.0f);
  (void)t.h->engine().set_param(kGrowl, Growl::CombTune, 0.0f);
  t.wait(20);
  t.pad(snap_pad(1), 10);
  t.wait(10);
  CHECK(seq.slewing() == kOlderGliding + 2u);
  t.wait(400);
  CHECK(t.param(kVoice, AcidVoice::AccDecay) == doctest::Approx(900.0f).epsilon(0.001));
  CHECK(t.param(kGrowl, Growl::CombTune) == doctest::Approx(5.0f).epsilon(0.001));
}

TEST_CASE("snapshots: B5a, a snapshot takes the wobble's five settings and its latch and gives them back, the enums switching at the recall and not gliding") {
  Hands t;
  const auto turn_key = [&](unsigned k, int detents) {  // on the WOBBLE screen already (a second Sample Editor would turn the page)
    t.tap(app::fkey(k));
    t.turn(detents);
    t.wait(10);
  };
  const auto latch = [&] {
    t.tap(Button::F8);
    t.wait(10);
  };
  const auto wob = [] { return app_sequencer().wobble(); };  // a copy: read it fresh
  t.to_wobble();
  latch();                                  // on, at the defaults
  REQUIRE(wob().on);
  turn_key(0, 2);                           // A: RATE 1/8 -> 1/16
  turn_key(1, 2);                           // SHAPE: sine -> two on
  turn_key(2, -30);                         // DEPTH down
  turn_key(4, 1);                           // RESET on
  const seq::Wobble a = wob();
  REQUIRE(a.rate == seq::kWobble1_16);
  REQUIRE(a.shape != seq::kWobbleSine);
  REQUIRE(a.reset_on_note);
  t.to_perform();
  store(t, 0);                              // A on pad 1, the latch on
  t.to_wobble();
  turn_key(0, -4);                          // B: another rate, shape, depth, RESET off
  turn_key(1, -4);
  turn_key(2, 60);
  turn_key(4, -1);
  const seq::Wobble b = wob();
  REQUIRE(b.rate != a.rate);
  REQUIRE(b.shape != a.shape);
  REQUIRE(b.depth > a.depth + 0.2f);
  REQUIRE_FALSE(b.reset_on_note);
  t.to_perform();
  store(t, 1);                              // B on pad 2
  t.to_wobble();
  latch();                                  // off, with B set
  REQUIRE_FALSE(wob().on);
  t.to_perform();
  t.pad(snap_pad(0), 10);                   // A, then B: the morph runs from A (position 0) to B (32)
  t.wait(300);
  t.pad(snap_pad(1), 10);
  t.wait(300);
  CHECK(wob().on);                          // the latch came back with the recall
  CHECK(wob().rate == b.rate);
  CHECK(wob().shape == b.shape);
  CHECK_FALSE(wob().reset_on_note);
  CHECK(wob().depth == doctest::Approx(b.depth));
  t.turn(-15);                              // position 17: the depth is a blend, the enums are still B's (they switch at the middle, 16 and 17 are B's)
  t.wait(10);
  CHECK(wob().rate == b.rate);
  CHECK(wob().shape == b.shape);
  CHECK(wob().depth < b.depth);
  CHECK(wob().depth > a.depth);
  CHECK(wob().on);                          // a morph detent writes the settings, never drops the latch (set_wobble keeps the wave's phase)
  t.turn(-2);                               // position 15: the enums have switched, in one step
  t.wait(10);
  CHECK(wob().rate == a.rate);
  CHECK(wob().shape == a.shape);
  CHECK(wob().reset_on_note);
  CHECK(wob().depth > a.depth);
  CHECK(wob().depth < b.depth);
  CHECK(wob().on);
}
