// SPDX-License-Identifier: GPL-3.0-or-later
// The editing operations (o3_303/seq/ops.h): pure functions on a Pattern, each checked on its own.
#include <doctest/doctest.h>

#include <o3sdk/core/scales.h>
#include <o3_303/seq/ops.h>
#include <o3_303/seq/rng.h>
#include <o3_303/units/tb303/tb303.h>

#include <cstring>
#include <initializer_list>

using namespace o3_303;
using namespace o3_303::seq;

namespace {

// A pattern whose steps all play (no flags): the given notes first, then the default note. (The fresh pattern, clear(), is all rests.)
Pattern make(std::initializer_list<int> notes, unsigned length = 16) {
  Pattern p;
  clear(p);
  for (Step& s : p.steps) {
    s.flags = 0;
  }
  p.length = static_cast<std::uint8_t>(length);
  unsigned i = 0;
  for (const int n : notes) {
    p.steps[i++].note = static_cast<std::uint8_t>(n);
  }
  return p;
}

bool same(const Pattern& a, const Pattern& b) { return std::memcmp(&a, &b, sizeof(Pattern)) == 0; }

std::uint8_t pt(const Pattern& p, unsigned lane, unsigned step, unsigned q) { return p.motion[lane][step * kMotionPointsPerStep + q]; }

}  // namespace

TEST_CASE("set_note clamps and leaves the flags alone") {
  Pattern p = make({});
  set_flag(p, 3, kStepRest, true);
  set_note(p, 3, 60);
  CHECK(p.steps[3].note == 60);
  CHECK(p.steps[3].flags == kStepRest);
  set_note(p, 3, 500);
  CHECK(p.steps[3].note == 127);
  set_note(p, 999, 40);  // the step is clamped too
  CHECK(p.steps[63].note == 40);
}

TEST_CASE("flags: accent, slide, tie and rest; tie and rest exclude each other") {
  Pattern p = make({});
  toggle_flag(p, 0, kStepAccent);
  toggle_flag(p, 0, kStepSlide);
  CHECK(p.steps[0].flags == (kStepAccent | kStepSlide));
  toggle_flag(p, 0, kStepAccent);
  CHECK(p.steps[0].flags == kStepSlide);
  set_flag(p, 0, kStepTie, true);
  CHECK(has_flag(p, 0, kStepTie));
  set_flag(p, 0, kStepRest, true);
  CHECK(has_flag(p, 0, kStepRest));
  CHECK_FALSE(has_flag(p, 0, kStepTie));
  set_flag(p, 0, kStepTie, true);
  CHECK(has_flag(p, 0, kStepTie));
  CHECK_FALSE(has_flag(p, 0, kStepRest));
  set_flag(p, 0, kStepTie, false);
  CHECK(p.steps[0].flags == kStepSlide);
  toggle_flag(p, 0, kStepRest);
  toggle_flag(p, 0, kStepRest);
  CHECK(p.steps[0].flags == kStepSlide);
}

TEST_CASE("write_advance writes the note with exactly the given flags and returns the next cursor") {
  Pattern p = make({}, 8);
  set_flag(p, 0, kStepRest, true);
  CHECK(write_advance(p, 0, 50, kStepAccent) == 1);
  CHECK(p.steps[0].note == 50);
  CHECK(p.steps[0].flags == kStepAccent);  // the old rest is gone: the step takes exactly these flags
  CHECK(write_advance(p, 1, 52, kStepAccent | kStepSlide) == 2);
  CHECK(p.steps[1].flags == (kStepAccent | kStepSlide));
  // A rest and a tie keep the step's note.
  CHECK(write_advance(p, 2, 99, kStepRest) == 3);
  CHECK(p.steps[2].note == kDefaultNote);
  CHECK(p.steps[2].flags == kStepRest);
  CHECK(write_advance(p, 3, 99, kStepTie) == 4);
  CHECK(p.steps[3].note == kDefaultNote);
  CHECK(p.steps[3].flags == kStepTie);
  CHECK(write_advance(p, 4, 60, kStepRest | kStepTie) == 5);  // rest beats tie
  CHECK(p.steps[4].flags == kStepRest);
  // The cursor wraps at the end of the length.
  CHECK(write_advance(p, 7, 61, 0) == 0);
}

