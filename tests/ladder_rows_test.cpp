// SPDX-License-Identifier: GPL-3.0-or-later
// The ladder rows (docs/BACKLOG.md item 8, B4): HOME's FILT rows 3 and 4 and PERFORM's row 3 are one kind of row. A row follows the latched knob or holds a
// knob of its own; hold an F key and tap a pad of the row to assign it (the tap moves nothing), again to put it back to its default; Shift + FILT resets
// the HOME rows; a tap jumps the row's knob and, with REC on while the pattern plays, writes the jump as motion.
#include <doctest/doctest.h>

#include "hands.h"
#include "../app/src/home_map.h"
#include "../app/src/ladder_rows.h"

#include <o3sdk/app/pad_leds.h>
#include <o3sdk/core/param.h>
#include <o3_303/app_access.h>
#include <o3_303/seq/ops.h>
#include <o3_303/units/acid_voice/acid_voice.h>

#include <cstring>

using namespace hands;
using namespace o3_303;

namespace {

const unsigned kFiltPad = role_pad(PadRole::Filt);

unsigned row_pad(unsigned row, unsigned col) { return app::pad_at(row, col); }  // row 2: HOME's / PERFORM's row 3, row 3: HOME's row 4

float goal(Hands& t, unsigned id, unsigned col) {
  const core::ParamDesc* d = t.h->engine().param_desc(kVoice, static_cast<std::uint16_t>(id));
  REQUIRE(d != nullptr);
  return core::param_denorm(*d, ladder_norm(col));
}

float val(Hands& t, unsigned id) { return t.param(kVoice, id); }

// Hold `f`, tap the pad, let go: the assigning gesture.
void assign(Hands& t, Button f, unsigned pad) {
  t.press(f);
  t.wait(30);
  t.pad(pad, 10);
  t.release(f);
  t.wait(30);
}

void home_filt(Hands& t) {
  t.pad(kFiltPad);
  t.wait(40);
}

}  // namespace

TEST_CASE("ladder rows: assign, assign again (back to the default), no lane, reset, follow") {
  Wiring w;
  Controls c;
  c.init(w);
  const Entry decay{Item::Voice, AcidVoice::Decay, nullptr};
  const Entry cutoff{Item::Voice, AcidVoice::Cutoff, nullptr};
  const Entry tempo{Item::Tempo, 0, nullptr};
  CHECK(w.ladder[kLadderHome3].id == AcidVoice::Cutoff);
  CHECK(w.ladder[kLadderHome4].id == AcidVoice::Reso);
  CHECK(w.ladder[kLadderPerform].target == kLaneNone);

  CHECK(ladder_assign(w, c, kLadderHome3, decay) == LadderAssign::Held);
  CHECK(w.ladder[kLadderHome3].id == AcidVoice::Decay);
  CHECK(ladder_assign(w, c, kLadderHome3, decay) == LadderAssign::Default);
  CHECK(w.ladder[kLadderHome3].id == AcidVoice::Cutoff);
  CHECK(ladder_assign(w, c, kLadderHome3, cutoff) == LadderAssign::Default);  // the default is what it holds: still the default
  CHECK(w.ladder[kLadderHome3].id == AcidVoice::Cutoff);
  CHECK(ladder_assign(w, c, kLadderHome4, tempo) == LadderAssign::NoLane);
  CHECK(w.ladder[kLadderHome4].id == AcidVoice::Reso);

  // PERFORM's row follows by default; a held knob is left by the same gesture.
  CHECK(ladder_assign(w, c, kLadderPerform, decay) == LadderAssign::Held);
  CHECK(ladder_entry(c, w, kLadderPerform, cutoff).id == AcidVoice::Decay);
  CHECK(ladder_assign(w, c, kLadderPerform, decay) == LadderAssign::Default);
  CHECK(ladder_entry(c, w, kLadderPerform, cutoff).id == AcidVoice::Cutoff);  // follows the latched knob again

  // Shift + FILT: both HOME rows, and only them.
  (void)ladder_assign(w, c, kLadderHome3, decay);
  (void)ladder_assign(w, c, kLadderHome4, decay);
  (void)ladder_assign(w, c, kLadderPerform, decay);
  ladder_reset_home(w);
  CHECK(w.ladder[kLadderHome3].id == AcidVoice::Cutoff);
  CHECK(w.ladder[kLadderHome4].id == AcidVoice::Reso);
  CHECK(w.ladder[kLadderPerform].id == AcidVoice::Decay);
}

