// SPDX-License-Identifier: GPL-3.0-or-later
// Motion lanes through the pattern's lane table (docs/M3B-PLAN.md C2 and the review's finding 4): a p-lock (hold a step + the encoder) and a
// recorded move go to the lane that drives the focused parameter, scaled with that target's own descriptor or the mixer's range, not to "the
// lane with the voice's parameter number"; a parameter no lane drives claims one (UX-303 D2: the first lane with no motion, in the same undoable edit).
#include <doctest/doctest.h>

#include "hands.h"

#include <o3sdk/core/param.h>
#include <o3_303/seq/ops.h>
#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/acid_voice/acid_voice.h>

#include <cmath>
#include <cstdlib>

using namespace hands;
using namespace o3_303;

namespace {

float db_to_gain(float db) { return db <= -59.5f ? 0.0f : std::fmin(1.5f, std::pow(10.0f, db * 0.05f)); }

// The point of step `s` (first quarter) of `lane`.
std::uint8_t point_at(const Pattern& p, unsigned lane, unsigned s) { return p.motion[lane][s * kMotionPointsPerStep]; }

// Hold step `step` and turn the FX page's DRIVE (its F1) by `detents`: the distortion's Drive has no lane in a fresh pattern, so one is claimed.
// Starts and ends on the SOUND page.
void lock_drive(Hands& t, unsigned step, int detents) {
  t.to_page(kPageFx);
  t.tap(Button::F1);  // DRIVE
  t.down(step_pad_of(step));
  t.turn(detents);
  t.up(step_pad_of(step));
  t.back_to_sound(kPageFx);
}

}  // namespace

TEST_CASE("the default lanes are the SOUND page and its keys' p-locks land on them") {
  Hands t;
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    CHECK(t.pat().lanes[lane].target == kDefaultLanes[lane].target);
    CHECK(t.pat().lanes[lane].id == kDefaultLanes[lane].id);
  }
  // Hold step 5 and turn CUTOFF (F1), RESO (F2): lanes 0 and 1.
  t.tap(Button::F1);
  t.down(step_pad_of(5));
  t.turn(10);
  t.up(step_pad_of(5));
  t.tap(Button::F2);
  t.down(step_pad_of(5));
  t.turn(-10);
  t.up(step_pad_of(5));
  CHECK(point_at(t.pat(), 0, 5) != kNoLock);
  CHECK(point_at(t.pat(), 1, 5) != kNoLock);
  for (unsigned lane = 2; lane < kMotionLanes; ++lane) {
    CHECK_FALSE(seq::lane_has_motion(t.pat(), lane));
  }
}

TEST_CASE("hold a step + the encoder on VOLUME locks the mixer's level lane, scaled with the mixer's range (0..1.5 gain)") {
  Hands t;
  t.tap(Button::F8);  // VOLUME, -6.0 dB, half a dB a detent
  t.down(step_pad_of(4));
  t.turn(6);  // -3.0 dB
  t.up(step_pad_of(4));
  CHECK(seq::lane_has_motion(t.pat(), 7));
  for (unsigned lane = 0; lane < 7; ++lane) {
    CHECK_FALSE(seq::lane_has_motion(t.pat(), lane));  // and not the voice's id 7
  }
  CHECK(point_at(t.pat(), 7, 4) == mixer_value_to_point(kMixerLevel, db_to_gain(-3.0f)));
  CHECK(point_at(t.pat(), 7, 4) == mixer_value_to_point(kMixerLevel, 0.7079f));
  // The page's own VOLUME did not move: a p-lock is the step's.
  CHECK(t.h->engine().mix(0, dsp::MixField::Level) == doctest::Approx(db_to_gain(-6.0f)).epsilon(0.001));
  // Another turn on the same step goes on from the lock's value, not from the knob's.
  t.down(step_pad_of(4));
  t.turn(6);  // -3 -> 0 dB
  t.up(step_pad_of(4));
  const int unity = mixer_value_to_point(kMixerLevel, 1.0f);
  CHECK(std::abs(static_cast<int>(point_at(t.pat(), 7, 4)) - unity) <= 1);  // the point's step is 0.006 gain: -3 dB + 3 dB is unity to a point
}

TEST_CASE("a lock on a parameter no lane drives claims the first lane with no motion, in one undo step with the lock") {
  Hands t;
  const Pattern before = t.pat();
  bank().clear_history();
  lock_drive(t, 2, 3);
  CHECK(t.pat().lanes[0].target == kLaneDist);  // lane 0 (CUTOFF's) had no points: it is DRIVE's now
  CHECK(t.pat().lanes[0].id == Distortion::Drive);
  CHECK(point_at(t.pat(), 0, 2) != kNoLock);
  for (unsigned lane = 1; lane < kMotionLanes; ++lane) {
    CHECK(t.pat().lanes[lane].target == before.lanes[lane].target);
    CHECK(t.pat().lanes[lane].id == before.lanes[lane].id);
    CHECK_FALSE(seq::lane_has_motion(t.pat(), lane));
  }
  for (unsigned i = 0; i < 16; ++i) {  // the steps are untouched
    CHECK(t.pat().steps[i].note == before.steps[i].note);
  }
  // One undo takes the lock and the claim away together; the redo gives both back.
  CHECK(bank().undo() == static_cast<int>(t.selected()));
  CHECK(t.pat().lanes[0].target == kLaneVoice);
  CHECK(t.pat().lanes[0].id == AcidVoice::Cutoff);
  CHECK(point_at(t.pat(), 0, 2) == kNoLock);
  CHECK(bank().redo() == static_cast<int>(t.selected()));
  CHECK(t.pat().lanes[0].target == kLaneDist);
  CHECK(point_at(t.pat(), 0, 2) != kNoLock);
}

