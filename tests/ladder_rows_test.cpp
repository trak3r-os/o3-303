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
  if (id == AcidVoice::Cutoff) {  // F1: the Cutoff ladder covers the live range
    const KnobSpan span{AcidVoice::kCutoffLiveLoHz, AcidVoice::kCutoffLiveHiHz};
    return ladder_value(*d, col, &span);
  }
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
  CHECK(w.ladder[kLadderRetired].target == kLaneNone);

  CHECK(ladder_assign(w, c, kLadderHome3, decay) == LadderAssign::Held);
  CHECK(w.ladder[kLadderHome3].id == AcidVoice::Decay);
  CHECK(ladder_assign(w, c, kLadderHome3, decay) == LadderAssign::Default);
  CHECK(w.ladder[kLadderHome3].id == AcidVoice::Cutoff);
  CHECK(ladder_assign(w, c, kLadderHome3, cutoff) == LadderAssign::Default);  // the default is what it holds: still the default
  CHECK(w.ladder[kLadderHome3].id == AcidVoice::Cutoff);
  CHECK(ladder_assign(w, c, kLadderHome4, tempo) == LadderAssign::NoLane);
  CHECK(w.ladder[kLadderHome4].id == AcidVoice::Reso);

  // KN1: PERFORM's row 3 is HOME's row 3, so no row follows any more; the retired slot holds nothing and takes nothing.
  CHECK(ladder_assign(w, c, kLadderRetired, decay) == LadderAssign::NoLane);
  CHECK(ladder_entry(c, w, kLadderRetired).item == Item::Blank);
  CHECK(ladder_entry(c, w, kLadderHome3).id == AcidVoice::Cutoff);

  // Shift + FILT: both HOME rows, and only them.
  (void)ladder_assign(w, c, kLadderHome3, decay);
  (void)ladder_assign(w, c, kLadderHome4, decay);
  (void)ladder_assign(w, c, kLadderKnobs1, cutoff);
  ladder_reset_home(w);
  CHECK(w.ladder[kLadderHome3].id == AcidVoice::Cutoff);
  CHECK(w.ladder[kLadderHome4].id == AcidVoice::Reso);
  CHECK(w.ladder[kLadderKnobs1].id == AcidVoice::Cutoff);
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

TEST_CASE("PERFORM row 3 (KN1): CUTOFF whatever F key is latched; an F key held + a tap holds another knob, and again CUTOFF") {
  Hands t;
  t.to_perform();
  t.wait(60);
  const unsigned r3 = 2;
  // A latched F4 (DECAY) no longer steers the row: it drives CUTOFF, as HOME's row 3 does.
  t.tap(Button::F4);
  const float decay0 = val(t, AcidVoice::Decay);
  t.pad(row_pad(r3, 3), 10);
  t.wait(100);
  CHECK(val(t, AcidVoice::Cutoff) == doctest::Approx(goal(t, AcidVoice::Cutoff, 3)).epsilon(0.001));
  CHECK(val(t, AcidVoice::Decay) == decay0);
  const float cutoff = val(t, AcidVoice::Cutoff);
  const float accent0 = val(t, AcidVoice::Accent);
  // Hold F5 (ACCENT) and tap a pad: the row holds ACCENT, nothing moves.
  assign(t, Button::F5, row_pad(r3, 9));
  CHECK(val(t, AcidVoice::Accent) == accent0);
  CHECK(val(t, AcidVoice::Cutoff) == cutoff);
  t.tap(Button::F4);
  t.pad(row_pad(r3, 8), 10);
  t.wait(100);
  CHECK(val(t, AcidVoice::Accent) == doctest::Approx(goal(t, AcidVoice::Accent, 8)).epsilon(0.001));
  CHECK(val(t, AcidVoice::Decay) == decay0);
  // Again with ACCENT held: back to CUTOFF (not to the latch).
  assign(t, Button::F5, row_pad(r3, 9));
  t.tap(Button::F4);
  t.pad(row_pad(r3, 1), 10);
  t.wait(100);
  CHECK(val(t, AcidVoice::Cutoff) == doctest::Approx(goal(t, AcidVoice::Cutoff, 1)).epsilon(0.001));
  CHECK(val(t, AcidVoice::Decay) == decay0);
}

