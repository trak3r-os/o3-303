// SPDX-License-Identifier: GPL-3.0-or-later
// The scale on the keyboard (docs/M3B-PLAN.md R8, changed by S3 of docs/REVIEW-303.md): the scale shown on F5 always lights the keys (the scale mid, the root full,
// the others dark) and every key plays and writes, whatever the scale is. Config's KEYS: IN SCALE (key 8, ALL by default) adds the old lock: the pads out of the
// scale do nothing (no note, no entry). "No scale" and "Chromatic" light nothing and lock nothing.
#include <doctest/doctest.h>

#include "hands.h"

#include <o3sdk/app/pad_leds.h>
#include <o3_303/app_access.h>
#include <o3_303/config_keys.h>
#include <o3_303/pad_map.h>
#include <o3_303/perform.h>

using namespace hands;
using namespace o3_303;

namespace {

// PATTERN SETUP's F5 is Scale (Hands::choose_scale): 0 No scale, 1 Chromatic, 2 Minor, 3 Major ... 18 Penta Min (the default), 39. The encoder moves a
// place a detent (there are 40): down to the first, then up to `to`.
void choose_scale(Hands& t, int to) { t.choose_scale(to); }

void keys_in_scale(Hands& t, bool on) { t.config_set(config::kKeys, on ? 1.0f : 0.0f); }

}  // namespace

TEST_CASE("at boot every pad plays and the default scale (C minor pentatonic) lights the keys: the root full, the scale mid, the others dark") {
  Hands t;
  t.tap(Button::Rec);
  for (unsigned pc = 0; pc < 12; ++pc) {
    t.pad(key_pad(36 + pc), 10);
    CHECK(t.pat().steps[pc].note == 36 + pc);  // every key writes: KEYS is ALL
  }
  t.wait(40);
  CHECK(t.led(key_pad(48)) == app::kLedOn);   // C, the root
  CHECK(t.led(key_pad(51)) == app::kLedMid);  // Eb
  CHECK(t.led(key_pad(53)) == app::kLedMid);  // F
  CHECK(t.led(key_pad(50)) == app::kLedOff);  // D, a natural out of the scale: dark (and still plays)
  CHECK(t.led(key_pad(49)) == app::kLedOff);  // C#
}

TEST_CASE("with Major chosen and KEYS: ALL the scale only lights the keys: every pad plays and writes") {
  Hands t;
  choose_scale(t, 3);
  t.tap(Button::Rec);
  const bool in[12] = {true, false, true, false, true, true, false, true, false, true, false, true};
  for (unsigned pc = 0; pc < 12; ++pc) {
    t.pad(key_pad(36 + pc), 10);
    CHECK(t.pat().steps[pc].note == 36 + pc);
  }
  t.wait(40);
  for (unsigned pc = 0; pc < 12; ++pc) {
    CHECK(t.led(key_pad(48 + pc)) == (!in[pc] ? app::kLedOff : (pc == 0 ? app::kLedOn : app::kLedMid)));
  }
}

TEST_CASE("KEYS: IN SCALE with Major: the pads of C D E F G A B play and the others are dark and do nothing") {
  Hands t;
  keys_in_scale(t, true);
  choose_scale(t, 3);
  const Pattern before = t.pat();
  t.tap(Button::Rec);
  // REC + a key writes the cursor step and advances; a skipped pad writes nothing and does not advance.
  const bool in[12] = {true, false, true, false, true, true, false, true, false, true, false, true};
  unsigned written = 0;
  for (unsigned pc = 0; pc < 12; ++pc) {
    t.pad(key_pad(36 + pc), 10);
    if (in[pc]) {
      CHECK(t.pat().steps[written].note == 36 + pc);
      ++written;
    }
  }
  CHECK(written == 7);
  CHECK(t.pat().steps[7].note == before.steps[7].note);  // the skipped notes were not written, and the cursor did not run on
  t.wait(40);
  for (unsigned pc = 0; pc < 12; ++pc) {
    if (!in[pc]) {
      CHECK(t.led(key_pad(36 + pc)) == app::kLedOff);
      CHECK(t.led(key_pad(48 + pc)) == app::kLedOff);
    } else {
      CHECK(t.led(key_pad(48 + pc)) == (pc == 0 ? app::kLedOn : app::kLedMid));
    }
  }
}

