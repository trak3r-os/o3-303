// SPDX-License-Identifier: GPL-3.0-or-later
// The parameter pages end to end: F keys and the encoder, PARAMS and the pages it walks, p-locks and motion lanes.
// (Split from the former o3_303_app_test.cpp; the hands are in app_hands.h.)
#include "app_hands.h"

#include <o3sdk/units/delay/delay.h>
#include <o3_303/step_model.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/growl/growl.h>

#include <cstring>

TEST_CASE("pages: tap an F key to latch it, hold one and turn for a moment, two held move together, Shift is coarse") {
  Hands t;
  const float cutoff0 = t.param(kVoice, AcidVoice::Cutoff);
  const float reso0 = t.param(kVoice, AcidVoice::Reso);
  t.turn(5);  // F1 (cutoff) is latched at the start
  const float cutoff1 = t.param(kVoice, AcidVoice::Cutoff);
  CHECK(cutoff1 > cutoff0);
  t.tap(Button::F2);
  t.turn(-10);
  CHECK(t.param(kVoice, AcidVoice::Reso) < reso0);
  CHECK(t.param(kVoice, AcidVoice::Cutoff) == cutoff1);

  const float decay0 = t.param(kVoice, AcidVoice::Decay);
  t.press(Button::F4);
  t.turn(10);
  t.release(Button::F4);
  CHECK(t.param(kVoice, AcidVoice::Decay) > decay0);
  const float reso1 = t.param(kVoice, AcidVoice::Reso);
  t.turn(3);  // back on F2, the latched key
  CHECK(t.param(kVoice, AcidVoice::Reso) > reso1);

  const float env0 = t.param(kVoice, AcidVoice::EnvMod);
  const float acc0 = t.param(kVoice, AcidVoice::Accent);
  t.press(Button::F3);
  t.press(Button::F5);
  t.turn(10);
  t.release(Button::F3);
  t.release(Button::F5);
  CHECK(t.param(kVoice, AcidVoice::EnvMod) > env0);
  CHECK(t.param(kVoice, AcidVoice::Accent) > acc0);

  const float tune0 = t.param(kVoice, AcidVoice::Tune);
  t.tap(Button::F7);
  t.turn(1);
  const float fine = t.param(kVoice, AcidVoice::Tune) - tune0;
  t.press(Button::Shift);
  t.turn(1);
  t.release(Button::Shift);
  const float coarse = t.param(kVoice, AcidVoice::Tune) - tune0 - fine;
  CHECK(coarse > fine * 5.0f);

  t.tap(Button::F6);
  t.turn(1);
  CHECK(t.param(kVoice, AcidVoice::Wave) == doctest::Approx(1.0f));
}

TEST_CASE("PARAMS opens INSTRUMENT on VOICE and goes to the next page (FX, MODS); the pages reach the whole engine") {
  Hands t;
  const float drive0 = t.param(kDist, Distortion::Drive);
  t.to_instrument(kInstFx);
  CHECK(std::strcmp(t.title(), "INSTRUMENT FX") == 0);
  t.tap(Button::F1);
  t.turn(10);
  CHECK(t.param(kDist, Distortion::Drive) > drive0);
  t.tap(Button::F4);  // the distortion model
  t.turn(1);
  CHECK(t.param(kDist, Distortion::Type) == doctest::Approx(1.0f));
  t.tap(Button::F5);  // the delay
  t.turn(-10);
  t.tap(Button::F8);  // the delay's tone
  const float tone0 = t.param(kDelay, units::Delay::Tone);
  t.turn(-20);
  CHECK(t.param(kDelay, units::Delay::Tone) < tone0);

  t.tap(Button::Params);  // MODS: F1 is the vowel (the growl's Mix is 0, so it is still a bypass)
  CHECK(std::strcmp(t.title(), "INSTRUMENT MODS") == 0);
  t.tap(Button::F1);
  t.turn(10);
  CHECK(t.param(kGrowl, Growl::Vowel) > 0.0f);  // the growl is the fifth unit, after the freeze
  t.tap(Button::F5);  // the wobble's rate
  t.turn(1);
  CHECK(t.pat().length != 0);

  t.tap(Button::Params);  // wraps to VOICE; its latch is the SOUND page's (F1, CUTOFF), not the pages' that were walked
  CHECK(std::strcmp(t.title(), "INSTRUMENT VOICE") == 0);
  const float cutoff0 = t.param(kVoice, AcidVoice::Cutoff);
  t.turn(3);
  CHECK(t.param(kVoice, AcidVoice::Cutoff) != cutoff0);
  t.to_home();
  t.to_instrument(kInstMods);  // Params from another screen always opens VOICE; MODS is two more
  CHECK(std::strcmp(t.title(), "INSTRUMENT MODS") == 0);
}

