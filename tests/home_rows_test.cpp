// SPDX-License-Identifier: GPL-3.0-or-later
// The drawer rows (docs/UX-303.md 2.3, 5.1, 5.2, 5.5, decisions D2, D8, D22, D24; slice H3): on HOME, PATTERN SETUP and INSTRUMENT, Note / Instr / Vol / Fx
// latch the NOTE, GATE, PLAYS and LOCK rows on the encoder; a tap of an F key gives it back to the knob; Shift + the button is the button alone. The rows
// edit the held steps, else the cursor's: NOTE the semitone (Shift the octave, C1..C5), GATE the six-entry list, PLAYS the 17-entry list of D8 (Shift four
// places), LOCK the lock of the lock knob on the step (Shift coarse; below the minimum "--" removes it; a ninth lane gives the toast). The ENC text, the STEP
// box and the drawer's marks say it. The live actions of the buttons are PERFORM's (perf_buttons_test.cpp); the old OCT+ ratchet gesture is cut.
#include <doctest/doctest.h>

#include "hands.h"

#include "../app/src/home_map.h"
#include "../app/src/main_screen.h"

#include <o3_303/app_access.h>
#include <o3_303/seq/ops.h>
#include <o3_303/step_model.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/freeze/freeze.h>

#include <cstring>
#include <string>

using namespace hands;
using namespace o3_303;

namespace {

// A pattern of plain notes C2 + step, with a rest on step 7, so a row's edit is easy to see.
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

HomeMap& home_of(Hands& t) { return static_cast<MainScreen*>(t.h->runtime().context().screen())->home_map(); }

std::string enc(Hands& t, HomeMap& home) {
  char out[32];
  home.row_text(t.h->runtime().context(), out, sizeof out);
  return out;
}

std::string box(Hands& t, HomeMap& home) {
  char out[64];
  (void)home.step_box(t.h->runtime().context(), out, sizeof out);
  return out;
}

std::string toast_of(HomeMap& home) {
  frame::StatusView v;
  home.status(v);
  return v.toast;
}

// The lock point of step `s` on the lane that drives the voice's `id`, kNoLock when none.
std::uint8_t lock_of(Hands& t, unsigned id, unsigned s) {
  const int lane = seq::find_lane(t.pat(), kLaneVoice, static_cast<std::uint8_t>(id));
  return lane < 0 ? kNoLock : step_lock_point(t.pat(), static_cast<unsigned>(lane), s);
}

}  // namespace

TEST_CASE("Note, Instr, Vol and Fx latch the NOTE, GATE, PLAYS and LOCK rows; an F key gives the encoder back; Shift + the button is the button alone") {
  Hands t;
  HomeMap& home = home_of(t);
  CHECK(home.row() == DrawerRow::None);
  t.tap(Button::Note);
  CHECK(home.row() == DrawerRow::Note);
  CHECK(enc(t, home) == "NOTE");
  t.tap(Button::Instr);
  CHECK(home.row() == DrawerRow::Gate);
  CHECK(enc(t, home) == "GATE");
  t.tap(Button::Vol);
  CHECK(home.row() == DrawerRow::Plays);
  CHECK(enc(t, home) == "PLAYS");
  t.tap(Button::Fx);
  CHECK(home.row() == DrawerRow::Lock);
  CHECK(enc(t, home) == "LOCK CUTOFF");  // the latched knob at boot: F1 CUTOFF
  t.tap(Button::Fx);                     // again: it stays
  CHECK(home.row() == DrawerRow::Lock);
  t.tap(Button::F2);
  CHECK(home.row() == DrawerRow::None);
  // Shift + a button is the button alone (D22), a held button too: no live action starts on this screen.
  t.shift_tap(Button::Vol);
  CHECK(home.row() == DrawerRow::Plays);
  t.shift_tap(Button::Fx);
  CHECK(home.row() == DrawerRow::Lock);
  CHECK(enc(t, home) == "LOCK RESO");  // the knob F2 latched
  CHECK(t.param(kFreeze, Freeze::Active) == 0.0f);
  CHECK(app_sequencer().perf() == 0);
  // Shift + F is a tool, not a latch of a knob: the row stays.
  t.shift_tap(Button::F8);
  CHECK(home.row() == DrawerRow::Lock);
  t.tap(Button::F8);
  CHECK(home.row() == DrawerRow::None);
}

