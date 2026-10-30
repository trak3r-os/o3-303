// SPDX-License-Identifier: GPL-3.0-or-later
// Step entry end to end (DESIGN-303 sections 2 and 3): the key pads, REC + keys, OCT, hold-a-step gestures, the arrows, Copy / Insert / Delete, Play and tap tempo, the step pads on a long line.
// (Split from the former o3_303_app_test.cpp; the hands are in app_hands.h.)
#include "app_hands.h"

#include <o3_303/app_access.h>
#include <o3_303/step_model.h>

TEST_CASE("a key pad lights while it is held, and a step pad taps the cursor there") {
  Hands t;
  t.down(key_pad(52));
  CHECK(t.led(key_pad(52)) == app::kLedOn);
  t.up(key_pad(52));
  t.wait(40);
  CHECK(t.led(key_pad(52)) != app::kLedOn);

  t.pad(step_pad_of(6));  // a tap selects step 7: the delete that follows clears it
  t.tap(Button::Delete);
  CHECK((t.pat().steps[6].flags & kStepRest) != 0);
  CHECK((t.pat().steps[7].flags & kStepRest) == (bank().pattern(0).steps[7].flags & kStepRest));
}

TEST_CASE("REC + keys write the cursor step and advance, with ACC or SLIDE held; REST and TIE advance too") {
  Hands t;
  t.select_pattern(12);
  REQUIRE(t.selected() == 12);
  REQUIRE(pattern_is_empty(t.pat()));
  t.tap(Button::Rec);
  t.pad(key_pad(48));                       // step 1: C3
  t.down(kAccPad);                          // step 2: D#3 with accent
  t.pad(key_pad(51));
  t.up(kAccPad);
  t.down(kSlidePad);                        // step 3: G3 with slide
  t.pad(key_pad(55));
  t.up(kSlidePad);
  t.pad(kRestPad);                          // step 4: a rest
  t.pad(key_pad(53));                       // step 5: F3
  t.pad(kTiePad);                           // step 6: a tie
  t.down(kAccPad);                          // step 7: both held: accent and slide
  t.down(kSlidePad);
  t.pad(key_pad(58));
  t.up(kSlidePad);
  t.up(kAccPad);

  const Pattern& p = t.pat();
  CHECK(p.steps[0].note == 48);
  CHECK(p.steps[0].flags == 0);
  CHECK(p.steps[1].note == 51);
  CHECK(p.steps[1].flags == kStepAccent);
  CHECK(p.steps[2].note == 55);
  CHECK(p.steps[2].flags == kStepSlide);
  CHECK(p.steps[3].flags == kStepRest);
  CHECK(p.steps[4].note == 53);
  CHECK(p.steps[4].flags == 0);
  CHECK(p.steps[5].flags == kStepTie);
  CHECK(p.steps[6].note == 58);
  CHECK(p.steps[6].flags == (kStepAccent | kStepSlide));
  CHECK((p.steps[7].flags & kStepRest) != 0);  // untouched: still a rest

  // Without REC a key writes nothing.
  t.tap(Button::Rec);
  t.pad(key_pad(59));
  CHECK(t.pat().steps[7].note != 59);
  // The cursor wraps at the end of the length: 16 entries and the 17th is step 1 again.
  t.tap(Button::Rec);
  for (unsigned i = 0; i < 9; ++i) {
    t.pad(key_pad(49));
  }
  t.pad(key_pad(50));
  CHECK(t.pat().steps[0].note == 50);
}

