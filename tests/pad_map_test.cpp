// SPDX-License-Identifier: GPL-3.0-or-later
// The pad map (app/include/o3_303/pad_map.h): which pad plays which note.
#include <doctest/doctest.h>

#include <o3_303/pad_map.h>
#include <o3_303/pattern.h>

using namespace o3_303;
using o3sdk::app::pad_at;

TEST_CASE("pad map: the bottom row is C2..B2, the row above it an octave up (the pattern's own register)") {
  CHECK(key_note(pad_at(3, 0), 0) == 36);  // C2, the bottom left pad: a fresh step's note
  CHECK(key_note(pad_at(3, 11), 0) == 47);  // B2
  CHECK(key_note(pad_at(2, 0), 0) == 48);  // C3
  CHECK(key_note(pad_at(2, 11), 0) == 59);  // B3
  for (unsigned col = 0; col < o3sdk::app::kPadCols; ++col) {
    CHECK(key_note(pad_at(2, col), 0) == key_note(pad_at(3, col), 0) + 12);
    CHECK(key_note(pad_at(3, col), 0) == 36 + static_cast<int>(col));
  }
  CHECK(kLowestNote == kDefaultNote);  // the bottom left C is also the zero of the live transpose
}

TEST_CASE("pad map: the step pads and pads past the panel play nothing") {
  for (unsigned row = 0; row < 2; ++row) {
    for (unsigned col = 0; col < o3sdk::app::kPadCols; ++col) {
      CHECK(key_note(pad_at(row, col), 0) == -1);
    }
  }
  CHECK(key_note(o3sdk::app::kPadCount, 0) == -1);
  CHECK(key_note(1000, 0) == -1);
}

TEST_CASE("pad map: the octave shift moves the keyboard and stops at the end of the note range") {
  CHECK(key_note(pad_at(3, 0), 1) == 48);
  CHECK(key_note(pad_at(3, 0), -1) == 24);
  CHECK(key_note(pad_at(3, 0), kMinOctave) == 0);   // C-1 at the bottom left
  CHECK(key_note(pad_at(3, 0), kMaxOctave) == 72);
  // The clamped octave range keeps every key inside 0..127; the function still refuses a shift beyond it.
  for (int octave = kMinOctave; octave <= kMaxOctave; ++octave) {
    for (unsigned pad = 0; pad < o3sdk::app::kPadCount; ++pad) {
      if (o3sdk::app::pad_row(pad) >= 2) {
        CHECK(key_note(pad, octave) >= 0);
      }
    }
  }
  CHECK(key_note(pad_at(2, 11), 6) == -1);  // 47 + 12 + 72
  CHECK(key_note(pad_at(3, 0), -5) == -1);  // 36 - 60 = -24
}

TEST_CASE("pad map: clamp_octave") {
  CHECK(clamp_octave(0) == 0);
  CHECK(clamp_octave(kMinOctave - 1) == kMinOctave);
  CHECK(clamp_octave(kMaxOctave + 1) == kMaxOctave);
  CHECK(clamp_octave(-100) == kMinOctave);
}

TEST_CASE("pad map: the step pads, the modifier block and the keys (UX-303 6.2)") {
  // Rows 0 and 1: eight step pads then four modifiers; rows 2 and 3: keys.
  CHECK(pad_role(pad_at(0, 0)) == PadRole::Step);
  CHECK(pad_role(pad_at(0, 7)) == PadRole::Step);
  CHECK(pad_role(pad_at(1, 0)) == PadRole::Step);
  CHECK(pad_role(pad_at(1, 7)) == PadRole::Step);
  CHECK(pad_role(pad_at(0, 8)) == PadRole::Acc);
  CHECK(pad_role(pad_at(0, 9)) == PadRole::Slide);
  CHECK(pad_role(pad_at(0, 10)) == PadRole::Tie);
  CHECK(pad_role(pad_at(0, 11)) == PadRole::Rest);
  CHECK(pad_role(pad_at(1, 8)) == PadRole::OctDown);
  CHECK(pad_role(pad_at(1, 9)) == PadRole::OctUp);
  CHECK(pad_role(pad_at(1, 10)) == PadRole::Trnsp);
  CHECK(pad_role(pad_at(1, 11)) == PadRole::Filt);
  for (unsigned pad = 24; pad < o3sdk::app::kPadCount; ++pad) {
    CHECK(pad_role(pad) == PadRole::Key);
  }
  CHECK(pad_role(o3sdk::app::kPadCount) == PadRole::None);
}

TEST_CASE("pad map: a page of 16 steps is the 16 step pads, in reading order") {
  for (unsigned k = 0; k < kPageSteps; ++k) {
    const unsigned pad = step_pad(k);
    REQUIRE(pad < o3sdk::app::kPadCount);
    CHECK(pad_role(pad) == PadRole::Step);
    CHECK(pad_page_step(pad) == static_cast<int>(k));
  }
  CHECK(step_pad(0) == 0);
  CHECK(step_pad(7) == 7);
  CHECK(step_pad(8) == 12);
  CHECK(step_pad(15) == 19);
  CHECK(step_pad(16) == o3sdk::app::kPadCount);
  CHECK(pad_page_step(8) == -1);  // ACC
  CHECK(pad_page_step(30) == -1);  // a key
}

TEST_CASE("pad map: role_pad finds each modifier, and refuses the others") {
  CHECK(role_pad(PadRole::Acc) == 8);
  CHECK(role_pad(PadRole::Slide) == 9);
  CHECK(role_pad(PadRole::Tie) == 10);
  CHECK(role_pad(PadRole::Rest) == 11);
  CHECK(role_pad(PadRole::OctDown) == 20);
  CHECK(role_pad(PadRole::OctUp) == 21);
  CHECK(role_pad(PadRole::Trnsp) == 22);
  CHECK(role_pad(PadRole::Filt) == 23);
  CHECK(role_pad(PadRole::Step) == o3sdk::app::kPadCount);
  CHECK(role_pad(PadRole::Key) == o3sdk::app::kPadCount);
}

TEST_CASE("pad map: naturals and sharps") {
  const bool natural[12] = {true, false, true, false, true, true, false, true, false, true, false, true};
  for (unsigned n = 0; n < 12; ++n) {
    CHECK(is_natural(n) == natural[n]);
    CHECK(is_natural(n + 48) == natural[n]);
  }
}
