// SPDX-License-Identifier: GPL-3.0-or-later
// The pad filter ladders (docs/UX-303.md 6.2): with FILT on, rows 3 and 4 of the pads are 12 places of Cutoff and Reso; a tap jumps there through a
// 30 ms slew; the LEDs draw the position as a bar; FILT is a toggle, and the keyboard is back when it is off. Rec while the pattern plays writes a
// ladder tap as motion.
#include <doctest/doctest.h>

#include "hands.h"

#include <o3sdk/app/pad_leds.h>
#include <o3sdk/core/param.h>
#include <o3_303/app_access.h>
#include <o3_303/seq/ops.h>
#include <o3_303/units/acid_voice/acid_voice.h>

using namespace hands;
using namespace o3_303;

namespace {

const unsigned kFiltPad = role_pad(PadRole::Filt);
const unsigned kTrnspPad = role_pad(PadRole::Trnsp);

unsigned ladder_pad(unsigned row, unsigned col) { return app::pad_at(row, col); }

float expected(Hands& t, unsigned id, unsigned col) {
  const core::ParamDesc* d = t.h->engine().param_desc(kVoice, static_cast<std::uint16_t>(id));
  REQUIRE(d != nullptr);
  return core::param_denorm(*d, ladder_norm(col));
}

}  // namespace

TEST_CASE("FILT on: row 3 is the Cutoff ladder and row 4 the Reso ladder; Shift changes nothing") {
  Hands t;
  seq::Sequencer& s = app_sequencer();
  t.pad(kFiltPad);
  const float reso0 = t.param(kVoice, AcidVoice::Reso);
  const float env0 = t.param(kVoice, AcidVoice::EnvMod);
  // The ends and the middle of the Cutoff ladder (a log knob: the positions are on the knob, not in Hz).
  for (unsigned col : {0u, 5u, 11u}) {
    t.pad(ladder_pad(2, col), 10);
    t.wait(100);
    CHECK(t.param(kVoice, AcidVoice::Cutoff) == doctest::Approx(expected(t, AcidVoice::Cutoff, col)).epsilon(0.001));
  }
  CHECK(t.param(kVoice, AcidVoice::Cutoff) == doctest::Approx(4500.0f));  // the last pad is the knob's maximum
  t.pad(ladder_pad(2, 0), 10);
  t.wait(100);
  CHECK(t.param(kVoice, AcidVoice::Cutoff) == doctest::Approx(expected(t, AcidVoice::Cutoff, 0)));  // and the first its minimum
  CHECK(t.param(kVoice, AcidVoice::Cutoff) == doctest::Approx(t.h->engine().param_desc(kVoice, AcidVoice::Cutoff)->min));
  // Reso on row 4.
  t.pad(ladder_pad(3, 8), 10);
  t.wait(100);
  CHECK(t.param(kVoice, AcidVoice::Reso) == doctest::Approx(expected(t, AcidVoice::Reso, 8)).epsilon(0.001));
  CHECK(t.param(kVoice, AcidVoice::EnvMod) == env0);
  CHECK(t.param(kVoice, AcidVoice::Reso) != reso0);
  // Shift: the Env Mod ladder is cut (UX-303 D20): row 4 is Reso either way.
  t.press(Button::Shift);
  t.pad(ladder_pad(3, 2), 10);
  t.release(Button::Shift);
  t.wait(100);
  CHECK(t.param(kVoice, AcidVoice::Reso) == doctest::Approx(expected(t, AcidVoice::Reso, 2)).epsilon(0.001));
  CHECK(t.param(kVoice, AcidVoice::EnvMod) == env0);
  CHECK(s.slewing() == 0);
}

TEST_CASE("a ladder tap is a 30 ms slew, not a jump, and the knob reads the goal at once") {
  Hands t;
  seq::Sequencer& s = app_sequencer();
  t.pad(kFiltPad);
  t.down(ladder_pad(2, 10));
  t.up(ladder_pad(2, 10));
  t.wait(5);
  CHECK(s.slewing() >= 1);  // the slew is running: 30 ms are about 20 blocks
  CHECK(t.param(kVoice, AcidVoice::Cutoff) == doctest::Approx(expected(t, AcidVoice::Cutoff, 10)).epsilon(0.001));  // what the strip shows
  t.wait(60);
  CHECK(s.slewing() == 0);
}

