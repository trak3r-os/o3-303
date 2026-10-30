// SPDX-License-Identifier: GPL-3.0-or-later
// What the step view draws, as pure functions of a pattern (app/include/o3_303/step_model.h): how a step reads, how it joins its
// neighbours, the pitch window of the lane, the names.
#include <doctest/doctest.h>

#include <o3_303/pattern.h>
#include <o3_303/step_model.h>

#include <cstring>

using namespace o3_303;

namespace {

// A pattern of `len` rests.
Pattern rests(unsigned len) {
  Pattern p;
  clear(p);
  p.length = static_cast<std::uint8_t>(len);
  for (Step& s : p.steps) {
    s.flags = kStepRest;
  }
  return p;
}

void note(Pattern& p, unsigned i, unsigned n, std::uint8_t flags = 0) {
  p.steps[i].note = static_cast<std::uint8_t>(n);
  p.steps[i].flags = flags;
}

}  // namespace

TEST_CASE("step model: a note, a rest, a step past the length") {
  Pattern p = rests(16);
  note(p, 2, 40, kStepAccent);
  const StepInfo n = describe(p, 2);
  CHECK(n.kind == Cell::Note);
  CHECK(n.note == 40);
  CHECK(n.accent);
  CHECK_FALSE(n.slide);
  CHECK_FALSE(n.to_next);
  CHECK(n.ramp_to == -1);
  CHECK(describe(p, 3).kind == Cell::Rest);
  CHECK(describe(p, 16).kind == Cell::Rest);
  CHECK(describe(p, 63).kind == Cell::Rest);
}

TEST_CASE("step model: a tie carries the note before it, through other ties, and keeps its colour") {
  Pattern p = rests(16);
  note(p, 4, 43, kStepAccent);
  note(p, 5, 0, kStepTie);
  note(p, 6, 0, kStepTie);
  const StepInfo first = describe(p, 4);
  CHECK(first.to_next);  // a tie follows: the bar goes on
  const StepInfo t = describe(p, 5);
  CHECK(t.kind == Cell::Tie);
  CHECK(t.note == 43);
  CHECK(t.accent);
  CHECK(t.from_prev);
  CHECK(t.to_next);
  const StepInfo last = describe(p, 6);
  CHECK(last.kind == Cell::Tie);
  CHECK(last.note == 43);
  CHECK_FALSE(last.to_next);
}

TEST_CASE("step model: a tie after a rest, or with nothing before it, is silent") {
  Pattern p = rests(16);
  note(p, 3, 40);
  note(p, 4, 0, kStepRest);
  note(p, 5, 0, kStepTie);
  CHECK(describe(p, 5).kind == Cell::Rest);
  Pattern q = rests(8);
  for (unsigned i = 0; i < 8; ++i) {
    note(q, i, 0, kStepTie);
  }
  CHECK(describe(q, 0).kind == Cell::Rest);  // a pattern of ties only
  // A rest flag wins over a tie flag on the same step.
  note(p, 6, 40, kStepTie | kStepRest);
  CHECK(describe(p, 6).kind == Cell::Rest);
}

TEST_CASE("step model: a tie at the start of the pattern carries the last note (the loop holds it)") {
  Pattern p = rests(16);
  note(p, 15, 45);
  note(p, 0, 0, kStepTie);
  const StepInfo t = describe(p, 0);
  CHECK(t.kind == Cell::Tie);
  CHECK(t.note == 45);
  CHECK(describe(p, 15).to_next);
}

TEST_CASE("step model: a slide joins the next note with a ramp, and holds into a rest or a tie without one") {
  Pattern p = rests(16);
  note(p, 0, 36, kStepSlide);
  note(p, 1, 48);
  const StepInfo a = describe(p, 0);
  CHECK(a.slide);
  CHECK(a.to_next);
  CHECK(a.ramp_to == 48);
  // Into a rest: held, nothing to glide to.
  note(p, 4, 36, kStepSlide);
  const StepInfo r = describe(p, 4);
  CHECK(r.to_next);
  CHECK(r.ramp_to == -1);
  // Into a tie: held on, no glide.
  note(p, 8, 36, kStepSlide);
  note(p, 9, 0, kStepTie);
  const StepInfo t = describe(p, 8);
  CHECK(t.to_next);
  CHECK(t.ramp_to == -1);
  // The same pitch: a ramp of no height (the caller joins the bars).
  note(p, 12, 40, kStepSlide);
  note(p, 13, 40);
  CHECK(describe(p, 12).ramp_to == 40);
}

