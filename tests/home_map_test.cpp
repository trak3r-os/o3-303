// SPDX-License-Identifier: GPL-3.0-or-later
// The home map (docs/UX-303.md 6.2, slice H1): the PADS line's text for each state of the table (a pure function), the right block's pads and LEDs
// (ACC SLIDE TIE REST / OCT- OCT+ TRNSP FILT), TRNSP and FILT as toggles, the keyboard that plays the voice by default, Rec while the pattern plays, and
// the gestures that moved with the block (the Pattern chord, Shift + Insert, Shift + Delete, Shift + Up / Down); H2 took the Pattern + Insert / Delete bridges
// away (PATTERN SETUP has DUP>NEXT and CLEAR LOCKS).
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
  CHECK(line_of(grid) == "48 PATTERNS . TAP = SELECT (PLAYING: QUEUE) . COPY + PAD: COPY HERE . DELETE + PAD: CLEAR IT");  // Pattern held outranks the rest (F7)
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

TEST_CASE("TRNSP: a key held with Shift lifts the line only while it is held and gives back what it was (I6 a); a plain key stays") {
  Hands t;
  seq::Sequencer& s = app_sequencer();
  t.tap(Button::Play);
  t.wait(200);
  t.toggle_trnsp();
  t.pad(key_pad(43), 10);  // G2: +7, latched
  t.wait(40);
  REQUIRE(s.live_transpose() == 7);
  t.press(Button::Shift);
  t.down(key_pad(45));  // A2, +9, momentary
  t.release(Button::Shift);  // Shift may go first: the key's release is what ends it
  t.wait(40);
  CHECK(s.live_transpose() == 9);
  t.up(key_pad(45));
  t.wait(40);
  CHECK(s.live_transpose() == 7);  // back to the latched +7
  // Two keys held with Shift: the last one decides, and its release gives back the lift from before the first.
  t.press(Button::Shift);
  t.down(key_pad(40));  // +4
  t.down(key_pad(41));  // +5
  t.release(Button::Shift);
  CHECK(s.live_transpose() == 5);
  t.up(key_pad(40));  // the older key: nothing changes
  t.wait(20);
  CHECK(s.live_transpose() == 5);
  t.up(key_pad(41));
  t.wait(40);
  CHECK(s.live_transpose() == 7);
  // A plain key while a Shift key is down is a latched lift: the release gives nothing back.
  t.press(Button::Shift);
  t.down(key_pad(38));  // +2 momentary
  t.release(Button::Shift);
  t.pad(key_pad(47), 10);  // plain B2: +11
  t.wait(20);
  t.up(key_pad(38));
  t.wait(40);
  CHECK(s.live_transpose() == 11);
  // TRNSP off while held: the release leaves the line as written.
  t.press(Button::Shift);
  t.down(key_pad(42));
  t.release(Button::Shift);
  t.toggle_trnsp();
  t.up(key_pad(42));
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
  t.tap(Button::Fx);  // WOBBLE on (the Fx key flips it from every screen); it stays on across the change
  t.to_home();
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
  CHECK(t.led(key_pad(36 + 1)) == app::kLedOff);  // and the LEDs are the keyboard's: the default Penta Min lit (root full, scale mid), the rest off
  CHECK(t.led(key_pad(36 + 3)) == app::kLedMid);
  t.tap(Button::Fx);
  CHECK_FALSE(app_sequencer().wobble().on);
}

TEST_CASE("Pattern held + Delete, Shift + Delete, Insert and Copy change nothing: CLEAR LOCKS, DUP>NEXT and COPY PAT are PATTERN SETUP's (UX-303 D30)") {
  Hands t;
  const auto lock = [&](unsigned step, int d) {
    t.down(step_pad_of(step));
    t.turn(d);
    t.up(step_pad_of(step));
  };
  t.tap(Button::F1);
  lock(3, 10);
  lock(6, -10);
  REQUIRE(seq::lane_has_motion(t.pat(), 0));
  const Pattern before = t.pat();
  bank().clear_history();
  t.press(Button::Pattern);
  t.tap(Button::Delete);
  t.press(Button::Shift);
  t.tap(Button::Delete);
  t.release(Button::Shift);
  t.tap(Button::Insert);
  t.tap(Button::Copy);
  t.release(Button::Pattern);
  t.wait(40);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);  // the chord was no tap
  CHECK(t.selected() == 0);
  CHECK(seq::lane_has_motion(t.pat(), 0));
  CHECK_FALSE(bank().can_undo());
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == before.steps[i].note);
    CHECK(t.pat().steps[i].flags == before.steps[i].flags);
  }
  // With the pattern not held, Delete is the step's and Shift + Delete the backspace: no lane is touched.
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

