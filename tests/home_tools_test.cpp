// SPDX-License-Identifier: GPL-3.0-or-later
// HOME's Shift row (UX-303 5.1 and 7, slice H4), against the real app: NEW VARY RHYTHM NOTES REVERSE CLEAR REDO UNDO at once, each edit one UNDO step; the
// F keys' latch is the SOUND page's, shared with PERFORM and INSTRUMENT VOICE (D3); no F key does something its label does not say. The operations
// themselves (regen_rhythm, regen_notes, reverse) are tested alone in app/seq/tests; PATTERN SETUP's Shift row is in pattern_setup_test.cpp.
#include <doctest/doctest.h>

#include "hands.h"

#include "../app/src/main_screen.h"

#include <o3_303/pad_map.h>
#include <o3_303/step_model.h>
#include <o3_303/units/acid_voice/acid_voice.h>

#include <cstring>
#include <string>

using namespace hands;
using namespace o3_303;

namespace {

// A line of notes C2 + step with a rest on step 7, no accents or slides, no history: every tool has something to change.
void line(Hands& t) {
  Pattern p = t.pat();
  for (unsigned i = 0; i < 16; ++i) {
    p.steps[i].note = static_cast<std::uint8_t>(36 + i);
    p.steps[i].flags = 0;
    p.steps[i].cond = kCondAlways;
  }
  p.steps[7].flags = kStepRest;
  p.length = 16;
  bank().replace(t.selected(), p);
  bank().clear_history();
}

bool same_pattern(const Pattern& a, const Pattern& b) { return std::memcmp(&a, &b, sizeof(Pattern)) == 0; }

struct Keys {
  std::string label[app::kFKeyCount];
  std::uint8_t dim = 0;
};

// The soft keys the screen shows now.
Keys soft_keys(Hands& t) {
  app::SoftKeys keys;
  t.h->runtime().context().screen()->soft_keys(t.h->runtime().context(), keys);
  Keys out;
  for (unsigned k = 0; k < app::kFKeyCount; ++k) {
    out.label[k] = keys.label[k] != nullptr ? keys.label[k] : "";
  }
  out.dim = keys.dim;
  return out;
}

}  // namespace

TEST_CASE("each tool of the Shift row is one UNDO step: UNDO gives the pattern back exactly, REDO makes it again") {
  for (unsigned k = 0; k < 6; ++k) {  // NEW VARY RHYTHM NOTES REVERSE CLEAR
    INFO("Shift + F" << k + 1);
    Hands t;
    line(t);
    const Pattern before = t.pat();
    t.shift_tap(app::fkey(k));
    const Pattern after = t.pat();
    CHECK_FALSE(same_pattern(after, before));
    CHECK(bank().can_undo());
    t.shift_tap(Button::F8);  // UNDO: one step
    CHECK(same_pattern(t.pat(), before));
    CHECK_FALSE(bank().can_undo());
    CHECK(bank().can_redo());
    t.shift_tap(Button::F7);  // REDO
    CHECK(same_pattern(t.pat(), after));
    CHECK_FALSE(bank().can_redo());
    CHECK(t.h->display().peak_frame_words() < 1800u);
  }
}

TEST_CASE("the Shift row works at the press of Shift: no wait, no still encoder") {
  Hands t;
  line(t);
  const Pattern before = t.pat();
  t.press(Button::Shift);
  t.turn(2);  // a coarse turn on the latched knob first
  t.tap(Button::F5);  // REVERSE, at once
  t.release(Button::Shift);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == before.steps[15 - i].note);
  }
}

TEST_CASE("RHYTHM keeps every pitch and redraws the rest; NOTES keeps the rhythm and redraws the pitches") {
  Hands t;
  line(t);
  const Pattern before = t.pat();
  t.shift_tap(Button::F3);  // RHYTHM
  const Pattern rhythm = t.pat();
  bool flags_differ = false;
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(rhythm.steps[i].note == before.steps[i].note);
    flags_differ = flags_differ || rhythm.steps[i].flags != before.steps[i].flags;
  }
  CHECK(flags_differ);
  t.shift_tap(Button::F4);  // NOTES
  bool notes_differ = false;
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].flags == rhythm.steps[i].flags);
    notes_differ = notes_differ || t.pat().steps[i].note != rhythm.steps[i].note;
  }
  CHECK(notes_differ);
  CHECK(t.pat().length == 16);
}

TEST_CASE("NEW is a new line with the project's generator settings: density, accent and slide chance of PATTERN SETUP") {
  Hands t;
  t.tap(Button::Pattern);  // PATTERN SETUP
  t.tap(Button::F6);
  t.turn(100);  // 100 %: every step sounds
  t.tap(Button::F7);
  t.turn(100);  // every step accented
  t.tap(Button::F8);
  t.turn(-100);  // none slides
  t.tap(Button::Pattern);
  t.shift_tap(Button::F1);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK((t.pat().steps[i].flags & kStepRest) == 0u);
    CHECK((t.pat().steps[i].flags & kStepAccent) != 0u);
    CHECK((t.pat().steps[i].flags & kStepSlide) == 0u);
  }
}