TEST_CASE("step model: pattern_is_empty, pitch_range and motion questions") {
  Pattern p = rests(16);
  CHECK(pattern_is_empty(p));
  int lo = 0;
  int hi = 0;
  CHECK_FALSE(pitch_range(p, lo, hi));
  note(p, 3, 50);
  note(p, 9, 38);
  note(p, 10, 70, kStepTie);  // a tie has no pitch of its own
  note(p, 20, 90);            // past the length of 16
  CHECK_FALSE(pattern_is_empty(p));
  REQUIRE(pitch_range(p, lo, hi));
  CHECK(lo == 38);
  CHECK(hi == 50);

  CHECK_FALSE(pattern_has_motion(p, 0));
  CHECK_FALSE(step_has_motion(p, 2, 5));
  p.motion[2][5 * kMotionPointsPerStep + 1] = 100;
  CHECK(step_has_motion(p, 2, 5));
  CHECK_FALSE(step_has_motion(p, 1, 5));
  CHECK_FALSE(step_has_motion(p, 2, 6));
  CHECK(pattern_has_motion(p, 2));
  CHECK_FALSE(pattern_has_motion(p, 3));
  CHECK(pattern_has_motion(p, kMotionLanes));  // any lane
  CHECK_FALSE(step_has_motion(p, kMotionLanes, 5));
  CHECK_FALSE(step_has_motion(p, 2, kMaxSteps));
}

TEST_CASE("step model: the step view's window is the real range and three semitones each side (S1)") {
  const int lane_h = 202;
  // A narrow line: 11 rows of the tallest size, centred, exactly the range +-3.
  const PitchWindow narrow = range_window(40, 44, lane_h);
  CHECK(narrow.px == 16);
  CHECK(narrow.rows == 11);
  CHECK(narrow.lo == 37);
  CHECK(narrow.hi() == 47);
  // A wider one gets smaller rows, still exactly the range +-3 while 5 px rows hold it.
  const PitchWindow mid = range_window(36, 60, lane_h);
  CHECK(mid.rows == 25 + 6);
  CHECK(mid.lo == 33);
  CHECK(mid.px == lane_h / mid.rows);
  CHECK(mid.rows * mid.px <= lane_h);
  // Too wide for 5 px rows: the rows that fit, round the middle, never past the lane.
  const PitchWindow huge = range_window(10, 100, lane_h);
  CHECK(huge.px == 5);
  CHECK(huge.rows == lane_h / 5);
  CHECK(huge.rows * huge.px <= lane_h);
  CHECK(huge.lo >= 0);
  CHECK(huge.hi() <= 127);
  // The edges of MIDI: the window slides to stay inside 0..127, a swapped range is the same.
  const PitchWindow low = range_window(0, 2, lane_h);
  CHECK(low.lo == 0);
  const PitchWindow high = range_window(126, 127, lane_h);
  CHECK(high.hi() == 127);
  const PitchWindow swapped = range_window(44, 40, lane_h);
  CHECK(swapped.lo == narrow.lo);
  CHECK(swapped.rows == narrow.rows);
  for (int lowest = 0; lowest <= 120; lowest += 5) {
    for (int span = 0; span <= 40; span += 4) {
      if (lowest + span > 127) {
        continue;
      }
      const PitchWindow w = range_window(lowest, lowest + span, lane_h);
      CHECK(w.px >= 5);
      CHECK(w.px <= 16);
      CHECK(w.rows * w.px <= lane_h);
      CHECK(w.lo >= 0);
      CHECK(w.hi() <= 127);
      if (w.rows >= span + 7 && lowest >= 3 && lowest + span + 3 <= 127) {
        CHECK(w.lo == lowest - 3);
        CHECK(w.hi() == lowest + span + 3);
      }
    }
  }
}

TEST_CASE("step model: note and pattern names") {
  char n[8];
  CHECK(note_name(36, n, sizeof n) == 2);
  CHECK(std::strcmp(n, "C2") == 0);
  note_name(37, n, sizeof n);
  CHECK(std::strcmp(n, "C#2") == 0);
  note_name(48, n, sizeof n);
  CHECK(std::strcmp(n, "C3") == 0);
  note_name(0, n, sizeof n);
  CHECK(std::strcmp(n, "C-1") == 0);
  note_name(127, n, sizeof n);
  CHECK(std::strcmp(n, "G9") == 0);
  note_name(70, n, sizeof n);
  CHECK(std::strcmp(n, "A#4") == 0);
  char tiny[3];
  CHECK(note_name(36, tiny, sizeof tiny) == 0);  // no room: nothing written but the terminator

  char p[8];
  pattern_name(0, p, sizeof p);
  CHECK(std::strcmp(p, "A01") == 0);
  pattern_name(6, p, sizeof p);
  CHECK(std::strcmp(p, "A07") == 0);
  pattern_name(11, p, sizeof p);
  CHECK(std::strcmp(p, "A12") == 0);
  pattern_name(12, p, sizeof p);
  CHECK(std::strcmp(p, "B01") == 0);
  pattern_name(47, p, sizeof p);
  CHECK(std::strcmp(p, "D12") == 0);
  pattern_name(500, p, sizeof p);  // past the project: the last one
  CHECK(std::strcmp(p, "D12") == 0);
}

