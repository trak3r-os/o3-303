// SPDX-License-Identifier: GPL-3.0-or-later
// The Shift row and the pattern tools end to end: Shift + F1..F8 at once, UNDO / REDO, the first UNDO after boot (UX-303 5.1 and 7; home_tools_test.cpp has
// each tool alone). The RANDOM popup, INVERT and DOUBLE are gone: NEW acts at once, NOTES covers INVERT, LENGTH up in PATTERN SETUP is DOUBLE.
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

TEST_CASE("the tools under Shift + F1..F8 change the pattern and UNDO / REDO walk them (UX-303 5.1)") {
  Hands t;
  const Pattern first = bank().pattern(0);
  t.shift_tap(Button::F1);  // NEW: the line is made at once, no popup
  const Pattern fresh = t.pat();
  bool differs = false;
  for (unsigned i = 0; i < 16; ++i) {
    differs = differs || fresh.steps[i].note != first.steps[i].note || fresh.steps[i].flags != first.steps[i].flags;
  }
  CHECK(differs);
  t.shift_tap(Button::F5);  // REVERSE
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == fresh.steps[15 - i].note);
  }
  t.shift_tap(Button::F6);  // CLEAR
  CHECK(pattern_is_empty(t.pat()));
  t.shift_tap(Button::F8);  // UNDO
  CHECK_FALSE(pattern_is_empty(t.pat()));
  t.shift_tap(Button::F8);  // the REVERSE is undone
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == fresh.steps[i].note);
  }
  t.shift_tap(Button::F7);  // REDO
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == fresh.steps[15 - i].note);
  }
  t.shift_tap(Button::F2);  // VARY
  t.shift_tap(Button::F3);  // RHYTHM
  t.shift_tap(Button::F4);  // NOTES
  CHECK(t.pat().length == 16);  // no tool changes the length: that is PATTERN SETUP's LENGTH
  // A tool is not a page key: the pages' latch is where it was.
  const float cutoff0 = t.param(kVoice, AcidVoice::Cutoff);
  t.turn(3);
  CHECK(t.param(kVoice, AcidVoice::Cutoff) != cutoff0);
}

TEST_CASE("Shift shows the Shift row on the soft keys at once, a turn does not take it away, and the knob strip is never replaced") {
  Hands t;
  const auto label = [&](unsigned k) {
    app::SoftKeys keys;
    t.h->runtime().context().screen()->soft_keys(t.h->runtime().context(), keys);
    return std::string(keys.label[k] != nullptr ? keys.label[k] : "");
  };
  CHECK(label(0).empty());  // no labels: the knob strip's boxes carry the F-numbers and names
  t.press(Button::Shift);
  CHECK(label(0) == "NEW");  // at once: no 600 ms wait
  CHECK(label(1) == "VARY");
  CHECK(label(2) == "RHYTHM");
  CHECK(label(3) == "NOTES");
  CHECK(label(4) == "REVERSE");
  CHECK(label(5) == "CLEAR");
  CHECK(label(6) == "REDO");
  CHECK(label(7) == "UNDO");
  t.turn(3);  // a coarse step
  CHECK(label(0) == "NEW");
  t.release(Button::Shift);
  CHECK(label(0).empty());

  // The tools work from the press of Shift.
  t.press(Button::Shift);
  t.tap(Button::F6);  // CLEAR at once
  t.release(Button::Shift);
  CHECK(pattern_is_empty(t.pat()));
}

TEST_CASE("NEW is not the same line after every boot") {
  Pattern first;
  {
    Hands t;
    t.shift_tap(Button::F1);
    first = t.pat();
  }
  Pattern second;
  {
    Hands t;
    t.wait(37);  // a hand does not press at the same moment twice
    t.shift_tap(Button::F1);
    second = t.pat();
  }
  bool differs = false;
  for (unsigned i = 0; i < 16; ++i) {
    differs = differs || first.steps[i].note != second.steps[i].note || first.steps[i].flags != second.steps[i].flags;
  }
  CHECK(differs);
}
