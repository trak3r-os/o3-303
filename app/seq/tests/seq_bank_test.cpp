// SPDX-License-Identifier: GPL-3.0-or-later
// The bank (o3_303/seq/bank.h): the 48 patterns, selection, edits with undo / redo, clipboards, the render-side snapshot.
#include <doctest/doctest.h>

#include <o3_303/seq/bank.h>

#include <cstring>
#include <memory>

using namespace o3_303;
using namespace o3_303::seq;

namespace {

std::unique_ptr<Bank> make_bank() {
  auto b = std::make_unique<Bank>();  // value-initialised: zero, like the SDRAM
  b->init();
  return b;
}

bool same(const Pattern& a, const Pattern& b) { return std::memcmp(&a, &b, sizeof(Pattern)) == 0; }

}  // namespace

TEST_CASE("init: 48 silent patterns of length 16, nothing to undo, pattern 0 selected") {
  auto bank = make_bank();
  for (unsigned i = 0; i < kPatternCount; ++i) {
    const Pattern& p = bank->pattern(i);
    CHECK(p.length == kDefaultLength);
    CHECK(p.gate == kDefaultGate);
    CHECK(p.swing == 0);
    CHECK(p.transpose == 0);
    for (unsigned s = 0; s < kMaxSteps; ++s) {
      CHECK(p.steps[s].flags == kStepRest);
      CHECK(p.steps[s].note == kDefaultNote);
    }
    CHECK_FALSE(lane_has_motion(p, 0));
    CHECK((bank->version(i) & 1u) == 0u);
  }
  CHECK(bank->selected() == 0);
  CHECK_FALSE(bank->can_undo());
  CHECK_FALSE(bank->can_redo());
  CHECK(bank->undo() == -1);
  CHECK(bank->redo() == -1);
  // A bank in the SDRAM starts zeroed and init() makes it usable again after any state.
  bank->set_note(3, 0, 70);
  bank->init();
  CHECK(bank->pattern(3).steps[0].note == kDefaultNote);
  CHECK_FALSE(bank->can_undo());
}

TEST_CASE("select clamps to the 48 patterns; pattern() clamps its index") {
  auto bank = make_bank();
  bank->select(17);
  CHECK(bank->selected() == 17);
  bank->select(47);
  CHECK(bank->selected() == 47);
  bank->select(48);
  CHECK(bank->selected() == 47);
  bank->select(1000);
  CHECK(bank->selected() == 47);
  CHECK(&bank->pattern(1000) == &bank->pattern(47));
  CHECK(bank->edit(48, [](Pattern&) {}) == false);  // an edit of a pattern that does not exist
}

TEST_CASE("edit, undo and redo restore exact states, across patterns") {
  auto bank = make_bank();
  const Pattern p0 = bank->pattern(0);
  const Pattern p5 = bank->pattern(5);
  CHECK(bank->toggle_flag(0, 1, kStepAccent));
  CHECK(bank->set_length(5, 32));
  const Pattern p0b = bank->pattern(0);
  const Pattern p5b = bank->pattern(5);
  CHECK_FALSE(same(p0, p0b));
  CHECK(bank->can_undo());
  CHECK_FALSE(bank->can_redo());

  CHECK(bank->undo() == 5);  // the last edit first, whichever pattern it was
  CHECK(same(bank->pattern(5), p5));
  CHECK(same(bank->pattern(0), p0b));
  CHECK(bank->can_redo());
  CHECK(bank->undo() == 0);
  CHECK(same(bank->pattern(0), p0));
  CHECK_FALSE(bank->can_undo());
  CHECK(bank->undo() == -1);

  CHECK(bank->redo() == 0);
  CHECK(same(bank->pattern(0), p0b));
  CHECK(bank->redo() == 5);
  CHECK(same(bank->pattern(5), p5b));
  CHECK_FALSE(bank->can_redo());
  CHECK(bank->redo() == -1);

  // A new edit after an undo ends the redo line.
  CHECK(bank->undo() == 5);
  CHECK(bank->can_redo());
  CHECK(bank->set_swing(7, 30));
  CHECK_FALSE(bank->can_redo());
  CHECK(bank->redo() == -1);
  CHECK(bank->undo() == 7);
  CHECK(bank->pattern(7).swing == 0);
  CHECK(bank->undo() == 0);
}