TEST_CASE("step model: the GATE row's names and glyphs follow the list, past the end the last") {
  static constexpr const char* kNames[] = {"REST", "NOTE", "ACC", "SLD", "ACC+SLD", "TIE"};
  static constexpr const char* kGlyphs[] = {".", "-", "A", "S", "AS", "~"};
  for (unsigned i = 0; i < 6; ++i) {
    CHECK(std::strcmp(gate_name(i), kNames[i]) == 0);
    CHECK(std::strcmp(gate_glyph(i), kGlyphs[i]) == 0);
  }
  CHECK(std::strcmp(gate_name(99), "TIE") == 0);
  CHECK(std::strcmp(gate_glyph(99), "~") == 0);
}

TEST_CASE("step model: the PLAYS row shows the condition, else the ratchet, a stray value as itself") {
  char t[16];
  CHECK(plays_text(0, kCondAlways, t, sizeof t) == 2);
  CHECK(std::strcmp(t, "x1") == 0);
  (void)plays_text(with_ratchet(0, 3), kCondAlways, t, sizeof t);
  CHECK(std::strcmp(t, "x4") == 0);
  (void)plays_text(0, 50, t, sizeof t);
  CHECK(std::strcmp(t, "50%") == 0);
  (void)plays_text(0, cond_ratio(1, 2), t, sizeof t);
  CHECK(std::strcmp(t, "1:2") == 0);
  (void)plays_text(0, kCondNotFill, t, sizeof t);
  CHECK(std::strcmp(t, "!FILL") == 0);
  // Both (old data): the condition. A probability off the list and an A:B off it are shown as themselves; a reserved byte plays as always.
  (void)plays_text(with_ratchet(0, 2), kCondFirst, t, sizeof t);
  CHECK(std::strcmp(t, "FIRST") == 0);
  (void)plays_text(0, 33, t, sizeof t);
  CHECK(std::strcmp(t, "33%") == 0);
  (void)plays_text(0, cond_ratio(3, 8), t, sizeof t);
  CHECK(std::strcmp(t, "3:8") == 0);
  (void)plays_text(with_ratchet(0, 1), 0xC9, t, sizeof t);
  CHECK(std::strcmp(t, "x2") == 0);
  CHECK(plays_text(0, kCondAlways, nullptr, 0) == 0);
}

TEST_CASE("step model: the STEP box shows the step whole: note, accent, slide, ratchet and condition, or a rest or a tie") {
  Pattern p = rests(16);
  char t[48];
  note(p, 4, 39, kStepAccent | kStepSlide);  // D#2
  p.steps[4].flags = with_ratchet(p.steps[4].flags, 1);
  CHECK(step_text(p, 4, t, sizeof t) == std::strlen("STEP 5: D#2 ACC SLD x2"));
  CHECK(std::strcmp(t, "STEP 5: D#2 ACC SLD x2") == 0);
  p.steps[4].cond = cond_ratio(1, 2);  // a condition shows instead of the ratchet
  (void)step_text(p, 4, t, sizeof t);
  CHECK(std::strcmp(t, "STEP 5: D#2 ACC SLD 1:2") == 0);
  note(p, 5, 36);
  (void)step_text(p, 5, t, sizeof t);
  CHECK(std::strcmp(t, "STEP 6: C2") == 0);  // x1 is not said
  p.steps[6].flags = kStepTie;
  (void)step_text(p, 6, t, sizeof t);
  CHECK(std::strcmp(t, "STEP 7: TIE") == 0);
  (void)step_text(p, 7, t, sizeof t);
  CHECK(std::strcmp(t, "STEP 8: REST") == 0);
  (void)step_text(p, 90, t, sizeof t);  // past the length: the last step
  CHECK(std::strcmp(t, "STEP 16: REST") == 0);
  char tiny[6];
  (void)step_text(p, 4, tiny, sizeof tiny);  // cut, still terminated
  CHECK(std::strlen(tiny) == 5);
}

TEST_CASE("step model: a step's lock is the first of its four points that has one") {
  Pattern p = rests(16);
  clear(p);
  CHECK(step_lock_point(p, 1, 3) == kNoLock);
  p.motion[1][3 * kMotionPointsPerStep + 2] = 77;
  CHECK(step_lock_point(p, 1, 3) == 77);
  p.motion[1][3 * kMotionPointsPerStep + 0] = 5;
  CHECK(step_lock_point(p, 1, 3) == 5);
  CHECK(step_lock_point(p, kMotionLanes, 3) == kNoLock);
  CHECK(step_lock_point(p, 1, kMaxSteps) == kNoLock);
}