TEST_CASE("insert_step and delete_step shift the steps and their motion inside the length") {
  Pattern p = make({10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25});
  set_flag(p, 5, kStepAccent, true);
  set_plock(p, 2, 5, 100);
  insert_step(p, 3);
  CHECK(p.steps[2].note == 12);
  CHECK(p.steps[3].flags == kStepRest);  // the new step is a rest
  CHECK(p.steps[3].note == kDefaultNote);
  CHECK(p.steps[4].note == 13);
  CHECK(p.steps[6].note == 15);
  CHECK(p.steps[6].flags == kStepAccent);  // the accent moved with its step
  CHECK(pt(p, 2, 6, 0) == 100);            // and so did the lock
  CHECK(pt(p, 2, 5, 0) == kNoLock);
  CHECK(p.steps[15].note == 24);  // the last one fell off (25)
  CHECK(pt(p, 2, 3, 2) == kNoLock);

  delete_step(p, 3);
  CHECK(p.steps[3].note == 13);
  CHECK(p.steps[5].flags == kStepAccent);
  CHECK(pt(p, 2, 5, 3) == 100);
  CHECK(p.steps[15].flags == kStepRest);  // the last step of the length is a rest
  CHECK(p.steps[15].note == kDefaultNote);
  CHECK(p.steps[14].note == 24);

  // Outside the length, nothing happens.
  Pattern q = make({1, 2, 3, 4, 5, 6, 7, 8}, 8);
  const Pattern before = q;
  insert_step(q, 8);
  delete_step(q, 12);
  CHECK(same(q, before));
}

TEST_CASE("steps are copied and pasted with their locks") {
  Pattern p = make({40, 41, 42});
  set_flag(p, 1, kStepAccent, true);
  set_flag(p, 1, kStepSlide, true);
  set_motion_point(p, 4, 1, 2, 77);
  const StepClip clip = copy_step(p, 1);
  CHECK(clip.step.note == 41);
  CHECK(clip.motion[4][2] == 77);
  paste_step(p, 9, clip);
  CHECK(p.steps[9].note == 41);
  CHECK(p.steps[9].flags == (kStepAccent | kStepSlide));
  CHECK(pt(p, 4, 9, 2) == 77);
  CHECK(pt(p, 4, 9, 0) == kNoLock);
  // Pasting over a step with a lock replaces the locks, not merges.
  set_plock(p, 1, 2, 5);
  paste_step(p, 2, clip);
  CHECK(pt(p, 1, 2, 0) == kNoLock);
}

TEST_CASE("rotate moves the steps and their motion, wrapping inside the length") {
  Pattern p = make({0, 1, 2, 3, 4, 5, 6, 7}, 8);
  p.steps[8].note = 99;  // hidden data stays put
  set_flag(p, 0, kStepAccent, true);
  set_motion_point(p, 0, 0, 1, 50);
  rotate(p, 1);
  CHECK(p.steps[0].note == 7);
  CHECK(p.steps[1].note == 0);
  CHECK(p.steps[1].flags == kStepAccent);
  CHECK(pt(p, 0, 1, 1) == 50);
  CHECK(pt(p, 0, 0, 1) == kNoLock);
  CHECK(p.steps[7].note == 6);
  CHECK(p.steps[8].note == 99);
  rotate(p, -1);
  for (unsigned i = 0; i < 8; ++i) {
    CHECK(p.steps[i].note == i);
  }
  CHECK(pt(p, 0, 0, 1) == 50);
  rotate(p, -3);
  CHECK(p.steps[0].note == 3);
  CHECK(p.steps[4].note == 7);
  CHECK(p.steps[5].note == 0);
  const Pattern before = p;
  rotate(p, 8);  // a whole turn
  rotate(p, -16);
  rotate(p, 0);
  CHECK(same(p, before));
  rotate(p, 11);  // 11 == 3 mod 8: undoes the -3
  CHECK(p.steps[0].note == 0);
}

