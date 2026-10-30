// SPDX-License-Identifier: GPL-3.0-or-later
// The app starts: it identifies itself, boots with the factory line in A01, draws every frame inside the display list (the worst cases too), and its system CONFIG screen opens.
// (Split from the former o3_303_app_test.cpp; the hands are in app_hands.h.)
#include "app_hands.h"

#include <o3_303/seq/gen.h>
#include <o3_303/step_model.h>

#include <cstring>

TEST_CASE("o3_303: the app identifies itself") {
  CHECK(std::strcmp(o3sdk_app().info().name, "O3-303") == 0);
  CHECK(std::strcmp(o3sdk_app().info().version, "0.0.1") == 0);
}

TEST_CASE("boot: the factory line is in A01, the rest of the project is empty, and the LEDs say so (DESIGN-303 section 2)") {
  Hands t;
  seq::gen::Preset first;
  seq::gen::factory(first, 0, 0);
  CHECK_FALSE(pattern_is_empty(bank().pattern(0)));
  for (unsigned i = 1; i < kPatternCount; ++i) {
    CHECK(pattern_is_empty(bank().pattern(i)));
  }
  CHECK(bank().selected() == 0);
  CHECK_FALSE(bank().can_undo());  // the boot load is not an edit of the user's: the first UNDO must not empty A01
  CHECK_FALSE(bank().can_redo());
  CHECK(t.h->engine().tempo() == seq::gen::to_values(first.patch).tempo_mbpm);
  CHECK_FALSE(t.h->engine().playing());
  CHECK(std::strcmp(t.title(), "STEPS") == 0);

  // Step LEDs: off = rest, dim = a note, low = an accented note (the cursor, step 1, blinks: not checked).
  const Pattern& p = bank().pattern(0);
  for (unsigned k = 1; k < 16; ++k) {
    const StepInfo info = describe(p, k);
    const std::uint8_t expect = info.kind == Cell::Rest ? app::kLedOff : (info.accent ? app::kLedLow : app::kLedDim);
    CHECK(t.led(step_pad_of(k)) == expect);
  }
  // Keyboard LEDs: naturals dim, sharps off, the note of the cursor's step at mid level (UX-303 6.2). The keyboard is in the pattern's register: the
  // bottom left pad is C2, which is the first note of the factory line, under the cursor.
  const StepInfo cursor_step = describe(p, 0);
  REQUIRE(cursor_step.kind == Cell::Note);
  REQUIRE(cursor_step.note >= 36);
  REQUIRE(cursor_step.note <= 59);
  const unsigned current_pad = key_pad(cursor_step.note);
  for (unsigned pad = 24; pad < 48; ++pad) {
    const int note = key_note(pad, 0);
    REQUIRE(note >= 0);
    const std::uint8_t expect = pad == current_pad ? app::kLedMid : (is_natural(static_cast<unsigned>(note)) ? app::kLedDim : app::kLedOff);
    CHECK(t.led(pad) == expect);
  }
  CHECK(t.led(key_pad(36)) == app::kLedMid);  // C2
  CHECK(t.led(key_pad(37)) == app::kLedOff); // C#2
  CHECK(t.led(key_pad(52)) == app::kLedDim); // E3
  // Move the cursor to a step with another note: its key is the one at mid level.
  unsigned other = 1;
  while (other < 16 && (describe(p, other).kind != Cell::Note || describe(p, other).note == cursor_step.note || describe(p, other).note > 59)) {
    ++other;
  }
  REQUIRE(other < 16);
  t.pad(step_pad_of(other));
  t.wait(30);
  CHECK(t.led(key_pad(describe(p, other).note)) == app::kLedMid);
  CHECK(t.led(key_pad(cursor_step.note)) == (is_natural(cursor_step.note) ? app::kLedDim : app::kLedOff));
}

TEST_CASE("every frame of a session fits the display list") {
  Hands t;
  t.select_pattern(5);
  t.tap(Button::Rec);
  t.pad(key_pad(50));
  t.shift_tap(Button::F1);
  t.tap(Button::Play);
  t.wait(1500);
  for (unsigned i = 0; i < kPageSeq; ++i) {
    t.tap(Button::Params);
  }
  t.press(Button::Pattern);
  t.wait(400);
  t.release(Button::Pattern);
  t.tap(Button::File);
  t.wait(200);
  CHECK(t.h->runtime().frames() > 30u);
  CHECK(t.h->runtime().frame_failures() == 0u);
  CHECK(t.h->display().render_failures() == 0u);
  CHECK(t.h->display().peak_frame_words() < 2048u);
  CHECK(t.h->engine().dropped_commands() == 0u);
}

TEST_CASE("CONFIG opens the kit's system screen, and its DISPLAY section changes the theme") {
  Hands t;
  const ui::ThemeId a = t.h->runtime().context().theme();
  t.tap(Button::Config);
  CHECK(t.h->runtime().system_open());  // (the runtime's: the app never sees the button; no update path in the simulator, so DISPLAY is first)
  t.turn(1);
  CHECK(t.h->runtime().context().theme() != a);
  t.turn(1);
  t.turn(1);
  CHECK(t.h->runtime().context().theme() == a);
  t.tap(Button::Config);
  CHECK_FALSE(t.h->runtime().system_open());
}

TEST_CASE("worst cases for the display list: a full 64-step line, every ramp, all the locks, 48 patterns and a chain of 16") {
  Hands t;
  // The generator at full density with every accent and slide chance, over 64 steps.
  for (unsigned i = 0; i < kPageSeq; ++i) {
    t.tap(Button::Params);
  }
  t.tap(Button::F7);
  t.turn(100);
  t.tap(Button::F8);
  t.turn(100);
  t.shift_tap(Button::F5);
  t.shift_tap(Button::F5);
  REQUIRE(t.pat().length == 64);
  t.shift_tap(Button::F1);  // RANDOM over the 64 steps
  // Every step of the first page locked on a lane, the cursor on the last step of the page.
  t.tap(Button::Params);
  t.tap(Button::F1);
  for (unsigned k = 0; k < 16; ++k) {
    t.down(step_pad_of(k));
    t.turn(7 + static_cast<int>(k));
    t.up(step_pad_of(k));
  }
  t.tap(Button::Rec);
  t.press(Button::Shift);
  t.wait(30);
  const std::size_t peak_tools = t.h->display().peak_frame_words();
  t.release(Button::Shift);
  CHECK(peak_tools < 2048u);
  t.tap(Button::Pattern, 40);  // the piano roll
  t.wait(60);
  t.tap(Button::Pattern, 40);
  t.wait(60);

  // 48 patterns, all with something in them, and a chain of 16 while playing.
  for (unsigned i = 1; i < kPatternCount; ++i) {
    t.duplicate_next();
  }
  t.select_pattern(0);
  t.tap(Button::Play);
  t.wait(200);
  t.press(Button::Pattern);
  t.wait(350);
  for (unsigned i = 1; i <= 16; ++i) {
    t.pad(i * 2);
  }
  t.wait(100);
  CHECK(t.h->display().peak_frame_words() < 2048u);
  t.release(Button::Pattern);
  t.tap(Button::Play);
  CHECK(t.h->runtime().frame_failures() == 0u);
  CHECK(t.h->display().render_failures() == 0u);
  CHECK(t.h->display().peak_frame_words() < 2048u);
  // Headroom: the worst frame (the grid of 48 patterns with a chain of 16, drawn in colour passes) is about 1400 words.
  CHECK(t.h->display().peak_frame_words() < 1700u);
}