TEST_CASE("ladder rows: the strip names both HOME rows") {
  PadsLineState s;
  s.filt = true;
  char out[200];
  pads_line_text(s, out, sizeof out);
  CHECK(std::strstr(out, "ROW 3 CUTOFF . ROW 4 RESO") != nullptr);
  s.row3 = "DECAY";
  s.row4 = "LATCHED";
  pads_line_text(s, out, sizeof out);
  CHECK(std::strstr(out, "ROW 3 DECAY . ROW 4 LATCHED") != nullptr);
}

TEST_CASE("HOME: F key held + a tap on a ladder row assigns and moves nothing; again, back to CUTOFF; Shift + FILT resets both rows") {
  Hands t;
  home_filt(t);
  const float cut0 = val(t, AcidVoice::Cutoff);
  const float decay0 = val(t, AcidVoice::Decay);
  const float reso0 = val(t, AcidVoice::Reso);
  assign(t, Button::F4, row_pad(2, 9));  // DECAY on row 3
  CHECK(val(t, AcidVoice::Decay) == decay0);  // the tap that assigns does not move the value
  CHECK(val(t, AcidVoice::Cutoff) == cut0);
  CHECK(t.led(kFiltPad) == app::kLedOn);  // FILT stayed on
  t.pad(row_pad(2, 2), 10);
  t.wait(100);
  CHECK(val(t, AcidVoice::Decay) == doctest::Approx(goal(t, AcidVoice::Decay, 2)).epsilon(0.001));
  CHECK(val(t, AcidVoice::Cutoff) == cut0);
  // The LEDs draw the row's new knob.
  for (unsigned col = 0; col < 12; ++col) {
    CHECK(t.led(row_pad(2, col)) == (col < 2 ? app::kLedMid : (col == 2 ? app::kLedOn : app::kLedOff)));
  }
  // Again with the knob the row holds: CUTOFF is back.
  assign(t, Button::F4, row_pad(2, 9));
  t.pad(row_pad(2, 6), 10);
  t.wait(100);
  CHECK(val(t, AcidVoice::Cutoff) == doctest::Approx(goal(t, AcidVoice::Cutoff, 6)).epsilon(0.001));
  CHECK(val(t, AcidVoice::Decay) == doctest::Approx(goal(t, AcidVoice::Decay, 2)).epsilon(0.001));  // DECAY stayed where it was set

  // Row 4 takes ACCENT; Shift + FILT puts both rows back (FILT is not toggled).
  assign(t, Button::F5, row_pad(3, 4));
  assign(t, Button::F3, row_pad(2, 4));
  t.pad(row_pad(3, 11), 10);
  t.wait(100);
  CHECK(val(t, AcidVoice::Reso) == reso0);  // row 4 is the accent now
  t.press(Button::Shift);
  t.pad(kFiltPad, 10);
  t.release(Button::Shift);
  t.wait(60);
  CHECK(t.led(kFiltPad) == app::kLedOn);
  t.pad(row_pad(3, 1), 10);
  t.pad(row_pad(2, 10), 10);
  t.wait(100);
  CHECK(val(t, AcidVoice::Reso) == doctest::Approx(goal(t, AcidVoice::Reso, 1)).epsilon(0.001));
  CHECK(val(t, AcidVoice::Cutoff) == doctest::Approx(goal(t, AcidVoice::Cutoff, 10)).epsilon(0.001));
}