TEST_CASE("KEYS: IN SCALE with C minor pentatonic: a natural out of it is dark and skipped, a sharp in it is lit and written") {
  Hands t;
  keys_in_scale(t, true);
  choose_scale(t, 18);
  t.wait(40);
  CHECK(t.led(key_pad(39)) == app::kLedMid);  // Eb
  CHECK(t.led(key_pad(46)) == app::kLedMid);  // Bb
  CHECK(t.led(key_pad(37)) == app::kLedOff);  // C#
  CHECK(t.led(key_pad(38)) == app::kLedOff);  // D
  CHECK(t.led(key_pad(40)) == app::kLedOff);  // E
  CHECK(t.led(key_pad(45)) == app::kLedOff);  // A
  CHECK(t.led(key_pad(47)) == app::kLedOff);  // B
  CHECK(t.led(key_pad(41)) == app::kLedMid);  // F
  CHECK(t.led(key_pad(43)) == app::kLedMid);  // G
  t.tap(Button::Rec);
  t.pad(key_pad(38), 10);  // D: skipped
  CHECK(t.pat().steps[0].note != 38);
  t.pad(key_pad(41), 10);  // F: written on step 0
  CHECK(t.pat().steps[0].note == 41);
  t.pad(key_pad(39), 10);  // Eb: a sharp in the scale: written on step 1
  CHECK(t.pat().steps[1].note == 39);
}

TEST_CASE("KEYS: IN SCALE: a skipped pad plays no sound and a hold-a-step key does not write the step") {
  Hands t;
  keys_in_scale(t, true);
  choose_scale(t, 3);  // C major
  const Pattern before = t.pat();
  t.down(step_pad_of(4));
  t.pad(key_pad(37), 10);  // C#: out
  CHECK(t.pat().steps[4].note == before.steps[4].note);
  t.pad(key_pad(38), 10);  // D: in
  CHECK(t.pat().steps[4].note == 38);
  t.up(step_pad_of(4));
  // A skipped pad does not transpose the running pattern either (TRNSP on).
  t.toggle_trnsp();
  t.tap(Button::Play);
  t.wait(200);
  t.pad(key_pad(37), 10);  // C#: out
  t.wait(40);
  CHECK(app_sequencer().live_transpose() == 0);
  t.pad(key_pad(43), 10);  // G: in, seven semitones over the bottom left C
  t.wait(40);
  CHECK(app_sequencer().live_transpose() == 7);
  t.tap(Button::Play);
}

TEST_CASE("KEYS: ALL: a pad out of the scale transposes the running pattern too (TRNSP on)") {
  Hands t;
  choose_scale(t, 3);  // C major
  t.toggle_trnsp();
  t.tap(Button::Play);
  t.wait(200);
  t.pad(key_pad(37), 10);  // C#: out of the scale, still a key
  t.wait(40);
  CHECK(app_sequencer().live_transpose() == 1);
  t.tap(Button::Play);
}

TEST_CASE("No scale and Chromatic light nothing and lock nothing, with KEYS: IN SCALE too") {
  Hands t;
  keys_in_scale(t, true);
  choose_scale(t, 3);
  t.tap(Button::Rec);
  t.pad(key_pad(37), 10);
  CHECK(t.pat().steps[0].note != 37);
  choose_scale(t, 1);  // Chromatic
  t.pad(key_pad(37), 10);
  CHECK(t.pat().steps[0].note == 37);
  choose_scale(t, 3);
  t.pad(key_pad(38), 10);  // D: step 1
  t.pad(key_pad(39), 10);  // Eb: out of C major again, skipped
  t.pad(key_pad(40), 10);  // E: lands on step 2, so the Eb did not advance the cursor
  CHECK(t.pat().steps[1].note == 38);
  CHECK(t.pat().steps[2].note == 40);
  choose_scale(t, 0);  // No scale
  t.pad(key_pad(39), 10);
  CHECK(t.pat().steps[3].note == 39);
  t.wait(40);
  CHECK(t.led(key_pad(38)) == app::kLedDim);  // the naturals dim, the sharps off, as a piano drawn with one colour
  CHECK(t.led(key_pad(37)) == app::kLedOff);
}

TEST_CASE("the scale's lamps follow the keyboard's octave and end with the run") {
  Hands t;
  choose_scale(t, 3);
  t.pad(role_pad(PadRole::OctUp), 10);
  t.wait(40);
  // An octave up the pads are the same pitch classes.
  CHECK(t.led(key_pad(36 + 1)) == app::kLedOff);
  CHECK(t.led(key_pad(36 + 2)) == app::kLedMid);
  CHECK(t.led(key_pad(36 + 4)) == app::kLedMid);
  Hands again;  // a new run: KEYS is ALL
  again.tap(Button::Rec);
  again.pad(key_pad(37), 10);
  CHECK(again.pat().steps[0].note == 37);
}
