// SPDX-License-Identifier: GPL-3.0-or-later
// PERFORM KNOBS (the KNOBS pads mode, K2): PERFORM's second page, four ladder rows of pads. ROW 1 ENV MOD and ROW 2 DECAY are its own, ROW 3 CUTOFF and ROW 4 RESO are HOME's FILT rows; an F key
// held + a tap on a row binds it, Shift + F1..F4 points the encoder at the row's list of every bindable knob, Shift + F8 puts the four rows back. The page is the second of three:
// PERFORM, KNOBS, SETUP.
#include <doctest/doctest.h>

#include "hands.h"
#include "../app/src/ladder_rows.h"

#include <o3sdk/app/pad_leds.h>
#include <o3sdk/core/param.h>
#include <o3_303/units/acid_voice/acid_voice.h>

#include <cstring>

using namespace hands;
using namespace o3_303;

namespace {

unsigned row_pad(unsigned row, unsigned col) { return app::pad_at(row, col); }

float goal(Hands& t, unsigned id, unsigned col) {
  const core::ParamDesc* d = t.h->engine().param_desc(kVoice, static_cast<std::uint16_t>(id));
  REQUIRE(d != nullptr);
  if (id == AcidVoice::Cutoff) {  // F1: the Cutoff ladder covers the live range
    const KnobSpan span{AcidVoice::kCutoffLiveLoHz, AcidVoice::kCutoffLiveHiHz};
    return ladder_value(*d, col, &span);
  }
  return core::param_denorm(*d, ladder_norm(col));
}

float val(Hands& t, unsigned id) { return t.param(kVoice, id); }

void to_knobs(Hands& t) {
  t.to_perform();
  t.tap(Button::Performance);
  t.wait(60);
  REQUIRE(std::strcmp(t.title(), "PERFORM KNOBS") == 0);
}

// Hold `f`, tap the pad, let go: the binding gesture.
void bind(Hands& t, Button f, unsigned pad) {
  t.press(f);
  t.wait(30);
  t.pad(pad, 10);
  t.release(f);
  t.wait(30);
}

// Shift + F<row+1>, then Shift up: the encoder is the row's list.
void point_at_row(Hands& t, unsigned row) {
  t.press(Button::Shift);
  t.tap(app::fkey(row));
  t.release(Button::Shift);
  t.wait(30);
}

}  // namespace

TEST_CASE("PERFORM KNOBS: the second page of three, Perform cycles PERFORM, KNOBS, SETUP") {
  Hands t;
  t.to_perform();
  CHECK(std::strcmp(t.title(), "PERFORM") == 0);
  t.tap(Button::Performance);
  CHECK(std::strcmp(t.title(), "PERFORM KNOBS") == 0);
  t.tap(Button::Performance);
  CHECK(std::strcmp(t.title(), "PERFORM SETUP") == 0);
  t.tap(Button::Performance);
  CHECK(std::strcmp(t.title(), "PERFORM") == 0);
  // From another screen Perform always opens page 1.
  t.tap(Button::Performance);
  t.to_home();
  t.to_perform();
  CHECK(std::strcmp(t.title(), "PERFORM") == 0);
}

TEST_CASE("PERFORM KNOBS: a pad of each row sets ENV MOD, DECAY, CUTOFF and RESO; the colour buttons stay PERFORM's actions") {
  Hands t;
  to_knobs(t);
  t.pad(row_pad(0, 7), 10);
  t.pad(row_pad(1, 4), 10);
  t.pad(row_pad(2, 9), 10);
  t.pad(row_pad(3, 2), 10);
  t.wait(150);
  CHECK(val(t, AcidVoice::EnvMod) == doctest::Approx(goal(t, AcidVoice::EnvMod, 7)).epsilon(0.001));
  CHECK(val(t, AcidVoice::Decay) == doctest::Approx(goal(t, AcidVoice::Decay, 4)).epsilon(0.001));
  CHECK(val(t, AcidVoice::Cutoff) == doctest::Approx(goal(t, AcidVoice::Cutoff, 9)).epsilon(0.001));
  CHECK(val(t, AcidVoice::Reso) == doctest::Approx(goal(t, AcidVoice::Reso, 2)).epsilon(0.001));
  // The lamp of each row sits at the knob's pad.
  t.wait(40);
  CHECK(t.led(row_pad(0, 7)) > t.led(row_pad(0, 10)));
  // The Note button is MUTE here, as on PERFORM: held, it mutes the sequencer and does not touch the rows.
  const float decay = val(t, AcidVoice::Decay);
  t.press(Button::Note);
  t.wait(20);
  t.release(Button::Note);
  CHECK(val(t, AcidVoice::Decay) == decay);
  CHECK(std::strcmp(t.title(), "PERFORM KNOBS") == 0);
}