TEST_CASE("the NOTE row: a turn is a semitone on the cursor step, Shift an octave, kept inside C1..C5; held steps move together and are not selected") {
  Hands t;
  line(t);
  HomeMap& home = home_of(t);
  t.pad(step_pad_of(4));  // the cursor on step 5 (C2 + 4 = 40)
  t.tap(Button::Note);
  t.turn(1);
  CHECK(t.pat().steps[4].note == 41);
  CHECK(toast_of(home) == "STEP 5 NOTE F2");
  t.turn(-3);
  CHECK(t.pat().steps[4].note == 38);
  t.press(Button::Shift);
  t.turn(1);
  t.release(Button::Shift);
  CHECK(t.pat().steps[4].note == 50);
  t.press(Button::Shift);
  t.turn(-2);
  t.release(Button::Shift);
  CHECK(t.pat().steps[4].note == 26);
  CHECK(t.pat().steps[3].note == 39);  // the others are as they were
  t.turn(-9);
  CHECK(t.pat().steps[4].note == static_cast<std::uint8_t>(kRowNoteLow));  // C1 is the floor
  t.turn(100);
  CHECK(t.pat().steps[4].note == static_cast<std::uint8_t>(kRowNoteHigh));  // C5 the ceiling
  // Two held steps move together, each from where it stands; the cursor stays on step 5.
  t.down(step_pad_of(1));
  t.down(step_pad_of(2));
  t.turn(-2);
  t.up(step_pad_of(1));
  t.up(step_pad_of(2));
  CHECK(t.pat().steps[1].note == 35);
  CHECK(t.pat().steps[2].note == 36);
  CHECK(home.cursor() == 4);
  // One undo takes a run of turns on a step back.
  CHECK(bank().can_undo());
}

TEST_CASE("the GATE row walks REST, NOTE, ACC, SLD, ACC+SLD, TIE on the cursor step and stops at the ends; the ratchet and the note stay") {
  Hands t;
  line(t);
  HomeMap& home = home_of(t);
  t.pad(step_pad_of(2));
  // Step 3 is a plain note with a ratchet: GATE leaves the ratchet alone.
  Pattern p = t.pat();
  p.steps[2].flags = with_ratchet(0, 2);
  bank().replace(t.selected(), p);
  t.tap(Button::Instr);
  t.turn(1);
  CHECK(seq::gate_list_index(t.pat().steps[2].flags) == seq::kGateAcc);
  CHECK((t.pat().steps[2].flags & kStepAccent) != 0);
  CHECK(ratchet_of(t.pat().steps[2].flags) == 2);
  CHECK(toast_of(home) == "STEP 3 GATE ACC");
  t.turn(1);
  CHECK(seq::gate_list_index(t.pat().steps[2].flags) == seq::kGateSld);
  t.turn(1);
  CHECK(seq::gate_list_index(t.pat().steps[2].flags) == seq::kGateAccSld);
  CHECK(toast_of(home) == "STEP 3 GATE ACC+SLD");
  t.turn(1);
  CHECK(seq::gate_list_index(t.pat().steps[2].flags) == seq::kGateTie);
  t.turn(5);
  CHECK(seq::gate_list_index(t.pat().steps[2].flags) == seq::kGateTie);  // the end
  t.turn(-9);
  CHECK(seq::gate_list_index(t.pat().steps[2].flags) == seq::kGateRest);
  CHECK(t.pat().steps[2].note == 38);
  CHECK(box(t, home) == "STEP 3: REST");
  t.turn(1);
  CHECK(box(t, home) == "STEP 3: D2 x3");
}

