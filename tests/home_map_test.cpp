// SPDX-License-Identifier: GPL-3.0-or-later
// The home map (docs/UX-303.md 6.2, slice H1): the PADS line's text for each state of the table (a pure function), the right block's pads and LEDs
// (ACC SLIDE TIE REST / OCT- OCT+ TRNSP FILT), TRNSP and FILT as toggles, the keyboard that plays the voice by default, Rec while the pattern plays, and
// the gestures that moved with the block (the Pattern chord, Shift + Insert, Shift + Delete, Shift + Up / Down).
#include <doctest/doctest.h>

#include "hands.h"

#include "../app/src/home_map.h"

#include <o3_303/app_access.h>
#include <o3_303/seq/ops.h>
#include <o3_303/step_model.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

using namespace hands;
using namespace o3_303;

namespace {

const unsigned kAccPad = role_pad(PadRole::Acc);
const unsigned kSlidePad = role_pad(PadRole::Slide);
const unsigned kTiePad = role_pad(PadRole::Tie);
const unsigned kRestPad = role_pad(PadRole::Rest);
const unsigned kOctDownPad = role_pad(PadRole::OctDown);
const unsigned kOctUpPad = role_pad(PadRole::OctUp);
const unsigned kTrnspPad = role_pad(PadRole::Trnsp);
const unsigned kFiltPad = role_pad(PadRole::Filt);

std::string line_of(const PadsLineState& s) {
  char out[128];
  pads_line_text(s, out, sizeof out);
  return out;
}

}  // namespace

TEST_CASE("the PADS line says what the four rows are in each state of the table") {
  const std::string prefix = "1-16 STEPS . ACC SLIDE TIE REST . OCT- OCT+ TRNSP FILT . ";
  PadsLineState s;
  CHECK(line_of(s) == prefix + "KEYS C2-B3 PLAY THE VOICE");  // plain: the spec's exact text
  CHECK(line_of(s) == "1-16 STEPS . ACC SLIDE TIE REST . OCT- OCT+ TRNSP FILT . KEYS C2-B3 PLAY THE VOICE");
  s.octave = 1;
  CHECK(line_of(s) == prefix + "KEYS C3-B4 PLAY THE VOICE");  // the range follows the octave
  s.octave = -3;
  CHECK(line_of(s) == prefix + "KEYS C-1-B0 PLAY THE VOICE");
  s.octave = 0;
  s.trnsp = true;
  CHECK(line_of(s) == prefix + "KEYS TRANSPOSE THE LINE");
  s.trnsp = false;
  s.rec = true;
  CHECK(line_of(s) == prefix + "KEYS WRITE AND ADVANCE");  // Rec, stopped
  s.playing = true;
  CHECK(line_of(s) == prefix + "KEYS RECORD AT THE PLAYHEAD");  // Rec, playing
  s.trnsp = true;
  CHECK(line_of(s) == prefix + "KEYS RECORD AT THE PLAYHEAD");  // Rec beats TRNSP: the keys write
  s.filt = true;
  CHECK(line_of(s) == prefix + "ROW 3 CUTOFF . ROW 4 RESO");  // FILT: the rows are no keys at all
  s.rec = false;
  s.trnsp = false;
  CHECK(line_of(s) == prefix + "ROW 3 CUTOFF . ROW 4 RESO");

  PadsLineState held;
  held.step_held = 5;
  held.turn = "GATE";
  CHECK(line_of(held) == "KEY = NOTE OF STEP 5 . ACC SLIDE TIE REST TOGGLE IT . TURN = GATE");
  held.turn = "LOCK CUTOFF";
  CHECK(line_of(held) == "KEY = NOTE OF STEP 5 . ACC SLIDE TIE REST TOGGLE IT . TURN = LOCK CUTOFF");
  held.turn = "";  // the encoder's owner is not the step's: nothing is said about the turn
  CHECK(line_of(held) == "KEY = NOTE OF STEP 5 . ACC SLIDE TIE REST TOGGLE IT");
  held.filt = true;  // the rows are the ladders: the line says so
  CHECK(line_of(held) == prefix + "ROW 3 CUTOFF . ROW 4 RESO");

  PadsLineState attr;
  attr.attribute = PadRole::Acc;
  CHECK(line_of(attr) == "STEPS TOGGLE ACC");
  attr.attribute = PadRole::Slide;
  CHECK(line_of(attr) == "STEPS TOGGLE SLIDE");
  attr.attribute = PadRole::Tie;
  CHECK(line_of(attr) == "STEPS TOGGLE TIE");
  attr.attribute = PadRole::Rest;
  attr.step_held = 3;  // an attribute held outranks a held step
  CHECK(line_of(attr) == "STEPS TOGGLE REST");

  PadsLineState grid;
  grid.overlay = true;
  grid.attribute = PadRole::Acc;
  grid.rec = true;
  CHECK(line_of(grid) == "48 PATTERNS . TAP = SELECT (PLAYING: QUEUE) . PATTERN+DELETE CLEARS A LANE");  // Pattern held outranks the rest
}

