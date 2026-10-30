// SPDX-License-Identifier: GPL-3.0-or-later
// Step editing of M3b (docs/M3B-PLAN.md R3, R4, R5): the ratchet is the PLAYS row (hold a step + the encoder; the OCT+ / OCT- gesture is cut), hold a step + Shift + up / down its
// octave (up / down alone are a semitone, on the held step or the cursor's), hold a step + the encoder on INSTRUMENT MORE's RATCHET and COND entries edits them (the condition
// through seq::step_cond), hold ACC (or SLIDE) + left / right rotates that layer alone. Every edit goes through the Bank, so it is undoable.
#include <doctest/doctest.h>

#include "hands.h"

#include <o3_303/seq/ops.h>
#include <o3_303/step_model.h>

using namespace hands;
using namespace o3_303;

namespace {

const unsigned kAccPad = role_pad(PadRole::Acc);
const unsigned kSlidePad = role_pad(PadRole::Slide);
const unsigned kOctDownPad = role_pad(PadRole::OctDown);
const unsigned kOctUpPad = role_pad(PadRole::OctUp);

// One step with a distinct flag mix, so that a layer rotation is visible: accents on 0 and 4, slides on 1 and 2.
void set_layers(Hands& t) {
  Pattern p = t.pat();
  for (unsigned i = 0; i < 16; ++i) {
    p.steps[i].note = static_cast<std::uint8_t>(36 + i);
    p.steps[i].flags = 0;
    p.steps[i].cond = kCondAlways;
  }
  p.steps[0].flags = kStepAccent;
  p.steps[4].flags = kStepAccent;
  p.steps[1].flags = kStepSlide;
  p.steps[2].flags = kStepSlide;
  p.steps[7].flags = kStepRest;
  p.length = 16;
  bank().replace(t.selected(), p);
  bank().clear_history();
}

}  // namespace

TEST_CASE("hold a step + the PLAYS row moves its ratchet one hit up or down, x1 to x4; OCT+ / OCT- with a step held are only the keyboard's octave (D24)") {
  Hands t;
  set_layers(t);
  const Pattern before = t.pat();
  t.tap(Button::Vol);  // the PLAYS row owns the encoder
  t.down(step_pad_of(5));
  t.turn(1);
  CHECK(seq::get_ratchet(t.pat(), 5) == 1);
  t.turn(2);
  CHECK(seq::get_ratchet(t.pat(), 5) == 3);
  t.turn(1);  // x4 is the first stop; the list goes on with 75 % (a condition replaces the ratchet)
  CHECK(seq::get_cond(t.pat(), 5) == 75);
  t.turn(-1);
  CHECK(seq::get_cond(t.pat(), 5) == kCondAlways);
  CHECK(seq::get_ratchet(t.pat(), 5) == 3);
  t.turn(-1);
  CHECK(seq::get_ratchet(t.pat(), 5) == 2);
  t.up(step_pad_of(5));
  // The note and the other flags of the step, the other steps and the keyboard's octave are as they were.
  CHECK(t.pat().steps[5].note == before.steps[5].note);
  CHECK((t.pat().steps[5].flags & ~kStepRatchetMask) == before.steps[5].flags);
  CHECK(seq::get_ratchet(t.pat(), 4) == 0);
  CHECK(seq::get_ratchet(t.pat(), 6) == 0);
  // The keyboard did not move an octave: REC writes the C#2 pad as C#2 on the cursor step.
  t.tap(Button::Rec);
  t.pad(key_pad(37));
  CHECK(t.pat().steps[0].note == 37);
  t.tap(Button::Rec);

  // Down below x1 stops at x1.
  t.down(step_pad_of(5));
  t.turn(-5);
  CHECK(seq::get_ratchet(t.pat(), 5) == 0);
  // Two held steps move together, each from where it stands.
  t.turn(1);  // step 5: x2
  t.down(step_pad_of(9));
  t.turn(1);  // step 5: x3, step 9: x2
  t.up(step_pad_of(5));
  t.up(step_pad_of(9));
  CHECK(seq::get_ratchet(t.pat(), 5) == 2);
  CHECK(seq::get_ratchet(t.pat(), 9) == 1);
  // The cursor did not follow the held steps: the REC entry above left it on step 1.
  const unsigned note = t.pat().steps[1].note;
  t.tap(Button::Up);
  CHECK(t.pat().steps[1].note == note + 1);

  // The old gesture is cut: OCT+ / OCT- with a step held are the keyboard's octave and leave the ratchet where it is.
  t.down(step_pad_of(5));
  t.pad(kOctUpPad);
  CHECK(seq::get_ratchet(t.pat(), 5) == 2);
  t.pad(kOctDownPad);
  CHECK(seq::get_ratchet(t.pat(), 5) == 2);
  t.up(step_pad_of(5));
  t.pad(kOctUpPad);
  t.tap(Button::Rec);
  t.pad(key_pad(36));  // the keyboard is an octave up: C3, written on the cursor, which the tap of the step pad left on step 6
  CHECK(t.pat().steps[5].note == 48);
  t.tap(Button::Rec);
}

