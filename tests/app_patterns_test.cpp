// SPDX-License-Identifier: GPL-3.0-or-later
// The pattern pads end to end: select, queue and chain with Pattern held, long patterns, Pattern + Copy, and the limits of Shift + Insert.
// (Split from the former o3_303_app_test.cpp; the hands are in app_hands.h.)
#include "app_hands.h"

#include <o3_303/step_model.h>

#include <cstring>

TEST_CASE("hold Pattern: the 48 pads are the patterns; stopped, a tap selects, a quick tap on Pattern flips to the piano roll") {
  Hands t;
  t.press(Button::Pattern);
  t.wait(350);
  CHECK(std::strcmp(t.title(), "PATTERNS") == 0);
  CHECK(t.led(0) != app::kLedOff);  // A01 holds the factory line
  CHECK(t.led(30) == app::kLedOff); // C07 is empty
  t.pad(30);
  CHECK(t.selected() == 30);
  t.release(Button::Pattern);
  t.wait(5);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  CHECK(t.selected() == 30);  // the grid was not a tap: the view did not flip

  t.tap(Button::Pattern, 40);
  CHECK(std::strcmp(t.title(), "ROLL") == 0);
  t.tap(Button::Pattern, 40);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
}

TEST_CASE("playing, taps with Pattern held queue the next pattern and several taps chain; the screen follows what plays") {
  Hands t;
  // Two more patterns with something in them: copies of A01.
  t.duplicate_next();  // A02
  t.duplicate_next();  // A03
  REQUIRE(t.selected() == 2);
  t.select_pattern(0);
  t.tap(Button::Play);
  t.wait(300);
  t.press(Button::Pattern);
  t.wait(350);
  t.pad(2);
  t.pad(1);
  t.release(Button::Pattern);
  t.wait(200);
  CHECK(t.selected() == 0);  // still A01 until it ends
  // A01 is 16 steps: at 125 BPM about 1.9 s. Then A03, then A02.
  t.wait(2200);
  CHECK(t.selected() == 2);
  t.wait(2100);
  CHECK(t.selected() == 1);
  t.wait(2100);
  CHECK(t.selected() == 1);  // the chain is used up: the last pattern repeats
  t.tap(Button::Play);
  CHECK_FALSE(t.h->engine().playing());
}

TEST_CASE("long patterns: the step pads show a page of 16 and the cursor and the playhead take the page along") {
  Hands t;
  t.shift_tap(Button::F5);  // DOUBLE: 32 steps
  CHECK(t.pat().length == 32);
  const Pattern p = t.pat();
  // Page 1: the pads are steps 1..16.
  for (unsigned k = 1; k < 16; ++k) {
    CHECK((t.led(step_pad_of(k)) == app::kLedOff) == (describe(p, k).kind == Cell::Rest));
  }
  t.tap(Button::Left);  // wraps to step 32: page 2
  t.wait(30);
  t.shift_tap(Button::F6);  // CLEAR (clears everything: only to compare the pads of page 2 with the pattern)
  CHECK(pattern_is_empty(t.pat()));
  t.shift_tap(Button::F8);  // UNDO
  t.wait(30);
  for (unsigned k = 0; k < 15; ++k) {  // the cursor is step 32 = pad 15 of page 2
    CHECK((t.led(step_pad_of(k)) == app::kLedOff) == (describe(p, 16 + k).kind == Cell::Rest));
  }
  t.tap(Button::Right);  // step 1: page 1
  t.wait(30);
  CHECK((t.led(step_pad_of(4)) == app::kLedOff) == (describe(p, 4).kind == Cell::Rest));

  // Playing follows the playhead's page.
  t.tap(Button::Play);
  t.wait(2200);  // a little over 16 steps at 125 BPM
  bool second_page = false;
  for (unsigned k = 0; k < 16; ++k) {
    second_page = second_page || t.led(step_pad_of(k)) == app::kLedOn;
  }
  CHECK(second_page);
  t.tap(Button::Play);
  CHECK(t.h->display().peak_frame_words() < 2048u);

  // Shorter lengths: the pads past the end are dark.
  for (unsigned i = 0; i < kPageSeq; ++i) {
    t.tap(Button::Params);
  }
  t.tap(Button::F3);
  t.turn(-3);  // 8 steps
  CHECK(t.pat().length == 8);
  t.wait(30);
  for (unsigned k = 8; k < 16; ++k) {
    CHECK(t.led(step_pad_of(k)) == app::kLedOff);
  }
}

TEST_CASE("Pattern held + Copy copies the pattern; Shift + Copy pastes it into the selected one") {
  Hands t;
  const Pattern first = bank().pattern(0);
  t.press(Button::Pattern);
  t.wait(350);
  t.tap(Button::Copy);  // the pattern, not the step
  t.release(Button::Pattern);
  t.wait(5);
  t.select_pattern(7);
  REQUIRE(pattern_is_empty(t.pat()));
  t.shift_tap(Button::Copy);
  CHECK_FALSE(pattern_is_empty(t.pat()));
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == first.steps[i].note);
    CHECK(t.pat().steps[i].flags == first.steps[i].flags);
  }
  CHECK(pattern_is_empty(bank().pattern(8)));
  // Copying a step afterwards makes the next paste a step again.
  t.tap(Button::Copy);
  t.pad(step_pad_of(1));
  t.shift_tap(Button::Copy);
  CHECK(t.pat().steps[1].note == t.pat().steps[0].note);
}

TEST_CASE("Shift + Insert on the last pattern refuses (nothing wraps round to overwrite A01)") {
  Hands t;
  const Pattern first = bank().pattern(0);
  t.select_pattern(kPatternCount - 1);
  REQUIRE(t.selected() == kPatternCount - 1);
  t.duplicate_next();
  CHECK(t.selected() == kPatternCount - 1);
  CHECK_FALSE(pattern_is_empty(bank().pattern(0)));
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(bank().pattern(0).steps[i].note == first.steps[i].note);
    CHECK(bank().pattern(0).steps[i].flags == first.steps[i].flags);
  }
  // One before the last still duplicates.
  t.select_pattern(kPatternCount - 2);
  t.duplicate_next();
  CHECK(t.selected() == kPatternCount - 1);
}
