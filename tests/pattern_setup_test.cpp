// SPDX-License-Identifier: GPL-3.0-or-later
// PATTERN SETUP (docs/UX-303.md 5.2, slice H2) end to end: a tap of Pattern opens it and a tap closes it, F1 LENGTH is one undo each way, F5 SCALE locks
// the keyboard, F6..F8 are the generator's settings (project-wide), the Shift row (COPY PAT, PASTE PAT, DUP>NEXT, CLEAR LOCKS, UNDO) replaces the
// bridges of H1 (Pattern held + Insert / Delete), and every one of today's functions stays reachable.
#include <doctest/doctest.h>

#include "hands.h"

#include "../app/src/main_screen.h"

#include <o3sdk/app/pad_leds.h>
#include <o3_303/pad_map.h>
#include <o3_303/seq/ops.h>
#include <o3_303/step_model.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3sdk/core/scales.h>

#include <cstring>

using namespace hands;
using namespace o3_303;

namespace {

void open_setup(Hands& t) {
  t.tap(Button::Pattern, 40);
  t.wait(30);
  REQUIRE(std::strcmp(t.title(), "PATTERN SETUP") == 0);
}

void lock(Hands& t, unsigned step, int d) {
  t.down(step_pad_of(step));
  t.turn(d);
  t.up(step_pad_of(step));
}

// F1 CUTOFF locked on two steps and F2 RESO on one, from HOME (a lock needs a lane, and a lane the SOUND knob that owns the encoder).
void make_locks(Hands& t) {
  t.tap(Button::F1);
  lock(t, 3, 10);
  lock(t, 6, -10);
  t.tap(Button::F2);
  lock(t, 3, 10);
  REQUIRE(seq::lane_has_motion(t.pat(), 0));
  REQUIRE(seq::lane_has_motion(t.pat(), 1));
}

}  // namespace

TEST_CASE("a tap of Pattern flips HOME and PATTERN SETUP; the ROLL view is gone") {
  Hands t;
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  t.tap(Button::Pattern, 40);
  CHECK(std::strcmp(t.title(), "PATTERN SETUP") == 0);
  t.tap(Button::Pattern, 40);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  t.tap(Button::Pattern, 40);
  t.tap(Button::Pattern, 40);
  t.tap(Button::Pattern, 40);
  CHECK(std::strcmp(t.title(), "PATTERN SETUP") == 0);  // odd taps: SETUP; there is no third page
}

TEST_CASE("Pattern held on SETUP is the 48 patterns too; its release does not flip the page") {
  Hands t;
  open_setup(t);
  t.press(Button::Pattern);
  t.wait(350);
  CHECK(std::strcmp(t.title(), "PATTERNS") == 0);
  CHECK(t.led(0) != app::kLedOff);  // A01 holds the factory line
  t.pad(30);
  CHECK(t.selected() == 30);
  t.release(Button::Pattern);
  t.wait(5);
  CHECK(std::strcmp(t.title(), "PATTERN SETUP") == 0);
  CHECK(t.selected() == 30);
  // A chord is no tap either: Pattern + Insert (which has no job now) leaves the page where it is.
  t.press(Button::Pattern);
  t.tap(Button::Insert);
  t.release(Button::Pattern);
  t.wait(40);
  CHECK(std::strcmp(t.title(), "PATTERN SETUP") == 0);
}

TEST_CASE("coming back from another screen with Pattern lands on HOME, not on SETUP (D6)") {
  Hands t;
  open_setup(t);
  t.to_perform();
  CHECK(std::strcmp(t.title(), "PERFORM") == 0);
  t.tap(Button::Pattern);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
}

