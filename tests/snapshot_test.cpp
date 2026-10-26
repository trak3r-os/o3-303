// SPDX-License-Identifier: GPL-3.0-or-later
// The snapshot bank (app/include/o3_303/snapshot.h, docs/M3B-PLAN.md R2): store, recall, the morph from the previous snapshot to the last one
// recalled, and the discrete targets that switch instead of gliding.
#include <doctest/doctest.h>

#include <o3_303/snapshot.h>

using namespace o3_303;

namespace {

void fill(float* v, float base) {
  for (unsigned i = 0; i < kSnapValues; ++i) {
    v[i] = base + static_cast<float>(i);
  }
}

}  // namespace

TEST_CASE("snapshots: a fresh bank is empty and recalls nothing") {
  SnapBank b;
  float live[kSnapValues];
  fill(live, 0.0f);
  for (unsigned i = 0; i < kSnapSlots; ++i) {
    CHECK_FALSE(b.used(i));
    CHECK_FALSE(b.recall(i, live));
  }
  CHECK_FALSE(b.used(kSnapSlots));
  CHECK_FALSE(b.recall(kSnapSlots, live));
  CHECK(b.last() == -1);
  CHECK_FALSE(b.morphing());
}

TEST_CASE("snapshots: the first recall morphs back to the sound as it was, the next ones to the snapshot before") {
  SnapBank b;
  float a[kSnapValues];
  float c[kSnapValues];
  float live[kSnapValues];
  fill(a, 100.0f);
  fill(c, 200.0f);
  fill(live, 0.0f);
  b.store(2, a);
  b.store(5, c);
  CHECK(b.used(2));
  CHECK(b.used(5));
  CHECK_FALSE(b.used(3));

  REQUIRE(b.recall(2, live));
  CHECK(b.last() == 2);
  CHECK(b.previous() == -1);
  CHECK(b.position() == kMorphSteps);
  CHECK(b.value(0, false) == doctest::Approx(100.0f));  // at the end: the snapshot itself
  b.turn(-static_cast<int>(kMorphSteps), false);
  CHECK(b.position() == 0);
  CHECK(b.value(3, false) == doctest::Approx(3.0f));  // at the start: the live sound of the first recall

  REQUIRE(b.recall(5, live));  // live is ignored now: the start is snapshot 2
  CHECK(b.last() == 5);
  CHECK(b.previous() == 2);
  CHECK(b.position() == kMorphSteps);
  CHECK(b.value(1, false) == doctest::Approx(201.0f));
  b.turn(-static_cast<int>(kMorphSteps), false);
  CHECK(b.value(1, false) == doctest::Approx(101.0f));
  b.turn(static_cast<int>(kMorphSteps / 2), false);  // half way: the mean of the two
  CHECK(b.value(1, false) == doctest::Approx(151.0f));
}

TEST_CASE("snapshots: the encoder moves the position in fine and coarse steps and stops at both ends") {
  SnapBank b;
  float a[kSnapValues];
  fill(a, 1.0f);
  b.store(0, a);
  REQUIRE(b.recall(0, a));
  CHECK(b.turn(1, false) == kMorphSteps);  // already at the end
  CHECK(b.turn(-1, false) == kMorphSteps - 1u);
  CHECK(b.turn(-1, true) == kMorphSteps - 1u - kMorphCoarse);
  CHECK(b.turn(1000, false) == kMorphSteps);
  CHECK(b.turn(-1000, true) == 0u);
  CHECK(b.turn(-1, false) == 0u);
}

TEST_CASE("snapshots: a discrete target switches at the middle of the morph") {
  SnapBank b;
  float a[kSnapValues];
  float c[kSnapValues];
  fill(a, 0.0f);
  fill(c, 10.0f);
  b.store(0, a);
  b.store(1, c);
  REQUIRE(b.recall(0, a));
  REQUIRE(b.recall(1, a));  // from snapshot 0 to snapshot 1
  b.turn(-static_cast<int>(kMorphSteps), false);
  CHECK(b.value(4, true) == doctest::Approx(4.0f));
  b.turn(static_cast<int>(kMorphSteps / 2) - 1, false);
  CHECK(b.value(4, true) == doctest::Approx(4.0f));  // just before the middle
  b.turn(1, false);
  CHECK(b.value(4, true) == doctest::Approx(14.0f));  // the middle and beyond: the end
  CHECK(b.value(4, false) == doctest::Approx(9.0f));  // a gliding target is half way
}

TEST_CASE("snapshots: recalling the snapshot at the end of the morph keeps its start; a store does not change a morph in progress") {
  SnapBank b;
  float a[kSnapValues];
  float c[kSnapValues];
  fill(a, 0.0f);
  fill(c, 50.0f);
  b.store(0, a);
  b.store(1, c);
  REQUIRE(b.recall(0, a));
  REQUIRE(b.recall(1, a));
  CHECK(b.previous() == 0);
  float edited[kSnapValues];
  fill(edited, 500.0f);
  b.store(1, edited);                       // the snapshot is rewritten while it is the end of the morph ...
  CHECK(b.value(0, false) == doctest::Approx(50.0f));  // ... the morph still ends on what was recalled
  REQUIRE(b.recall(1, a));                  // recalled again: the new contents, the same start
  CHECK(b.previous() == 0);
  CHECK(b.value(0, false) == doctest::Approx(500.0f));
  b.clear();
  CHECK_FALSE(b.used(0));
  CHECK(b.last() == -1);
}