TEST_CASE("the PLAYS row walks the list of D8: x1..x4, 75 / 50 / 25 %, the ratios, FILL, !FILL, FIRST, !FIRST; one value at a time; Shift four places") {
  Hands t;
  line(t);
  HomeMap& home = home_of(t);
  t.pad(step_pad_of(3));
  t.tap(Button::Vol);
  t.turn(1);
  CHECK(seq::get_ratchet(t.pat(), 3) == 1);
  CHECK(toast_of(home) == "STEP 4 PLAYS x2");
  CHECK(box(t, home) == "STEP 4: D#2 x2");
  t.turn(3);
  CHECK(seq::get_cond(t.pat(), 3) == 75);
  CHECK(seq::get_ratchet(t.pat(), 3) == 0);  // setting a condition clears the ratchet
  CHECK(box(t, home) == "STEP 4: D#2 75%");
  t.turn(-4);
  CHECK(seq::get_cond(t.pat(), 3) == kCondAlways);
  CHECK(seq::get_ratchet(t.pat(), 3) == 0);
  t.turn(7);  // 1:2
  CHECK(seq::get_cond(t.pat(), 3) == cond_ratio(1, 2));
  CHECK(toast_of(home) == "STEP 4 PLAYS 1:2");
  t.press(Button::Shift);
  t.turn(1);  // four places on: from 1:2 (7) to 3:4 (11)
  t.release(Button::Shift);
  CHECK(seq::get_cond(t.pat(), 3) == cond_ratio(3, 4));
  t.turn(50);
  CHECK(seq::get_cond(t.pat(), 3) == kCondNotFirst);  // the end
  t.turn(-1);
  CHECK(seq::get_cond(t.pat(), 3) == kCondFirst);
  // A stray value (a probability off the list) is shown as itself and the first turn lands on the nearest entry.
  Pattern p = t.pat();
  p.steps[3].cond = 33;
  bank().replace(t.selected(), p);
  CHECK(box(t, home) == "STEP 4: D#2 33%");
  t.turn(1);
  CHECK(seq::get_cond(t.pat(), 3) == 25);  // 33 % is nearest 25 % on the list
  t.turn(-1);
  CHECK(seq::get_cond(t.pat(), 3) == 50);
}

TEST_CASE("the LOCK row locks the lock knob on the step, claims its lane, shows \"--\" when there is no lock and removes the lock turned below the minimum") {
  Hands t;
  line(t);
  HomeMap& home = home_of(t);
  t.tap(Button::F4);  // DECAY: the lock knob
  t.pad(step_pad_of(5));
  t.tap(Button::Fx);
  CHECK(enc(t, home) == "LOCK DECAY");
  CHECK(lock_of(t, AcidVoice::Decay, 5) == kNoLock);
  CHECK(box(t, home) == "STEP 6: F2");
  t.turn(-1);  // below the knob's own value: the lock is made
  const std::uint8_t pt = lock_of(t, AcidVoice::Decay, 5);
  REQUIRE(pt != kNoLock);
  CHECK(box(t, home).find("DECAY") != std::string::npos);
  CHECK(toast_of(home).find("STEP 6 LOCK DECAY") == 0);
  t.press(Button::Shift);
  t.turn(-1);  // coarse
  t.release(Button::Shift);
  const std::uint8_t coarse = lock_of(t, AcidVoice::Decay, 5);
  CHECK(coarse < pt);
  CHECK(pt - coarse > 1);
  t.turn(-200);
  CHECK(lock_of(t, AcidVoice::Decay, 5) == 0);  // the minimum is a lock
  t.turn(-1);
  CHECK(lock_of(t, AcidVoice::Decay, 5) == kNoLock);  // below it: "--", no lock
  CHECK(toast_of(home) == "STEP 6 LOCK DECAY --");
  CHECK(box(t, home) == "STEP 6: F2");
  CHECK_FALSE(seq::lane_has_motion(t.pat(), static_cast<unsigned>(seq::find_lane(t.pat(), kLaneVoice, AcidVoice::Decay))));
  t.turn(-1);  // from "--" a turn starts at the knob's own value, which is above its minimum: a lock again
  CHECK(lock_of(t, AcidVoice::Decay, 5) != kNoLock);
  CHECK(bank().undo() >= 0);  // undo takes it back
  CHECK(lock_of(t, AcidVoice::Decay, 5) == kNoLock);
}