TEST_CASE("an edit that changes nothing is not recorded") {
  auto bank = make_bank();
  CHECK(bank->write_advance(0, 0, 100, 0) == 1);  // one real edit: a playing step (the new bank is all rests)
  CHECK_FALSE(bank->set_note(0, 0, 100));         // the same note
  CHECK_FALSE(bank->set_flag(0, 0, kStepAccent, false));       // already off
  CHECK_FALSE(bank->edit(0, [](Pattern&) { return false; }));  // the function says so
  CHECK_FALSE(bank->transpose_notes(0, 200));                  // refused by the operation: 100 + 200 > 127
  CHECK_FALSE(bank->set_length(0, 12));
  CHECK_FALSE(bank->invert(0));  // a single pitch mirrors onto itself
  CHECK(bank->double_length(0)); // a real one does: 16 -> 32
  CHECK(bank->pattern(0).length == 32);
  CHECK(bank->undo() == 0);
  CHECK(bank->pattern(0).length == 16);
  CHECK(bank->undo() == 0);  // the write: the no-ops left no entries between them
  CHECK_FALSE(bank->can_undo());
  CHECK(bank->pattern(0).steps[0].flags == kStepRest);
}

TEST_CASE("the history is bounded: the oldest edits go") {
  auto bank = make_bank();
  for (unsigned i = 0; i < kHistoryDepth + 8; ++i) {
    CHECK(bank->set_note(1, i % 16u, 40u + i));  // each a different step or value, so no coalescing (key differs per step)
  }
  unsigned undone = 0;
  while (bank->undo() >= 0) {
    ++undone;
  }
  CHECK(undone == kHistoryDepth);
  // The 8 oldest are lost for good: their notes stay changed, the 32 newest are undone.
  CHECK(bank->pattern(1).steps[0].note == 40);        // i = 0 was among the oldest
  CHECK(bank->pattern(1).steps[7].note == 40 + 7);    // i = 7
  CHECK(bank->pattern(1).steps[8].note == kDefaultNote);  // i = 8 is the oldest one kept (undone)
  unsigned redone = 0;
  while (bank->redo() >= 0) {
    ++redone;
  }
  CHECK(redone == kHistoryDepth);
  CHECK(bank->pattern(1).steps[(kHistoryDepth + 7) % 16].note == 40 + kHistoryDepth + 7);
}

TEST_CASE("edits of one gesture join into one undo step; an undo ends the gesture") {
  auto bank = make_bank();
  // The encoder turning the note of step 4: many edits, one undo.
  for (unsigned n = 40; n < 60; ++n) {
    CHECK(bank->set_note(2, 4, n));
  }
  CHECK(bank->pattern(2).steps[4].note == 59);
  CHECK(bank->undo() == 2);
  CHECK(bank->pattern(2).steps[4].note == kDefaultNote);
  CHECK_FALSE(bank->can_undo());
  CHECK(bank->redo() == 2);
  CHECK(bank->pattern(2).steps[4].note == 59);
  // After an undo / redo the same gesture key starts a new entry.
  CHECK(bank->set_note(2, 4, 61));
  CHECK(bank->undo() == 2);
  CHECK(bank->pattern(2).steps[4].note == 59);
  CHECK(bank->undo() == 2);
  CHECK(bank->pattern(2).steps[4].note == kDefaultNote);

  // Another step or another pattern is another gesture; so is another kind of edit in between.
  CHECK(bank->set_note(2, 4, 50));
  CHECK(bank->set_note(2, 5, 50));
  CHECK(bank->set_note(3, 5, 50));
  CHECK(bank->set_note(3, 5, 51));
  CHECK(bank->toggle_flag(3, 5, kStepAccent));
  CHECK(bank->set_note(3, 5, 52));
  unsigned n = 0;
  while (bank->undo() >= 0) {
    ++n;
  }
  CHECK(n == 5);
}