TEST_CASE("a chord with Pattern is no tap, so the page does not flip; a plain tap flips HOME and PATTERN SETUP") {
  Hands t;
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  t.press(Button::Pattern);
  t.tap(Button::Insert);  // a chord, well under the 300 ms of a hold
  t.release(Button::Pattern);
  t.wait(40);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  CHECK(t.selected() == 0);  // nothing was copied into the next slot: DUP>NEXT is SETUP's
  CHECK(pattern_is_empty(bank().pattern(1)));
  t.press(Button::Pattern);
  t.tap(Button::Copy);
  t.release(Button::Pattern);
  t.wait(40);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  t.tap(Button::Pattern, 40);
  CHECK(std::strcmp(t.title(), "PATTERN SETUP") == 0);
  t.tap(Button::Pattern, 40);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
}

TEST_CASE("Pattern held + Copy held + a pad copies this pattern onto that pad, Delete held + a pad clears it: one undo each, the selection stays (F7)") {
  Hands t;
  REQUIRE(bank().edit(0, [](Pattern& p) {
    p.steps[3].note = 52;
    p.steps[3].flags = kStepAccent;
  }));
  REQUIRE(pattern_is_empty(bank().pattern(18)));
  bank().clear_history();

  // Copy held, then a pad: A01 > B03 (pattern 18); the pad is no chain entry, nothing is queued, the page does not flip
  t.press(Button::Pattern);
  t.press(Button::Copy);
  t.pad(18);
  t.release(Button::Copy);
  t.release(Button::Pattern);
  t.wait(40);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  CHECK(t.selected() == 0);
  CHECK(bank().pattern(18).steps[3].note == 52);
  CHECK(bank().pattern(18).steps[3].flags == kStepAccent);
  CHECK(bank().pattern(0).steps[3].note == 52);  // the source is as it was

  // a second pad with Copy still held is a second copy; an own pad is nothing
  t.press(Button::Pattern);
  t.press(Button::Copy);
  t.pad(0);
  CHECK(bank().can_undo());
  t.pad(19);
  t.release(Button::Copy);
  t.release(Button::Pattern);
  CHECK(bank().pattern(19).steps[3].note == 52);
  CHECK(bank().undo() == 19);  // one undo step each
  CHECK(pattern_is_empty(bank().pattern(19)));
  CHECK(bank().undo() == 18);
  CHECK(pattern_is_empty(bank().pattern(18)));
  CHECK_FALSE(bank().can_undo());

  // Delete held, then a pad: that pattern is cleared (not the selected one), one undo step brings it back
  REQUIRE(bank().replace(18, bank().pattern(0)));
  bank().clear_history();
  t.press(Button::Pattern);
  t.press(Button::Delete);
  t.pad(18);
  t.pad(30);  // an empty pattern: "WAS EMPTY", no edit
  t.release(Button::Delete);
  t.release(Button::Pattern);
  t.wait(40);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  CHECK(t.selected() == 0);
  CHECK(pattern_is_empty(bank().pattern(18)));
  CHECK(bank().pattern(0).steps[3].note == 52);
  CHECK(bank().undo() == 18);
  CHECK(bank().pattern(18).steps[3].note == 52);
  CHECK_FALSE(bank().can_undo());

  // Without Copy or Delete a pad still selects (stopped), and Copy alone is still the step's
  t.select_pattern(18);
  CHECK(t.selected() == 18);
  t.select_pattern(0);
  t.tap(Button::Copy);
  CHECK(bank().has_step_clip());
}