TEST_CASE("hold a step + the encoder p-locks the focused parameter on that step, on the lane that drives it") {
  Hands t;
  const float knob = t.param(kVoice, AcidVoice::Cutoff);
  t.down(step_pad_of(3));
  t.turn(10);
  t.turn(10);
  t.up(step_pad_of(3));
  const Pattern& p = t.pat();
  REQUIRE(step_has_motion(p, 0, 3));
  const std::uint8_t first = p.motion[0][3 * kMotionPointsPerStep];
  for (unsigned q = 1; q < kMotionPointsPerStep; ++q) {
    CHECK(p.motion[0][3 * kMotionPointsPerStep + q] == first);  // all four points of the step
  }
  const core::ParamDesc* desc = t.h->engine().param_desc(kVoice, AcidVoice::Cutoff);
  REQUIRE(desc != nullptr);
  CHECK(first > seq::value_to_point(*desc, knob));                     // turned up from the knob's value
  CHECK(t.param(kVoice, AcidVoice::Cutoff) == doctest::Approx(knob));      // the knob itself did not move
  CHECK_FALSE(step_has_motion(p, 0, 4));
  CHECK_FALSE(step_has_motion(p, 1, 3));

  t.tap(Button::F2);  // another lane, on another step
  t.down(step_pad_of(9));
  t.turn(-15);
  t.up(step_pad_of(9));
  CHECK(step_has_motion(t.pat(), 1, 9));
}

TEST_CASE("REC + Play + the encoder records motion under the playhead; without REC the encoder records nothing") {
  Hands t;
  t.tap(Button::Rec);
  t.tap(Button::Play);
  t.wait(200);
  for (unsigned i = 0; i < 12; ++i) {
    t.turn(4);
    t.wait(100);
  }
  t.tap(Button::Play);
  t.tap(Button::Rec);
  CHECK(pattern_has_motion(t.pat(), 0));

  // Without REC nothing is written (the MOTION latch is gone).
  t.shift_tap(Button::F6);  // the tools' CLEAR: no motion either
  CHECK_FALSE(pattern_has_motion(t.pat(), kMotionLanes));
  t.tap(Button::Play);
  t.wait(200);
  for (unsigned i = 0; i < 8; ++i) {
    t.turn(4);
    t.wait(100);
  }
  t.tap(Button::Play);
  CHECK_FALSE(pattern_has_motion(t.pat(), kMotionLanes));
}

TEST_CASE("a p-lock with an F key held gives the latch back at the release, like any hold-and-turn") {
  Hands t;
  t.tap(Button::F1);  // the cutoff is latched
  t.down(step_pad_of(3));
  t.press(Button::F3);  // hold ENV MOD
  t.turn(5);            // p-lock on its lane
  t.release(Button::F3);
  t.up(step_pad_of(3));
  CHECK(step_has_motion(t.pat(), 2, 3));
  CHECK_FALSE(step_has_motion(t.pat(), 0, 3));
  const float cutoff = t.param(kVoice, AcidVoice::Cutoff);
  const float env = t.param(kVoice, AcidVoice::EnvMod);
  t.turn(5);  // nothing held: the latched key, the cutoff
  CHECK(t.param(kVoice, AcidVoice::Cutoff) > cutoff);
  CHECK(t.param(kVoice, AcidVoice::EnvMod) == doctest::Approx(env));
}