TEST_CASE("HOME: the rows blink while an F key is held, and a row's knob survives a page change") {
  Hands t;
  home_filt(t);
  t.pad(row_pad(2, 0), 10);  // the cutoff at its minimum: the last pad of row 3 is dark
  t.wait(100);
  CHECK(t.led(row_pad(2, 11)) == app::kLedOff);
  t.press(Button::F2);
  bool lit = false;
  bool dark = false;
  for (unsigned i = 0; i < 40; ++i) {
    t.wait(20);
    (t.led(row_pad(2, 11)) == app::kLedOn ? lit : dark) = true;
  }
  CHECK(lit);
  CHECK(dark);
  t.release(Button::F2);
  t.wait(60);
  CHECK(t.led(row_pad(2, 11)) == app::kLedOff);

  assign(t, Button::F4, row_pad(2, 5));  // DECAY on row 3
  t.to_perform();
  t.wait(60);
  t.to_home();
  t.wait(60);
  if (t.led(kFiltPad) != app::kLedOn) {
    home_filt(t);
  }
  const float cut = val(t, AcidVoice::Cutoff);
  t.pad(row_pad(2, 11), 10);
  t.wait(100);
  CHECK(val(t, AcidVoice::Decay) == doctest::Approx(goal(t, AcidVoice::Decay, 11)).epsilon(0.001));
  CHECK(val(t, AcidVoice::Cutoff) == cut);
}

TEST_CASE("HOME: a row can hold a switch (the wave): a tap sets it at once, with no slew") {
  Hands t;
  home_filt(t);
  assign(t, Button::F6, row_pad(2, 0));
  t.pad(row_pad(2, 1), 10);  // a list has a pad a value: SAW, SQR, then dark pads
  t.wait(30);
  CHECK(val(t, AcidVoice::Wave) == doctest::Approx(1.0f));
  CHECK(app_sequencer().slewing() == 0);
  t.pad(row_pad(2, 11), 10);  // a dark pad does nothing
  t.wait(30);
  CHECK(val(t, AcidVoice::Wave) == doctest::Approx(1.0f));
  t.pad(row_pad(2, 0), 10);
  t.wait(30);
  CHECK(val(t, AcidVoice::Wave) == doctest::Approx(0.0f));
  // The LEDs: the chosen value full, the other value dim, dark after the last.
  CHECK(t.led(row_pad(2, 0)) == app::kLedOn);
  CHECK(t.led(row_pad(2, 1)) == app::kLedDim);
  CHECK(t.led(row_pad(2, 2)) == app::kLedOff);
  CHECK(t.led(row_pad(2, 11)) == app::kLedOff);
}

TEST_CASE("HOME: REC while the pattern plays writes a tap of a held knob's row as motion on that knob's lane") {
  Hands t;
  home_filt(t);
  assign(t, Button::F4, row_pad(2, 0));  // DECAY on row 3
  t.tap(Button::Play);
  t.wait(300);
  t.tap(Button::Rec);
  t.wait(100);
  const int before = seq::find_lane(t.pat(), kLaneVoice, AcidVoice::Decay);
  t.pad(row_pad(2, 8), 10);
  t.wait(300);
  const int lane = seq::find_lane(t.pat(), kLaneVoice, AcidVoice::Decay);
  REQUIRE(lane >= 0);
  (void)before;
  CHECK(seq::lane_has_motion(t.pat(), static_cast<unsigned>(lane)));
  const int cut_lane = seq::find_lane(t.pat(), kLaneVoice, AcidVoice::Cutoff);
  if (cut_lane >= 0) {
    CHECK_FALSE(seq::lane_has_motion(t.pat(), static_cast<unsigned>(cut_lane)));  // the row drove DECAY, not CUTOFF
  }
  t.tap(Button::Play);
}