TEST_CASE("OCT- and OCT+ move the keyboard; the keyboard writes the notes it shows") {
  Hands t;
  t.select_pattern(13);
  t.tap(Button::Rec);
  t.pad(36);  // the bottom left pad: C2
  CHECK(t.pat().steps[0].note == 36);
  t.pad(kOctUpPad);
  t.pad(36);  // C3 now
  CHECK(t.pat().steps[1].note == 48);
  t.pad(24);  // the row above: C4
  CHECK(t.pat().steps[2].note == 60);
  t.pad(kOctDownPad);
  t.pad(kOctDownPad);
  t.pad(36);
  CHECK(t.pat().steps[3].note == 24);
  for (unsigned i = 0; i < 6; ++i) {  // the shift stops at the ends of the note range
    t.pad(kOctDownPad);
  }
  t.pad(36);
  CHECK(t.pat().steps[4].note == 0);
  for (unsigned i = 0; i < 12; ++i) {
    t.pad(kOctUpPad);
  }
  t.pad(24);
  CHECK(t.pat().steps[5].note == 36 + 12 * (kMaxOctave + 1));
}

TEST_CASE("hold a step + a key sets that step's note and leaves the cursor where it was") {
  Hands t;
  t.select_pattern(14);
  t.tap(Button::Rec);
  t.pad(key_pad(48));  // step 1 written, the cursor is on step 2
  t.down(step_pad_of(5));
  t.pad(key_pad(52));
  t.pad(key_pad(55));  // a second key on the same held step: the last one wins
  t.up(step_pad_of(5));
  CHECK(t.pat().steps[5].note == 55);
  CHECK((t.pat().steps[5].flags & kStepRest) == 0);  // a rest becomes a note
  t.pad(key_pad(57));                                // the cursor was still on step 2
  CHECK(t.pat().steps[1].note == 57);
  CHECK(t.pat().steps[5].note == 55);
}

TEST_CASE("hold a step + ACC / SLIDE / TIE / REST toggles that attribute on it, the PLAYS row is its ratchet, and the step is not selected") {
  Hands t;
  const Pattern before = bank().pattern(0);
  t.tap(Button::Vol);      // the PLAYS row owns the encoder (H3: the old OCT+ gesture is cut)
  t.down(step_pad_of(3));  // step 4: C3, plain
  t.pad(kAccPad);
  t.pad(kSlidePad);
  t.turn(1);  // the ratchet x2
  t.up(step_pad_of(3));
  CHECK(seq::get_ratchet(t.pat(), 3) == 1);
  CHECK((t.pat().steps[3].flags & ~kStepRatchetMask) == ((before.steps[3].flags ^ kStepAccent ^ kStepSlide) & ~kStepRatchetMask));
  CHECK(t.pat().steps[3].note == before.steps[3].note);
  t.pad(step_pad_of(3), 5);  // a tap selects; a second toggle through the cursor and the tap on a modifier
  t.tap(Button::Delete);
  CHECK((t.pat().steps[3].flags & kStepRest) != 0);
  t.down(step_pad_of(3));
  t.pad(kRestPad);  // REST toggles off again
  t.pad(kTiePad);
  t.up(step_pad_of(3));
  CHECK((t.pat().steps[3].flags & kStepRest) == 0);
  CHECK((t.pat().steps[3].flags & kStepTie) != 0);
  CHECK(t.pat().steps[0].note == before.steps[0].note);  // nothing else moved
  // The cursor did not follow the held steps: Up changes the note of step 4, where the tap left it.
  const unsigned note = t.pat().steps[3].note;
  t.tap(Button::Up);
  CHECK(t.pat().steps[3].note == note + 1);
}

TEST_CASE("hold a modifier alone: the step pads show its steps and tapping them toggles (TR style)") {
  Hands t;
  const Pattern before = bank().pattern(0);
  t.down(kAccPad);
  t.wait(20);
  for (unsigned k = 1; k < 16; ++k) {
    const bool acc = (before.steps[k].flags & kStepAccent) != 0;
    CHECK(t.led(step_pad_of(k)) == (acc ? app::kLedMid : app::kLedOff));
  }
  t.pad(step_pad_of(1));  // step 2 had an accent: off now
  t.pad(step_pad_of(2));  // step 3 had none: on now
  t.wait(300);            // held longer than a tap: only a look at the steps, not a toggle of the cursor step
  t.up(kAccPad);
  CHECK(((t.pat().steps[1].flags ^ before.steps[1].flags) & kStepAccent) != 0);  // toggled
  CHECK(((t.pat().steps[2].flags ^ before.steps[2].flags) & kStepAccent) != 0);
  CHECK(t.pat().steps[0].flags == before.steps[0].flags);

  // REST held alone works the same way.
  t.down(kRestPad);
  t.pad(step_pad_of(8));
  t.pad(step_pad_of(9));
  t.up(kRestPad);
  CHECK((t.pat().steps[8].flags & kStepRest) != (before.steps[8].flags & kStepRest));
  CHECK((t.pat().steps[9].flags & kStepRest) != (before.steps[9].flags & kStepRest));
}