TEST_CASE("Shift + Insert pushes the steps right from the cursor; Shift + Delete is backspace and clears nothing else") {
  Hands t;
  const Pattern before = t.pat();
  t.pad(step_pad_of(6));
  t.shift_tap(Button::Insert);
  for (unsigned i = 0; i < 6; ++i) {
    CHECK(t.pat().steps[i].note == before.steps[i].note);  // before the cursor nothing moved
    CHECK(t.pat().steps[i].flags == before.steps[i].flags);
  }
  for (unsigned i = 7; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == before.steps[i - 1].note);  // after it everything is one later
  }
  t.tap(Button::Delete);  // the cursor step is the new one
  CHECK((t.pat().steps[6].flags & kStepRest) != 0);
  CHECK(t.pat().steps[0].flags == before.steps[0].flags);
  t.pad(step_pad_of(2));
  const Pattern pre = t.pat();
  t.shift_tap(Button::Delete);
  for (unsigned i = 2; i < 15; ++i) {
    CHECK(t.pat().steps[i].note == pre.steps[i + 1].note);
  }
  CHECK(t.pat().steps[0].flags == before.steps[0].flags);  // not a clear of the pattern
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

TEST_CASE("Insert on held steps: each becomes a tie, a tie becomes a rest; Up or Down wakes a rest or a tie before moving it") {
  Hands t;
  Pattern p = t.pat();
  for (unsigned i = 0; i < 16; ++i) {
    p.steps[i].note = static_cast<std::uint8_t>(40 + i);
    p.steps[i].flags = 0;
  }
  bank().replace(t.selected(), p);
  t.down(step_pad_of(2));
  t.down(step_pad_of(5));
  t.tap(Button::Insert);
  t.up(step_pad_of(2));
  t.up(step_pad_of(5));
  CHECK(t.pat().steps[2].flags == kStepTie);
  CHECK(t.pat().steps[5].flags == kStepTie);
  CHECK(t.pat().steps[3].flags == 0);  // only the held ones
  t.down(step_pad_of(2));
  t.tap(Button::Insert);  // a tie becomes a rest
  t.up(step_pad_of(2));
  CHECK(t.pat().steps[2].flags == kStepRest);
  // Up on the rest at step 3: the first press makes it the last note before it (step 2 is a rest, step 1 sounds: 41), no move.
  t.down(step_pad_of(2));
  t.tap(Button::Up);
  t.up(step_pad_of(2));
  CHECK(t.pat().steps[2].flags == 0);
  CHECK(t.pat().steps[2].note == 41);
  t.down(step_pad_of(2));
  t.tap(Button::Down);  // later presses move it
  t.up(step_pad_of(2));
  CHECK(t.pat().steps[2].note == 40);
  // Down on the tie at step 6 carries the pitch of step 5 (44): the tie becomes that pitch, still not moved.
  t.down(step_pad_of(5));
  t.tap(Button::Down);
  t.up(step_pad_of(5));
  CHECK(t.pat().steps[5].flags == 0);
  CHECK(t.pat().steps[5].note == 44);
}

TEST_CASE("a rest with no note before it wakes as the keys' C") {
  Hands t;
  Pattern p = t.pat();
  for (unsigned i = 0; i < 16; ++i) {
    p.steps[i].flags = kStepRest;
  }
  bank().replace(t.selected(), p);
  t.pad(step_pad_of(0));
  t.tap(Button::Up);
  CHECK((t.pat().steps[0].flags & kStepRest) == 0);
  CHECK(t.pat().steps[0].note % 12 == 0);
}

TEST_CASE("the key LEDs of a scale: the root full, the scale mid, the rest dark; without a scale the naturals are dim") {
  CHECK(key_led_level(true, false, true, false) == app::kLedDim);
  CHECK(key_led_level(false, false, true, false) == app::kLedOff);
  CHECK(key_led_level(true, true, true, false) == app::kLedMid);
  CHECK(key_led_level(false, true, true, false) == app::kLedMid);
  CHECK(key_led_level(true, true, true, true) == app::kLedOn);
  CHECK(key_led_level(true, true, false, false) == app::kLedOff);
  CHECK(key_led_level(false, true, false, false) == app::kLedOff);
}