TEST_CASE("F1 LENGTH: a step up copies the line into the new part, a step down truncates, and each is one UNDO") {
  Hands t;
  const unsigned sel = t.selected();
  const Pattern before = t.pat();
  REQUIRE(before.length == 16);
  bank().clear_history();
  open_setup(t);  // F1 LENGTH is latched when SETUP opens
  t.turn(1);
  REQUIRE(t.pat().length == 32);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[16 + i].note == before.steps[i].note);
    CHECK(t.pat().steps[16 + i].flags == before.steps[i].flags);
  }
  t.turn(-2);  // 8
  REQUIRE(t.pat().length == 8);
  CHECK(t.pat().steps[10].flags == kStepRest);  // cut
  // UNDO is on the Shift row of SETUP (F8): 32 again, then the 16 the line started with.
  t.shift_tap(Button::F8);
  CHECK(t.pat().length == 32);
  CHECK(t.pat().steps[16 + 5].note == before.steps[5].note);
  t.shift_tap(Button::F8);
  CHECK(t.pat().length == 16);
  CHECK_FALSE(bank().can_undo());
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == before.steps[i].note);
    CHECK(t.pat().steps[i].flags == before.steps[i].flags);
  }
  CHECK(t.selected() == sel);
  // The redo walks forward again: HOME's Shift + F7 (SETUP's row has no REDO).
  t.tap(Button::Pattern);
  t.shift_tap(Button::F7);
  CHECK(t.pat().length == 32);
}

TEST_CASE("LENGTH up to 64 and down: the cursor and the step pads follow the new length") {
  Hands t;
  open_setup(t);
  t.turn(2);  // 64
  REQUIRE(t.pat().length == 64);
  t.tap(Button::Pattern);  // HOME
  t.tap(Button::Left);     // the cursor wraps to step 64: page 4
  t.wait(30);
  t.tap(Button::Pattern);  // SETUP
  t.turn(-3);              // 8: the cursor is past the end, it is brought back
  t.wait(30);
  REQUIRE(t.pat().length == 8);
  for (unsigned k = 8; k < 16; ++k) {
    CHECK(t.led(step_pad_of(k)) == app::kLedOff);
  }
  CHECK(t.h->display().peak_frame_words() < 1800u);
}

TEST_CASE("F2 SWING, F3 GATE and F4 TRANSP are this pattern's own, a held F key moves one while the latch comes back") {
  Hands t;
  open_setup(t);
  t.tap(Button::F2);
  t.turn(2);
  CHECK(t.pat().swing == 2);
  t.tap(Button::F3);
  t.turn(-2);
  CHECK(t.pat().gate == kDefaultGate - 2);
  t.tap(Button::F4);
  t.turn(3);
  CHECK(t.pat().transpose == 3);
  t.tap(Button::F1);
  // Hold F2 and turn: SWING moves, and at the release F1 has the encoder back.
  t.press(Button::F2);
  t.turn(1);
  t.release(Button::F2);
  CHECK(t.pat().swing == 3);
  t.turn(1);
  CHECK(t.pat().length == 32);
  // Another pattern has its own.
  t.select_pattern(7);
  CHECK(t.pat().swing == 0);
  CHECK(t.pat().transpose == 0);
}

TEST_CASE("SETUP has its own latch: HOME's stays where it was, and so does SETUP's") {
  Hands t;
  t.tap(Button::F3);  // HOME: ENV MOD
  const float env = t.param(kVoice, AcidVoice::EnvMod);
  open_setup(t);
  t.tap(Button::F2);  // SETUP: SWING
  t.tap(Button::Pattern);
  t.turn(5);  // HOME's F3 is still the owner
  CHECK(t.param(kVoice, AcidVoice::EnvMod) != env);
  CHECK(t.pat().swing == 0);
  t.tap(Button::Pattern);
  t.turn(2);  // SETUP's F2 is still the owner
  CHECK(t.pat().swing == 2);
}

TEST_CASE("F5 SCALE locks the keyboard: the pads out of the scale are dark and do nothing, and the scale is what NEW uses") {
  Hands t;
  open_setup(t);
  t.tap(Button::F5);
  t.turn(-40);  // No scale
  t.turn(3);    // Major
  t.wait(40);
  t.tap(Button::Pattern);  // HOME
  t.wait(40);
  CHECK(t.led(key_pad(38)) == app::kLedDim);  // D is in C major
  CHECK(t.led(key_pad(37)) == app::kLedOff);
  CHECK(t.led(key_pad(39)) == app::kLedOff);
  // REC + keys: C# and D# write nothing and do not advance; D and E do.
  t.tap(Button::Rec);
  t.pad(key_pad(37), 10);
  t.pad(key_pad(38), 10);
  t.pad(key_pad(39), 10);
  t.pad(key_pad(40), 10);
  CHECK(t.pat().steps[0].note == 38);
  CHECK(t.pat().steps[1].note == 40);
  t.tap(Button::Rec);
  // SETUP keeps the lock on its own keyboard (the pads are the home map's), and "No scale" unlocks it again.
  t.tap(Button::Pattern);
  t.tap(Button::F5);
  t.turn(-40);
  t.wait(40);
  t.tap(Button::Rec);
  t.pad(key_pad(37), 10);
  CHECK(t.pat().steps[2].note == 37);
}