TEST_CASE("OCT+ / OCT- with no step held still move the keyboard's octave") {
  Hands t;
  t.pad(kOctUpPad);
  t.wait(40);
  // The bottom left pad is now C3 (48): REC writes it.
  t.tap(Button::Rec);
  t.pad(key_pad(36));
  CHECK(t.pat().steps[0].note == 48);
  t.pad(kOctDownPad);
  t.pad(kOctDownPad);
  t.pad(key_pad(36));
  CHECK(t.pat().steps[1].note == 24);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(seq::get_ratchet(t.pat(), i) == 0);
  }
}

TEST_CASE("a ratchet edit is undoable, and a run of presses on one step is one undo step") {
  Hands t;
  set_layers(t);
  const Pattern before = t.pat();
  t.tap(Button::Vol);
  t.down(step_pad_of(5));
  t.turn(1);
  t.turn(1);
  t.turn(1);
  t.up(step_pad_of(5));
  CHECK(seq::get_ratchet(t.pat(), 5) == 3);
  CHECK(bank().can_undo());
  CHECK(bank().undo() == static_cast<int>(t.selected()));
  CHECK(seq::get_ratchet(t.pat(), 5) == 0);
  CHECK(t.pat().steps[5].flags == before.steps[5].flags);
  CHECK_FALSE(bank().can_undo());
  CHECK(bank().redo() == static_cast<int>(t.selected()));
  CHECK(seq::get_ratchet(t.pat(), 5) == 3);
}

TEST_CASE("hold a step + Shift + UP / DOWN moves its octave (the note is kept inside 0..127); UP / DOWN are a semitone, on the held step or the cursor's") {
  Hands t;
  set_layers(t);
  const Pattern before = t.pat();
  t.down(step_pad_of(3));
  t.shift_tap(Button::Up);
  CHECK(t.pat().steps[3].note == before.steps[3].note + 12);
  t.shift_tap(Button::Down);
  t.shift_tap(Button::Down);
  CHECK(t.pat().steps[3].note == before.steps[3].note - 12);
  t.tap(Button::Up);  // without Shift: a semitone on the held step
  CHECK(t.pat().steps[3].note == before.steps[3].note - 11);
  t.up(step_pad_of(3));
  CHECK(t.pat().steps[0].note == before.steps[0].note);
  // The edge: a step that would leave 0..127 stays where it is.
  Pattern edge = t.pat();
  edge.steps[6].note = 120;
  edge.steps[8].note = 5;
  bank().replace(t.selected(), edge);
  t.down(step_pad_of(6));
  t.shift_tap(Button::Up);
  CHECK(t.pat().steps[6].note == 120);  // 132 is out
  t.shift_tap(Button::Down);
  CHECK(t.pat().steps[6].note == 108);
  t.up(step_pad_of(6));
  t.down(step_pad_of(8));
  t.shift_tap(Button::Down);
  CHECK(t.pat().steps[8].note == 5);  // -7 is out
  t.shift_tap(Button::Up);
  CHECK(t.pat().steps[8].note == 17);
  t.up(step_pad_of(8));
  // Alone: the cursor step (step 0), one semitone.
  t.tap(Button::Up);
  CHECK(t.pat().steps[0].note == before.steps[0].note + 1);
  t.tap(Button::Down);
  t.tap(Button::Down);
  CHECK(t.pat().steps[0].note == before.steps[0].note - 1);
}