TEST_CASE("PERFORM row 3: follows the latched knob; an F key held + a tap holds another knob, and again follows again") {
  Hands t;
  t.to_perform();
  t.wait(60);
  const unsigned r3 = 2;
  // Follow: F4 latches DECAY, the row drives it.
  t.tap(Button::F4);
  t.pad(row_pad(r3, 3), 10);
  t.wait(100);
  CHECK(val(t, AcidVoice::Decay) == doctest::Approx(goal(t, AcidVoice::Decay, 3)).epsilon(0.001));
  const float decay = val(t, AcidVoice::Decay);
  const float accent0 = val(t, AcidVoice::Accent);
  // Hold F5 (ACCENT) and tap a pad: the row holds ACCENT, the value does not move, and neither does DECAY.
  assign(t, Button::F5, row_pad(r3, 9));
  CHECK(val(t, AcidVoice::Accent) == accent0);
  CHECK(val(t, AcidVoice::Decay) == decay);
  // F4 is latched again (the latch is F5's after the assign: tap F4), the row still drives ACCENT.
  t.tap(Button::F4);
  t.pad(row_pad(r3, 8), 10);
  t.wait(100);
  CHECK(val(t, AcidVoice::Accent) == doctest::Approx(goal(t, AcidVoice::Accent, 8)).epsilon(0.001));
  CHECK(val(t, AcidVoice::Decay) == decay);
  // Again with ACCENT held: the row follows the latch (DECAY).
  assign(t, Button::F5, row_pad(r3, 9));
  t.tap(Button::F4);
  t.pad(row_pad(r3, 1), 10);
  t.wait(100);
  CHECK(val(t, AcidVoice::Decay) == doctest::Approx(goal(t, AcidVoice::Decay, 1)).epsilon(0.001));
  CHECK(val(t, AcidVoice::Accent) == doctest::Approx(goal(t, AcidVoice::Accent, 8)).epsilon(0.001));
}

TEST_CASE("PERFORM row 3: a held knob survives leaving PERFORM; a switch is set at once; the row blinks while an F key is held") {
  Hands t;
  t.to_perform();
  t.wait(60);
  const unsigned r3 = 2;
  assign(t, Button::F6, row_pad(r3, 0));  // the wave
  t.to_home();
  t.wait(60);
  t.to_perform();
  t.wait(60);
  t.tap(Button::F1);  // the latch is CUTOFF: the row ignores it
  t.pad(row_pad(r3, 1), 10);
  t.wait(30);
  CHECK(val(t, AcidVoice::Wave) == doctest::Approx(1.0f));
  CHECK(app_sequencer().slewing() == 0);
  t.press(Button::F2);
  bool lit = false;
  bool dark = false;
  for (unsigned i = 0; i < 40; ++i) {
    t.wait(20);
    (t.led(row_pad(r3, 0)) == app::kLedOn ? lit : dark) = true;
  }
  t.release(Button::F2);
  CHECK(lit);
  CHECK(dark);
}

TEST_CASE("PERFORM row 3: REC while the pattern plays writes the jump as motion, following or holding") {
  Hands t;
  t.to_perform();
  t.wait(60);
  const unsigned r3 = 2;
  t.tap(Button::Play);
  t.wait(300);
  t.tap(Button::Rec);
  t.wait(100);
  t.tap(Button::F2);  // follows RESO
  t.pad(row_pad(r3, 7), 10);
  t.wait(300);
  const int reso = seq::find_lane(t.pat(), kLaneVoice, AcidVoice::Reso);
  REQUIRE(reso >= 0);
  CHECK(seq::lane_has_motion(t.pat(), static_cast<unsigned>(reso)));
  const int decay_before = seq::find_lane(t.pat(), kLaneVoice, AcidVoice::Decay);
  if (decay_before >= 0) {
    CHECK_FALSE(seq::lane_has_motion(t.pat(), static_cast<unsigned>(decay_before)));
  }
  assign(t, Button::F4, row_pad(r3, 0));  // holds DECAY
  t.tap(Button::F2);
  t.pad(row_pad(r3, 10), 10);
  t.wait(300);
  const int decay = seq::find_lane(t.pat(), kLaneVoice, AcidVoice::Decay);
  REQUIRE(decay >= 0);
  CHECK(seq::lane_has_motion(t.pat(), static_cast<unsigned>(decay)));
  t.tap(Button::Play);
}