TEST_CASE("a pattern's version is odd while it is written, even after, and moves with every change") {
  auto bank = make_bank();
  const std::uint32_t v0 = bank->version(4);
  CHECK((v0 & 1u) == 0u);
  std::uint32_t inside = 0;
  Pattern snap;
  std::uint32_t snap_version = 0;
  bool snap_ok = true;
  bank->edit(4, [&](Pattern& p) {
    inside = bank->version(4);
    snap_ok = bank->snapshot(4, snap, &snap_version);  // a read that overlaps a write is told so
    set_note(p, 0, 70);
  });
  CHECK((inside & 1u) == 1u);
  CHECK_FALSE(snap_ok);
  const std::uint32_t v1 = bank->version(4);
  CHECK((v1 & 1u) == 0u);
  CHECK(v1 == v0 + 2u);
  CHECK(bank->version(5) == v0);  // the other patterns did not move

  CHECK(bank->snapshot(4, snap, &snap_version));
  CHECK(snap_version == v1);
  CHECK(same(snap, bank->pattern(4)));

  bank->undo();
  CHECK(bank->version(4) == v1 + 2u);
  bank->redo();
  CHECK(bank->version(4) == v1 + 4u);
  CHECK(bank->snapshot(4, snap, nullptr));
  CHECK(snap.steps[0].note == 70);
}

TEST_CASE("every named edit does what its operation does") {
  auto bank = make_bank();
  const unsigned i = 9;
  CHECK(bank->set_note(i, 2, 55));
  CHECK(bank->pattern(i).steps[2].note == 55);
  CHECK(bank->set_flag(i, 2, kStepAccent, true));
  CHECK(bank->toggle_flag(i, 2, kStepSlide));
  CHECK(bank->pattern(i).steps[2].flags == (kStepRest | kStepAccent | kStepSlide));  // the step was a rest; flags add up
  CHECK(bank->write_advance(i, 0, 60, kStepAccent) == 1);
  CHECK(bank->pattern(i).steps[0].note == 60);
  CHECK(bank->pattern(i).steps[0].flags == kStepAccent);
  CHECK(bank->write_advance(i, 15, 61, 0) == 0);  // wraps

  CHECK(bank->insert_step(i, 1));
  CHECK(bank->pattern(i).steps[3].note == 55);  // 2 -> 3
  CHECK(bank->delete_step(i, 1));
  CHECK(bank->pattern(i).steps[2].note == 55);
  CHECK(bank->rotate(i, 1));
  CHECK(bank->pattern(i).steps[3].note == 55);
  CHECK(bank->rotate(i, -1));
  CHECK(bank->transpose_notes(i, 12));
  CHECK(bank->pattern(i).steps[2].note == 67);
  CHECK(bank->pattern(i).steps[0].note == 72);
  CHECK(bank->reverse(i));
  CHECK(bank->pattern(i).steps[13].note == 67);
  CHECK(bank->write_advance(i, 4, 50, 0) == 5);  // a second playing pitch, so that the mirror moves something
  CHECK(bank->invert(i));
  CHECK(bank->double_length(i));
  CHECK(bank->pattern(i).length == 32);
  CHECK(bank->set_length(i, 8));
  CHECK(bank->set_swing(i, 25));
  CHECK(bank->set_gate(i, 75));
  CHECK(bank->set_transpose(i, -5));
  CHECK(bank->pattern(i).length == 8);
  CHECK(bank->pattern(i).swing == 25);
  CHECK(bank->pattern(i).gate == 75);
  CHECK(bank->pattern(i).transpose == -5);

  CHECK(bank->set_plock(i, 3, 4, 100));
  CHECK(bank->pattern(i).motion[3][4 * 4 + 1] == 100);
  CHECK(bank->set_motion_point(i, 3, 5, 2, 9));
  CHECK(bank->pattern(i).motion[3][5 * 4 + 2] == 9);
  CHECK(bank->clear_plock(i, 3, 4));
  CHECK(bank->pattern(i).motion[3][4 * 4 + 1] == kNoLock);
  CHECK(bank->set_plock(i, 2, 1, 1));
  CHECK(bank->clear_step_motion(i, 1));
  CHECK(bank->set_plock(i, 2, 1, 1));
  CHECK(bank->clear_lane_motion(i, 2));
  CHECK(bank->clear_motion(i));
  CHECK_FALSE(lane_has_motion(bank->pattern(i), 3));

  GenParams g;
  CHECK(bank->random_fill(i, g, 4));
  const Pattern filled = bank->pattern(i);
  CHECK(bank->mutate(i, g, 5, 100));
  CHECK_FALSE(same(filled, bank->pattern(i)));
  CHECK(bank->clear_pattern(i));
  CHECK(bank->pattern(i).steps[0].flags == kStepRest);

  Pattern other;
  clear(other);
  other.steps[0].note = 33;
  CHECK(bank->replace(i, other));
  CHECK(same(bank->pattern(i), other));

  // And all of it can be walked back to the start.
  unsigned undone = 0;
  while (bank->undo() >= 0) {
    ++undone;
  }
  CHECK(undone > 20);
  CHECK(bank->pattern(i).steps[0].flags == kStepRest);
  CHECK(bank->pattern(i).steps[0].note == kDefaultNote);
  CHECK(bank->pattern(i).length == 16);
}