TEST_CASE("PERFORM KNOBS: ROW 3 and ROW 4 are HOME's FILT rows") {
  Hands t;
  to_knobs(t);
  t.pad(row_pad(2, 6), 10);
  t.wait(150);
  const float cutoff = val(t, AcidVoice::Cutoff);
  CHECK(cutoff == doctest::Approx(goal(t, AcidVoice::Cutoff, 6)).epsilon(0.001));
  // HOME's FILT page: row 3 is the same knob and holds the same value (a pad there moves the same CUTOFF).
  t.to_home();
  t.pad(role_pad(PadRole::Filt));
  t.wait(40);
  t.pad(row_pad(2, 3), 10);
  t.wait(150);
  CHECK(val(t, AcidVoice::Cutoff) == doctest::Approx(goal(t, AcidVoice::Cutoff, 3)).epsilon(0.001));
  to_knobs(t);
  CHECK(val(t, AcidVoice::Cutoff) == doctest::Approx(goal(t, AcidVoice::Cutoff, 3)).epsilon(0.001));
}

TEST_CASE("PERFORM KNOBS: an F key held + a tap binds a row, the tap moves nothing; again, back to its default") {
  Hands t;
  to_knobs(t);
  const float accent = val(t, AcidVoice::Accent);
  const float envmod = val(t, AcidVoice::EnvMod);
  bind(t, Button::F5, row_pad(0, 3));  // ACCENT on ROW 1
  CHECK(val(t, AcidVoice::Accent) == accent);
  CHECK(val(t, AcidVoice::EnvMod) == envmod);
  t.pad(row_pad(0, 9), 10);
  t.wait(150);
  CHECK(val(t, AcidVoice::Accent) == doctest::Approx(goal(t, AcidVoice::Accent, 9)).epsilon(0.001));
  CHECK(val(t, AcidVoice::EnvMod) == envmod);
  bind(t, Button::F5, row_pad(0, 3));  // again: ENV MOD
  t.pad(row_pad(0, 2), 10);
  t.wait(150);
  CHECK(val(t, AcidVoice::EnvMod) == doctest::Approx(goal(t, AcidVoice::EnvMod, 2)).epsilon(0.001));
}

TEST_CASE("PERFORM KNOBS: Shift + F2 walks ROW 2's list of knobs, Shift + F8 puts the rows back") {
  Hands t;
  to_knobs(t);
  float before[AcidVoice::kParamCount];
  for (unsigned i = 0; i < AcidVoice::kParamCount; ++i) {
    before[i] = val(t, i);
  }
  point_at_row(t, 1);
  t.h->turn(1);
  t.wait(30);
  t.pad(row_pad(1, 11), 10);  // the walked knob, to the top of its range
  t.wait(200);
  unsigned moved = 0;
  for (unsigned i = 0; i < AcidVoice::kParamCount; ++i) {
    moved += val(t, i) != before[i] ? 1u : 0u;
  }
  CHECK(moved == 1u);
  CHECK(val(t, AcidVoice::Decay) == before[AcidVoice::Decay]);  // ROW 2 is not DECAY any more
  // DEFAULTS: Shift + F8; ROW 2 is DECAY again.
  t.press(Button::Shift);
  t.tap(Button::F8);
  t.release(Button::Shift);
  t.wait(30);
  t.pad(row_pad(1, 5), 10);
  t.wait(200);
  CHECK(val(t, AcidVoice::Decay) == doctest::Approx(goal(t, AcidVoice::Decay, 5)).epsilon(0.001));
}

TEST_CASE("PERFORM KNOBS: a tap on a row that holds a mod ends MODS OFF (STOCK), as the encoder does") {
  Hands t;
  t.press(Button::Shift);  // Shift + Sample Loader: MODS OFF (STOCK on)
  t.tap(Button::SampleLoad);
  t.release(Button::Shift);
  t.wait(40);
  REQUIRE(val(t, AcidVoice::Stock) == 1.0f);
  to_knobs(t);
  // A tap on ROW 1 (ENV MOD, not a mod) leaves STOCK on.
  t.pad(row_pad(0, 5), 10);
  t.wait(60);
  CHECK(val(t, AcidVoice::Stock) == 1.0f);
  // Walk ROW 2 along the list until a tap on it ends STOCK: a mod is on the list.
  bool ended = false;
  for (unsigned i = 0; i < 80 && !ended; ++i) {
    point_at_row(t, 1);  // a tap on a pad lets the encoder go: point it at the row again
    t.h->turn(1);
    t.wait(20);
    t.pad(row_pad(1, 11), 10);
    t.wait(30);
    ended = val(t, AcidVoice::Stock) == 0.0f;
  }
  CHECK(ended);
}