TEST_CASE("hold a step + the encoder on RATCHET (INSTRUMENT MORE's F7) sets that step's ratchet; hold two and both move") {
  Hands t;
  set_layers(t);
  t.to_instrument(kInstMore);
  t.tap(Button::F7);
  t.down(step_pad_of(2));
  t.turn(2);
  CHECK(seq::get_ratchet(t.pat(), 2) == 2);
  t.turn(5);  // clamped at x4
  CHECK(seq::get_ratchet(t.pat(), 2) == 3);
  t.turn(-1);
  CHECK(seq::get_ratchet(t.pat(), 2) == 2);
  // Another step held: both move by the same detents from where each stands (2 -> 1; 0 stays 0 at the bottom).
  t.down(step_pad_of(11));
  t.turn(-1);
  CHECK(seq::get_ratchet(t.pat(), 2) == 1);
  CHECK(seq::get_ratchet(t.pat(), 11) == 0);
  t.up(step_pad_of(2));
  t.up(step_pad_of(11));
  // The held steps were not selected: the cursor stays on step 0.
  CHECK(seq::get_ratchet(t.pat(), 0) == 0);
  // No step held: the encoder edits the cursor step (the entry is "the step the screen is on").
  t.turn(1);
  CHECK(seq::get_ratchet(t.pat(), 0) == 1);
  // A lone tap on a step pad selects it: the entry follows the cursor.
  t.pad(step_pad_of(6), 10);
  t.turn(3);
  CHECK(seq::get_ratchet(t.pat(), 6) == 3);
  CHECK(seq::get_ratchet(t.pat(), 0) == 1);
  // Undo takes the whole run on a step back.
  CHECK(bank().can_undo());
}

TEST_CASE("hold a step + the encoder on COND walks the conditions with seq::step_cond, each held step from its own") {
  Hands t;
  set_layers(t);
  Pattern p = t.pat();
  p.steps[9].cond = cond_probability(50);  // a step that starts somewhere else on the walk
  bank().replace(t.selected(), p);
  bank().clear_history();
  t.to_instrument(kInstMore);
  t.tap(Button::F8);
  t.down(step_pad_of(3));
  t.turn(1);
  CHECK(seq::get_cond(t.pat(), 3) == cond_probability(10));
  t.turn(1);
  CHECK(seq::get_cond(t.pat(), 3) == cond_probability(20));
  t.turn(-2);
  CHECK(seq::get_cond(t.pat(), 3) == kCondAlways);
  t.turn(-5);  // the bottom of the list
  CHECK(seq::get_cond(t.pat(), 3) == kCondAlways);
  t.turn(10);  // ... 90 %, 1:2
  CHECK(seq::get_cond(t.pat(), 3) == cond_ratio(1, 2));
  t.turn(100);  // the top: NOT FIRST
  CHECK(seq::get_cond(t.pat(), 3) == kCondNotFirst);
  t.turn(-1);
  CHECK(seq::get_cond(t.pat(), 3) == kCondFirst);
  t.turn(-1);
  CHECK(seq::get_cond(t.pat(), 3) == kCondNotFill);
  t.turn(-1);
  CHECK(seq::get_cond(t.pat(), 3) == kCondFill);
  // Two steps: 3 is on FILL, 9 on 50 %; one detent each from its own value (step_cond's rule: a value that is on the list moves along it).
  t.down(step_pad_of(9));
  t.turn(1);
  CHECK(seq::get_cond(t.pat(), 3) == kCondNotFill);
  CHECK(seq::get_cond(t.pat(), 9) == cond_probability(60));
  t.up(step_pad_of(3));
  t.up(step_pad_of(9));
  CHECK(seq::get_cond(t.pat(), 4) == kCondAlways);  // nobody else moved
  // Shift + turn is coarse: five places (the descriptor's coarse step).
  t.down(step_pad_of(12));
  t.press(Button::Shift);
  t.turn(1);
  t.release(Button::Shift);
  CHECK(seq::get_cond(t.pat(), 12) == seq::cond_at(5));
  t.up(step_pad_of(12));
}