TEST_CASE("F6 DENSITY, F7 ACC % and F8 SLD % are the generator's settings, for every pattern (D1)") {
  Hands t;
  open_setup(t);
  t.tap(Button::F6);
  t.turn(100);  // 100 %: every step sounds
  t.tap(Button::F7);
  t.turn(100);  // every step accented
  t.tap(Button::F8);
  t.turn(-100);  // and none slides
  t.tap(Button::Pattern);
  const auto generate = [&] {
    t.random_popup();
    t.tap(Button::F8);  // GENERATE
    t.random_popup();
  };
  generate();
  for (unsigned i = 0; i < 16; ++i) {
    CHECK((t.pat().steps[i].flags & kStepRest) == 0u);
    CHECK((t.pat().steps[i].flags & kStepAccent) != 0u);
    CHECK((t.pat().steps[i].flags & kStepSlide) == 0u);
  }
  // Another pattern: the same settings, nothing per pattern.
  t.select_pattern(5);
  generate();
  for (unsigned i = 0; i < 16; ++i) {
    CHECK((t.pat().steps[i].flags & kStepRest) == 0u);
    CHECK((t.pat().steps[i].flags & kStepAccent) != 0u);
    CHECK((t.pat().steps[i].flags & kStepSlide) == 0u);
  }
}

TEST_CASE("Shift row of SETUP: COPY PAT and PASTE PAT move a whole pattern; the step clipboard of HOME is untouched") {
  Hands t;
  const Pattern first = bank().pattern(0);
  open_setup(t);
  t.shift_tap(Button::F1);  // COPY PAT
  t.select_pattern(7);
  REQUIRE(pattern_is_empty(t.pat()));
  t.shift_tap(Button::F2);  // PASTE PAT
  CHECK_FALSE(pattern_is_empty(t.pat()));
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == first.steps[i].note);
    CHECK(t.pat().steps[i].flags == first.steps[i].flags);
  }
  CHECK(pattern_is_empty(bank().pattern(8)));
  // One undo takes the paste back.
  t.shift_tap(Button::F8);
  CHECK(pattern_is_empty(t.pat()));
  // HOME's Shift + Copy pastes a step, as ever: Copy a step, then paste it.
  t.tap(Button::Pattern);
  t.tap(Button::Copy);
  t.pad(step_pad_of(1));
  t.shift_tap(Button::Copy);
  CHECK(t.pat().steps[1].note == t.pat().steps[0].note);
}

TEST_CASE("DUP>NEXT copies the pattern into the next slot and selects it; the last pattern refuses") {
  Hands t;
  const Pattern first = t.pat();
  open_setup(t);
  t.shift_tap(Button::F3);
  CHECK(t.selected() == 1);
  CHECK(std::strcmp(t.title(), "PATTERN SETUP") == 0);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == first.steps[i].note);
    CHECK(t.pat().steps[i].flags == first.steps[i].flags);
  }
  t.shift_tap(Button::F8);  // one undo empties the slot again
  CHECK(pattern_is_empty(bank().pattern(1)));
  // The last pattern has no slot after it: nothing wraps round to overwrite A01.
  t.select_pattern(kPatternCount - 1);
  t.shift_tap(Button::F3);
  CHECK(t.selected() == kPatternCount - 1);
  CHECK_FALSE(pattern_is_empty(bank().pattern(0)));
  // Playing, it queues the copy.
  t.select_pattern(2);
  t.tap(Button::Play);
  t.wait(100);
  t.shift_tap(Button::F3);
  t.wait(30);
  CHECK(t.selected() == 2);  // still the playing one until it ends
  t.tap(Button::Play);
}

