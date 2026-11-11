// SPDX-License-Identifier: GPL-3.0-or-later
// PERFORM KNOBS (K3): a pad of the page and the encoder are two hands on the same knob. A tap on the top pad of CUTOFF's row and the encoder turned up past
// the top leave the same engine value, write the same motion point on the same lane, and sound the same (level and peak) once the slew has settled. REC writes one lane; a
// knob with no free lane says so and writes nothing; HOME's FILT rows keep their defaults.
#include <doctest/doctest.h>

#include "hands.h"
#include "../app/src/ladder_rows.h"

#include <o3sdk/app/pad_leds.h>
#include <o3sdk/core/param.h>
#include <o3_303/app_access.h>
#include <o3_303/seq/ops.h>
#include <o3_303/units/acid_voice/acid_voice.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace hands;
using namespace o3_303;

namespace {

constexpr unsigned kTop = 11;  // the last pad of a row: a continuous knob's maximum (ladder_norm(11) is 1)

unsigned row_pad(unsigned row, unsigned col) { return app::pad_at(row, col); }

struct Outcome {
  float value = 0.0f;
  int lane = -1;
  unsigned lanes_with_motion = 0;
  unsigned points = 0;
  std::uint8_t point = kNoLock;
  std::vector<std::int16_t> audio;
};

// Cutoff to its top with REC on while the pattern plays, by a tap on KNOBS' row 3 (`pad`) or by the encoder on HOME with F1 latched; then the
// pattern stopped, the slew let to settle, and a held note of the keyboard captured.
Outcome run(bool pad) {
  Hands t;
  if (pad) {
    t.to_perform();
    t.tap(Button::Performance);
    t.wait(60);
    REQUIRE(std::strcmp(t.title(), "PERFORM KNOBS") == 0);
  } else {
    t.tap(Button::F1);  // CUTOFF
  }
  t.tap(Button::Play);
  t.wait(300);
  t.tap(Button::Rec);
  t.wait(100);
  if (pad) {
    t.pad(row_pad(2, kTop), 10);
  } else {
    t.turn(200);
  }
  t.wait(300);
  t.tap(Button::Rec);
  t.tap(Button::Play);
  t.wait(600);

  Outcome o;
  o.value = t.param(kVoice, AcidVoice::Cutoff);
  o.lane = seq::find_lane(t.pat(), kLaneVoice, AcidVoice::Cutoff);
  for (unsigned l = 0; l < kMotionLanes; ++l) {
    if (seq::lane_has_motion(t.pat(), l)) {
      ++o.lanes_with_motion;
    }
  }
  if (o.lane >= 0) {
    for (unsigned i = 0; i < kMaxSteps * kMotionPointsPerStep; ++i) {
      const std::uint8_t p = t.pat().motion[o.lane][i];
      if (p != kNoLock) {
        if (o.points == 0) {
          o.point = p;
        }
        ++o.points;
      }
    }
  }
  t.to_home();  // KNOBS' four rows are ladders: the keyboard is HOME's
  t.wait(60);
  t.h->record(true);
  t.down(key_pad(48));
  t.wait(400);
  t.up(key_pad(48));
  t.wait(100);
  t.h->record(false);
  o.audio = t.h->audio();
  return o;
}

}  // namespace

TEST_CASE("KNOBS: a pad tap and an encoder turn to the same value leave the same value, the same motion point on the same lane, the same sound") {
  const Outcome a = run(true);
  const Outcome b = run(false);
  const core::ParamDesc* d = nullptr;
  {
    Hands t;
    d = t.h->engine().param_desc(kVoice, AcidVoice::Cutoff);
    REQUIRE(d != nullptr);
    CHECK(a.value == doctest::Approx(AcidVoice::kCutoffLiveHiHz).epsilon(0.0001));  // F1: the top of the live range
    CHECK(b.value == doctest::Approx(AcidVoice::kCutoffLiveHiHz).epsilon(0.0001));
  }
  CHECK(a.value == b.value);
  CHECK(a.lane >= 0);
  CHECK(a.lane == b.lane);
  CHECK(a.lane == 0);  // CUTOFF is the SOUND page's first lane
  REQUIRE(a.points > 0u);
  REQUIRE(b.points > 0u);
  CHECK(a.point == b.point);
  CHECK(a.point == seq::value_to_point(*d, AcidVoice::kCutoffLiveHiHz));
  // REC wrote one lane, and only that one.
  CHECK(a.lanes_with_motion == 1u);
  CHECK(b.lanes_with_motion == 1u);
  // The audio of the held note after both settled. The two runs reach it by different button taps, so the note starts a block apart and the samples are not
  // identical; the sound is the same: the same peak and the same level (RMS) of the held note, within a few per cent.
  REQUIRE(a.audio.size() > 8000u);
  REQUIRE(b.audio.size() > 8000u);
  auto level = [](const std::vector<std::int16_t>& w, double* rms, std::int32_t* peak) {
    double sum = 0.0;
    std::size_t n = 0;
    *peak = 0;
    for (std::size_t i = 4000; i < w.size(); ++i) {
      const std::int32_t m = w[i] < 0 ? -static_cast<std::int32_t>(w[i]) : w[i];
      *peak = m > *peak ? m : *peak;
      sum += static_cast<double>(m) * static_cast<double>(m);
      ++n;
    }
    *rms = n > 0 ? std::sqrt(sum / static_cast<double>(n)) : 0.0;
  };
  double ra = 0.0;
  double rb = 0.0;
  std::int32_t pa = 0;
  std::int32_t pb = 0;
  level(a.audio, &ra, &pa);
  level(b.audio, &rb, &pb);
  CHECK(pa > 200);  // the note sounded
  CHECK(pb > 200);
  CHECK(ra == doctest::Approx(rb).epsilon(0.03));
  CHECK(static_cast<double>(pa) == doctest::Approx(static_cast<double>(pb)).epsilon(0.05));
}