TEST_CASE("the ladder pads are no notes: nothing is written by REC and nothing sounds; the keyboard is back when FILT is off") {
  Hands t;
  const Pattern before = t.pat();
  t.tap(Button::Rec);
  t.pad(kFiltPad);
  t.pad(ladder_pad(2, 3), 10);
  t.pad(ladder_pad(3, 7), 10);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == before.steps[i].note);
    CHECK(t.pat().steps[i].flags == before.steps[i].flags);
  }
  // Off, the same pad is a key again: REC writes it on the cursor step.
  t.pad(kFiltPad);
  t.pad(ladder_pad(3, 2), 10);
  CHECK(t.pat().steps[0].note == 38);  // D2
}

TEST_CASE("the LEDs draw the ladder position as a bar while FILT is on, and the keyboard again after") {
  Hands t;
  t.pad(kFiltPad);
  t.pad(ladder_pad(2, 4), 10);
  t.pad(ladder_pad(3, 9), 10);
  t.wait(80);
  for (unsigned col = 0; col < 12; ++col) {
    CHECK(t.led(ladder_pad(2, col)) == (col < 4 ? app::kLedMid : (col == 4 ? app::kLedOn : app::kLedOff)));
    CHECK(t.led(ladder_pad(3, col)) == (col < 9 ? app::kLedMid : (col == 9 ? app::kLedOn : app::kLedOff)));
  }
  // The step pads and the modifier block are not touched by the ladder; FILT is lit.
  CHECK(t.led(kFiltPad) == app::kLedOn);
  t.pad(kFiltPad);
  t.wait(80);
  CHECK(t.led(kFiltPad) == app::kLedLow);
  // The keyboard's own LEDs: naturals dim, sharps off.
  CHECK(t.led(key_pad(36 + 1)) == app::kLedOff);
  CHECK(t.led(key_pad(36 + 2)) == app::kLedDim);
  CHECK(t.led(key_pad(48 + 1)) == app::kLedOff);
}

TEST_CASE("TRNSP and FILT are toggles that take effect at the press and are independent of each other") {
  Hands t;
  CHECK(t.led(kTrnspPad) == app::kLedLow);
  CHECK(t.led(kFiltPad) == app::kLedLow);
  t.down(kTrnspPad);
  t.wait(10);
  CHECK(t.led(kTrnspPad) == app::kLedOn);  // at the press, not at the release
  t.up(kTrnspPad);
  t.wait(40);
  CHECK(t.led(kTrnspPad) == app::kLedOn);  // a toggle: it stays
  CHECK(t.led(kFiltPad) == app::kLedLow);
  t.pad(kFiltPad);
  t.wait(40);
  CHECK(t.led(kFiltPad) == app::kLedOn);
  CHECK(t.led(kTrnspPad) == app::kLedOn);
  // Held a long time: still just a toggle (no hold role).
  t.down(kTrnspPad);
  t.wait(400);
  t.up(kTrnspPad);
  t.wait(40);
  CHECK(t.led(kTrnspPad) == app::kLedLow);
  t.pad(kFiltPad);
  t.wait(40);
  CHECK(t.led(kFiltPad) == app::kLedLow);
}

TEST_CASE("a ladder tap while REC is on and the pattern plays is written as motion on the knob's lane") {
  Hands t;
  seq::Sequencer& s = app_sequencer();
  CHECK_FALSE(seq::lane_has_motion(t.pat(), 0));
  t.pad(kFiltPad);
  // Not recording: nothing is written.
  t.tap(Button::Play);
  t.wait(300);
  t.pad(ladder_pad(2, 7), 10);
  t.wait(300);
  CHECK_FALSE(seq::lane_has_motion(t.pat(), 0));
  // REC on: each tap is written under the playhead.
  t.tap(Button::Rec);
  t.wait(100);
  t.pad(ladder_pad(2, 3), 10);
  t.wait(50);
  t.pad(ladder_pad(3, 11), 10);  // Reso is lane 1
  t.wait(300);
  CHECK(seq::lane_has_motion(t.pat(), 0));
  CHECK(seq::lane_has_motion(t.pat(), 1));
  CHECK_FALSE(seq::lane_has_motion(t.pat(), 2));
  // The point is the knob position of the ladder pad, through the descriptor's curve.
  const core::ParamDesc* d = t.h->engine().param_desc(kVoice, AcidVoice::Cutoff);
  bool found = false;
  for (unsigned i = 0; i < 16 * kMotionPointsPerStep && !found; ++i) {
    const std::uint8_t pt = t.pat().motion[0][i];
    if (pt != kNoLock) {
      found = true;
      CHECK(seq::point_to_value(*d, pt) == doctest::Approx(expected(t, AcidVoice::Cutoff, 3)).epsilon(0.02));
    }
  }
  CHECK(found);
  t.tap(Button::Play);
  (void)s;
}
