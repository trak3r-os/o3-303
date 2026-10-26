// SPDX-License-Identifier: GPL-3.0-or-later
// Motion lanes through the pattern's lane table (docs/M3B-PLAN.md C2 and the review's finding 4): a p-lock (hold a step + the encoder) and a
// recorded move go to the lane that drives the focused parameter, scaled with that target's own descriptor or the mixer's range, not to "the
// lane with the voice's parameter number"; hold MOTION + an F key points the lane the screen shows at that key's parameter.
#include <doctest/doctest.h>

#include "hands.h"

#include <o3sdk/core/param.h>
#include <o3_303/seq/ops.h>
#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/tb303/tb303.h>

#include <cmath>
#include <cstdlib>

using namespace hands;
using namespace o3_303;

namespace {

const unsigned kMotionPad = role_pad(PadRole::Motion);

float db_to_gain(float db) { return db <= -59.5f ? 0.0f : std::fmin(1.5f, std::pow(10.0f, db * 0.05f)); }

// The point of step `s` (first quarter) of `lane`.
std::uint8_t point_at(const Pattern& p, unsigned lane, unsigned s) { return p.motion[lane][s * kMotionPointsPerStep]; }

// Points the lane shown (the focused SOUND key's lane) at the FX page's F1, DRIVE: the lane 3 (DECAY's) becomes the distortion's Drive.
// Starts and ends on the SOUND page (the FX page's F1 stays the latched key there).
void repoint_lane3_to_drive(Hands& t) {
  t.tap(Button::F4);  // DECAY: the screen shows lane 3
  t.to_page(kPageFx);
  t.down(kMotionPad);
  t.tap(Button::F1);  // DRIVE
  t.up(kMotionPad);
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

TEST_CASE("hold MOTION + an F key points the lane shown at that key's parameter, clears the lane's points, and is one undo step") {
  Hands t;
  // A p-lock on DECAY (lane 3) first: it is the lane's old content.
  t.tap(Button::F4);
  t.down(step_pad_of(2));
  t.turn(5);
  t.up(step_pad_of(2));
  REQUIRE(point_at(t.pat(), 3, 2) != kNoLock);
  bank().clear_history();
  const Pattern before = t.pat();

  repoint_lane3_to_drive(t);
  CHECK(t.pat().lanes[3].target == kLaneDist);
  CHECK(t.pat().lanes[3].id == Distortion::Drive);
  CHECK(point_at(t.pat(), 3, 2) == kNoLock);  // the points were another parameter's
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    if (lane != 3) {
      CHECK(t.pat().lanes[lane].target == before.lanes[lane].target);
      CHECK(t.pat().lanes[lane].id == before.lanes[lane].id);
    }
  }
  for (unsigned i = 0; i < 16; ++i) {  // the steps are untouched
    CHECK(t.pat().steps[i].note == before.steps[i].note);
  }
  // One undo gives the lane and its points back; and the redo does it again.
  CHECK(bank().undo() == static_cast<int>(t.selected()));
  CHECK(t.pat().lanes[3].target == kLaneVoice);
  CHECK(t.pat().lanes[3].id == Tb303::Decay);
  CHECK(point_at(t.pat(), 3, 2) == point_at(before, 3, 2));
  CHECK(bank().redo() == static_cast<int>(t.selected()));
  CHECK(t.pat().lanes[3].target == kLaneDist);
}

TEST_CASE("after the lane points at DRIVE, a p-lock on DRIVE is scaled with the distortion's descriptor on that lane") {
  Hands t;
  repoint_lane3_to_drive(t);
  const core::ParamDesc* d = t.h->engine().param_desc(kDist, Distortion::Drive);
  REQUIRE(d != nullptr);
  const float drive0 = t.param(kDist, Distortion::Drive);
  t.to_page(kPageFx);
  t.tap(Button::F1);  // DRIVE
  t.down(step_pad_of(6));
  t.turn(3);
  t.up(step_pad_of(6));
  t.back_to_sound(kPageFx);
  CHECK(point_at(t.pat(), 3, 6) == seq::value_to_point(*d, core::param_step(*d, drive0, 3)));
  CHECK(t.param(kDist, Distortion::Drive) == drive0);  // the knob stays: a p-lock is the step's
  // The other lanes got nothing (not lane 0 = "the voice's id 0" for DRIVE's id 0).
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    if (lane != 3) {
      CHECK_FALSE(seq::lane_has_motion(t.pat(), lane));
    }
  }
}