TEST_CASE("a lock on DRIVE is scaled with the distortion's descriptor on its claimed lane; the knob stays") {
  Hands t;
  const core::ParamDesc* d = t.h->engine().param_desc(kDist, Distortion::Drive);
  REQUIRE(d != nullptr);
  const float drive0 = t.param(kDist, Distortion::Drive);
  lock_drive(t, 6, 3);
  CHECK(point_at(t.pat(), 0, 6) == seq::value_to_point(*d, core::param_step(*d, drive0, 3)));
  CHECK(t.param(kDist, Distortion::Drive) == drive0);  // a p-lock is the step's
  for (unsigned lane = 1; lane < kMotionLanes; ++lane) {
    CHECK_FALSE(seq::lane_has_motion(t.pat(), lane));  // not "the voice's id 0" for DRIVE's id 0
  }
  // A second lock on the same parameter finds its lane and claims no other.
  lock_drive(t, 8, -2);
  CHECK(point_at(t.pat(), 0, 8) != kNoLock);
  CHECK_FALSE(seq::lane_has_motion(t.pat(), 1));
}

TEST_CASE("a key no lane can drive is not locked: hold a step + the encoder moves the knob, and writes no motion") {
  Hands t;
  t.to_page(kPageSeq);
  t.tap(Button::F1);  // TEMPO
  const std::uint32_t tempo0 = t.h->engine().tempo();
  t.down(step_pad_of(3));
  t.turn(5);
  t.up(step_pad_of(3));
  CHECK(t.h->engine().tempo() != tempo0);
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    CHECK_FALSE(seq::lane_has_motion(t.pat(), lane));
  }
  t.back_to_sound(kPageSeq);
}

TEST_CASE("with all eight lanes in use a lock on a ninth parameter changes nothing") {
  Hands t;
  for (unsigned k = 0; k < 8; ++k) {  // F1..F8 of the SOUND page: the eight default lanes
    t.tap(app::fkey(k));
    t.down(step_pad_of(k));
    t.turn(3);
    t.up(step_pad_of(k));
  }
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    REQUIRE(seq::lane_has_motion(t.pat(), lane));
  }
  const Pattern before = t.pat();
  lock_drive(t, 12, 3);
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    CHECK(t.pat().lanes[lane].target == before.lanes[lane].target);
    CHECK(t.pat().lanes[lane].id == before.lanes[lane].id);
    CHECK(point_at(t.pat(), lane, 12) == kNoLock);
  }
}

TEST_CASE("a recorded move goes to the lane that drives the knob: DRIVE claims one, VOLUME is lane 7; with REC off nothing is written") {
  Hands t;
  const core::ParamDesc* d = t.h->engine().param_desc(kDist, Distortion::Drive);
  REQUIRE(d != nullptr);
  t.tap(Button::Play);
  t.wait(200);
  // Not recording: the encoder moves the knob and writes nothing.
  t.to_page(kPageFx);
  t.tap(Button::F1);  // DRIVE
  t.turn(1);
  t.wait(200);
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    CHECK_FALSE(seq::lane_has_motion(t.pat(), lane));
  }
  t.tap(Button::Rec);  // REC while playing: moves of the encoder are written
  t.wait(40);
  t.turn(-5);
  t.wait(300);
  t.turn(2);
  t.wait(300);
  const float drive = t.param(kDist, Distortion::Drive);
  CHECK(t.pat().lanes[0].target == kLaneDist);  // claimed: lane 0 had no motion
  CHECK(seq::lane_has_motion(t.pat(), 0));
  // The last point written is the knob's value now, through the descriptor (the playing pattern may have wrapped: look for any point equal).
  bool found = false;
  for (unsigned i = 0; i < 16 * kMotionPointsPerStep; ++i) {
    if (t.pat().motion[0][i] == seq::value_to_point(*d, drive)) {
      found = true;
    }
  }
  CHECK(found);
  for (unsigned lane = 1; lane < kMotionLanes; ++lane) {
    CHECK_FALSE(seq::lane_has_motion(t.pat(), lane));
  }
  // VOLUME (SOUND F8) is the mixer's level, lane 7.
  t.back_to_sound(kPageFx);
  t.tap(Button::F8);
  t.turn(4);  // -6 dB -> -4 dB
  t.wait(300);
  CHECK(seq::lane_has_motion(t.pat(), 7));
  found = false;
  for (unsigned i = 0; i < 16 * kMotionPointsPerStep; ++i) {
    if (t.pat().motion[7][i] == mixer_value_to_point(kMixerLevel, db_to_gain(-4.0f))) {
      found = true;
    }
  }
  CHECK(found);
  // DECAY still has lane 3: turning it while recording writes there.
  t.tap(Button::F4);
  t.turn(5);
  t.wait(300);
  CHECK(seq::lane_has_motion(t.pat(), 3));
  t.tap(Button::Play);
}