TEST_CASE("the PADS line fits the strip in every state (it is cut at no state's length)") {
  PadsLineState s;
  char out[128];
  for (unsigned mask = 0; mask < 64; ++mask) {
    s.overlay = (mask & 1u) != 0u;
    s.rec = (mask & 2u) != 0u;
    s.playing = (mask & 4u) != 0u;
    s.trnsp = (mask & 8u) != 0u;
    s.filt = (mask & 16u) != 0u;
    s.step_held = (mask & 32u) != 0u ? 16u : 0u;
    s.turn = "LOCK ENV MOD";
    pads_line_text(s, out, sizeof out);
    CHECK(std::strlen(out) < 100u);  // 800 px of Micro text from x = 42
    CHECK(std::strlen(out) > 10u);
  }
}

TEST_CASE("the right block: ACC SLIDE TIE REST / OCT- OCT+ TRNSP FILT are pads 8..11 and 20..23") {
  CHECK(kAccPad == 8);
  CHECK(kSlidePad == 9);
  CHECK(kTiePad == 10);
  CHECK(kRestPad == 11);
  CHECK(kOctDownPad == 20);
  CHECK(kOctUpPad == 21);
  CHECK(kTrnspPad == 22);
  CHECK(kFiltPad == 23);
}

TEST_CASE("the block's LEDs: low, mid when the cursor's step has it, on while held; OCT low and on while held; TRNSP and FILT low, on when active") {
  Hands t;
  t.wait(60);
  // Step 1 of the factory line has no tie, no rest and no slide; the accent is checked below with the others.
  for (unsigned pad : {kSlidePad, kTiePad, kRestPad, kOctDownPad, kOctUpPad, kTrnspPad, kFiltPad}) {
    CHECK(t.led(pad) == app::kLedLow);
  }
  // Held: on.
  t.down(kAccPad);
  t.wait(60);
  CHECK(t.led(kAccPad) == app::kLedOn);
  t.up(kAccPad);
  t.down(kOctUpPad);
  t.wait(60);
  CHECK(t.led(kOctUpPad) == app::kLedOn);
  t.up(kOctUpPad);
  t.wait(60);
  CHECK(t.led(kOctUpPad) == app::kLedLow);  // the octave is shown on the status line, not on the pad
  // The cursor's step has the attribute: mid. Step 1 of the factory line has neither a slide nor a tie.
  t.pad(kSlidePad, 10);  // a tap toggles the slide on the cursor step
  t.wait(60);
  CHECK(t.led(kSlidePad) == app::kLedMid);
  t.pad(kSlidePad, 10);
  t.wait(60);
  CHECK(t.led(kSlidePad) == app::kLedLow);
}