TEST_CASE("a key no lane drives is not locked: hold a step + the encoder moves the knob, and writes no motion") {
  Hands t;
  repoint_lane3_to_drive(t);  // DECAY lost its lane
  t.tap(Button::F4);
  const float decay0 = t.param(kVoice, Tb303::Decay);
  t.down(step_pad_of(3));
  t.turn(5);
  t.up(step_pad_of(3));
  CHECK(t.param(kVoice, Tb303::Decay) != decay0);
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    CHECK_FALSE(seq::lane_has_motion(t.pat(), lane));
  }
  // The same on a key that is no lane target at all: the SEQ page's TEMPO.
  t.to_page(kPageSeq);
  t.tap(Button::F1);
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

TEST_CASE("MOTION + the F key of a parameter some lane already drives only shows that lane; of a key no lane can drive, nothing") {
  Hands t;
  const Pattern before = t.pat();
  t.down(kMotionPad);
  t.tap(Button::F3);  // ENV MOD: lane 2 already has it
  t.up(kMotionPad);
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    CHECK(t.pat().lanes[lane].target == before.lanes[lane].target);
    CHECK(t.pat().lanes[lane].id == before.lanes[lane].id);
  }
  CHECK_FALSE(bank().can_undo());
  // SEQ's TEMPO is no lane target.
  t.to_page(kPageSeq);
  t.down(kMotionPad);
  t.tap(Button::F1);
  t.up(kMotionPad);
  t.back_to_sound(kPageSeq);
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    CHECK(t.pat().lanes[lane].target == before.lanes[lane].target);
    CHECK(t.pat().lanes[lane].id == before.lanes[lane].id);
  }
  CHECK_FALSE(bank().can_undo());
  // A plain tap of MOTION still latches the recording.
  t.pad(kMotionPad);
  t.wait(40);
  CHECK(t.led(kMotionPad) == app::kLedMid);
}

TEST_CASE("a recorded move goes to the lane that drives the knob: DRIVE on lane 3, VOLUME on lane 7; a knob with no lane writes nothing") {
  Hands t;
  repoint_lane3_to_drive(t);
  const core::ParamDesc* d = t.h->engine().param_desc(kDist, Distortion::Drive);
  REQUIRE(d != nullptr);
  t.tap(Button::Play);
  t.wait(200);
  t.pad(kMotionPad);  // the MOTION latch: moves of the encoder are written while the pattern plays
  t.wait(40);
  t.to_page(kPageFx);
  t.tap(Button::F1);  // DRIVE
  t.turn(-5);
  t.wait(300);
  t.turn(2);
  t.wait(300);
  const float drive = t.param(kDist, Distortion::Drive);
  CHECK(seq::lane_has_motion(t.pat(), 3));
  // The last point written is the knob's value now, through the descriptor (the playing pattern may have wrapped: look for any point equal).
  bool found = false;
  for (unsigned i = 0; i < 16 * kMotionPointsPerStep; ++i) {
    if (t.pat().motion[3][i] == seq::value_to_point(*d, drive)) {
      found = true;
    }
  }
  CHECK(found);
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    if (lane != 3) {
      CHECK_FALSE(seq::lane_has_motion(t.pat(), lane));
    }
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
  // DECAY lost its lane: turning it while recording writes nothing (it used to write lane 3).
  const Pattern mid = t.pat();
  t.tap(Button::F4);
  t.turn(5);
  t.wait(300);
  for (unsigned i = 0; i < 16 * kMotionPointsPerStep; ++i) {
    CHECK(t.pat().motion[3][i] == mid.motion[3][i]);
  }
  t.tap(Button::Play);
}