TEST_CASE("CLEAR LOCKS clears every lane of this pattern and keeps the notes, in one UNDO") {
  Hands t;
  make_locks(t);
  const Pattern before = t.pat();
  bank().clear_history();
  open_setup(t);
  t.shift_tap(Button::F4);
  t.wait(40);
  CHECK(std::strcmp(t.title(), "PATTERN SETUP") == 0);
  CHECK_FALSE(pattern_has_motion(t.pat(), kMotionLanes));
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == before.steps[i].note);
    CHECK(t.pat().steps[i].flags == before.steps[i].flags);
  }
  t.shift_tap(Button::F8);  // UNDO: both lanes are back
  CHECK(seq::lane_has_motion(t.pat(), 0));
  CHECK(seq::lane_has_motion(t.pat(), 1));
  CHECK_FALSE(bank().can_undo());
}

TEST_CASE("the free keys of SETUP's Shift row do nothing, and the bridges of H1 are gone: Pattern + Insert / Delete / Copy change no pattern") {
  Hands t;
  make_locks(t);
  const Pattern before = t.pat();
  bank().clear_history();
  open_setup(t);
  t.shift_tap(Button::F5);
  t.shift_tap(Button::F6);
  t.shift_tap(Button::F7);
  CHECK_FALSE(bank().can_undo());
  CHECK(t.pat().length == before.length);
  // On both pages: Pattern held with Insert, Delete (and Shift + Delete) and Copy does nothing to any pattern.
  for (int page = 0; page < 2; ++page) {
    t.press(Button::Pattern);
    t.tap(Button::Insert);
    t.tap(Button::Delete);
    t.press(Button::Shift);
    t.tap(Button::Delete);
    t.release(Button::Shift);
    t.tap(Button::Copy);
    t.release(Button::Pattern);
    t.wait(40);
    CHECK_FALSE(bank().can_undo());
    CHECK(t.selected() == 0);
    CHECK(seq::lane_has_motion(t.pat(), 0));
    CHECK(seq::lane_has_motion(t.pat(), 1));
    for (unsigned i = 0; i < 16; ++i) {
      CHECK(t.pat().steps[i].note == before.steps[i].note);
      CHECK(t.pat().steps[i].flags == before.steps[i].flags);
    }
    CHECK(pattern_is_empty(bank().pattern(1)));
    t.tap(Button::Pattern);  // the other page (a chord was no tap, so the page was where it was)
  }
}

TEST_CASE("a lock on HOME works while the encoder owns a SETUP knob only through HOME: a held step + turn on SETUP changes no lock") {
  Hands t;
  open_setup(t);
  const Pattern before = t.pat();
  t.down(step_pad_of(3));
  t.turn(5);  // F1 LENGTH is the owner: no lane drives it
  t.up(step_pad_of(3));
  CHECK_FALSE(pattern_has_motion(t.pat(), kMotionLanes));
  CHECK(t.pat().steps[3].note == before.steps[3].note);
}

TEST_CASE("the drawer on SETUP is the same one: a step pad still toggles, the playing line shows, and every frame fits the display list") {
  Hands t;
  open_setup(t);
  t.tap(Button::Play);
  t.wait(1200);
  for (unsigned k = 0; k < 16; ++k) {
    t.wait(100);
    CHECK(t.h->display().peak_frame_words() < 1800u);
  }
  t.press(Button::Shift);
  t.wait(700);  // the Shift row is up
  CHECK(t.h->display().peak_frame_words() < 1800u);
  t.release(Button::Shift);
  t.press(Button::Pattern);
  t.wait(350);
  CHECK(t.h->display().peak_frame_words() < 1800u);
  t.release(Button::Pattern);
  t.tap(Button::Play);
  CHECK(t.h->runtime().frame_failures() == 0u);
}