TEST_CASE("a condition edit is undoable and the pattern is otherwise as it was") {
  Hands t;
  set_layers(t);
  const Pattern before = t.pat();
  t.to_instrument(kInstMore);
  t.tap(Button::F8);
  t.down(step_pad_of(3));
  t.turn(4);
  t.turn(4);
  t.up(step_pad_of(3));
  CHECK(seq::get_cond(t.pat(), 3) == seq::cond_at(8));
  CHECK(bank().undo() == static_cast<int>(t.selected()));
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].cond == before.steps[i].cond);
    CHECK(t.pat().steps[i].flags == before.steps[i].flags);
    CHECK(t.pat().steps[i].note == before.steps[i].note);
  }
}

TEST_CASE("hold a step + the encoder on a SOUND key still locks it (the entries of INSTRUMENT MORE did not take the encoder away)") {
  Hands t;
  set_layers(t);
  t.tap(Button::F1);  // CUTOFF on SOUND
  t.down(step_pad_of(4));
  t.turn(10);
  t.up(step_pad_of(4));
  CHECK(step_has_motion(t.pat(), 0, 4));
  CHECK(seq::get_ratchet(t.pat(), 4) == 0);
  CHECK(seq::get_cond(t.pat(), 4) == kCondAlways);
}

TEST_CASE("hold ACC + right / left rotates the accent layer alone; hold SLIDE does the slides; the notes and the rest stay") {
  Hands t;
  set_layers(t);
  const Pattern before = t.pat();
  t.down(kAccPad);
  t.tap(Button::Right);
  t.up(kAccPad);
  // Accents were on 0 and 4: now on 1 and 5. Everything else is where it was.
  for (unsigned i = 0; i < 16; ++i) {
    const bool acc = i == 1 || i == 5;
    CHECK(((t.pat().steps[i].flags & kStepAccent) != 0) == acc);
    CHECK((t.pat().steps[i].flags & kStepSlide) == (before.steps[i].flags & kStepSlide));
    CHECK((t.pat().steps[i].flags & kStepRest) == (before.steps[i].flags & kStepRest));
    CHECK(t.pat().steps[i].note == before.steps[i].note);
  }
  // The release of ACC was not a tap: the cursor step (0) did not toggle its accent.
  CHECK((t.pat().steps[0].flags & kStepAccent) == 0);
  // Left puts them back; a wrap: accents on 0 and 4, one left of 0 is the last step.
  t.down(kAccPad);
  t.tap(Button::Left);
  t.tap(Button::Left);
  t.up(kAccPad);
  CHECK((t.pat().steps[15].flags & kStepAccent) != 0);
  CHECK((t.pat().steps[3].flags & kStepAccent) != 0);
  // The slides: 1 and 2 -> 2 and 3 with SLIDE + right.
  t.down(kSlidePad);
  t.tap(Button::Right);
  t.up(kSlidePad);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(((t.pat().steps[i].flags & kStepSlide) != 0) == (i == 2 || i == 3));
  }
  CHECK((t.pat().steps[1].flags & kStepSlide) == 0);
  // The cursor did not move: Shift + Right (no modifier) rotates the whole pattern as before.
  t.shift_tap(Button::Right);
  CHECK(t.pat().steps[1].note == before.steps[0].note);
}

TEST_CASE("a tap on ACC with nothing else still toggles the cursor step's accent, and right / left alone still move the cursor") {
  Hands t;
  set_layers(t);
  t.pad(kAccPad);
  CHECK((t.pat().steps[0].flags & kStepAccent) == 0);  // it had one: the tap toggled it off
  t.tap(Button::Right);
  const unsigned note = t.pat().steps[1].note;
  t.tap(Button::Up);  // the cursor is on step 1 now
  CHECK(t.pat().steps[1].note == note + 1);
}

TEST_CASE("a layer rotation is one undoable edit") {
  Hands t;
  set_layers(t);
  const Pattern before = t.pat();
  t.down(kAccPad);
  t.tap(Button::Right);
  t.up(kAccPad);
  CHECK(bank().can_undo());
  CHECK(bank().undo() == static_cast<int>(t.selected()));
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].flags == before.steps[i].flags);
  }
}