TEST_CASE("a tap on ACC, SLIDE, TIE or REST toggles it on the cursor step; with REC and stopped, TIE and REST are entries") {
  Hands t;
  const Pattern before = bank().pattern(0);
  t.pad(kAccPad);  // the cursor is on step 1
  CHECK(t.pat().steps[0].flags == (before.steps[0].flags ^ kStepAccent));
  t.pad(kAccPad);
  CHECK(t.pat().steps[0].flags == before.steps[0].flags);
  t.pad(kSlidePad);
  CHECK((t.pat().steps[0].flags & kStepSlide) != (before.steps[0].flags & kStepSlide));
  t.pad(kSlidePad);

  t.tap(Button::Rec);
  t.pad(kRestPad);  // an entry: step 1 becomes a rest and the cursor moves to step 2
  CHECK(t.pat().steps[0].flags == kStepRest);
  t.pad(kTiePad);   // step 2 becomes a tie
  CHECK(t.pat().steps[1].flags == kStepTie);
  t.tap(Button::Rec);
  t.pad(kAccPad);   // REC off again: a toggle on the cursor (step 3)
  CHECK((t.pat().steps[2].flags & kStepAccent) != (before.steps[2].flags & kStepAccent));
}

TEST_CASE("the arrows move the cursor across pages; Shift + Left / Right rotate, Shift + Up / Down move the octave") {
  Hands t;
  const Pattern before = bank().pattern(0);
  t.tap(Button::Right);
  t.tap(Button::Delete);  // step 2
  CHECK((t.pat().steps[1].flags & kStepRest) != 0);
  t.tap(Button::Left);
  t.tap(Button::Left);  // wraps to step 16
  t.tap(Button::Delete);
  CHECK((t.pat().steps[15].flags & kStepRest) != 0);

  t.tap(Button::Up);  // the cursor step's note +1 (step 16 is a rest now but keeps its note)
  CHECK(t.pat().steps[15].note == before.steps[15].note + 1);
  t.tap(Button::Down);
  CHECK(t.pat().steps[15].note == before.steps[15].note);

  const Pattern now = t.pat();
  t.shift_tap(Button::Right);  // rotate: every step moves one later
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[(i + 1) % 16].note == now.steps[i].note);
    CHECK(t.pat().steps[(i + 1) % 16].flags == now.steps[i].flags);
  }
  t.shift_tap(Button::Left);
  CHECK(t.pat().steps[3].note == now.steps[3].note);
  // Shift + Up / Down: the cursor step's octave (the pattern's transpose is PATTERN SETUP's now).
  const unsigned at = 3;
  t.pad(step_pad_of(at));
  const unsigned note = t.pat().steps[at].note;
  t.shift_tap(Button::Up);
  CHECK(t.pat().steps[at].note == note + 12);
  t.shift_tap(Button::Down);
  t.shift_tap(Button::Down);
  CHECK(t.pat().steps[at].note == note - 12);
  CHECK(t.pat().transpose == 0);
}