TEST_CASE("the scale field of the info lines: the name, its first three letters when the strip is full, and \"--\" until a scale locks the keyboard") {
  char out[24];
  scale_value_text(18, true, false, out, sizeof out);
  CHECK(std::strcmp(out, "PENTA MIN") == 0);
  scale_value_text(18, true, true, out, sizeof out);
  CHECK(std::strcmp(out, "PEN") == 0);
  scale_value_text(3, true, false, out, sizeof out);
  CHECK(std::strcmp(out, "MAJOR") == 0);
  scale_value_text(3, true, true, out, sizeof out);
  CHECK(std::strcmp(out, "MAJ") == 0);
  // Not locked (the generator's default scale is not a choice): "--", whatever the scale is, in either form.
  scale_value_text(18, false, false, out, sizeof out);
  CHECK(std::strcmp(out, "--") == 0);
  scale_value_text(18, false, true, out, sizeof out);
  CHECK(std::strcmp(out, "--") == 0);
  // The table pads a name to its field: no trailing space.
  for (unsigned i = 0; i < o3sdk::core::kScaleCount; ++i) {
    scale_value_text(i, true, false, out, sizeof out);
    CHECK(out[0] != '\0');
    CHECK(out[std::strlen(out) - 1u] != ' ');
  }
}

TEST_CASE("HOME with Major locked, a pattern queued and a step held with a lock draws and fits (the scale field takes its short form when the strip is full)") {
  Hands t;
  open_setup(t);
  t.tap(Button::F5);
  t.turn(-40);
  t.turn(3);  // Major
  t.tap(Button::Pattern);
  REQUIRE(std::strcmp(t.title(), "STEPS") == 0);
  t.tap(Button::Play);
  t.wait(200);
  t.press(Button::Pattern);
  t.wait(350);
  t.pad(3);  // queued
  t.release(Button::Pattern);
  t.down(step_pad_of(4));
  t.turn(30);  // a lock on the latched CUTOFF: the STEP box says it
  t.wait(100);
  CHECK(t.h->display().peak_frame_words() < 1800u);
  CHECK(t.h->runtime().frame_failures() == 0u);
  t.up(step_pad_of(4));
  t.tap(Button::Play);
}

TEST_CASE("all eight lanes in use: the toast names CLEAR LOCKS (SETUP), the one gesture that frees them until H3") {
  CHECK(std::strcmp(kLanesUsedToast, "8 LANES USED: CLEAR LOCKS (SETUP)") == 0);
  Hands t;
  // Eight lanes held by the distortion's parameters, each with a lock: no lane is free for CUTOFF.
  for (unsigned id = 0; id < 8; ++id) {
    REQUIRE(bank().lock_step(t.selected(), kLaneDist, static_cast<std::uint8_t>(id), 2, 100) >= 0);
  }
  t.tap(Button::F1);  // CUTOFF
  t.down(step_pad_of(5));
  t.turn(5);
  t.up(step_pad_of(5));
  t.wait(10);
  frame::StatusView v;
  static_cast<MainScreen*>(t.h->runtime().context().screen())->home_map().status(v);
  CHECK(std::strcmp(v.toast, kLanesUsedToast) == 0);
  CHECK(v.toast_off);  // it says something did not happen
  // CLEAR LOCKS on SETUP frees them.
  t.tap(Button::Pattern);
  t.shift_tap(Button::F4);
  CHECK_FALSE(pattern_has_motion(t.pat(), kMotionLanes));
}

TEST_CASE("Master's tap latches TEMPO; leaving the screen and coming back gives the encoder to the page again") {
  Hands t;
  t.tap(Button::Master);
  const auto tempo = t.h->engine().tempo();
  t.turn(4);
  REQUIRE(t.h->engine().tempo() != tempo);  // the latch is on TEMPO
  const auto tempo1 = t.h->engine().tempo();
  t.to_perform();
  t.tap(Button::Pattern);  // back on HOME
  REQUIRE(std::strcmp(t.title(), "STEPS") == 0);
  const float cutoff = t.param(kVoice, AcidVoice::Cutoff);
  t.turn(4);
  CHECK(t.h->engine().tempo() == tempo1);                    // not the tempo
  CHECK(t.param(kVoice, AcidVoice::Cutoff) != cutoff);       // the page's F1 CUTOFF
}
