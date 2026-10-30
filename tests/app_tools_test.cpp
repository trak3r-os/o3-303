// SPDX-License-Identifier: GPL-3.0-or-later
// The tools row and the pattern tools end to end: Shift + F1..F8, UNDO / REDO, the RANDOM popup, the first UNDO after boot.
// (Split from the former o3_303_app_test.cpp; the hands are in app_hands.h.)
#include "app_hands.h"

#include <o3_303/step_model.h>
#include <o3_303/units/acid_voice/acid_voice.h>

TEST_CASE("the first UNDO does not empty the factory line; the edits after it can be undone") {
  Hands t;
  const Pattern first = bank().pattern(0);
  t.shift_tap(Button::F8);  // UNDO with nothing to undo
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == first.steps[i].note);
    CHECK(t.pat().steps[i].flags == first.steps[i].flags);
  }
  CHECK_FALSE(pattern_is_empty(t.pat()));
  t.tap(Button::Delete);  // one edit
  CHECK(bank().can_undo());
  t.shift_tap(Button::F8);
  CHECK(t.pat().steps[0].flags == first.steps[0].flags);
  CHECK_FALSE(bank().can_undo());
}

TEST_CASE("the tools under Shift + F1..F8 change the pattern and UNDO / REDO walk them (DESIGN-303 section 3)") {
  Hands t;
  const Pattern first = bank().pattern(0);
  t.shift_tap(Button::F1);  // RANDOM: the popup opens (C3) ...
  CHECK(t.pat().steps[0].note == first.steps[0].note);
  t.tap(Button::F8);        // ... and F8 makes the line
  t.shift_tap(Button::F1);  // Shift + F1 closes the popup
  const Pattern random = t.pat();
  bool differs = false;
  for (unsigned i = 0; i < 16; ++i) {
    differs = differs || random.steps[i].note != first.steps[i].note || random.steps[i].flags != first.steps[i].flags;
  }
  CHECK(differs);
  t.shift_tap(Button::F3);  // REVERSE
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == random.steps[15 - i].note);
  }
  t.shift_tap(Button::F5);  // DOUBLE
  CHECK(t.pat().length == 32);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[16 + i].note == t.pat().steps[i].note);
  }
  t.shift_tap(Button::F6);  // CLEAR
  CHECK(pattern_is_empty(t.pat()));
  t.shift_tap(Button::F8);  // UNDO
  CHECK_FALSE(pattern_is_empty(t.pat()));
  t.shift_tap(Button::F8);
  CHECK(t.pat().length == 16);
  t.shift_tap(Button::F7);  // REDO
  CHECK(t.pat().length == 32);
  t.shift_tap(Button::F2);  // MUTATE
  t.shift_tap(Button::F4);  // INVERT
  // A tool is not a page key: the pages' latch is where it was.
  const float cutoff0 = t.param(kVoice, AcidVoice::Cutoff);
  t.turn(3);
  CHECK(t.param(kVoice, AcidVoice::Cutoff) != cutoff0);
}

TEST_CASE("Shift shows the tools row only while the encoder is still: a coarse Shift + turn keeps the knob strip; the key row has no names otherwise (they are in the boxes)") {
  Hands t;
  const auto first_label = [&]() {
    app::SoftKeys keys;
    t.h->runtime().context().screen()->soft_keys(t.h->runtime().context(), keys);
    return std::string(keys.label[0] != nullptr ? keys.label[0] : "");
  };
  CHECK(first_label().empty());
  t.press(Button::Shift);
  t.wait(100);
  CHECK(first_label().empty());  // not yet: a turn may follow
  t.wait(700);
  CHECK(first_label() == "RANDOM");
  t.turn(3);  // a coarse step: the strip with its names and values comes back
  CHECK(first_label().empty());
  t.wait(300);
  CHECK(first_label().empty());
  t.wait(700);
  CHECK(first_label() == "RANDOM");
  t.release(Button::Shift);
  CHECK(first_label().empty());

  // The tools work from the press of Shift, with or without the row shown.
  t.press(Button::Shift);
  t.tap(Button::F6);  // CLEAR at once
  t.release(Button::Shift);
  CHECK(pattern_is_empty(t.pat()));
}

TEST_CASE("RANDOM is not the same line after every boot") {
  Pattern first;
  {
    Hands t;
    t.shift_tap(Button::F1);  // the popup
    t.tap(Button::F8);        // GENERATE
    first = t.pat();
  }
  Pattern second;
  {
    Hands t;
    t.wait(37);  // a hand does not press at the same moment twice
    t.shift_tap(Button::F1);
    t.tap(Button::F8);
    second = t.pat();
  }
  bool differs = false;
  for (unsigned i = 0; i < 16; ++i) {
    differs = differs || first.steps[i].note != second.steps[i].note || first.steps[i].flags != second.steps[i].flags;
  }
  CHECK(differs);
}