TEST_CASE("Copy, Shift + Copy, Insert, Delete work on steps; Pattern + Insert duplicates; Shift + Insert goes to step 1; Shift + Delete is backspace") {
  Hands t;
  const Pattern before = bank().pattern(0);
  t.pad(step_pad_of(2));  // step 3
  t.tap(Button::Copy);
  t.pad(step_pad_of(4));  // step 5
  t.shift_tap(Button::Copy);
  CHECK(t.pat().steps[4].note == before.steps[2].note);
  CHECK(t.pat().steps[4].flags == before.steps[2].flags);

  t.pad(step_pad_of(8));  // step 9: insert shifts the steps after it one later
  const Pattern pre = t.pat();
  t.tap(Button::Insert);
  CHECK((t.pat().steps[8].flags & kStepRest) != 0);
  CHECK(t.pat().steps[9].note == pre.steps[8].note);
  CHECK(t.pat().steps[10].note == pre.steps[9].note);

  t.shift_tap(Button::Insert);  // Shift + Insert (Home): the cursor goes to step 1, nothing is inserted
  CHECK(t.selected() == 0);
  CHECK((t.pat().steps[8].flags & kStepRest) != 0);  // (the rest at step 9 is still the one Insert made; no second one)
  CHECK(t.pat().steps[9].note == pre.steps[8].note);
  t.tap(Button::Delete);  // ... and Delete clears step 1
  CHECK(t.pat().steps[0].flags == kStepRest);

  t.duplicate_next();  // a copy in the next slot, which is now selected
  CHECK(t.selected() == 1);
  CHECK_FALSE(pattern_is_empty(t.pat()));
  CHECK(t.pat().steps[4].note == bank().pattern(0).steps[4].note);

  // Shift + Delete (Backspace): the cursor's step goes and the steps after it move one earlier.
  t.pad(step_pad_of(4));
  const Pattern pre_bs = t.pat();
  t.shift_tap(Button::Delete);
  for (unsigned i = 4; i < 15; ++i) {
    CHECK(t.pat().steps[i].note == pre_bs.steps[i + 1].note);
    CHECK(t.pat().steps[i].flags == pre_bs.steps[i + 1].flags);
  }
  CHECK((t.pat().steps[15].flags & kStepRest) != 0);
  CHECK_FALSE(pattern_is_empty(t.pat()));  // the pattern is not cleared: that is the CLEAR tool's (Shift + F6)
  CHECK_FALSE(pattern_is_empty(bank().pattern(0)));
}

TEST_CASE("Delete clears the step whole: a rest with no ratchet, no condition and no locks; held steps too") {
  Hands t;
  // Step 4: a ratchet, a condition and a lock on it.
  t.tap(Button::Vol);
  t.down(step_pad_of(3));
  t.turn(1);          // hold a step + the PLAYS row: the ratchet
  t.tap(Button::F1);  // the encoder is the knob's again
  t.turn(10);         // the latched knob (CUTOFF): a lock on the held step
  t.up(step_pad_of(3));
  REQUIRE(ratchet_of(t.pat().steps[3].flags) > 0u);
  REQUIRE(step_has_motion(t.pat(), 0, 3));
  bank().edit(t.selected(), [](Pattern& p) { p.steps[3].cond = kCondFill; });
  t.pad(step_pad_of(3));  // the cursor on step 4
  t.tap(Button::Delete);
  const Step& st = t.pat().steps[3];
  CHECK(st.flags == kStepRest);
  CHECK(st.cond == kCondAlways);
  CHECK_FALSE(step_has_motion(t.pat(), 0, 3));
  // A held step is cleared instead of the cursor's (step 4, still the cursor).
  const Pattern before = t.pat();
  t.down(step_pad_of(6));
  t.tap(Button::Delete);
  t.up(step_pad_of(6));
  CHECK(t.pat().steps[6].flags == kStepRest);
  CHECK(t.pat().steps[3].note == before.steps[3].note);
  // Each clear is one undo step.
  const unsigned selected = t.selected();
  CHECK(bank().undo() == static_cast<int>(selected));
  CHECK((t.pat().steps[6].flags & kStepRest) == (before.steps[6].flags & kStepRest));
}

