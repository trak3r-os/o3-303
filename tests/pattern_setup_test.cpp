// SPDX-License-Identifier: GPL-3.0-or-later
// PATTERN SETUP (docs/UX-303.md 5.2, slice H2) end to end: a tap of Pattern opens it and a tap closes it, F1 LENGTH is one undo each way, F5 CHANCE is how many steps play,
// F6..F8 are the generator's settings (project-wide), the Shift row (COPY PAT, PASTE PAT, DUP>NEXT, CLEAR LOCKS, REVERSE, RANDOM, UNDO)
// replaces the bridges of H1 (Pattern held + Insert / Delete), and every one of today's functions stays reachable. HOME's NOTE and SCALE (G1) are here too.
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
  t.turn(16);
  REQUIRE(t.pat().length == 32);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[16 + i].note == before.steps[i].note);
    CHECK(t.pat().steps[16 + i].flags == before.steps[i].flags);
  }
  // The turns of one gesture are one UNDO step (I1: a step a detent): UNDO is on the Shift row of SETUP (F8), back to the 16 the line started with.
  t.turn(-24);  // 8, the same gesture
  REQUIRE(t.pat().length == 8);
  CHECK(t.pat().steps[10].flags == kStepRest);  // cut
  t.shift_tap(Button::F8);
  CHECK(t.pat().length == 16);
  CHECK_FALSE(bank().can_undo());
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == before.steps[i].note);
    CHECK(t.pat().steps[i].flags == before.steps[i].flags);
  }
  CHECK(t.selected() == sel);
  // The undo ends the gesture: a turn after it is a step of its own (32 again, then the 16).
  t.turn(16);
  REQUIRE(t.pat().length == 32);
  t.shift_tap(Button::F8);
  CHECK(t.pat().length == 16);
  // The redo walks forward again: Shift + F7 on SETUP itself (U4), and SETUP stays up.
  t.shift_tap(Button::F7);
  CHECK(std::strcmp(t.title(), "PATTERN SETUP") == 0);
  CHECK(t.pat().length == 32);
}