TEST_CASE("a lock on a ninth parameter says 8 LANES USED, and removing a lock frees its lane") {
  Hands t;
  line(t);
  HomeMap& home = home_of(t);
  // Eight lanes held by the distortion's parameters, each with a lock: no lane is free for CUTOFF.
  for (unsigned id = 0; id < 8; ++id) {
    REQUIRE(bank().lock_step(t.selected(), kLaneDist, static_cast<std::uint8_t>(id), 2, 100) >= 0);
  }
  t.tap(Button::F1);  // CUTOFF
  t.pad(step_pad_of(5));
  t.tap(Button::Fx);
  t.turn(5);
  CHECK(toast_of(home) == kLanesUsedToast);
  CHECK(lock_of(t, AcidVoice::Cutoff, 5) == kNoLock);
  // The toast is the one of D2; removing the last lock of one parameter frees its lane (a lane is free when it has no points).
  REQUIRE(bank().unlock_step(t.selected(), kLaneDist, 0, 2));
  t.turn(5);
  CHECK(lock_of(t, AcidVoice::Cutoff, 5) != kNoLock);
  // The held-step gesture says the same: RESO has no lane either (seven are the distortion's, one is CUTOFF's).
  t.tap(Button::F2);
  t.down(step_pad_of(6));
  t.turn(5);
  t.up(step_pad_of(6));
  CHECK(toast_of(home) == kLanesUsedToast);
  CHECK(lock_of(t, AcidVoice::Reso, 6) == kNoLock);
}

TEST_CASE("a row turn on a held step edits that step and does not select it; the drawer, the PADS line and the knob strip say what the encoder owns") {
  Hands t;
  line(t);
  HomeMap& home = home_of(t);
  t.pad(step_pad_of(0));
  t.tap(Button::Instr);
  t.down(step_pad_of(6));
  t.turn(1);
  CHECK(home.drawer(t.h->runtime().context()).row == DrawerRow::Gate);
  char pads[160];
  home.pads_line(t.h->runtime().context(), pads, sizeof pads);
  CHECK(std::strstr(pads, "KEY = NOTE OF STEP 7") != nullptr);
  CHECK(std::strstr(pads, "TURN = GATE") != nullptr);
  t.up(step_pad_of(6));
  CHECK(seq::gate_list_index(t.pat().steps[6].flags) == seq::kGateAcc);
  CHECK(home.cursor() == 0);  // the turn used the hold: the release did not select the step
  // The drawer's LOCK row texts: the lock knob's lock on each step of the page, "" none.
  t.tap(Button::F1);
  t.down(step_pad_of(2));
  t.turn(10);
  t.up(step_pad_of(2));
  const view::Marks m = home.drawer(t.h->runtime().context());
  CHECK(m.lock[2][0] != '\0');
  CHECK(m.lock[3][0] == '\0');
  CHECK(std::strlen(m.lock[2]) < view::kLockChars);
}

TEST_CASE("the old gesture is cut: hold a step + OCT+ is the keyboard's octave and leaves the ratchet alone") {
  Hands t;
  line(t);
  HomeMap& home = home_of(t);
  t.down(step_pad_of(3));
  t.pad(role_pad(PadRole::OctUp));
  CHECK(home.octave() == 1);
  t.pad(role_pad(PadRole::OctDown));
  CHECK(home.octave() == 0);
  t.up(step_pad_of(3));
  CHECK(seq::get_ratchet(t.pat(), 3) == 0);
}