TEST_CASE("TRNSP off is the default: playing, a key plays the voice and does not transpose the line; TRNSP on transposes it; off again ends it") {
  Hands t;
  seq::Sequencer& s = app_sequencer();
  t.tap(Button::Play);
  t.wait(200);
  t.pad(key_pad(43), 10);  // G2
  t.wait(40);
  CHECK(s.live_transpose() == 0);
  t.toggle_trnsp();
  t.pad(key_pad(43), 10);
  t.wait(40);
  CHECK(s.live_transpose() == 7);
  t.toggle_trnsp();  // off: the line is as written again, and the keys play the voice
  t.wait(40);
  CHECK(s.live_transpose() == 0);
  t.pad(key_pad(43), 10);
  t.wait(40);
  CHECK(s.live_transpose() == 0);
  t.tap(Button::Play);
}

TEST_CASE("stopped, TRNSP on or off: a key plays the voice (there is no line to transpose)") {
  Hands t;
  seq::Sequencer& s = app_sequencer();
  t.toggle_trnsp();
  t.h->record(true);
  t.pad(key_pad(48), 100);
  t.wait(200);
  CHECK(s.live_transpose() == 0);
  float peak = 0.0f;
  for (std::int16_t v : t.h->audio()) {
    peak = std::fmax(peak, std::fabs(static_cast<float>(v)));
  }
  CHECK(peak > 300.0f);
}

TEST_CASE("Rec while the pattern plays writes the key under the playhead whatever TRNSP says, and records the encoder as motion") {
  Hands t;
  t.select_pattern(12);
  REQUIRE(pattern_is_empty(t.pat()));
  t.toggle_trnsp();
  t.tap(Button::Rec);
  t.tap(Button::Play);
  t.wait(300);
  t.pad(key_pad(52), 10);
  CHECK_FALSE(pattern_has_motion(t.pat(), kMotionLanes));
  for (unsigned i = 0; i < 6; ++i) {  // the latched knob (CUTOFF) turned: written on its lane under the playhead
    t.turn(4);
    t.wait(100);
  }
  t.tap(Button::Play);
  unsigned written = 0;
  for (unsigned i = 0; i < 16; ++i) {
    written += ((t.pat().steps[i].flags & kStepRest) == 0 && t.pat().steps[i].note == 52) ? 1u : 0u;
  }
  CHECK(written == 1);
  CHECK(app_sequencer().live_transpose() == 0);  // it wrote: it did not transpose
  CHECK(pattern_has_motion(t.pat(), 0));
}

TEST_CASE("Rec toggled off while the pattern plays: the keys and the encoder write nothing more") {
  Hands t;
  t.select_pattern(12);
  t.tap(Button::Rec);
  t.tap(Button::Play);
  t.wait(300);
  t.turn(4);
  t.wait(200);
  t.tap(Button::Rec);  // off, mid-run
  t.wait(40);
  const Pattern kept = t.pat();
  for (unsigned i = 0; i < 6; ++i) {
    t.turn(4);
    t.wait(100);
  }
  t.pad(key_pad(52), 10);  // plays the voice, writes nothing
  t.wait(200);
  t.tap(Button::Play);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == kept.steps[i].note);
    CHECK(t.pat().steps[i].flags == kept.steps[i].flags);
  }
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    for (unsigned i = 0; i < kMaxSteps * kMotionPointsPerStep; ++i) {
      CHECK(t.pat().motion[lane][i] == kept.motion[lane][i]);
    }
  }
}

TEST_CASE("Rec playing: REST and TIE are punched in at the playhead's step, like a key") {
  Hands t;
  t.select_pattern(12);
  REQUIRE(pattern_is_empty(t.pat()));
  t.tap(Button::Rec);
  t.tap(Button::Play);
  t.wait(300);
  t.pad(key_pad(48), 10);
  t.wait(120);
  t.pad(kTiePad, 10);
  t.wait(240);
  t.pad(kRestPad, 10);
  t.tap(Button::Play);
  unsigned ties = 0;
  unsigned notes = 0;
  for (unsigned i = 0; i < 16; ++i) {
    ties += t.pat().steps[i].flags == kStepTie ? 1u : 0u;
    notes += ((t.pat().steps[i].flags & (kStepRest | kStepTie)) == 0 && t.pat().steps[i].note == 48) ? 1u : 0u;
  }
  CHECK(notes == 1);
  CHECK(ties == 1);  // a tie, written where the playhead was (the cursor did not advance)
  CHECK(t.pat().length == 16);
}