TEST_CASE("KNOBS: a list takes a pad a value, the same value as the encoder's detent, and the same motion point") {
  // WAVE: SAW is value 0, SQR value 1: pad 1 of the row, or one detent up.
  auto wave = [](bool pad) {
    Hands t;
    if (pad) {
      t.to_perform();
      t.tap(Button::Performance);
      t.wait(60);
      // Hold F6 (WAVE) and tap ROW 1: the row is the WAVE list.
      t.press(Button::F6);
      t.wait(30);
      t.pad(row_pad(0, 0), 10);
      t.release(Button::F6);
      t.wait(30);
    } else {
      t.tap(Button::F6);
    }
    t.tap(Button::Play);
    t.wait(300);
    t.tap(Button::Rec);
    t.wait(100);
    if (pad) {
      t.pad(row_pad(0, 1), 10);
    } else {
      t.turn(1);
    }
    t.wait(300);
    Outcome o;
    o.value = t.param(kVoice, AcidVoice::Wave);
    o.lane = seq::find_lane(t.pat(), kLaneVoice, AcidVoice::Wave);
    if (o.lane >= 0) {
      for (unsigned i = 0; i < kMaxSteps * kMotionPointsPerStep; ++i) {
        const std::uint8_t p = t.pat().motion[o.lane][i];
        if (p != kNoLock) {
          o.point = o.point == kNoLock ? p : o.point;
          ++o.points;
        }
      }
    }
    t.tap(Button::Rec);
    t.tap(Button::Play);
    return o;
  };
  const Outcome a = wave(true);
  const Outcome b = wave(false);
  CHECK(a.value == 1.0f);
  CHECK(b.value == 1.0f);
  CHECK(a.lane >= 0);
  CHECK(a.lane == b.lane);
  REQUIRE(a.points > 0u);
  REQUIRE(b.points > 0u);
  CHECK(a.point == b.point);
}

TEST_CASE("KNOBS: with all eight lanes held by other parameters a tap still moves the knob, says so, and writes nothing") {
  Hands t;
  for (unsigned id = 0; id < 8; ++id) {  // the distortion holds every lane, each with a lock
    REQUIRE(bank().lock_step(t.selected(), kLaneDist, static_cast<std::uint8_t>(id), 2, 100) >= 0);
  }
  t.to_perform();
  t.tap(Button::Performance);
  t.wait(60);
  REQUIRE(std::strcmp(t.title(), "PERFORM KNOBS") == 0);
  t.tap(Button::Play);
  t.wait(300);
  t.tap(Button::Rec);
  t.wait(100);
  t.pad(row_pad(2, kTop), 10);  // the ninth knob: CUTOFF
  t.wait(300);
  CHECK(t.param(kVoice, AcidVoice::Cutoff) > 0.9f * AcidVoice::kCutoffLiveHiHz);  // the knob moved
  CHECK(seq::find_lane(t.pat(), kLaneVoice, AcidVoice::Cutoff) < 0);                                              // no lane took it
  for (unsigned l = 0; l < kMotionLanes; ++l) {
    CHECK(t.pat().lanes[l].target == kLaneDist);
  }
  t.tap(Button::Rec);
  t.tap(Button::Play);
}

TEST_CASE("KNOBS: HOME's FILT rows keep their defaults, CUTOFF on row 3 and RESO on row 4") {
  Hands t;
  t.pad(role_pad(PadRole::Filt), 10);
  t.wait(30);
  t.pad(row_pad(2, kTop), 10);
  t.pad(row_pad(3, kTop), 10);
  t.wait(300);
  const core::ParamDesc* cut = t.h->engine().param_desc(kVoice, AcidVoice::Cutoff);
  const core::ParamDesc* res = t.h->engine().param_desc(kVoice, AcidVoice::Reso);
  REQUIRE(cut != nullptr);
  REQUIRE(res != nullptr);
  CHECK(t.param(kVoice, AcidVoice::Cutoff) == doctest::Approx(AcidVoice::kCutoffLiveHiHz).epsilon(0.0001));
  CHECK(t.param(kVoice, AcidVoice::Reso) == doctest::Approx(res->max).epsilon(0.0001));
  // The same two rows are KNOBS' rows 3 and 4.
  t.tap(Button::Pattern);
  t.to_perform();
  t.tap(Button::Performance);
  t.wait(60);
  t.pad(row_pad(2, 0), 10);
  t.pad(row_pad(3, 0), 10);
  t.wait(300);
  CHECK(t.param(kVoice, AcidVoice::Cutoff) == doctest::Approx(AcidVoice::kCutoffLiveLoHz).epsilon(0.001));
  CHECK(t.param(kVoice, AcidVoice::Reso) == doctest::Approx(res->min).epsilon(0.001));
}