TEST_CASE("transpose_notes shifts the notes and refuses to leave 0..127") {
  Pattern p = make({60, 62, 64, 65});
  p.length = 8;
  CHECK(transpose_notes(p, 5));
  CHECK(p.steps[0].note == 65);
  CHECK(p.steps[3].note == 70);
  CHECK(p.steps[4].note == kDefaultNote + 5);  // every step of the length moves
  CHECK(p.steps[8].note == kDefaultNote);      // the hidden ones do not
  CHECK(transpose_notes(p, -5));
  CHECK(p.steps[0].note == 60);

  Pattern high = make({125, 120});
  const Pattern before = high;
  CHECK_FALSE(transpose_notes(high, 3));  // 125 + 3 > 127: nothing changes, the interval survives
  CHECK(same(high, before));
  CHECK_FALSE(transpose_notes(high, -200));

  // Rests and ties move with the rest, and count against the range: the range check covers every step of the length.
  Pattern r = make({60, 62});
  set_flag(r, 1, kStepRest, true);
  set_flag(r, 2, kStepTie, true);
  r.steps[2].note = 120;
  CHECK_FALSE(transpose_notes(r, 8));  // the tie's 120 would pass 127
  CHECK(r.steps[0].note == 60);
  CHECK(transpose_notes(r, 7));
  CHECK(r.steps[1].note == 69);  // the rest's pitch moved
  CHECK(r.steps[2].note == 127);
  CHECK(r.steps[1].flags == kStepRest);
  CHECK(r.steps[2].flags == kStepTie);

  // The hidden steps (above the length) are not part of it.
  Pattern h = make({60}, 8);
  h.steps[20].note = 127;
  CHECK(transpose_notes(h, 12));
  CHECK(h.steps[20].note == 127);
}

TEST_CASE("the fresh pattern is silent: every step a rest with C2 under it") {
  Pattern p;
  clear(p);
  for (unsigned i = 0; i < kMaxSteps; ++i) {
    CHECK(p.steps[i].flags == kStepRest);
    CHECK(p.steps[i].note == kDefaultNote);
  }
  CHECK(p.length == kDefaultLength);
  CHECK(p.swing == 0);
  CHECK(p.transpose == 0);
  CHECK(p.gate == kDefaultGate);
  CHECK_FALSE(lane_has_motion(p, 0));
  // Writing a note sets its pitch and clears the rest.
  CHECK(write_advance(p, 0, 55, 0) == 1);
  CHECK(p.steps[0].note == 55);
  CHECK(p.steps[0].flags == 0);
  CHECK(p.steps[1].flags == kStepRest);
}

TEST_CASE("reverse puts the steps and the motion in reverse order") {
  Pattern p = make({0, 1, 2, 3, 4, 5, 6, 7}, 8);
  set_flag(p, 0, kStepAccent, true);
  set_flag(p, 6, kStepTie, true);
  for (unsigned q = 0; q < 4; ++q) {
    set_motion_point(p, 3, 1, q, static_cast<std::uint8_t>(10 + q));
  }
  reverse(p);
  for (unsigned i = 0; i < 8; ++i) {
    CHECK(p.steps[i].note == 7 - i);
  }
  CHECK(p.steps[7].flags == kStepAccent);
  CHECK(p.steps[1].flags == kStepTie);
  // Step 1 moved to step 6 and its four points run backwards.
  CHECK(pt(p, 3, 6, 0) == 13);
  CHECK(pt(p, 3, 6, 1) == 12);
  CHECK(pt(p, 3, 6, 2) == 11);
  CHECK(pt(p, 3, 6, 3) == 10);
  reverse(p);
  CHECK(p.steps[0].note == 0);
  CHECK(pt(p, 3, 1, 0) == 10);
}

TEST_CASE("invert mirrors the pitches inside the pattern's own register") {
  Pattern p = make({36, 40, 43, 48}, 8);
  set_flag(p, 5, kStepRest, true);  // a rest keeps its note and does not set the register
  p.steps[5].note = 10;
  invert(p);
  CHECK(p.steps[0].note == 48);  // lowest + highest - note = 84 - note
  CHECK(p.steps[1].note == 44);
  CHECK(p.steps[2].note == 41);
  CHECK(p.steps[3].note == 36);
  CHECK(p.steps[5].note == 10);
  CHECK(p.steps[4].note == 84 - kDefaultNote);
  invert(p);  // the register is the same, so inverting twice is the identity
  CHECK(p.steps[0].note == 36);
  CHECK(p.steps[3].note == 48);

  Pattern silent = make({50}, 8);
  for (unsigned i = 0; i < 8; ++i) {
    set_flag(silent, i, kStepRest, true);
  }
  const Pattern before = silent;
  invert(silent);
  CHECK(same(silent, before));
}