TEST_CASE("clipboards: a step with its locks, and a whole pattern; pastes are undoable") {
  auto bank = make_bank();
  CHECK_FALSE(bank->has_step_clip());
  CHECK_FALSE(bank->has_pattern_clip());
  CHECK_FALSE(bank->paste_step(0, 0));
  CHECK_FALSE(bank->paste_pattern(0));

  bank->set_note(1, 3, 64);
  bank->set_flag(1, 3, kStepRest, false);
  bank->set_flag(1, 3, kStepSlide, true);
  bank->set_plock(1, 0, 3, 88);
  bank->copy_step(1, 3);
  CHECK(bank->has_step_clip());
  CHECK(bank->paste_step(2, 10));
  CHECK(bank->pattern(2).steps[10].note == 64);
  CHECK(bank->pattern(2).steps[10].flags == kStepSlide);
  CHECK(bank->pattern(2).motion[0][10 * 4 + 2] == 88);
  bank->undo();
  CHECK(bank->pattern(2).steps[10].flags == kStepRest);

  bank->set_swing(1, 20);
  bank->copy_pattern(1);
  CHECK(bank->has_pattern_clip());
  CHECK(bank->paste_pattern(30));
  CHECK(same(bank->pattern(30), bank->pattern(1)));
  CHECK(bank->pattern(30).swing == 20);
  // The clipboard is a copy: editing the source afterwards does not change what is pasted.
  bank->set_note(1, 3, 20);
  CHECK(bank->paste_pattern(31));
  CHECK(bank->pattern(31).steps[3].note == 64);
  bank->undo();
  bank->undo();
  bank->undo();  // 31's paste, the source edit, 30's paste
  CHECK(bank->pattern(30).swing == 0);
  CHECK(bank->pattern(1).steps[3].note == 64);
}

TEST_CASE("project_bank is one object") {
  CHECK(&project_bank() == &project_bank());
  Bank& b = project_bank();
  b.init();
  CHECK(b.selected() == 0);
  b.set_note(47, 0, 33);
  CHECK(b.pattern(47).steps[0].note == 33);
  b.init();
}