TEST_CASE("a tool that changes nothing leaves no UNDO step: CLEAR of an empty pattern, UNDO and REDO with nothing to walk") {
  Hands t;
  line(t);
  t.shift_tap(Button::F6);  // CLEAR
  REQUIRE(pattern_is_empty(t.pat()));
  t.shift_tap(Button::F6);  // CLEAR again: nothing changed
  t.shift_tap(Button::F7);  // REDO with nothing to redo
  t.shift_tap(Button::F8);  // one UNDO is all there is
  CHECK_FALSE(pattern_is_empty(t.pat()));
  CHECK_FALSE(bank().can_undo());
  t.shift_tap(Button::F8);  // nothing more: the line stays
  CHECK_FALSE(pattern_is_empty(t.pat()));
}

TEST_CASE("a tool is not a page key: the latch stays where it was, and the release of the F key is not the page's") {
  Hands t;
  line(t);
  t.tap(Button::F2);  // RESO latched
  t.press(Button::Shift);
  t.press(Button::F3);  // RHYTHM
  t.release(Button::Shift);  // Shift first, as a hand may
  t.release(Button::F3);
  const float reso = t.param(kVoice, AcidVoice::Reso);
  const float env = t.param(kVoice, AcidVoice::EnvMod);
  t.turn(3);
  CHECK(t.param(kVoice, AcidVoice::Reso) != reso);
  CHECK(t.param(kVoice, AcidVoice::EnvMod) == env);
}

TEST_CASE("HOME's latch is PERFORM's and INSTRUMENT VOICE's: one SOUND latch (D3)") {
  Hands t;
  t.tap(Button::F2);  // RESO latched on HOME
  t.to_perform();
  float reso = t.param(kVoice, AcidVoice::Reso);
  t.turn(3);  // PERFORM's encoder is the ladder on RESO
  CHECK(t.param(kVoice, AcidVoice::Reso) != reso);
  t.tap(Button::F3);  // a key tapped on PERFORM: ENV MOD
  t.to_home();
  const float env = t.param(kVoice, AcidVoice::EnvMod);
  reso = t.param(kVoice, AcidVoice::Reso);
  t.turn(3);  // HOME has it too
  CHECK(t.param(kVoice, AcidVoice::EnvMod) != env);
  CHECK(t.param(kVoice, AcidVoice::Reso) == reso);
  t.to_instrument(o3_303::kInstVoice);
  const float decay = t.param(kVoice, AcidVoice::Decay);
  t.to_home();
  t.tap(Button::F4);
  t.to_instrument(o3_303::kInstVoice);
  t.turn(3);
  CHECK(t.param(kVoice, AcidVoice::Decay) != decay);
  // SETUP keeps its own latch: a tap there does not move the SOUND one.
  t.to_home();
  t.tap(Button::Pattern);
  t.tap(Button::F3);  // GATE of SETUP
  t.tap(Button::Pattern);
  const float decay2 = t.param(kVoice, AcidVoice::Decay);
  t.turn(3);
  CHECK(t.param(kVoice, AcidVoice::Decay) != decay2);
}

TEST_CASE("the soft keys say what each F key does: HOME's row, SETUP's row, nothing without Shift; a free key does nothing") {
  Hands t;
  line(t);
  for (const std::string& l : soft_keys(t).label) {
    CHECK(l.empty());
  }
  t.press(Button::Shift);
  static constexpr const char* kHome[app::kFKeyCount] = {"NEW", "VARY", "RHYTHM", "NOTES", "REVERSE", "CLEAR", "REDO", "UNDO"};
  Keys k = soft_keys(t);
  for (unsigned i = 0; i < app::kFKeyCount; ++i) {
    CHECK(k.label[i] == kHome[i]);
  }
  CHECK((k.dim & (1u << 6)) != 0u);  // REDO: nothing to redo
  CHECK((k.dim & (1u << 7)) != 0u);  // UNDO: nothing to undo
  CHECK((k.dim & 0x3Fu) == 0u);
  t.tap(Button::F5);
  k = soft_keys(t);
  CHECK((k.dim & (1u << 7)) == 0u);  // UNDO has a step now
  t.release(Button::Shift);

  t.tap(Button::Pattern);  // PATTERN SETUP
  t.press(Button::Shift);
  static constexpr const char* kSetup[app::kFKeyCount] = {"COPY PAT", "PASTE", "DUP>NEXT", "CLR LOCK", "-", "-", "-", "UNDO"};
  k = soft_keys(t);
  for (unsigned i = 0; i < app::kFKeyCount; ++i) {
    CHECK(k.label[i] == kSetup[i]);
  }
  CHECK((k.dim & 0x70u) == 0x70u);  // the three free keys are dark
  const Pattern before = t.pat();
  const bool undo = bank().can_undo();
  for (unsigned i = 4; i < 7; ++i) {
    t.tap(app::fkey(i));  // "-": nothing happens
  }
  CHECK(same_pattern(t.pat(), before));
  CHECK(bank().can_undo() == undo);
  t.release(Button::Shift);
}