TEST_CASE("double_length copies the pattern into the new half, up to 64") {
  Pattern p = make({1, 2, 3, 4, 5, 6, 7, 8}, 8);
  set_flag(p, 2, kStepAccent, true);
  set_motion_point(p, 1, 3, 1, 33);
  CHECK(double_length(p));
  CHECK(p.length == 16);
  CHECK(p.steps[8].note == 1);
  CHECK(p.steps[10].flags == kStepAccent);
  CHECK(pt(p, 1, 11, 1) == 33);
  CHECK(p.steps[15].note == 8);
  CHECK(double_length(p));
  CHECK(p.length == 32);
  CHECK(p.steps[16 + 10].flags == kStepAccent);
  CHECK(p.steps[31].note == 8);
  CHECK(double_length(p));
  CHECK(p.length == 64);
  CHECK(p.steps[63].note == 8);
  CHECK(pt(p, 1, 32 + 16 + 3, 1) == 33);
  const Pattern before = p;
  CHECK_FALSE(double_length(p));
  CHECK(same(p, before));
}

TEST_CASE("clear_pattern silences every step and keeps the pattern's settings") {
  Pattern p = make({1, 2, 3});
  p.length = 32;
  p.swing = 40;
  p.gate = 70;
  p.transpose = 5;
  set_plock(p, 2, 4, 9);
  set_flag(p, 1, kStepAccent, true);
  clear_pattern(p);
  for (unsigned i = 0; i < kMaxSteps; ++i) {
    CHECK(p.steps[i].flags == kStepRest);
    CHECK(p.steps[i].note == kDefaultNote);
    CHECK_FALSE(step_has_motion(p, i));
  }
  CHECK(p.length == 32);
  CHECK(p.swing == 40);
  CHECK(p.gate == 70);
  CHECK(p.transpose == 5);
}