TEST_CASE("LENGTH up to 64 and down: the cursor and the step pads follow the new length") {
  Hands t;
  open_setup(t);
  t.turn(48);  // 64, a step a detent
  REQUIRE(t.pat().length == 64);
  t.tap(Button::Pattern);  // HOME
  t.tap(Button::Left);     // the cursor wraps to step 64: page 4
  t.wait(30);
  t.tap(Button::Pattern);  // SETUP
  t.turn(-56);             // 8: the cursor is past the end, it is brought back
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
  CHECK(t.pat().length == 17);
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

TEST_CASE("HOME's SCALE lights the keyboard; every pad still plays and writes (KEYS: ALL, S3)") {
  Hands t;
  t.choose_scale(3);  // Major
  CHECK(t.led(key_pad(38)) == app::kLedMid);  // D is in C major: mid
  CHECK(t.led(key_pad(36)) == app::kLedOn);   // the root (C) is full
  CHECK(t.led(key_pad(37)) == app::kLedOff);
  CHECK(t.led(key_pad(39)) == app::kLedOff);
  // REC + keys: every pad writes and advances, in or out of the scale.
  t.tap(Button::Rec);
  t.pad(key_pad(37), 10);
  t.pad(key_pad(38), 10);
  t.pad(key_pad(39), 10);
  t.pad(key_pad(40), 10);
  CHECK(t.pat().steps[0].note == 37);
  CHECK(t.pat().steps[1].note == 38);
  CHECK(t.pat().steps[2].note == 39);
  CHECK(t.pat().steps[3].note == 40);
  t.tap(Button::Rec);
  // "No scale" lights the naturals as before.
  t.choose_scale(0);
  CHECK(t.led(key_pad(38)) == app::kLedDim);
}

TEST_CASE("F5 CHANCE: how many steps play, per pattern, 1 to 100, one UNDO a gesture (G2)") {
  Hands t;
  const unsigned sel = t.selected();
  REQUIRE(t.pat().chance == kDefaultChance);
  bank().clear_history();
  open_setup(t);
  t.tap(Button::F5);
  t.turn(-20);  // 100 down to 80 (a detent a percent)
  CHECK(t.pat().chance == 80);
  t.turn(-30);
  CHECK(t.pat().chance == 50);  // the same gesture
  t.turn(-100);
  CHECK(t.pat().chance == kMinChance);  // no wrap, never 0
  t.turn(100);
  CHECK(t.pat().chance == kMaxChance);  // no wrap above
  t.turn(-50);
  CHECK(t.pat().chance == 50);
  // Another pattern keeps its own.
  CHECK(bank().pattern((sel + 1u) % 48u).chance == kDefaultChance);
  // One gesture is one UNDO step, back to 100 (the turn that came before the pause shares the key).
  t.shift_tap(Button::F8);
  CHECK(t.pat().chance == kDefaultChance);
  CHECK_FALSE(bank().can_undo());
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
  const auto generate = [&] { t.new_line(); };  // SETUP's Shift + F6 RANDOM, with the project's generator settings
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

TEST_CASE("REDO with nothing to redo does nothing, and the bridges of H1 are gone: Pattern + Insert / Delete / Copy change no pattern") {
  Hands t;
  make_locks(t);
  const Pattern before = t.pat();
  bank().clear_history();
  open_setup(t);
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

TEST_CASE("NOTE and the drawer rows share one encoder: a row latched before Shift + F5 gives way to NOTE, and a row latched after it takes it") {
  Hands t;
  t.choose_scale(3);  // Major, back on HOME
  const auto root_on = [&](unsigned note) {
    t.wait(40);
    return t.led(key_pad(note)) == app::kLedOn;
  };
  t.row_key(Button::Instr, 1);  // the GATE row owns the encoder
  t.shift_tap(Button::F5);  // NOTE takes it back
  const std::uint8_t gate = t.pat().steps[1].flags;
  t.turn(2);
  CHECK(t.pat().steps[1].flags == gate);  // the row did not move
  CHECK(root_on(38));
  t.shift_tap(Button::F5);  // NOTE again, then a row latches: the row owns the turn
  t.row_key(Button::Instr, 1);
  t.turn(2);
  CHECK(t.pat().steps[1].flags != gate);
  CHECK(root_on(38));  // the root stayed
}

TEST_CASE("NOTE and SCALE do not outlive the screen: leaving HOME for MASTER and coming back gives the encoder to the page") {
  Hands t;
  t.shift_tap(Button::F5);  // NOTE
  t.tap(Button::Master);
  t.wait(40);
  t.tap(Button::Pattern);  // back: HOME
  t.wait(40);
  REQUIRE(std::strcmp(t.title(), "STEPS") == 0);
  const float cutoff = t.param(kVoice, AcidVoice::Cutoff);
  t.turn(2);  // the page's own knob (F1 CUTOFF), not the root
  CHECK(t.param(kVoice, AcidVoice::Cutoff) != cutoff);
  CHECK(t.led(key_pad(38)) != app::kLedOn);  // D did not become the root
  MainScreen* home = static_cast<MainScreen*>(t.h->runtime().context().screen());
  t.shift_tap(Button::F6);  // SCALE
  CHECK(home->enc_owner() == MainScreen::EncOwner::Scale);
  t.tap(Button::Master);
  t.wait(40);
  t.tap(Button::Pattern);
  t.wait(40);
  CHECK(home->enc_owner() == MainScreen::EncOwner::None);
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

TEST_CASE("the scale field of the info lines: the name, and its first three letters when the strip is full") {
  char out[24];
  scale_value_text(18, false, out, sizeof out);
  CHECK(std::strcmp(out, "PENTA MIN") == 0);
  scale_value_text(18, true, out, sizeof out);
  CHECK(std::strcmp(out, "PEN") == 0);
  scale_value_text(3, false, out, sizeof out);
  CHECK(std::strcmp(out, "MAJOR") == 0);
  scale_value_text(3, true, out, sizeof out);
  CHECK(std::strcmp(out, "MAJ") == 0);
  // The table pads a name to its field: no trailing space.
  for (unsigned i = 0; i < o3sdk::core::kScaleCount; ++i) {
    scale_value_text(i, false, out, sizeof out);
    CHECK(out[0] != '\0');
    CHECK(out[std::strlen(out) - 1u] != ' ');
  }
}

TEST_CASE("HOME with Major locked, a pattern queued and a step held with a lock draws and fits (the scale field takes its short form when the strip is full)") {
  Hands t;
  t.choose_scale(3);  // Major
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

TEST_CASE("HOME's Shift + F5 NOTE gives the encoder the generator's root (C to B, no wrap); an F key gives it back; SCALE shows its root") {
  Hands t;
  t.choose_scale(3);  // Major, back on HOME
  // The root is read off the keys of HOME: the root's pad is full, the others of the scale mid.
  const auto root_on = [&](unsigned note) {
    t.wait(40);
    return t.led(key_pad(note)) == app::kLedOn;
  };
  CHECK(root_on(36));  // C by default
  t.shift_tap(Button::F5);  // NOTE
  t.turn(2);
  CHECK(root_on(38));  // D
  t.turn(-9);
  CHECK(root_on(36));  // no wrap below C
  t.turn(40);
  CHECK(root_on(47));  // B, and none above it
  t.turn(1);
  CHECK(root_on(47));
  t.tap(Button::F5);  // F5 alone: the encoder is the F key's knob again
  const MainScreen* home = static_cast<MainScreen*>(t.h->runtime().context().screen());
  CHECK(home->enc_owner() == MainScreen::EncOwner::None);
  t.turn(1);
  CHECK(root_on(47));  // the root stayed
  // The scale text carries the root beside the name.
  char sc[24];
  scale_value_text(3, false, sc, sizeof sc, 2);
  CHECK(std::strncmp(sc, "D ", 2) == 0);
  scale_value_text(3, true, sc, sizeof sc, 10);
  CHECK(std::strncmp(sc, "A# ", 3) == 0);
  CHECK(std::strlen(sc) == 6);  // "A# " + three letters
  scale_value_text(0, false, sc, sizeof sc, 2);  // "No scale" and "Chromatic" have no root
  CHECK(std::strcmp(sc, "NO SCALE") == 0);
  scale_value_text(1, true, sc, sizeof sc, 2);
  CHECK(std::strcmp(sc, "CHR") == 0);
}

TEST_CASE("HOME's Shift + F6 SCALE gives the encoder the generator's scale (no wrap); the status line says ROOT or SCALE; an F key gives it back") {
  Hands t;
  MainScreen* home = static_cast<MainScreen*>(t.h->runtime().context().screen());
  // The scale is read off the keys of HOME: C major lights C (full), D E F G A B (mid) and leaves the sharps dark.
  const auto major = [&] {
    t.wait(40);
    bool ok = t.led(key_pad(48)) == app::kLedOn && t.led(key_pad(50)) == app::kLedMid && t.led(key_pad(49)) == app::kLedOff;
    return ok && t.led(key_pad(52)) == app::kLedMid && t.led(key_pad(51)) == app::kLedOff;
  };
  CHECK_FALSE(major());  // C minor pentatonic by default
  t.shift_tap(Button::F6);  // SCALE
  CHECK(home->enc_owner() == MainScreen::EncOwner::Scale);
  t.turn(-40);  // No scale: the first, no wrap
  CHECK_FALSE(home->scale_shown());
  t.turn(-5);
  CHECK_FALSE(home->scale_shown());
  t.turn(3);  // Major (0 No scale, 1 Chromatic, 2 Minor, 3 Major)
  CHECK(major());
  // SCALE owns the encoder: the page's CUTOFF does not move.
  const float cutoff = t.param(kVoice, AcidVoice::Cutoff);
  t.turn(1);
  CHECK(t.param(kVoice, AcidVoice::Cutoff) == cutoff);
  t.turn(-1);
  CHECK(major());
  // NOTE then takes the encoder: the root, not the scale.
  t.shift_tap(Button::F5);
  CHECK(home->enc_owner() == MainScreen::EncOwner::Root);
  t.turn(2);
  CHECK_FALSE(major());  // the root moved to D: C is no longer the full key
  CHECK(t.led(key_pad(50)) == app::kLedOn);
  // An F key (a plain tap) gives the encoder back to the knob.
  t.tap(Button::F2);
  CHECK(home->enc_owner() == MainScreen::EncOwner::None);
  const float reso = t.param(kVoice, AcidVoice::Reso);
  t.turn(3);
  CHECK(t.param(kVoice, AcidVoice::Reso) != reso);
  CHECK(t.led(key_pad(50)) == app::kLedOn);  // the root stayed
}

TEST_CASE("HOME: Shift + F5 held and a tap of a bottom-row key sets the root; SETUP's F5 is CHANCE and sets none; without them the key is no root (X4, G1, G2)") {
  Hands t;
  t.choose_scale(3);  // Major: the keyboard shows the root on the pads
  const auto root_on = [&](unsigned note) {
    t.wait(40);
    return t.led(key_pad(note)) == app::kLedOn;
  };
  REQUIRE(root_on(36));  // C by default
  t.press(Button::Shift);
  t.press(Button::F5);
  t.pad(key_pad(39));  // D#
  t.release(Button::F5);
  t.release(Button::Shift);
  CHECK(root_on(39));
  CHECK(!root_on(36));
  t.press(Button::Shift);
  t.press(Button::F5);
  t.pad(key_pad(47));  // B: the last key of the row
  t.release(Button::F5);
  t.release(Button::Shift);
  CHECK(root_on(47));
  t.pad(key_pad(38));  // no F key held: a key of the keyboard, not a root
  CHECK(root_on(47));
  t.press(Button::Shift);
  t.press(Button::F5);
  t.pad(key_pad(52));  // a key of the row above: not one of the twelve
  t.release(Button::F5);
  t.release(Button::Shift);
  CHECK(root_on(47));
  t.press(Button::F1);  // another F key held: no root either
  t.pad(key_pad(40));
  t.release(Button::F1);
  CHECK(root_on(47));
  // PATTERN SETUP: F5 is CHANCE (G2); held with a key it sets no root.
  open_setup(t);
  t.press(Button::F5);
  t.pad(key_pad(41));
  t.release(Button::F5);
  t.tap(Button::Pattern);  // HOME
  CHECK(root_on(47));
  // The encoder is the knob's again (the one more scale and the root stay): Shift + F5 held and F is the root, the encoder stays NOTE's.
  t.press(Button::Shift);
  t.press(Button::F5);
  t.pad(key_pad(41));  // F
  t.release(Button::F5);
  t.release(Button::Shift);
  t.wait(40);
  CHECK(t.led(key_pad(41)) == app::kLedOn);
  CHECK(t.led(key_pad(47)) != app::kLedOn);
  t.turn(-2);
  t.wait(40);
  CHECK(t.led(key_pad(39)) == app::kLedOn);  // D#: the encoder had NOTE
}

TEST_CASE("Left / Right with an F key held move that knob by 1/20 of its range and stop at the ends; without a key they move the cursor") {
  Hands t;
  const float v0 = t.param(kVoice, 0);  // F1 CUTOFF
  t.press(Button::F1);
  t.tap(Button::Right);
  const float v1 = t.param(kVoice, 0);
  CHECK(v1 > v0);
  t.tap(Button::Left);
  CHECK(t.param(kVoice, 0) == doctest::Approx(v0).epsilon(0.02));
  for (unsigned i = 0; i < 40; ++i) {
    t.tap(Button::Right);
  }
  const float top = t.param(kVoice, 0);
  t.tap(Button::Right);
  CHECK(t.param(kVoice, 0) == top);  // clamped
  for (unsigned i = 0; i < 40; ++i) {
    t.tap(Button::Left);
  }
  const float bottom = t.param(kVoice, 0);
  t.tap(Button::Left);
  CHECK(t.param(kVoice, 0) == bottom);
  CHECK(bottom < top);
  t.release(Button::F1);
  const Pattern before = t.pat();
  t.tap(Button::Right);  // no key held: the cursor, no knob
  CHECK(t.param(kVoice, 0) == bottom);
  CHECK(std::memcmp(&t.pat(), &before, sizeof(Pattern)) == 0);
}