TEST_CASE("ladder rows: a list or a short whole-number knob has a pad a value, a continuous knob twelve steps of its curve") {
  Hands t;
  const auto desc = [&](unsigned id) {
    const core::ParamDesc* d = t.h->engine().param_desc(kVoice, static_cast<std::uint16_t>(id));
    REQUIRE(d != nullptr);
    return d;
  };
  // The wave: an Enum of two values.
  const core::ParamDesc& wave = *desc(AcidVoice::Wave);
  CHECK(ladder_is_list(wave));
  CHECK(ladder_places(wave) == 2u);
  CHECK(ladder_value(wave, 0) == doctest::Approx(0.0f));
  CHECK(ladder_value(wave, 1) == doctest::Approx(1.0f));
  CHECK(ladder_pos(wave, 0.0f) == 0u);
  CHECK(ladder_pos(wave, 1.0f) == 1u);
  // Sub Osc: OFF, LOW, MID, HIGH, a whole-number knob of four values.
  const core::ParamDesc& sub = *desc(AcidVoice::SubOsc);
  CHECK(ladder_places(sub) == 4u);
  for (unsigned c = 0; c < 4; ++c) {
    CHECK(ladder_value(sub, c) == doctest::Approx(static_cast<float>(c)));
    CHECK(ladder_pos(sub, static_cast<float>(c)) == c);
  }
  // Cutoff: a continuous knob keeps the twelve steps of its curve, whatever the count.
  const core::ParamDesc& cut = *desc(AcidVoice::Cutoff);
  CHECK_FALSE(ladder_is_list(cut));
  CHECK(ladder_places(cut) == kLadderSteps);
  for (unsigned c = 0; c < kLadderSteps; ++c) {
    CHECK(ladder_value(cut, c) == doctest::Approx(core::param_denorm(cut, ladder_norm(c))));
    CHECK(ladder_pos(cut, ladder_value(cut, c)) == c);
  }
}

TEST_CASE("ladder rows: the LEDs of a list, of a bar, of a blink and of a row that holds nothing") {
  Hands t;
  const core::ParamDesc* wave = t.h->engine().param_desc(kVoice, AcidVoice::Wave);
  const core::ParamDesc* cut = t.h->engine().param_desc(kVoice, AcidVoice::Cutoff);
  REQUIRE(wave != nullptr);
  REQUIRE(cut != nullptr);
  std::uint8_t lv[kLadderSteps];
  ladder_levels(wave, 1.0f, false, lv);  // SQR: the first pad dim, the second full, the rest dark
  CHECK(lv[0] == app::kLedDim);
  CHECK(lv[1] == app::kLedOn);
  CHECK(lv[2] == app::kLedOff);
  CHECK(lv[kLadderSteps - 1u] == app::kLedOff);
  ladder_levels(cut, core::param_denorm(*cut, ladder_norm(5)), false, lv);  // a bar up to pad 5
  CHECK(lv[0] == app::kLedMid);
  CHECK(lv[4] == app::kLedMid);
  CHECK(lv[5] == app::kLedOn);
  CHECK(lv[6] == app::kLedOff);
  ladder_levels(wave, 0.0f, true, lv);  // the lit half of the blink: every pad full, a list's dark ones too
  for (unsigned c = 0; c < kLadderSteps; ++c) {
    CHECK(lv[c] == app::kLedOn);
  }
  ladder_levels(nullptr, 0.0f, true, lv);  // a blink needs no knob
  CHECK(lv[kLadderSteps - 1u] == app::kLedOn);
  ladder_levels(nullptr, 0.0f, false, lv);  // a row that holds nothing is dark
  for (unsigned c = 0; c < kLadderSteps; ++c) {
    CHECK(lv[c] == app::kLedOff);
  }
}

TEST_CASE("ladder rows: the two KNOBS rows default to ENV MOD and DECAY") {
  CHECK(kLadderRows == 5u);
  Wiring w;
  CHECK(w.ladder[kLadderKnobs1].target == kLaneVoice);
  CHECK(w.ladder[kLadderKnobs1].id == AcidVoice::EnvMod);
  CHECK(w.ladder[kLadderKnobs2].target == kLaneVoice);
  CHECK(w.ladder[kLadderKnobs2].id == AcidVoice::Decay);
  CHECK(w.ladder[kLadderHome3].id == AcidVoice::Cutoff);
  CHECK(w.ladder[kLadderHome4].id == AcidVoice::Reso);
  CHECK(w.ladder[kLadderPerform].target == kLaneNone);
}