TEST_CASE("a latched WOBBLE does not turn HOME's key row into the rate picker: a bottom-row pad plays the voice") {
  Hands t;
  t.assign_perf(Button::Note, PerfAction::Wobble);
  t.shift_tap(Button::Note);  // WOBBLE latched
  t.wait(50);
  t.h->record(true);
  const std::size_t mark = t.h->audio().size();
  t.pad(key_pad(40), 200);  // bottom row
  t.wait(200);
  CHECK(app_sequencer().wobble().override_rate == seq::kWobbleRateNone);  // no rate was picked
  float peak = 0.0f;
  for (std::size_t i = mark; i < t.h->audio().size(); ++i) {
    peak = std::fmax(peak, std::fabs(static_cast<float>(t.h->audio()[i])));
  }
  CHECK(peak > 300.0f);
  CHECK(t.led(key_pad(36 + 1)) == app::kLedOff);  // and the LEDs are the keyboard's: naturals dim, sharps off
  CHECK(t.led(key_pad(36 + 2)) == app::kLedDim);
  t.shift_tap(Button::Note);
}

TEST_CASE("Pattern + Delete clears the lane of the latched knob, Pattern + Shift + Delete every lane; the notes stay and each is one undo") {
  Hands t;
  const auto lock = [&](unsigned step, int d) {
    t.down(step_pad_of(step));
    t.turn(d);
    t.up(step_pad_of(step));
  };
  t.tap(Button::F1);
  lock(3, 10);
  lock(6, -10);
  t.tap(Button::F2);
  lock(3, 10);
  REQUIRE(seq::lane_has_motion(t.pat(), 0));
  REQUIRE(seq::lane_has_motion(t.pat(), 1));
  const Pattern before = t.pat();
  bank().clear_history();
  t.tap(Button::F1);
  t.wait(40);
  t.press(Button::Pattern);
  t.tap(Button::Delete);  // a chord: no overlay, no tap
  t.release(Button::Pattern);
  t.wait(40);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  CHECK_FALSE(seq::lane_has_motion(t.pat(), 0));  // the cutoff's lane
  CHECK(seq::lane_has_motion(t.pat(), 1));        // the reso's is not
  CHECK(t.pat().lanes[0].target == before.lanes[0].target);  // the lane is free (no points), still pointed at its parameter
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == before.steps[i].note);
    CHECK(t.pat().steps[i].flags == before.steps[i].flags);
  }
  CHECK(bank().undo() == static_cast<int>(t.selected()));  // one undo
  CHECK(seq::lane_has_motion(t.pat(), 0));
  CHECK_FALSE(bank().can_undo());
  // Every lane.
  t.press(Button::Pattern);
  t.press(Button::Shift);
  t.tap(Button::Delete);
  t.release(Button::Shift);
  t.release(Button::Pattern);
  t.wait(40);
  CHECK_FALSE(pattern_has_motion(t.pat(), kMotionLanes));
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == before.steps[i].note);
    CHECK(t.pat().steps[i].flags == before.steps[i].flags);
  }
  CHECK(bank().undo() == static_cast<int>(t.selected()));
  CHECK(seq::lane_has_motion(t.pat(), 0));
  CHECK(seq::lane_has_motion(t.pat(), 1));
  CHECK_FALSE(bank().can_undo());
  // With the pattern not held, Delete is the step's, and Shift + Delete the backspace: no lane is touched.
  t.tap(Button::Delete);
  CHECK(seq::lane_has_motion(t.pat(), 0));
}