TEST_CASE("Play plays and stops at the press; Shift + Master taps the tempo (M1: Shift + Play restarts, tempo_test.cpp)") {
  Hands t;
  t.press(Button::Play);
  t.wait(5);
  CHECK(t.h->engine().playing());
  t.release(Button::Play);
  t.wait(50);
  CHECK(t.h->engine().playing());
  t.tap(Button::Play);
  CHECK_FALSE(t.h->engine().playing());

  t.press(Button::Shift);
  for (unsigned i = 0; i < 5; ++i) {
    t.tap(Button::Master, 30);
    t.wait(470);  // 500 ms between the taps: 120 BPM
  }
  t.release(Button::Shift);
  CHECK_FALSE(t.h->engine().playing());
  CHECK(t.h->engine().tempo() > 118'000u);
  CHECK(t.h->engine().tempo() < 122'000u);
  // A gap over two seconds starts the measure again.
  t.wait(2500);
  t.press(Button::Shift);
  t.tap(Button::Master, 30);
  t.wait(970);
  t.tap(Button::Master, 30);
  t.release(Button::Shift);
  CHECK(t.h->engine().tempo() > 58'000u);
  CHECK(t.h->engine().tempo() < 62'000u);
}

TEST_CASE("REC + Play + a key writes the note under the playhead and sounds it") {
  Hands t;
  t.select_pattern(20);
  REQUIRE(pattern_is_empty(t.pat()));
  t.tap(Button::Rec);
  t.tap(Button::Play);
  t.wait(300);
  t.pad(key_pad(52));
  t.tap(Button::Play);
  unsigned written = 0;
  for (unsigned i = 0; i < 16; ++i) {
    if ((t.pat().steps[i].flags & kStepRest) == 0 && t.pat().steps[i].note == 52) {
      ++written;
    }
  }
  CHECK(written == 1);
}

TEST_CASE("hold a step + UP / DOWN moves the held step a semitone (Shift: an octave), not the cursor's") {
  Hands t;
  const Pattern before = bank().pattern(0);
  t.down(step_pad_of(5));
  t.tap(Button::Up);
  t.tap(Button::Up);
  t.tap(Button::Down);
  t.up(step_pad_of(5));
  CHECK(t.pat().steps[5].note == before.steps[5].note + 1);
  CHECK(t.pat().steps[0].note == before.steps[0].note);  // the cursor's step is untouched
  t.down(step_pad_of(5));
  t.shift_tap(Button::Up);
  t.up(step_pad_of(5));
  CHECK(t.pat().steps[5].note == before.steps[5].note + 13);
  // Two held steps move together.
  t.down(step_pad_of(2));
  t.down(step_pad_of(3));
  t.shift_tap(Button::Down);
  t.up(step_pad_of(2));
  t.up(step_pad_of(3));
  CHECK(t.pat().steps[2].note == before.steps[2].note - 12);
  CHECK(t.pat().steps[3].note == before.steps[3].note - 12);
  // Not held: the cursor's step, as before; and the held pads did not move the cursor.
  t.tap(Button::Up);
  CHECK(t.pat().steps[0].note == before.steps[0].note + 1);
}

TEST_CASE("playing, the playhead stays visible on the pad when the cursor is on the same step") {
  Hands t;
  t.tap(Button::Left);  // the cursor on step 16
  t.tap(Button::Play);
  unsigned on_cursor = 0;
  unsigned bright = 0;
  unsigned run = 0;
  for (unsigned i = 0; i < 2500; ++i) {
    t.wait(1);
    const seq::Playhead ph = app_sequencer().playhead();
    if (ph.playing && ph.pattern == 0 && ph.step == 15) {
      ++run;
      if (run >= 70) {  // the LEDs follow once a frame (33 ms): after two frames they show this step
        ++on_cursor;
        bright += t.led(step_pad_of(15)) >= app::kLedMid ? 1u : 0u;
      }
    } else {
      run = 0;
    }
  }
  t.tap(Button::Play);
  CHECK(on_cursor > 20);
  CHECK(bright == on_cursor);
}