TEST_CASE("the rows work on PATTERN SETUP and on INSTRUMENT; the LOCK row keeps the last sound knob where the page's knobs cannot be locked") {
  Hands t;
  line(t);
  HomeMap& home = home_of(t);
  t.tap(Button::F4);  // DECAY on HOME
  // PATTERN SETUP: the same drawer, the same rows.
  t.tap(Button::Pattern);
  REQUIRE(std::strcmp(t.title(), "PATTERN SETUP") == 0);
  t.pad(step_pad_of(1));
  t.tap(Button::Instr);
  t.turn(2);
  CHECK(seq::gate_list_index(t.pat().steps[1].flags) == seq::kGateSld);
  t.tap(Button::Fx);
  CHECK(enc(t, home) == "LOCK DECAY");  // SETUP's own knobs (LENGTH ...) have no lane: the last sound knob
  t.turn(3);
  CHECK(lock_of(t, AcidVoice::Decay, 1) != kNoLock);
  t.tap(Button::F1);  // LENGTH: the knob has the encoder
  CHECK(home.row() == DrawerRow::None);
  t.tap(Button::Fx);
  t.tap(Button::Pattern);  // HOME again: the row survives the flip, as SETUP and HOME are one screen
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  CHECK(home.row() == DrawerRow::Lock);
  // INSTRUMENT: the page's latched knob is the lock knob; Params gives the encoder back.
  t.to_instrument(kInstFx);
  CHECK(home.row() == DrawerRow::None);  // a screen shown again: the encoder is the knob's
  t.tap(Button::F1);                     // DRIVE
  t.pad(step_pad_of(4));
  t.tap(Button::Vol);
  t.turn(2);
  CHECK(seq::get_ratchet(t.pat(), 4) == 2);
  t.tap(Button::Note);
  t.turn(1);
  CHECK(t.pat().steps[4].note == 41);
  t.tap(Button::Fx);
  CHECK(enc(t, home) == "LOCK DRIVE");
  t.turn(-2);
  const int lane = seq::find_lane(t.pat(), kLaneDist, Distortion::Drive);
  REQUIRE(lane >= 0);
  CHECK(step_lock_point(t.pat(), static_cast<unsigned>(lane), 4) != kNoLock);
  t.tap(Button::Params);
  CHECK(home.row() == DrawerRow::None);
  CHECK(t.h->display().peak_frame_words() < 1800u);
}

TEST_CASE("the Shift row does not take the encoder from a latched row; an F key gives it back to its knob (H3's release, kept by H4)") {
  Hands t;
  line(t);
  HomeMap& home = home_of(t);
  t.pad(step_pad_of(4));
  t.tap(Button::Note);
  REQUIRE(home.row() == DrawerRow::Note);
  t.shift_tap(Button::F5);  // REVERSE: an edit of the pattern, not an owner of the encoder
  CHECK(home.row() == DrawerRow::Note);
  const Pattern reversed = t.pat();
  t.turn(1);
  CHECK(t.pat().steps[4].note == reversed.steps[4].note + 1);  // the row still has the turn
  t.tap(Button::Fx);
  REQUIRE(home.row() == DrawerRow::Lock);
  t.tap(Button::F2);  // an F key: the knob has the encoder, the row is let go
  CHECK(home.row() == DrawerRow::None);
  const float reso = t.param(kVoice, AcidVoice::Reso);
  t.turn(3);
  CHECK(t.param(kVoice, AcidVoice::Reso) != reso);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == reversed.steps[i].note + (i == 4 ? 1 : 0));  // no note moved by it
  }
}