TEST_CASE("FILT: a toggle at the press, independent of TRNSP; the keys come back when it is off") {
  Hands t;
  t.down(kFiltPad);
  t.wait(40);
  CHECK(t.led(kFiltPad) == app::kLedOn);  // at the press
  t.up(kFiltPad);
  t.wait(40);
  CHECK(t.led(kFiltPad) == app::kLedOn);
  CHECK(t.led(kTrnspPad) == app::kLedLow);
  // The key pads are ladder pads now: a tap writes nothing even with Rec on.
  const Pattern before = t.pat();
  t.tap(Button::Rec);
  t.pad(key_pad(40), 10);
  CHECK(t.pat().steps[0].note == before.steps[0].note);
  t.tap(Button::Rec);
  t.pad(kFiltPad);
  t.wait(40);
  CHECK(t.led(kFiltPad) == app::kLedLow);
}

TEST_CASE("Pattern + Copy and Pattern + Insert are chords: the release of Pattern is no tap, so the view does not flip") {
  Hands t;
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  t.press(Button::Pattern);
  t.tap(Button::Insert);  // a chord, well under the 300 ms of a hold
  t.release(Button::Pattern);
  t.wait(40);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  CHECK(t.selected() == 1);  // the pattern was copied into the next slot and that one is selected
  CHECK_FALSE(pattern_is_empty(t.pat()));
  t.press(Button::Pattern);
  t.tap(Button::Copy);
  t.release(Button::Pattern);
  t.wait(40);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  // A plain tap of Pattern still flips to the roll and back.
  t.tap(Button::Pattern, 40);
  CHECK(std::strcmp(t.title(), "ROLL") == 0);
  t.tap(Button::Pattern, 40);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
}

TEST_CASE("Shift + Insert goes to step 1 without inserting; Shift + Delete is backspace and clears nothing else") {
  Hands t;
  const Pattern before = t.pat();
  t.pad(step_pad_of(6));
  t.shift_tap(Button::Insert);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == before.steps[i].note);  // nothing was inserted
    CHECK(t.pat().steps[i].flags == before.steps[i].flags);
  }
  t.tap(Button::Delete);  // the cursor is on step 1 now
  CHECK(t.pat().steps[0].flags == kStepRest);
  CHECK(t.pat().steps[6].flags == before.steps[6].flags);
  t.pad(step_pad_of(2));
  const Pattern pre = t.pat();
  t.shift_tap(Button::Delete);
  for (unsigned i = 2; i < 15; ++i) {
    CHECK(t.pat().steps[i].note == pre.steps[i + 1].note);
  }
  CHECK(t.pat().steps[0].flags == kStepRest);  // step 1 is still the rest Delete made: not a clear of the pattern
  CHECK(t.pat().length == 16);
}

TEST_CASE("Shift + Up / Down is the octave of the cursor step; the pattern's transpose is left alone") {
  Hands t;
  const unsigned note = t.pat().steps[0].note;
  t.shift_tap(Button::Up);
  CHECK(t.pat().steps[0].note == note + 12);
  t.shift_tap(Button::Down);
  CHECK(t.pat().steps[0].note == note);
  CHECK(t.pat().transpose == 0);
}

TEST_CASE("Rec stopped: TIE and REST write and advance; with Rec off they toggle the cursor step") {
  Hands t;
  t.select_pattern(12);
  t.tap(Button::Rec);
  t.pad(key_pad(48), 10);  // step 1
  t.pad(kRestPad, 10);     // step 2: a rest, the cursor moves on
  t.pad(kTiePad, 10);      // step 3: a tie
  CHECK(t.pat().steps[0].note == 48);
  CHECK(t.pat().steps[1].flags == kStepRest);
  CHECK(t.pat().steps[2].flags == kStepTie);
  t.tap(Button::Rec);
  t.pad(step_pad_of(4), 10);
  t.pad(kTiePad, 10);  // off: the tie toggles on the cursor step (5)
  CHECK((t.pat().steps[4].flags & kStepTie) != 0);
  t.pad(kTiePad, 10);
  CHECK((t.pat().steps[4].flags & kStepTie) == 0);
}