TEST_CASE("more tools that change nothing leave no step: REVERSE of a palindrome, NOTES of a line with nothing sounding") {
  Hands t;
  line(t);
  Pattern p = t.pat();
  for (unsigned i = 0; i < 16; ++i) {  // a palindrome: step i and step 15 - i the same
    p.steps[i].note = static_cast<std::uint8_t>(36 + (i < 8 ? i : 15 - i));
    p.steps[i].flags = 0;
  }
  bank().replace(t.selected(), p);
  bank().clear_history();
  t.shift_tap(Button::F5);  // REVERSE
  CHECK(same_pattern(t.pat(), p));
  CHECK_FALSE(bank().can_undo());

  t.shift_tap(Button::F6);  // CLEAR: one step, every step a rest
  REQUIRE(pattern_is_empty(t.pat()));
  const Pattern empty = t.pat();
  t.shift_tap(Button::F4);  // NOTES: no step sounds, so no pitch is drawn
  CHECK(same_pattern(t.pat(), empty));
  t.shift_tap(Button::F8);  // the one step there is: the CLEAR
  CHECK(same_pattern(t.pat(), p));
  CHECK_FALSE(bank().can_undo());
}

TEST_CASE("RHYTHM of an empty line draws a one-pitch line and is one UNDO step") {
  Hands t;
  line(t);
  t.shift_tap(Button::F6);  // CLEAR
  bank().clear_history();
  t.shift_tap(Button::F3);  // RHYTHM
  CHECK_FALSE(pattern_is_empty(t.pat()));
  CHECK(bank().can_undo());
  t.shift_tap(Button::F8);
  CHECK(pattern_is_empty(t.pat()));
  CHECK_FALSE(bank().can_undo());
}

TEST_CASE("Shift + F with a step pad held: the tool runs, the held step lets go cleanly, the encoder is the knob's again") {
  Hands t;
  line(t);
  const Pattern before = t.pat();
  t.down(step_pad_of(4));
  t.press(Button::Shift);
  t.tap(Button::F5);  // REVERSE
  t.release(Button::Shift);
  t.up(step_pad_of(4));
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == before.steps[15 - i].note);
  }
  CHECK(bank().can_undo());
  t.shift_tap(Button::F8);  // one step back, nothing else was written by the hold
  CHECK(same_pattern(t.pat(), before));
  CHECK_FALSE(bank().can_undo());
  const float cutoff = t.param(kVoice, AcidVoice::Cutoff);
  t.turn(3);
  CHECK(t.param(kVoice, AcidVoice::Cutoff) != cutoff);
}

TEST_CASE("Shift + F with Rec on while the pattern plays: the tool runs and Rec stays on") {
  Hands t;
  line(t);
  const Pattern before = t.pat();
  t.tap(Button::Rec);
  t.tap(Button::Play);
  t.wait(200);
  MainScreen* home = static_cast<MainScreen*>(t.h->runtime().context().screen());
  REQUIRE(home->rec());
  t.shift_tap(Button::F6);  // CLEAR while recording
  CHECK(pattern_is_empty(t.pat()));
  CHECK(home->rec());
  t.shift_tap(Button::F8);  // UNDO
  CHECK(same_pattern(t.pat(), before));
  CHECK(t.h->engine().playing());
  t.tap(Button::Play);
}

TEST_CASE("Shift + F8 UNDO on PATTERN SETUP undoes a SETUP edit and leaves SETUP up") {
  Hands t;
  line(t);
  t.tap(Button::Pattern);
  REQUIRE(std::strcmp(t.title(), "PATTERN SETUP") == 0);
  t.tap(Button::F1);
  t.turn(1);  // LENGTH 16 > 32, one step
  REQUIRE(t.pat().length == 32);
  Keys k;
  t.press(Button::Shift);
  k = soft_keys(t);
  CHECK(k.label[7] == "UNDO");
  CHECK((k.dim & (1u << 7)) == 0u);
  t.tap(Button::F8);
  t.release(Button::Shift);
  CHECK(t.pat().length == 16);
  CHECK(std::strcmp(t.title(), "PATTERN SETUP") == 0);
  CHECK_FALSE(bank().can_undo());
  t.press(Button::Shift);
  CHECK((soft_keys(t).dim & (1u << 7)) != 0u);  // nothing more to undo
  t.release(Button::Shift);
}