TEST_CASE("PERFORM row 3 (KN1) is HOME's row 3: one assignment shows on both screens") {
  Hands t;
  t.to_perform();
  t.wait(60);
  assign(t, Button::F4, row_pad(2, 0));  // DECAY on PERFORM's row 3
  t.to_home();
  t.wait(60);
  home_filt(t);
  const float decay0 = val(t, AcidVoice::Decay);
  const float cut0 = val(t, AcidVoice::Cutoff);
  t.pad(row_pad(2, 11), 10);  // HOME's row 3 now drives DECAY too
  t.wait(100);
  CHECK(val(t, AcidVoice::Decay) != decay0);
  CHECK(val(t, AcidVoice::Cutoff) == cut0);
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

TEST_CASE("PERFORM row 3: REC while the pattern plays writes the jump as motion, CUTOFF or the knob held") {
  Hands t;
  t.to_perform();
  t.wait(60);
  const unsigned r3 = 2;
  t.tap(Button::Play);
  t.wait(300);
  t.tap(Button::Rec);
  t.wait(100);
  t.tap(Button::F2);  // the latch (RESO) is not the row's knob: CUTOFF is
  t.pad(row_pad(r3, 7), 10);
  t.wait(300);
  const int cutoff = seq::find_lane(t.pat(), kLaneVoice, AcidVoice::Cutoff);
  REQUIRE(cutoff >= 0);
  CHECK(seq::lane_has_motion(t.pat(), static_cast<unsigned>(cutoff)));
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

TEST_CASE("ladder rows, F1: with a span the twelve pads of a knob cover the live range, ends included") {
  Hands t;
  const core::ParamDesc* cut = t.h->engine().param_desc(kVoice, AcidVoice::Cutoff);
  REQUIRE(cut != nullptr);
  const KnobSpan span{AcidVoice::kCutoffLiveLoHz, AcidVoice::kCutoffLiveHiHz};
  CHECK(ladder_value(*cut, 0, &span) == doctest::Approx(200.0f).epsilon(0.001));
  CHECK(ladder_value(*cut, kLadderSteps - 1u, &span) == doctest::Approx(2394.0f).epsilon(0.001));
  float prev = 0.0f;
  for (unsigned c = 0; c < kLadderSteps; ++c) {
    const float v = ladder_value(*cut, c, &span);
    CHECK(v > prev);
    prev = v;
    CHECK(ladder_pos(*cut, v, &span) == c);
  }
  // Without a span (FISH, every other knob) the descriptor's whole travel, as before.
  CHECK(ladder_value(*cut, 0, nullptr) == doctest::Approx(core::param_denorm(*cut, ladder_norm(0))));
  // The bar: a value at the top of the span fills the row.
  std::uint8_t lv[kLadderSteps];
  ladder_levels(cut, 2394.0f, false, lv, &span);
  CHECK(lv[kLadderSteps - 1u] == app::kLedOn);
  CHECK(lv[0] == app::kLedMid);
}

TEST_CASE("ladder rows, F1: a tap on the FILT row's first and last pad lands on 200 and 2394 Hz on RANGE 303, and on the descriptor's ends on FISH") {
  Hands t;
  home_filt(t);
  t.pad(row_pad(2, 0), 10);
  t.wait(100);
  CHECK(val(t, AcidVoice::Cutoff) == doctest::Approx(200.0f).epsilon(0.001));
  t.pad(row_pad(2, 11), 10);
  t.wait(100);
  CHECK(val(t, AcidVoice::Cutoff) == doctest::Approx(2394.0f).epsilon(0.001));
  t.h->engine().set_param(kVoice, AcidVoice::Range, 1.0f);  // FISH
  t.wait(40);
  t.pad(row_pad(2, 11), 10);
  t.wait(100);
  const core::ParamDesc* dc = t.h->engine().param_desc(kVoice, AcidVoice::Cutoff);
  REQUIRE(dc != nullptr);
  CHECK(val(t, AcidVoice::Cutoff) == doctest::Approx(dc->max).epsilon(0.001));
  CHECK(val(t, AcidVoice::Cutoff) > 2394.0f);
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
  CHECK(w.ladder[kLadderRetired].target == kLaneNone);
}

TEST_CASE("KNOBS sets (KN3): four factory-filled sets, the live rows follow the set in use, an assignment writes through to its set, FACTORY puts a set back") {
  Wiring w;
  Controls c;
  c.init(w);
  CHECK(w.knob_set == 0u);
  for (unsigned s = 0; s < kKnobSets; ++s) {
    CHECK(knob_set_is_factory(w, s));
    CHECK(std::strlen(knob_set_name(s)) > 0u);
  }
  CHECK(std::strcmp(knob_set_name(0), "FILTER") == 0);
  CHECK(std::strcmp(knob_set_name(3), "FX") == 0);
  CHECK(std::strlen(knob_set_name(kKnobSets)) == 0u);
  // Set 2 (MODDED): ACC DECAY, SLIDE, OVERDRIVE, FILTER FM.
  knob_set_use(w, 1);
  CHECK(w.knob_set == 1u);
  CHECK(w.ladder[kLadderKnobs1].id == AcidVoice::AccDecay);
  CHECK(w.ladder[kLadderKnobs2].id == AcidVoice::Slide);
  CHECK(w.ladder[kLadderHome3].id == AcidVoice::Overdrive);
  CHECK(w.ladder[kLadderHome4].id == AcidVoice::FilterFm);
  // An assignment on the set in use is the set's.
  const Entry cutoff{Item::Voice, AcidVoice::Cutoff, nullptr};
  CHECK(ladder_assign(w, c, kLadderKnobs2, cutoff) == LadderAssign::Held);
  CHECK(w.knob_sets.row[1][1].id == AcidVoice::Cutoff);
  CHECK(!knob_set_is_factory(w, 1));
  CHECK(knob_set_is_factory(w, 0));
  knob_set_use(w, 0);
  CHECK(w.ladder[kLadderKnobs2].id == AcidVoice::Decay);  // set 1 is untouched
  knob_set_use(w, 1);
  CHECK(w.ladder[kLadderKnobs2].id == AcidVoice::Cutoff);
  // Shift + FILT resets rows 3 and 4 to the set in use's factory knobs.
  CHECK(ladder_assign(w, c, kLadderHome3, cutoff) == LadderAssign::Held);
  ladder_reset_home(w);
  CHECK(w.ladder[kLadderHome3].id == AcidVoice::Overdrive);
  CHECK(w.knob_sets.row[1][2].id == AcidVoice::Overdrive);
  CHECK(w.ladder[kLadderKnobs2].id == AcidVoice::Cutoff);  // row 2 is not a HOME row
  // FACTORY of the set in use: the live rows follow; of another set: only its store.
  knob_set_factory(w, 1);
  CHECK(w.ladder[kLadderKnobs2].id == AcidVoice::Slide);
  CHECK(knob_set_is_factory(w, 1));
  CHECK(ladder_assign(w, c, kLadderKnobs1, cutoff) == LadderAssign::Held);
  knob_set_factory(w, 2);
  CHECK(w.ladder[kLadderKnobs1].id == AcidVoice::Cutoff);
  // Out of range: clamped to set 1, FACTORY a no-op.
  knob_set_use(w, 9);
  CHECK(w.knob_set == 0u);
  knob_set_factory(w, 9);
  CHECK(knob_set_is_factory(w, 0));
  CHECK(knob_row_pos(kLadderKnobs1) == 0u);
  CHECK(knob_row_pos(kLadderHome4) == 3u);
  CHECK(knob_row_pos(kLadderRetired) == kKnobsRows);
}

TEST_CASE("KNOBS walk (KN3): the family comes out with the knob, and a family jump lands on the first knob of the next or previous family") {
  Wiring w;
  Controls c;
  c.init(w);
  const char* fam = nullptr;
  const Entry first = ladder_walk(w, c, kLadderKnobs1, 1, &fam);
  REQUIRE(fam != nullptr);
  CHECK(std::strlen(fam) > 0u);
  CHECK(w.knob_sets.row[0][0].id == w.ladder[kLadderKnobs1].id);  // written through
  (void)first;
  const char* fam0 = fam;
  unsigned jumps = 0;
  do {
    (void)ladder_walk_family(w, c, kLadderKnobs1, 1, &fam);
    ++jumps;
  } while (std::strcmp(fam, fam0) != 0 && jumps < 12);
  CHECK(jumps > 1u);   // a different family each jump
  CHECK(jumps < 12u);  // and the walk wraps back
  const char* back = nullptr;
  (void)ladder_walk_family(w, c, kLadderKnobs1, -1, &back);
  REQUIRE(back != nullptr);
  CHECK(std::strcmp(back, fam0) != 0);
  (void)ladder_walk(w, c, kLadderRetired, 1, &fam);  // the retired slot: nothing is stored for it
  CHECK(w.knob_set == 0u);
}