TEST_CASE("set_length accepts 8, 16, 32 and 64 and keeps the hidden steps") {
  Pattern p = make({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
  CHECK(set_length(p, 8));
  CHECK(p.length == 8);
  CHECK(p.steps[12].note == 13);  // hidden, not lost
  CHECK(set_length(p, 16));
  CHECK(p.steps[12].note == 13);
  CHECK(set_length(p, 32));
  CHECK(set_length(p, 64));
  CHECK(p.length == 64);
  for (const unsigned bad : {0u, 1u, 7u, 9u, 12u, 15u, 17u, 24u, 48u, 63u, 65u, 128u}) {
    CHECK_FALSE(set_length(p, bad));
    CHECK(p.length == 64);
  }
}

TEST_CASE("swing, gate and transpose are clamped to their ranges") {
  Pattern p = make({});
  set_swing(p, 50);
  CHECK(p.swing == 50);
  set_swing(p, 200);
  CHECK(p.swing == kMaxSwing);
  set_swing(p, -3);
  CHECK(p.swing == 0);
  set_gate(p, 80);
  CHECK(p.gate == 80);
  set_gate(p, 0);
  CHECK(p.gate == 5);
  set_gate(p, 400);
  CHECK(p.gate == 100);
  set_transpose(p, -7);
  CHECK(p.transpose == -7);
  set_transpose(p, 99);
  CHECK(p.transpose == 24);
  set_transpose(p, -99);
  CHECK(p.transpose == -24);
}

TEST_CASE("motion: points, p-locks and their clearing") {
  Pattern p = make({});
  CHECK_FALSE(lane_has_motion(p, 0));
  CHECK_FALSE(step_has_motion(p, 0));
  set_motion_point(p, 0, 2, 1, 200);
  CHECK(pt(p, 0, 2, 1) == 200);
  CHECK(pt(p, 0, 2, 0) == kNoLock);
  CHECK(step_has_motion(p, 2));
  CHECK(lane_has_motion(p, 0));
  CHECK_FALSE(lane_has_motion(p, 1));
  set_motion_point(p, 0, 2, 1, 255);  // 255 is "no lock"
  CHECK(pt(p, 0, 2, 1) == kNoLock);
  set_motion_point(p, 0, 2, 1, 254);
  CHECK(pt(p, 0, 2, 1) == 254);

  set_plock(p, 5, 7, 99);
  for (unsigned q = 0; q < 4; ++q) {
    CHECK(pt(p, 5, 7, q) == 99);
  }
  set_plock(p, 6, 7, 10);
  clear_plock(p, 5, 7);
  CHECK(pt(p, 5, 7, 2) == kNoLock);
  CHECK(pt(p, 6, 7, 2) == 10);  // another lane of the step stays
  clear_step_motion(p, 7);
  CHECK_FALSE(step_has_motion(p, 7));
  CHECK(step_has_motion(p, 2));
  set_plock(p, 3, 10, 1);
  set_plock(p, 3, 20, 2);
  clear_lane_motion(p, 3);
  CHECK_FALSE(lane_has_motion(p, 3));
  CHECK(lane_has_motion(p, 0));
  clear_motion(p);
  for (unsigned l = 0; l < kMotionLanes; ++l) {
    CHECK_FALSE(lane_has_motion(p, l));
  }
  // Out of range arguments are ignored.
  set_motion_point(p, 8, 0, 0, 1);
  set_motion_point(p, 0, 0, 4, 1);
  clear_lane_motion(p, 9);
  CHECK_FALSE(lane_has_motion(p, 0));
}

TEST_CASE("a motion point is a knob position through the descriptor's curve") {
  using namespace o3sdk::core;
  static const char* const kWave[] = {"SAW", "SQR"};
  const ParamDesc linear{"LEVEL", "%", nullptr, nullptr, 0.0f, 100.0f, 50.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0};
  const ParamDesc log{"CUTOFF", "Hz", nullptr, nullptr, 25.0f, 4500.0f, 350.0f, 100, 10, 0, ParamUnit::Hz, ParamCurve::Log, 0, 0};
  const ParamDesc wave{"WAVE", "", kWave, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0};

  CHECK(value_to_point(linear, 0.0f) == 0);
  CHECK(value_to_point(linear, 100.0f) == kMaxPoint);
  CHECK(value_to_point(linear, 50.0f) == 127);
  CHECK(value_to_point(linear, 500.0f) == kMaxPoint);  // clamped
  CHECK(value_to_point(linear, -5.0f) == 0);
  CHECK(point_to_value(linear, 0) == doctest::Approx(0.0f));
  CHECK(point_to_value(linear, kMaxPoint) == doctest::Approx(100.0f));
  CHECK(point_to_value(linear, 127) == doctest::Approx(50.0f).epsilon(0.002));
  CHECK(point_to_value(linear, kNoLock) == doctest::Approx(100.0f));  // never past the maximum

  CHECK(point_to_value(log, 0) == doctest::Approx(25.0f).epsilon(0.01));
  CHECK(point_to_value(log, kMaxPoint) == doctest::Approx(4500.0f).epsilon(0.01));
  const float mid = point_to_value(log, 127);
  CHECK(mid > 300.0f);  // geometric: about sqrt(25 * 4500) = 335 Hz
  CHECK(mid < 370.0f);

  // Every point comes back as itself through the value (the knob position survives the round trip).
  for (unsigned v = 0; v <= kMaxPoint; ++v) {
    CHECK(value_to_point(linear, point_to_value(linear, static_cast<std::uint8_t>(v))) == v);
    CHECK(value_to_point(log, point_to_value(log, static_cast<std::uint8_t>(v))) == v);
  }

  // A choice snaps to its entries.
  CHECK(point_to_value(wave, 0) == 0.0f);
  CHECK(point_to_value(wave, 60) == 0.0f);
  CHECK(point_to_value(wave, 200) == 1.0f);
  CHECK(point_to_value(wave, kMaxPoint) == 1.0f);
  CHECK(value_to_point(wave, 1.0f) == kMaxPoint);
}

TEST_CASE("Rng: the same seed is the same stream; seed 0 is a seed of its own") {
  Rng a(7);
  Rng b(7);
  Rng c(8);
  bool differs = false;
  for (int i = 0; i < 100; ++i) {
    const std::uint32_t x = a.next();
    CHECK(x == b.next());
    differs = differs || x != c.next();
  }
  CHECK(differs);
  Rng z(0);
  Rng d(0x30303030u);
  CHECK(z.next() == d.next());
  Rng r(1);
  for (int i = 0; i < 1000; ++i) {
    const int v = r.next_int(-3, 4);
    CHECK(v >= -3);
    CHECK(v <= 4);
    const float f = r.next_float();
    CHECK(f >= 0.0f);
    CHECK(f < 1.0f);
  }
}

namespace {

bool in_scale(const GenParams& g, unsigned note) {
  const unsigned mask = o3sdk::core::kScaleDef[g.scale];
  return ((mask >> ((note + 12u - g.root) % 12u)) & 1u) != 0u;
}

}  // namespace

TEST_CASE("random_fill is deterministic, in the scale and in the range") {
  GenParams g;
  g.scale = 18;  // minor pentatonic
  g.root = 9;    // A
  g.low = 33;
  g.high = 57;
  Pattern a = make({});
  Pattern b = make({});
  random_fill(a, g, 1234);
  random_fill(b, g, 1234);
  CHECK(same(a, b));
  Pattern c = make({});
  random_fill(c, g, 1235);
  CHECK_FALSE(same(a, c));
  for (unsigned i = 0; i < a.length; ++i) {
    if (!has_flag(a, i, kStepRest)) {  // a rest keeps whatever note it had
      CHECK(a.steps[i].note >= g.low);
      CHECK(a.steps[i].note <= g.high);
      CHECK(in_scale(g, a.steps[i].note));
    }
    CHECK_FALSE(has_flag(a, i, kStepTie));
  }
  CHECK_FALSE(has_flag(a, 0, kStepRest));  // step 0 always sounds
  // The steps above the length and the motion are untouched.
  Pattern d = make({});
  d.length = 8;
  set_plock(d, 0, 2, 40);
  const Pattern before = d;
  random_fill(d, g, 9);
  for (unsigned i = 8; i < kMaxSteps; ++i) {
    CHECK(d.steps[i].note == before.steps[i].note);
    CHECK(d.steps[i].flags == before.steps[i].flags);
  }
  CHECK(pt(d, 0, 2, 1) == 40);
}

TEST_CASE("random_fill: density, accent and slide percentages") {
  GenParams g;
  Pattern p = make({}, 64);

  g.density = 100;
  g.accent_pct = 0;
  g.slide_pct = 0;
  random_fill(p, g, 5);
  for (unsigned i = 0; i < 64; ++i) {
    CHECK(p.steps[i].flags == 0);
  }

  g.accent_pct = 100;
  g.slide_pct = 100;
  random_fill(p, g, 5);
  for (unsigned i = 0; i < 64; ++i) {
    CHECK(p.steps[i].flags == (kStepAccent | kStepSlide));
  }

  g.density = 0;
  random_fill(p, g, 5);
  CHECK_FALSE(has_flag(p, 0, kStepRest));
  for (unsigned i = 1; i < 64; ++i) {
    CHECK(p.steps[i].flags == kStepRest);
  }

  // About the asked share, over many seeds.
  g.density = 60;
  g.accent_pct = 30;
  g.slide_pct = 20;
  unsigned sounding = 0;
  unsigned accents = 0;
  unsigned slides = 0;
  for (std::uint32_t seed = 1; seed <= 200; ++seed) {
    random_fill(p, g, seed);
    for (unsigned i = 1; i < 64; ++i) {
      if (!has_flag(p, i, kStepRest)) {
        ++sounding;
        accents += has_flag(p, i, kStepAccent) ? 1u : 0u;
        slides += has_flag(p, i, kStepSlide) ? 1u : 0u;
      }
    }
  }
  const float total = 200.0f * 63.0f;
  CHECK(static_cast<float>(sounding) / total == doctest::Approx(0.60f).epsilon(0.05));
  CHECK(static_cast<float>(accents) / static_cast<float>(sounding) == doctest::Approx(0.30f).epsilon(0.08));
  CHECK(static_cast<float>(slides) / static_cast<float>(sounding) == doctest::Approx(0.20f).epsilon(0.08));
}

TEST_CASE("random_fill: a single note range gives a single pitch, an empty scale falls back to the range") {
  GenParams g;
  g.low = 40;
  g.high = 40;
  Pattern p = make({});
  random_fill(p, g, 3);
  for (unsigned i = 0; i < p.length; ++i) {
    if (!has_flag(p, i, kStepRest)) {
      CHECK(p.steps[i].note == 40);
    }
  }
  GenParams swapped;
  swapped.low = 50;
  swapped.high = 30;  // reversed bounds are accepted
  random_fill(p, swapped, 3);
  for (unsigned i = 0; i < p.length; ++i) {
    if (!has_flag(p, i, kStepRest)) {
      CHECK(p.steps[i].note >= 30);
      CHECK(p.steps[i].note <= 50);
    }
  }
}

TEST_CASE("mutate changes about the asked share of the steps, in the scale") {
  GenParams g;
  g.scale = 2;  // minor
  g.root = 0;
  g.low = 24;
  g.high = 60;
  g.density = 100;
  g.accent_pct = 50;
  g.slide_pct = 50;
  Pattern base = make({}, 64);
  random_fill(base, g, 11);

  Pattern none = base;
  mutate(none, g, 3, 0);
  CHECK(same(none, base));

  Pattern all = base;
  mutate(all, g, 3, 100);
  unsigned changed = 0;
  for (unsigned i = 0; i < 64; ++i) {
    changed += (all.steps[i].note != base.steps[i].note || all.steps[i].flags != base.steps[i].flags) ? 1u : 0u;
    CHECK(in_scale(g, all.steps[i].note));
    CHECK(all.steps[i].note >= g.low);
    CHECK(all.steps[i].note <= g.high);
  }
  CHECK(changed > 20);  // 100 %: every step is drawn, about half of the draws change something
  CHECK(changed < 64);

  Pattern some = base;
  mutate(some, g, 3, 25);
  unsigned some_changed = 0;
  for (unsigned i = 0; i < 64; ++i) {
    some_changed += (some.steps[i].note != base.steps[i].note || some.steps[i].flags != base.steps[i].flags) ? 1u : 0u;
  }
  CHECK(some_changed > 0);
  CHECK(some_changed < changed);

  Pattern again = base;
  mutate(again, g, 3, 25);
  CHECK(same(again, some));  // deterministic from the seed

  // Mutating never touches motion or the steps above the length.
  Pattern short_p = make({}, 8);
  set_plock(short_p, 0, 1, 3);
  const Pattern before = short_p;
  mutate(short_p, g, 77, 100);
  CHECK(pt(short_p, 0, 1, 0) == 3);
  for (unsigned i = 8; i < kMaxSteps; ++i) {
    CHECK(short_p.steps[i].note == before.steps[i].note);
  }
}

TEST_CASE("a motion point on a Table-curve parameter (tb303's Cutoff) follows the table and round-trips") {
  o3_303::Tb303 voice;
  const o3sdk::core::ParamDesc& cutoff = voice.info().params[o3_303::Tb303::Cutoff];
  REQUIRE(cutoff.curve == o3sdk::core::ParamCurve::Table);
  CHECK(point_to_value(cutoff, 0) == doctest::Approx(25.0f));
  CHECK(point_to_value(cutoff, kMaxPoint) == doctest::Approx(4500.0f));
  float previous = 0.0f;
  for (unsigned v = 0; v <= kMaxPoint; ++v) {
    const float hz = point_to_value(cutoff, static_cast<std::uint8_t>(v));
    CHECK(hz >= previous);  // monotone
    previous = hz;
    CHECK(value_to_point(cutoff, hz) == v);
  }
  // A knob at 35 % of its travel is at the table's value there (about 245 Hz at 0.3484), not the geometric middle.
  const float at35 = point_to_value(cutoff, static_cast<std::uint8_t>(0.3484f * 254.0f + 0.5f));
  CHECK(at35 > 200.0f);
  CHECK(at35 < 300.0f);
}
