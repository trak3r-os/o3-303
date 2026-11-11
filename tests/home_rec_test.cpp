// SPDX-License-Identifier: GPL-3.0-or-later
// HOME, REC while the pattern plays (docs/TRACT-KNOBS-PLAN.md U1, hold = length, nearest step): a key lands on the nearest step (past half of the
// step it is played in: the next one), every step that starts while the key is still held is written as a tie, the whole note is one UNDO step, and
// a key released inside its step writes that step only. 120 BPM: a sixteenth is 125 ms.
#include <doctest/doctest.h>

#include "hands.h"

#include "../app/src/home_map.h"

#include <o3_303/app_access.h>
#include <o3_303/step_model.h>

#include <cstdint>

using namespace hands;
using namespace o3_303;

namespace {

constexpr unsigned kNote = 52;
constexpr unsigned kStepMs = 125;

// Waits until the playhead enters a step and returns it (the main loop sees it within a block or two).
int enter_next_step(Hands& t) {
  const seq::Playhead first = app_sequencer().playhead();
  for (unsigned i = 0; i < 4 * kStepMs; ++i) {
    t.wait(1);
    const seq::Playhead now = app_sequencer().playhead();
    if (now.playing && now.step != first.step) {
      return now.step;
    }
  }
  return -1;
}

struct Rec {
  Hands t;
  Pattern before;
  Rec() {
    t.h->engine().set_tempo(120'000);
    t.select_pattern(12);
    REQUIRE(pattern_is_empty(t.pat()));
    before = t.pat();
    t.tap(Button::Rec);
    t.tap(Button::Play);
    t.wait(300);
  }
  bool silent(unsigned step) const { return (t.pat().steps[step].flags & kStepRest) != 0u; }
  bool tie(unsigned step) const { return t.pat().steps[step].flags == kStepTie; }
  bool note(unsigned step) const {
    return (t.pat().steps[step].flags & (kStepRest | kStepTie)) == 0u && t.pat().steps[step].note == kNote;
  }
  unsigned next(unsigned step) const { return (step + 1u) % 16u; }
};

}  // namespace

TEST_CASE("the playhead's phase counts the sixteenths of a step and its point is phase / 4") {
  Hands t;
  t.h->engine().set_tempo(120'000);
  t.tap(Button::Play);
  t.wait(50);
  int step = enter_next_step(t);
  REQUIRE(step >= 0);
  int last = -1;
  bool rose = false;
  for (unsigned i = 0; i < kStepMs - 12; ++i) {
    const seq::Playhead p = app_sequencer().playhead();
    if (p.step != step) {
      break;
    }
    CHECK(p.phase / 4 == p.point);
    CHECK(p.phase >= last);
    rose = rose || p.phase > last;
    last = p.phase;
    t.wait(1);
  }
  CHECK(rose);
  CHECK(last >= 12);  // near the end of the step
  t.tap(Button::Play);
}

TEST_CASE("U1: a key played in the first half of a step lands on it, past half on the next") {
  for (const bool late : {false, true}) {
    INFO("percent of the step: " << (late ? 70 : 30));
    Rec r;
    const int s = enter_next_step(r.t);
    REQUIRE(s >= 0);
    r.t.wait(late ? 86u : 35u);
    r.t.pad(key_pad(kNote), 10);  // released inside its step: one step, no ties
    r.t.tap(Button::Play);
    const unsigned at = late ? r.next(static_cast<unsigned>(s)) : static_cast<unsigned>(s);
    unsigned notes = 0;
    for (unsigned i = 0; i < 16; ++i) {
      notes += r.note(i) ? 1u : 0u;
      CHECK_FALSE(r.tie(i));
    }
    CHECK(notes == 1);
    CHECK(r.note(at));
  }
}

TEST_CASE("U1: a key held over three steps writes the note and two ties, and one UNDO takes all three back") {
  Rec r;
  const int s = enter_next_step(r.t);
  REQUIRE(s >= 0);
  const unsigned a = static_cast<unsigned>(s);
  r.t.wait(35);
  r.t.down(key_pad(kNote));
  r.t.wait(250);  // 37 ms + 252 ms: into the third step
  r.t.up(key_pad(kNote));
  r.t.tap(Button::Play);
  CHECK(r.note(a));
  CHECK(r.tie(r.next(a)));
  CHECK(r.tie(r.next(r.next(a))));
  unsigned written = 0;
  for (unsigned i = 0; i < 16; ++i) {
    written += r.silent(i) ? 0u : 1u;
  }
  CHECK(written == 3);
  r.t.shift_tap(Button::F8);  // UNDO: the whole held note is one step
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(r.t.pat().steps[i].flags == r.before.steps[i].flags);
    CHECK(r.t.pat().steps[i].note == r.before.steps[i].note);
  }
  CHECK_FALSE(bank().can_undo());
}

TEST_CASE("U1: a key played past half a step and held writes the ties after the playhead has reached its step") {
  Rec r;
  const int s = enter_next_step(r.t);
  REQUIRE(s >= 0);
  const unsigned a = r.next(static_cast<unsigned>(s));  // the nearest step
  r.t.wait(86);
  r.t.down(key_pad(kNote));
  r.t.wait(20);  // still inside step s: no tie yet, the note waits on its own step
  CHECK(r.note(a));
  CHECK_FALSE(r.tie(a));
  CHECK(r.silent(r.next(a)));
  r.t.wait(kStepMs + 30);  // the playhead is on a, then on a + 1: that one is a tie
  r.t.up(key_pad(kNote));
  r.t.tap(Button::Play);
  CHECK(r.note(a));
  CHECK(r.tie(r.next(a)));
  CHECK(r.silent(r.next(r.next(a))));
}

TEST_CASE("U1: the transport stopped ends the hold: no ties are written after") {
  Rec r;
  const int s = enter_next_step(r.t);
  REQUIRE(s >= 0);
  r.t.wait(35);
  r.t.down(key_pad(kNote));
  r.t.wait(10);
  r.t.tap(Button::Play);  // stop
  r.t.wait(2 * kStepMs);
  r.t.up(key_pad(kNote));
  unsigned ties = 0;
  for (unsigned i = 0; i < 16; ++i) {
    ties += r.tie(i) ? 1u : 0u;
  }
  CHECK(ties == 0);
}