TEST_CASE("several held steps edited by one turn gesture are ONE undo step, and the history before it survives (NOTE, GATE, PLAYS, LOCK)") {
  Hands t;
  line(t);
  bank().edit(t.selected(), [](Pattern& p) { p.steps[10].note = 60; });  // an earlier edit, to be left standing
  const Pattern earlier = t.pat();
  const auto hold4 = [&] {
    for (unsigned k = 0; k < 4; ++k) {
      t.down(step_pad_of(k));
    }
  };
  const auto release4 = [&] {
    for (unsigned k = 0; k < 4; ++k) {
      t.up(step_pad_of(k));
    }
  };
  const auto same_as_earlier = [&] {
    for (unsigned i = 0; i < 16; ++i) {
      if (t.pat().steps[i].note != earlier.steps[i].note || t.pat().steps[i].flags != earlier.steps[i].flags || t.pat().steps[i].cond != earlier.steps[i].cond) {
        return false;
      }
    }
    return true;
  };
  // NOTE: forty detents over four steps (ten times the 32 entries a step each detent would take).
  t.tap(Button::Note);
  hold4();
  for (int i = 0; i < 40; ++i) {
    t.turn(1);
  }
  release4();
  CHECK(t.pat().steps[0].note == 72);
  CHECK_FALSE(same_as_earlier());
  CHECK(bank().undo() == static_cast<int>(t.selected()));
  CHECK(same_as_earlier());
  // GATE.
  t.tap(Button::Instr);
  hold4();
  for (int i = 0; i < 3; ++i) {
    t.turn(1);
    t.turn(-1);
    t.turn(2);
  }
  release4();
  CHECK_FALSE(same_as_earlier());
  CHECK(bank().undo() >= 0);
  CHECK(same_as_earlier());
  // PLAYS.
  t.tap(Button::Vol);
  hold4();
  for (int i = 0; i < 20; ++i) {
    t.turn(1);
  }
  release4();
  CHECK(seq::get_cond(t.pat(), 2) == kCondNotFirst);
  CHECK(bank().undo() >= 0);
  CHECK(same_as_earlier());
  // LOCK (the lane claim and four locks), by the row and by hold + turn of the knob.
  t.tap(Button::F4);
  t.tap(Button::Fx);
  hold4();
  for (int i = 0; i < 12; ++i) {
    t.turn(-1);
  }
  release4();
  for (unsigned k = 0; k < 4; ++k) {
    CHECK(lock_of(t, AcidVoice::Decay, k) != kNoLock);
  }
  CHECK(bank().undo() >= 0);
  for (unsigned k = 0; k < 4; ++k) {
    CHECK(lock_of(t, AcidVoice::Decay, k) == kNoLock);
  }
  t.tap(Button::F4);  // the knob has the encoder: hold + turn
  hold4();
  for (int i = 0; i < 12; ++i) {
    t.turn(1);
  }
  release4();
  for (unsigned k = 0; k < 4; ++k) {
    CHECK(lock_of(t, AcidVoice::Decay, k) != kNoLock);
  }
  CHECK(bank().undo() >= 0);
  for (unsigned k = 0; k < 4; ++k) {
    CHECK(lock_of(t, AcidVoice::Decay, k) == kNoLock);
  }
  // The earlier edit is the next undo: nothing was lost to the 32 entries of the history.
  CHECK(same_as_earlier());
  CHECK(t.pat().steps[10].note == 60);
  CHECK(bank().undo() >= 0);
  CHECK(t.pat().steps[10].note == 46);
}

TEST_CASE("the NOTE row never moves a note further out of C1..C5 and never jumps it; a note inside stays inside") {
  Hands t;
  line(t);
  Pattern p = t.pat();
  p.steps[2].note = 80;  // above C5
  p.steps[3].note = 20;  // below C1
  p.steps[4].note = 72;
  p.steps[5].note = 24;
  bank().replace(t.selected(), p);
  t.tap(Button::Note);
  const auto turn_step = [&](unsigned k, int d, bool shift = false) {
    t.pad(step_pad_of(k));  // the cursor
    if (shift) {
      t.press(Button::Shift);
    }
    t.turn(d);
    if (shift) {
      t.release(Button::Shift);
    }
  };
  turn_step(2, 1);  // further out: it stays
  CHECK(t.pat().steps[2].note == 80);
  turn_step(2, -1);  // toward the range by its own amount, no jump to C5
  CHECK(t.pat().steps[2].note == 79);
  turn_step(2, -3);
  CHECK(t.pat().steps[2].note == 76);
  turn_step(2, -1, true);  // an octave toward it lands inside
  CHECK(t.pat().steps[2].note == 64);
  turn_step(3, -1);
  CHECK(t.pat().steps[3].note == 20);
  turn_step(3, 1);
  CHECK(t.pat().steps[3].note == 21);
  turn_step(3, 1, true);
  CHECK(t.pat().steps[3].note == 33);
  turn_step(3, 100);  // inside: the ceiling
  CHECK(t.pat().steps[3].note == 72);
  turn_step(4, 1);  // the edges hold
  CHECK(t.pat().steps[4].note == 72);
  turn_step(5, -1);
  CHECK(t.pat().steps[5].note == 24);
  turn_step(5, 1);
  CHECK(t.pat().steps[5].note == 25);
}
