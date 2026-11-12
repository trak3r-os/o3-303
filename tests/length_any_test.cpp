// SPDX-License-Identifier: GPL-3.0-or-later
// I1: a pattern is 1 to 64 steps long, not only a power of two or a multiple of 8. What assumed one: the cursor and the step pages, the playhead,
// the TRACT line that follows the pattern (SAME), rotate, the record path (REC writes and wraps at the length). The sequencer's side (swing,
// chains, the grid) is in app/seq/tests; the LENGTH page's walk and jump are in pages_test.cpp.
#include "app_hands.h"

#include <o3_303/app_access.h>
#include <o3_303/step_model.h>
#include <o3_303/tract_line.h>

TEST_CASE("a line of 7 steps: the cursor wraps at the 7th step, the pads past it are dark") {
  Hands t;
  t.length_to(7);
  REQUIRE(t.pat().length == 7);
  const Pattern before = t.pat();
  t.tap(Button::Left);  // from step 1 to step 7, not to 16
  t.tap(Button::Delete);
  CHECK((t.pat().steps[6].flags & kStepRest) != 0);
  CHECK(t.pat().steps[7].flags == before.steps[7].flags);
  t.tap(Button::Right);  // and past the last step back to step 1
  t.tap(Button::Delete);
  CHECK((t.pat().steps[0].flags & kStepRest) != 0);
  t.wait(60);
  for (unsigned k = 7; k < 16; ++k) {
    CHECK(t.led(step_pad_of(k)) == app::kLedOff);
  }
}

TEST_CASE("a line of 7 steps: Shift + Right and Left rotate the seven steps only") {
  Hands t;
  t.length_to(7);
  const Pattern now = t.pat();
  t.shift_tap(Button::Right);
  for (unsigned i = 0; i < 7; ++i) {
    CHECK(t.pat().steps[(i + 1) % 7].note == now.steps[i].note);
    CHECK(t.pat().steps[(i + 1) % 7].flags == now.steps[i].flags);
  }
  for (unsigned i = 7; i < kMaxSteps; ++i) {
    CHECK(t.pat().steps[i].flags == now.steps[i].flags);
    CHECK(t.pat().steps[i].note == now.steps[i].note);
  }
  t.shift_tap(Button::Left);
  for (unsigned i = 0; i < 7; ++i) {
    CHECK(t.pat().steps[i].note == now.steps[i].note);
  }
}

TEST_CASE("a line of 7 steps: REC writes the 8th key over step 1, the record path wraps at the length") {
  Hands t;
  t.select_pattern(12);
  REQUIRE(pattern_is_empty(t.pat()));
  t.length_to(7);
  t.tap(Button::Rec);
  for (unsigned i = 0; i < 8; ++i) {
    t.pad(key_pad(48 + i));
  }
  CHECK(t.pat().steps[0].note == 55);  // the 8th key came back to step 1
  for (unsigned i = 1; i < 7; ++i) {
    CHECK(t.pat().steps[i].note == 48 + i);
  }
  CHECK(t.pat().length == 7);
}

TEST_CASE("a line of 7 steps playing: the playhead and the SAME TRACT line stay inside the seven, and reach the last") {
  Hands t;
  t.length_to(7);
  CHECK(tract_length(t.pat().tract, t.pat().length) == 7);
  t.tap(Button::Play);
  int max_step = -1;
  int max_tract = -1;
  bool seen[7] = {};
  for (unsigned i = 0; i < 2600; ++i) {
    t.wait(1);
    const seq::Playhead ph = app_sequencer().playhead();
    if (ph.playing && ph.pattern == 0) {
      REQUIRE(ph.step < 7);
      max_step = std::max(max_step, ph.step);
      if (ph.step >= 0) {
        seen[ph.step] = true;
      }
    }
    max_tract = std::max(max_tract, app_sequencer().tract_playhead());
  }
  t.tap(Button::Play);
  CHECK(max_step == 6);
  for (const bool s : seen) {
    CHECK(s);
  }
  CHECK(max_tract >= 0);
  CHECK(max_tract < 7);
}

TEST_CASE("a line of 40 steps: three pages, the last holds 8; Left from step 1 lands on step 40 and a step pad there is step 32 plus its place") {
  Hands t;
  t.select_pattern(12);
  REQUIRE(pattern_is_empty(t.pat()));
  t.length_to(40);
  REQUIRE(t.pat().length == 40);
  t.tap(Button::Left);  // step 40: page 3
  t.wait(60);
  for (unsigned k = 8; k < 16; ++k) {
    CHECK(t.led(step_pad_of(k)) == app::kLedOff);
  }
  CHECK(t.led(step_pad_of(7)) > app::kLedOff);  // the cursor
  t.pad(step_pad_of(2));                        // step 35 of the line
  t.tap(Button::Up);                            // wakes the rest as a note
  CHECK((t.pat().steps[34].flags & kStepRest) == 0);
  unsigned woken = 0;
  for (unsigned i = 0; i < kMaxSteps; ++i) {
    woken += (t.pat().steps[i].flags & kStepRest) == 0 ? 1u : 0u;
  }
  CHECK(woken == 1);
  for (unsigned i = 0; i < 6; ++i) {  // steps 36 to 40, the sixth wraps to step 1
    t.tap(Button::Right);
  }
  t.tap(Button::Up);  // step 1 is a rest too: this wakes it, and only it
  CHECK((t.pat().steps[0].flags & kStepRest) == 0);
  CHECK((t.pat().steps[40].flags & kStepRest) != 0);
}
