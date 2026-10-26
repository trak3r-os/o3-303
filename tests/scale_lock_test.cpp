// SPDX-License-Identifier: GPL-3.0-or-later
// The scale-locked keyboard (docs/M3B-PLAN.md R8): with a scale set, the keyboard pads out of it go dark and are skipped (a press does
// nothing: no note, no entry). A boot has the generator's default scale without it being a choice, so the keyboard is chromatic until the
// SEQ page's Scale is touched; "No scale" and "Chromatic" unlock it again.
#include <doctest/doctest.h>

#include "hands.h"

#include <o3sdk/app/pad_leds.h>
#include <o3_303/app_access.h>
#include <o3_303/pad_map.h>
#include <o3_303/perform.h>

using namespace hands;
using namespace o3_303;

namespace {

// The SEQ page's F6 is Scale: 0 No scale, 1 Chromatic, 2 Minor, 3 Major ... 18 Penta Min (the default), 39. The encoder moves a place a
// detent (there are 40): down to the first, then up to `to`. (A turn that leaves the value where it is, the default 18 chosen from 18, is
// no choice: the lock follows a choice the player made.)
void choose_scale(Hands& t, int to) {
  t.to_page(kPageSeq);
  t.tap(Button::F6);
  t.turn(-40);
  if (to != 0) {
    t.turn(to);
  }
  t.back_to_sound(kPageSeq);
  t.wait(40);
}

}  // namespace

TEST_CASE("at boot the keyboard is chromatic: every pad plays, whatever the generator's default scale is") {
  Hands t;
  t.tap(Button::Rec);
  for (unsigned pc = 0; pc < 12; ++pc) {
    t.pad(key_pad(36 + pc), 10);
    CHECK(t.pat().steps[pc].note == 36 + pc);
  }
  t.wait(40);
  CHECK(t.led(key_pad(37)) == app::kLedOff);  // sharps are dark as ever (a piano drawn with one colour) ...
  CHECK(t.led(key_pad(38)) == app::kLedDim);  // ... and the naturals dim
}

TEST_CASE("with Major chosen the pads of C D E F G A B play and the others are dark and do nothing") {
  Hands t;
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
  // The LEDs: with a scale chosen lit means playable: the pads of the scale are dim (the current note on), the others dark.
  for (unsigned pc = 0; pc < 12; ++pc) {
    if (!in[pc]) {
      CHECK(t.led(key_pad(36 + pc)) == app::kLedOff);
      CHECK(t.led(key_pad(48 + pc)) == app::kLedOff);
    } else {
      CHECK(t.led(key_pad(48 + pc)) == app::kLedDim);
    }
  }
}

TEST_CASE("a scale with a natural out of it (C minor pentatonic: C Eb F G Bb) darkens that natural") {
  Hands t;
  choose_scale(t, 18);  // the default's own index, chosen (down and up again): the keyboard locks to it
  t.wait(40);
  // D, E, A, B and the sharps C#, F#, G# are out; Eb, Bb are in: with a scale chosen lit means playable, so those two sharps are dim.
  CHECK(t.led(key_pad(39)) == app::kLedDim);  // Eb
  CHECK(t.led(key_pad(46)) == app::kLedDim);  // Bb
  CHECK(t.led(key_pad(37)) == app::kLedOff);  // C#
  CHECK(t.led(key_pad(38)) == app::kLedOff);  // D
  CHECK(t.led(key_pad(40)) == app::kLedOff);  // E
  CHECK(t.led(key_pad(45)) == app::kLedOff);  // A
  CHECK(t.led(key_pad(47)) == app::kLedOff);  // B
  CHECK(t.led(key_pad(41)) == app::kLedDim);  // F
  CHECK(t.led(key_pad(43)) == app::kLedDim);  // G
  t.tap(Button::Rec);
  t.pad(key_pad(38), 10);  // D: skipped
  CHECK(t.pat().steps[0].note != 38);
  t.pad(key_pad(41), 10);  // F: written on step 0
  CHECK(t.pat().steps[0].note == 41);
  t.pad(key_pad(39), 10);  // Eb: a sharp in the scale: written on step 1
  CHECK(t.pat().steps[1].note == 39);
}

TEST_CASE("a skipped pad plays no sound and a hold-a-step key does not write the step") {
  Hands t;
  choose_scale(t, 3);  // C major
  const Pattern before = t.pat();
  t.down(step_pad_of(4));
  t.pad(key_pad(37), 10);  // C#: out
  CHECK(t.pat().steps[4].note == before.steps[4].note);
  t.pad(key_pad(38), 10);  // D: in
  CHECK(t.pat().steps[4].note == 38);
  t.up(step_pad_of(4));
  // A skipped pad does not transpose the running pattern either.
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

TEST_CASE("No scale and Chromatic unlock the keyboard again") {
  Hands t;
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
  CHECK(t.led(key_pad(38)) == app::kLedDim);
}

TEST_CASE("the scale lock follows the keyboard's octave and ends with the run") {
  Hands t;
  choose_scale(t, 3);
  t.pad(role_pad(PadRole::OctUp), 10);
  t.wait(40);
  // An octave up the pads are the same pitch classes.
  CHECK(t.led(key_pad(36 + 1)) == app::kLedOff);
  CHECK(t.led(key_pad(36 + 2)) == app::kLedDim);
  CHECK(t.led(key_pad(36 + 4)) == app::kLedDim);
  Hands again;  // a new run: chromatic
  again.tap(Button::Rec);
  again.pad(key_pad(37), 10);
  CHECK(again.pat().steps[0].note == 37);
}