TEST_CASE("the bank keeps a has-motion flag per pattern with every edit, undo and redo") {
  auto bank = make_bank();
  for (unsigned i = 0; i < kPatternCount; ++i) {
    CHECK_FALSE(bank->has_motion(i));
  }
  CHECK(bank->set_motion_point(5, 2, 7, 1, 33));
  CHECK(bank->has_motion(5));
  CHECK_FALSE(bank->has_motion(4));
  Pattern snap;
  std::uint32_t version = 0;
  bool motion = false;
  REQUIRE(bank->snapshot(5, snap, &version, &motion));
  CHECK(motion);
  REQUIRE(bank->snapshot(4, snap, &version, &motion));
  CHECK_FALSE(motion);

  bank->undo();
  CHECK_FALSE(bank->has_motion(5));
  bank->redo();
  CHECK(bank->has_motion(5));
  CHECK(bank->clear_plock(5, 2, 7));  // the point goes back to "no lock" ...
  CHECK_FALSE(bank->has_motion(5));   // ... and the flag follows
  CHECK(bank->set_plock(6, 0, 0, 1));
  CHECK(bank->has_motion(6));
  CHECK(bank->clear_motion(6));
  CHECK_FALSE(bank->has_motion(6));
  // Pasting a pattern with motion, and replacing one, move the flag too.
  bank->set_plock(7, 1, 1, 1);
  bank->copy_pattern(7);
  CHECK(bank->paste_pattern(8));
  CHECK(bank->has_motion(8));
  Pattern empty;
  clear(empty);
  CHECK(bank->replace(8, empty));
  CHECK_FALSE(bank->has_motion(8));
  bank->init();
  CHECK_FALSE(bank->has_motion(7));
}

TEST_CASE("a snapshot taken while a write is going on says so and does not copy") {
  auto bank = make_bank();
  Pattern out;
  std::memset(&out, 0xAB, sizeof(out));
  Pattern marker = out;
  bool ok = true;
  std::uint32_t version = 0;
  bank->edit(2, [&](Pattern& p) {
    set_note(p, 0, 99);
    ok = bank->snapshot(2, out, &version);
  });
  CHECK_FALSE(ok);
  CHECK((version & 1u) == 1u);
  CHECK(same(out, marker));  // untouched
  CHECK(bank->snapshot(2, out, &version));
  CHECK((version & 1u) == 0u);
  CHECK(out.steps[0].note == 99);
}

TEST_CASE("clear_history forgets every undo and redo step and leaves the patterns alone") {
  auto bank = make_bank();
  // The boot: a preset loaded with replace() is an edit, so the first undo would empty the pattern ...
  Pattern preset;
  clear(preset);
  preset.steps[0].note = 52;
  preset.steps[0].flags = kStepAccent;
  CHECK(bank->replace(0, preset));
  CHECK(bank->can_undo());
  const std::uint32_t version = bank->version(0);
  bank->clear_history();  // ... unless the setup forgets it
  CHECK_FALSE(bank->can_undo());
  CHECK_FALSE(bank->can_redo());
  CHECK(bank->undo() == -1);
  CHECK(bank->redo() == -1);
  CHECK(same(bank->pattern(0), preset));
  CHECK(bank->version(0) == version);  // not a write: the render has nothing to pick up

  // The user's edits after it undo as usual, down to the loaded preset and no further.
  CHECK(bank->set_note(0, 3, 70));
  CHECK(bank->toggle_flag(0, 1, kStepSlide));
  CHECK(bank->undo() == 0);
  CHECK(bank->undo() == 0);
  CHECK(bank->undo() == -1);
  CHECK(same(bank->pattern(0), preset));

  // It also drops a redo line, the coalescing of the gesture before it, and works on an empty history.
  CHECK(bank->set_note(0, 3, 71));
  CHECK(bank->undo() == 0);
  CHECK(bank->can_redo());
  bank->clear_history();
  CHECK_FALSE(bank->can_redo());
  CHECK(bank->set_note(0, 3, 72));
  CHECK(bank->set_note(0, 3, 73));  // one gesture, one step ...
  bank->clear_history();
  CHECK(bank->set_note(0, 3, 74));  // ... but the gesture before the clear is not joined
  CHECK(bank->undo() == 0);
  CHECK(bank->pattern(0).steps[3].note == 73);
  CHECK(bank->undo() == -1);
  bank->clear_history();
  bank->clear_history();
  CHECK_FALSE(bank->can_undo());

  // Filling the ring again after a clear starts from zero.
  for (unsigned i = 0; i < kHistoryDepth + 3; ++i) {
    CHECK(bank->set_note(1, i % 16u, 40u + i));
  }
  unsigned undone = 0;
  while (bank->undo() >= 0) {
    ++undone;
  }
  CHECK(undone == kHistoryDepth);
}
