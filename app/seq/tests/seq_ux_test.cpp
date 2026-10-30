// SPDX-License-Identifier: GPL-3.0-or-later
// The UX operations of o3_303/seq/ops.h and bank.h (docs/UX-303.md 2.3, 5.1, 5.2, D2, D8): regen_rhythm, regen_notes, the PLAYS and GATE
// lists, claim_lane, resize, and the Bank's one-edit wrappers.
#include <doctest/doctest.h>

#include <o3_303/seq/bank.h>
#include <o3sdk/core/scales.h>

#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>

using namespace o3_303;
using namespace o3_303::seq;

namespace {

bool same(const Pattern& a, const Pattern& b) { return std::memcmp(&a, &b, sizeof(Pattern)) == 0; }

bool same_motion(const Pattern& a, const Pattern& b) { return std::memcmp(a.motion, b.motion, sizeof(a.motion)) == 0; }

// A pattern with something on every step (a mix of rests, ties, accents, slides, ratchets, conditions), distinct notes, and motion on
// lanes 0 and 3 (one point of each step marks it), length 16, with different data above the length.
Pattern make_line(unsigned length = 16) {
  Pattern p;
  clear(p);
  p.length = static_cast<std::uint8_t>(length);
  for (unsigned i = 0; i < kMaxSteps; ++i) {
    Step& s = p.steps[i];
    s.note = static_cast<std::uint8_t>(30 + (i * 7u) % 40u);
    switch (i % 5u) {
      case 0: s.flags = 0; break;
      case 1: s.flags = kStepAccent; break;
      case 2: s.flags = kStepRest; break;
      case 3: s.flags = kStepTie; break;
      default: s.flags = kStepSlide | kStepAccent; break;
    }
    s.flags = with_ratchet(s.flags, i % 4u);
    s.cond = (i % 3u == 0u) ? cond_ratio(1, 2) : kCondAlways;
    p.motion[0][i * kMotionPointsPerStep + 1] = static_cast<std::uint8_t>(i);
    p.motion[3][i * kMotionPointsPerStep] = static_cast<std::uint8_t>(100 + i);
  }
  return p;
}

std::unique_ptr<Bank> make_bank() {
  auto b = std::make_unique<Bank>();
  b->init();
  return b;
}

bool in_scale(const GenParams& g, unsigned note) {
  const unsigned mask = o3sdk::core::kScaleDef[g.scale];
  return ((mask >> ((note + 12u - g.root) % 12u)) & 1u) != 0u;
}

}  // namespace

// ---- regen_rhythm -----------------------------------------------------------------------------------------------------------

TEST_CASE("regen_rhythm keeps every pitch, ratchet and condition, motion and the hidden steps") {
  const Pattern before = make_line(16);
  Pattern p = before;
  GenParams g;
  REQUIRE(regen_rhythm(p, g, 1234));
  for (unsigned i = 0; i < kMaxSteps; ++i) {
    CHECK(p.steps[i].note == before.steps[i].note);
    CHECK(p.steps[i].cond == before.steps[i].cond);
    CHECK(ratchet_of(p.steps[i].flags) == ratchet_of(before.steps[i].flags));
    if (i >= 16) {
      CHECK(p.steps[i].flags == before.steps[i].flags);  // above the length: untouched
    }
  }
  CHECK(same_motion(p, before));
  CHECK(std::memcmp(p.lanes, before.lanes, sizeof(p.lanes)) == 0);
  CHECK(p.length == before.length);
  CHECK(p.swing == before.swing);
  CHECK(p.gate == before.gate);
  CHECK(p.transpose == before.transpose);
}

TEST_CASE("regen_rhythm redraws the flags from the density and the percentages") {
  GenParams g;
  Pattern p = make_line(64);
  g.density = 100;
  g.accent_pct = 0;
  g.slide_pct = 0;
  regen_rhythm(p, g, 5);
  for (unsigned i = 0; i < 64; ++i) {
    CHECK((p.steps[i].flags & (kStepAccent | kStepSlide | kStepTie | kStepRest)) == 0);  // every step plain: the ties and rests went
  }
  g.density = 0;
  regen_rhythm(p, g, 5);
  CHECK((p.steps[0].flags & kStepRest) == 0);  // step 0 always sounds
  for (unsigned i = 1; i < 64; ++i) {
    CHECK((p.steps[i].flags & kStepRest) != 0);
  }
  g.density = 100;
  g.accent_pct = 100;
  g.slide_pct = 100;
  regen_rhythm(p, g, 5);
  for (unsigned i = 0; i < 64; ++i) {
    CHECK((p.steps[i].flags & (kStepAccent | kStepSlide)) == (kStepAccent | kStepSlide));
    CHECK((p.steps[i].flags & kStepTie) == 0);
  }
  // At 50 % over 64 steps both outcomes occur.
  g.density = 50;
  g.accent_pct = 50;
  g.slide_pct = 50;
  regen_rhythm(p, g, 99);
  unsigned rests = 0, accents = 0, slides = 0;
  for (unsigned i = 0; i < 64; ++i) {
    rests += (p.steps[i].flags & kStepRest) != 0 ? 1u : 0u;
    accents += (p.steps[i].flags & kStepAccent) != 0 ? 1u : 0u;
    slides += (p.steps[i].flags & kStepSlide) != 0 ? 1u : 0u;
  }
  CHECK(rests > 10);
  CHECK(rests < 54);
  CHECK(accents > 5);
  CHECK(slides > 5);
}

TEST_CASE("regen_rhythm is deterministic from the seed, and reports no change when it changes nothing") {
  const GenParams g;
  Pattern a = make_line();
  Pattern b = make_line();
  regen_rhythm(a, g, 77);
  regen_rhythm(b, g, 77);
  CHECK(same(a, b));
  CHECK_FALSE(regen_rhythm(a, g, 77));  // the same draw again lands on the same flags
  Pattern c = make_line();
  regen_rhythm(c, g, 78);
  CHECK_FALSE(same(a, c));
  // A seed of 0 is legal (Rng maps it) and equals itself.
  Pattern d = make_line();
  Pattern e = make_line();
  regen_rhythm(d, g, 0);
  regen_rhythm(e, g, 0);
  CHECK(same(d, e));
}

TEST_CASE("regen_rhythm works on the steps below the length at every length") {
  for (const unsigned length : {8u, 16u, 32u, 64u}) {
    const Pattern before = make_line(length);
    Pattern p = before;
    regen_rhythm(p, GenParams{}, 4321);
    for (unsigned i = length; i < kMaxSteps; ++i) {
      CHECK(p.steps[i].flags == before.steps[i].flags);
    }
    CHECK(same_motion(p, before));
  }
}

// ---- regen_notes ------------------------------------------------------------------------------------------------------------

TEST_CASE("regen_notes keeps the rhythm and the flags, and redraws the sounding pitches in scale and range") {
  GenParams g;
  g.scale = 18;  // Penta Min
  g.root = 2;
  g.low = 36;
  g.high = 60;
  const Pattern before = make_line(32);
  Pattern p = before;
  REQUIRE(regen_notes(p, g, 31337));
  unsigned moved = 0;
  for (unsigned i = 0; i < kMaxSteps; ++i) {
    CHECK(p.steps[i].flags == before.steps[i].flags);
    CHECK(p.steps[i].cond == before.steps[i].cond);
    if (i >= 32 || (before.steps[i].flags & (kStepRest | kStepTie)) != 0) {
      CHECK(p.steps[i].note == before.steps[i].note);  // a rest, a tie and the hidden steps keep their pitch
      continue;
    }
    CHECK(p.steps[i].note >= g.low);
    CHECK(p.steps[i].note <= g.high);
    CHECK(in_scale(g, p.steps[i].note));
    moved += p.steps[i].note != before.steps[i].note ? 1u : 0u;
  }
  CHECK(moved > 5);
  CHECK(same_motion(p, before));
  CHECK(std::memcmp(p.lanes, before.lanes, sizeof(p.lanes)) == 0);
  CHECK(p.length == before.length);
}

TEST_CASE("regen_notes is deterministic from the seed and changes nothing on a silent pattern") {
  const GenParams g;
  Pattern a = make_line();
  Pattern b = make_line();
  regen_notes(a, g, 9);
  regen_notes(b, g, 9);
  CHECK(same(a, b));
  CHECK_FALSE(regen_notes(a, g, 9));
  Pattern c = make_line();
  regen_notes(c, g, 10);
  CHECK_FALSE(same(a, c));

  Pattern silent;
  clear(silent);
  const Pattern copy = silent;
  CHECK_FALSE(regen_notes(silent, g, 9));
  CHECK(same(silent, copy));
}

TEST_CASE("regen_notes survives a scale that leaves nothing and a reversed range") {
  GenParams g;
  g.low = 50;
  g.high = 40;  // reversed: swapped
  Pattern p = make_line();
  regen_notes(p, g, 3);
  for (unsigned i = 0; i < 16; ++i) {
    if ((p.steps[i].flags & (kStepRest | kStepTie)) == 0) {
      CHECK(p.steps[i].note >= 40);
      CHECK(p.steps[i].note <= 50);
    }
  }
  g.scale = 200;  // out of the table: falls back to scale 0
  g.high = 127;
  g.low = 120;
  regen_notes(p, g, 3);
  for (unsigned i = 0; i < 16; ++i) {
    if ((p.steps[i].flags & (kStepRest | kStepTie)) == 0) {
      CHECK(p.steps[i].note >= 120);
      CHECK(p.steps[i].note <= 127);
    }
  }
}

// ---- The PLAYS list (D8) ----------------------------------------------------------------------------------------------------

TEST_CASE("the PLAYS list has 17 entries in the order of UX-303 2.3") {
  static_assert(kPlaysCount == 17, "x1..x4, 75 50 25, six ratios, four named");
  for (unsigned i = 0; i < 4; ++i) {
    CHECK(plays_at(i).ratchet == i);
    CHECK(plays_at(i).cond == kCondAlways);
  }
  CHECK(plays_at(4).cond == 75);
  CHECK(plays_at(5).cond == 50);
  CHECK(plays_at(6).cond == 25);
  CHECK(plays_at(7).cond == cond_ratio(1, 2));
  CHECK(plays_at(8).cond == cond_ratio(2, 2));
  CHECK(plays_at(9).cond == cond_ratio(1, 4));
  CHECK(plays_at(10).cond == cond_ratio(2, 4));
  CHECK(plays_at(11).cond == cond_ratio(3, 4));
  CHECK(plays_at(12).cond == cond_ratio(4, 4));
  CHECK(plays_at(13).cond == kCondFill);
  CHECK(plays_at(14).cond == kCondNotFill);
  CHECK(plays_at(15).cond == kCondFirst);
  CHECK(plays_at(16).cond == kCondNotFirst);
  for (unsigned i = 4; i < kPlaysCount; ++i) {
    CHECK(plays_at(i).ratchet == 0);
  }
  CHECK(plays_at(17).cond == kCondNotFirst);  // past the end: the last
  CHECK(plays_at(1000).cond == kCondNotFirst);
}

TEST_CASE("set_plays sets one of ratchet and condition and clears the other; every entry round-trips") {
  Pattern p = make_line();
  for (unsigned i = 0; i < kPlaysCount; ++i) {
    const std::uint8_t flags_before = static_cast<std::uint8_t>(p.steps[3].flags & ~kStepRatchetMask);
    set_plays(p, 3, i);
    CHECK(plays_index(p.steps[3].flags, p.steps[3].cond) == i);
    CHECK(plays_exact(p.steps[3].flags, p.steps[3].cond));
    CHECK(p.steps[3].cond == plays_at(i).cond);
    CHECK(ratchet_of(p.steps[3].flags) == plays_at(i).ratchet);
    CHECK((p.steps[3].flags & ~kStepRatchetMask) == flags_before);  // the gate bits stay
  }
  // A ratchet entry clears the condition, a condition entry clears the ratchet.
  p.steps[4].cond = cond_ratio(1, 2);
  p.steps[4].flags = with_ratchet(0, 2);
  CHECK(set_plays(p, 4, 3));  // x4
  CHECK(p.steps[4].cond == kCondAlways);
  CHECK(ratchet_of(p.steps[4].flags) == 3);
  CHECK(set_plays(p, 4, 6));  // 25%
  CHECK(p.steps[4].cond == 25);
  CHECK(ratchet_of(p.steps[4].flags) == 0);
  CHECK_FALSE(set_plays(p, 4, 6));  // already there
  CHECK(set_plays(p, 4, 999));      // past the end: NOT FIRST
  CHECK(p.steps[4].cond == kCondNotFirst);
}

TEST_CASE("PLAYS shows a stray value as itself and moves to the nearest entry") {
  // A step with a condition and a ratchet (old data) shows the condition, which is on the list.
  CHECK(plays_index(with_ratchet(0, 2), cond_ratio(3, 4)) == 11);
  CHECK(plays_exact(with_ratchet(0, 2), cond_ratio(3, 4)));
  // Probabilities: the nearest of 75 / 50 / 25.
  CHECK(plays_index(0, 90) == 4);
  CHECK(plays_index(0, 63) == 4);
  CHECK(plays_index(0, 62) == 5);
  CHECK(plays_index(0, 38) == 5);
  CHECK(plays_index(0, 37) == 6);
  CHECK(plays_index(0, 10) == 6);
  CHECK(plays_index(0, 1) == 6);
  for (const unsigned pct : {10u, 20u, 30u, 33u, 40u, 60u, 70u, 80u, 90u, 99u}) {
    CHECK_FALSE(plays_exact(0, static_cast<std::uint8_t>(pct)));
  }
  CHECK(plays_exact(0, 75));
  CHECK(plays_exact(0, 50));
  CHECK(plays_exact(0, 25));
  // Ratios outside the list: the nearest cycle, then the nearest position.
  CHECK(plays_index(0, cond_ratio(1, 8)) == 9);  // 1:4
  CHECK(plays_index(0, cond_ratio(8, 8)) == 12);  // 4:4
  CHECK(plays_index(0, cond_ratio(5, 8)) == 10);  // 2:4 (on the tie, the earlier)
  CHECK(plays_index(0, cond_ratio(7, 8)) == 11);  // 3:4 (on the tie, the earlier)
  CHECK(plays_index(0, cond_ratio(1, 3)) == 7);   // B = 3 goes to 2
  CHECK(plays_index(0, cond_ratio(3, 3)) == 8);   // 2:2
  CHECK(plays_index(0, cond_ratio(3, 6)) == 10);  // B = 6 goes to 4; 3/6 is exactly 2:4
  CHECK_FALSE(plays_exact(0, cond_ratio(1, 3)));
  CHECK_FALSE(plays_exact(0, cond_ratio(1, 8)));
  // A reserved byte plays as always: it shows the ratchet and is not exact.
  CHECK(plays_index(with_ratchet(0, 1), 100) == 1);
  CHECK_FALSE(plays_exact(with_ratchet(0, 1), 100));
  CHECK_FALSE(plays_exact(0, 0xC9));
  CHECK(plays_index(with_ratchet(0, 2), 0x90) == 2);  // a ratio with a > b (3:1): reserved, plays as always
  CHECK_FALSE(plays_exact(with_ratchet(0, 2), 0x90));
}

TEST_CASE("plays_step walks the list, clamped, and lands a stray value on the nearest entry first") {
  CHECK(plays_step(0, kCondAlways, 0) == 0);
  CHECK(plays_step(0, kCondAlways, 1) == 1);
  CHECK(plays_step(0, kCondAlways, 5) == 5);
  CHECK(plays_step(0, kCondAlways, -3) == 0);
  CHECK(plays_step(0, kCondAlways, 100) == kPlaysCount - 1);
  CHECK(plays_step(with_ratchet(0, 3), kCondAlways, -2) == 1);
  CHECK(plays_step(0, kCondNotFirst, 1) == kPlaysCount - 1);
  CHECK(plays_step(0, kCondNotFirst, -1) == 15);
  // 33 % shows 25 % (index 6): a turn either way lands there, further detents go on from it.
  CHECK(plays_step(0, 33, 0) == 6);
  CHECK(plays_step(0, 33, 1) == 6);
  CHECK(plays_step(0, 33, -1) == 6);
  CHECK(plays_step(0, 33, 2) == 7);
  CHECK(plays_step(0, 33, -2) == 5);
  CHECK(plays_step(0, 33, 50) == kPlaysCount - 1);
  CHECK(plays_step(0, 33, -50) == 0);
  // Huge turns clamp without overflow.
  CHECK(plays_step(0, kCondAlways, std::numeric_limits<int>::max()) == kPlaysCount - 1);
  CHECK(plays_step(0, kCondAlways, std::numeric_limits<int>::min()) == 0);
  CHECK(plays_step(0, kCondNotFirst, std::numeric_limits<int>::max()) == kPlaysCount - 1);
  CHECK(plays_step(0, 33, std::numeric_limits<int>::max()) == kPlaysCount - 1);
  CHECK(plays_step(0, 33, std::numeric_limits<int>::min()) == 0);
}

// ---- The GATE list ----------------------------------------------------------------------------------------------------------

TEST_CASE("the GATE list maps REST, NOTE, ACC, SLD, ACC+SLD and TIE to and from the flags") {
  static_assert(kGateListCount == 6, "REST NOTE ACC SLD ACC+SLD TIE");
  CHECK(gate_list_flags(kGateRest) == kStepRest);
  CHECK(gate_list_flags(kGateNote) == 0);
  CHECK(gate_list_flags(kGateAcc) == kStepAccent);
  CHECK(gate_list_flags(kGateSld) == kStepSlide);
  CHECK(gate_list_flags(kGateAccSld) == (kStepAccent | kStepSlide));
  CHECK(gate_list_flags(kGateTie) == kStepTie);
  CHECK(gate_list_flags(77) == kStepTie);
  for (unsigned i = 0; i < kGateListCount; ++i) {
    CHECK(gate_list_index(gate_list_flags(i)) == i);
    for (unsigned r = 0; r <= kMaxRatchet; ++r) {
      CHECK(gate_list_index(with_ratchet(gate_list_flags(i), r)) == i);  // the ratchet is not part of the list
    }
  }
  // Stray combinations show a sensible entry: a rest wins, then a tie.
  CHECK(gate_list_index(kStepRest | kStepAccent) == kGateRest);
  CHECK(gate_list_index(kStepRest | kStepTie) == kGateRest);
  CHECK(gate_list_index(kStepTie | kStepSlide | kStepAccent) == kGateTie);
}

TEST_CASE("set_gate_list replaces the four gate bits and keeps the ratchet, the note and the condition") {
  Pattern p = make_line();
  Step& s = p.steps[6];
  s.flags = with_ratchet(kStepRest | kStepAccent, 2);
  s.cond = 50;
  s.note = 61;
  CHECK(set_gate_list(p, 6, kGateAccSld));
  CHECK(s.flags == with_ratchet(kStepAccent | kStepSlide, 2));
  CHECK(s.cond == 50);
  CHECK(s.note == 61);
  CHECK_FALSE(set_gate_list(p, 6, kGateAccSld));
  CHECK(set_gate_list(p, 6, kGateListCount));  // past the end: the last entry, TIE (as plays_at)
  CHECK(s.flags == with_ratchet(kStepTie, 2));
  CHECK_FALSE(set_gate_list(p, 6, 1000));
  CHECK(set_gate_list(p, 6, kGateAccSld));
  CHECK(set_gate_list(p, 6, kGateTie));
  CHECK(s.flags == with_ratchet(kStepTie, 2));
  CHECK(set_gate_list(p, 6, kGateRest));
  CHECK(s.flags == with_ratchet(kStepRest, 2));
  CHECK(set_gate_list(p, 6, kGateNote));
  CHECK(s.flags == with_ratchet(0, 2));
  CHECK((s.flags & (kStepRest | kStepTie)) == 0);
}

// ---- claim_lane (D2) --------------------------------------------------------------------------------------------------------

TEST_CASE("claim_lane returns the lane that already drives the target and changes nothing") {
  Pattern p;
  clear(p);
  p.motion[2][0] = 10;  // a lock on the voice's Env Mod
  const Pattern before = p;
  CHECK(claim_lane(p, kLaneVoice, 2) == 2);
  CHECK(claim_lane(p, kLaneMixer, kMixerLevel) == 7);
  CHECK(same(p, before));
  // Two lanes on one target: the lowest.
  p.lanes[5] = LaneRef{kLaneVoice, 2};
  CHECK(claim_lane(p, kLaneVoice, 2) == 2);
}

TEST_CASE("claim_lane re-points the first lane without points, and -1 when all eight are used") {
  Pattern p;
  clear(p);
  p.motion[0][3] = 20;  // lane 0 has a point; lanes 1..7 are empty
  p.motion[1][9] = 20;
  const int lane = claim_lane(p, kLaneDelay, 4);
  CHECK(lane == 2);  // the first free lane
  CHECK(p.lanes[2].target == kLaneDelay);
  CHECK(p.lanes[2].id == 4);
  CHECK(p.lanes[0].target == kDefaultLanes[0].target);  // the others are as they were
  CHECK(p.lanes[1].id == kDefaultLanes[1].id);
  CHECK(p.lanes[3].id == kDefaultLanes[3].id);
  CHECK(claim_lane(p, kLaneDelay, 4) == 2);  // now it drives the target
  // Give every lane a point: no lane is left.
  for (unsigned l = 0; l < kMotionLanes; ++l) {
    p.motion[l][0] = 1;
  }
  const Pattern before = p;
  CHECK(claim_lane(p, kLaneGrowl, 3) == -1);
  CHECK(same(p, before));
  CHECK(claim_lane(p, kLaneDelay, 4) == 2);  // a driven target is still found
  // A freed lane (its last lock removed) is claimable again.
  clear_lane_motion(p, 5);
  CHECK(claim_lane(p, kLaneGrowl, 3) == 5);
  CHECK(p.lanes[5].target == kLaneGrowl);
}

// ---- resize -----------------------------------------------------------------------------------------------------------------

TEST_CASE("resize up copies the steps and their motion into the new part, repeatedly") {
  Pattern p = make_line(8);
  const Pattern before = p;
  REQUIRE(resize(p, 32));
  CHECK(p.length == 32);
  for (unsigned i = 0; i < 32; ++i) {
    const unsigned src = i % 8u;
    CHECK(p.steps[i].note == before.steps[src].note);
    CHECK(p.steps[i].flags == before.steps[src].flags);
    CHECK(p.steps[i].cond == before.steps[src].cond);
    for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
      for (unsigned q = 0; q < kMotionPointsPerStep; ++q) {
        CHECK(p.motion[lane][i * kMotionPointsPerStep + q] == before.motion[lane][src * kMotionPointsPerStep + q]);
      }
    }
  }
  for (unsigned i = 32; i < kMaxSteps; ++i) {
    CHECK(p.steps[i].note == before.steps[i].note);  // above the new length: untouched
    CHECK(p.motion[3][i * kMotionPointsPerStep] == before.motion[3][i * kMotionPointsPerStep]);
  }
  CHECK(p.swing == before.swing);
  CHECK(p.gate == before.gate);
  CHECK(p.transpose == before.transpose);
  CHECK(std::memcmp(p.lanes, before.lanes, sizeof(p.lanes)) == 0);
}

TEST_CASE("resize 16 to 32 is double_length") {
  Pattern a = make_line(16);
  Pattern b = a;
  REQUIRE(resize(a, 32));
  REQUIRE(double_length(b));
  CHECK(same(a, b));
}

TEST_CASE("resize down truncates: the cut steps become rests and lose their motion") {
  Pattern p = make_line(32);
  const Pattern before = p;
  REQUIRE(resize(p, 16));
  CHECK(p.length == 16);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(p.steps[i].note == before.steps[i].note);
    CHECK(p.steps[i].flags == before.steps[i].flags);
  }
  for (unsigned i = 16; i < 32; ++i) {
    CHECK(p.steps[i].flags == kStepRest);
    CHECK(p.steps[i].cond == kCondAlways);
    CHECK(p.steps[i].note == kDefaultNote);
    for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
      for (unsigned q = 0; q < kMotionPointsPerStep; ++q) {
        CHECK(p.motion[lane][i * kMotionPointsPerStep + q] == kNoLock);
      }
    }
  }
  for (unsigned i = 32; i < kMaxSteps; ++i) {
    CHECK(p.steps[i].note == before.steps[i].note);  // already hidden before: not this resize's business
  }
  CHECK(std::memcmp(p.lanes, before.lanes, sizeof(p.lanes)) == 0);
  REQUIRE(resize(p, 8));
  CHECK(p.steps[12].flags == kStepRest);
  REQUIRE(resize(p, 64));  // growing copies the 8 steps that are left
  CHECK(p.steps[8].note == before.steps[0].note);
  CHECK(p.steps[63].note == before.steps[63 % 8].note);
}

TEST_CASE("resize down frees a lane whose only locks were above the new length (D2)") {
  Pattern p;
  clear(p);
  p.length = 32;
  for (unsigned l = 0; l < kMotionLanes; ++l) {
    set_plock(p, l, l == 3 ? 20u : 2u, 40);  // lane 3's only lock is on step 20; the others are on step 2
  }
  CHECK(claim_lane(p, kLaneGrowl, 0) == -1);
  REQUIRE(resize(p, 8));
  CHECK_FALSE(lane_has_motion(p, 3));
  CHECK(claim_lane(p, kLaneGrowl, 0) == 3);
}

TEST_CASE("Bank resize down: one UNDO restores the steps and the points") {
  auto bank = make_bank();
  bank->resize(0, 32);
  for (unsigned i = 0; i < 32; ++i) {
    bank->set_note(0, i, 40 + i);
    bank->set_flag(0, i, kStepRest, false);
  }
  bank->set_plock(0, 3, 20, 77);
  bank->clear_history();
  const Pattern start = bank->pattern(0);
  REQUIRE(bank->resize(0, 8));
  CHECK_FALSE(lane_has_motion(bank->pattern(0), 3));
  CHECK_FALSE(bank->has_motion(0));
  CHECK(bank->pattern(0).steps[20].flags == kStepRest);
  CHECK(bank->undo() == 0);
  CHECK(same(bank->pattern(0), start));
  CHECK(bank->pattern(0).motion[3][20 * kMotionPointsPerStep] == 77);
  CHECK(bank->has_motion(0));
  CHECK_FALSE(bank->can_undo());
}

TEST_CASE("resize refuses an illegal length and the length it has") {
  Pattern p = make_line(16);
  const Pattern before = p;
  for (const unsigned bad : {0u, 7u, 12u, 15u, 17u, 24u, 48u, 65u, 128u}) {
    CHECK_FALSE(resize(p, bad));
  }
  CHECK_FALSE(resize(p, 16));
  CHECK(same(p, before));
  // A pattern with a corrupt length is repaired by any legal resize, reading it as the default length.
  Pattern q = make_line(16);
  q.length = 0;
  CHECK(resize(q, 32));
  CHECK(q.length == 32);
  CHECK(q.steps[16].note == q.steps[0].note);
}

// ---- Bank: one undoable edit each -------------------------------------------------------------------------------------------

TEST_CASE("Bank regen_rhythm, regen_notes and resize are one undo step each, and none when nothing changes") {
  auto bank = make_bank();
  for (unsigned i = 0; i < 16; ++i) {
    bank->set_note(0, i, 40 + i);
    bank->set_flag(0, i, kStepRest, false);
  }
  bank->clear_history();
  const GenParams g;
  const Pattern start = bank->pattern(0);

  REQUIRE(bank->regen_rhythm(0, g, 5));
  CHECK_FALSE(same(bank->pattern(0), start));
  CHECK_FALSE(bank->regen_rhythm(0, g, 5));  // the same draw: nothing changed, nothing recorded
  CHECK(bank->undo() == 0);
  CHECK(same(bank->pattern(0), start));
  CHECK_FALSE(bank->can_undo());

  REQUIRE(bank->regen_notes(0, g, 5));
  CHECK_FALSE(bank->regen_notes(0, g, 5));
  CHECK(bank->undo() == 0);
  CHECK(same(bank->pattern(0), start));
  CHECK_FALSE(bank->can_undo());

  REQUIRE(bank->resize(0, 64));
  CHECK(bank->pattern(0).length == 64);
  CHECK_FALSE(bank->resize(0, 64));
  CHECK_FALSE(bank->resize(0, 13));
  CHECK(bank->undo() == 0);
  CHECK(same(bank->pattern(0), start));
  CHECK_FALSE(bank->can_undo());
  CHECK(bank->redo() == 0);
  CHECK(bank->pattern(0).length == 64);

  CHECK_FALSE(bank->resize(kPatternCount, 8));  // no such pattern
  CHECK_FALSE(bank->regen_rhythm(kPatternCount, g, 1));
}

TEST_CASE("Bank set_gate_list and set_plays: a run of turns on one step is one undo step") {
  auto bank = make_bank();
  bank->clear_history();
  const Pattern start = bank->pattern(0);
  CHECK(bank->set_gate_list(0, 4, kGateNote));
  CHECK(bank->set_gate_list(0, 4, kGateAcc));
  CHECK(bank->set_gate_list(0, 4, kGateAccSld));
  CHECK_FALSE(bank->set_gate_list(0, 4, kGateAccSld));
  CHECK(bank->pattern(0).steps[4].flags == (kStepAccent | kStepSlide));
  CHECK(bank->set_plays(0, 4, 2));   // x3
  CHECK(bank->set_plays(0, 4, 7));   // 1:2
  CHECK(bank->pattern(0).steps[4].cond == cond_ratio(1, 2));
  CHECK(ratchet_of(bank->pattern(0).steps[4].flags) == 0);
  CHECK(bank->undo() == 0);  // the PLAYS run
  CHECK(bank->pattern(0).steps[4].flags == (kStepAccent | kStepSlide));
  CHECK(bank->pattern(0).steps[4].cond == kCondAlways);
  CHECK(bank->undo() == 0);  // the GATE run
  CHECK(same(bank->pattern(0), start));
  CHECK_FALSE(bank->can_undo());
  // A different step is a different undo step.
  bank->set_gate_list(0, 1, kGateTie);
  bank->set_gate_list(0, 2, kGateTie);
  bank->undo();
  CHECK(bank->pattern(0).steps[1].flags == kStepTie);
  CHECK(bank->pattern(0).steps[2].flags == kStepRest);
}

TEST_CASE("Bank claim_lane: one edit that re-points a free lane, none when the lane exists or all are used") {
  auto bank = make_bank();
  bank->clear_history();
  const Pattern start = bank->pattern(0);
  CHECK(bank->claim_lane(0, kLaneVoice, 3) == 3);  // already driving it
  CHECK_FALSE(bank->can_undo());
  CHECK(bank->claim_lane(0, kLaneDelay, 1) == 0);  // lane 0 has no points: re-pointed
  CHECK(bank->pattern(0).lanes[0].target == kLaneDelay);
  CHECK(bank->can_undo());
  CHECK(bank->claim_lane(0, kLaneDelay, 1) == 0);
  CHECK(bank->undo() == 0);
  CHECK(same(bank->pattern(0), start));
  CHECK_FALSE(bank->can_undo());
  // All eight used.
  for (unsigned l = 0; l < kMotionLanes; ++l) {
    bank->set_plock(0, l, 0, 10);
  }
  bank->clear_history();
  const Pattern full = bank->pattern(0);
  CHECK(bank->claim_lane(0, kLaneGrowl, 0) == -1);
  CHECK(same(bank->pattern(0), full));
  CHECK_FALSE(bank->can_undo());
}

TEST_CASE("Bank lock_step: the lock and its lane claim are one edit, one undo removes both") {
  auto bank = make_bank();
  bank->clear_history();
  const Pattern start = bank->pattern(0);
  // A knob that has no lane: the voice's Reso is on lane 1; the distortion's Drive has none.
  CHECK(bank->lock_step(0, kLaneVoice, 1, 5, 100) == 1);  // existing lane: only the lock
  CHECK(bank->undo() == 0);
  CHECK(same(bank->pattern(0), start));

  const int lane = bank->lock_step(0, kLaneDist, 2, 6, 200);
  REQUIRE(lane == 0);  // the first lane with no points
  const Pattern& p = bank->pattern(0);
  CHECK(p.lanes[0].target == kLaneDist);
  CHECK(p.lanes[0].id == 2);
  for (unsigned q = 0; q < kMotionPointsPerStep; ++q) {
    CHECK(p.motion[0][6 * kMotionPointsPerStep + q] == 200);
  }
  CHECK(bank->has_motion(0));
  // More turns on the same step and parameter are the same undo step.
  CHECK(bank->lock_step(0, kLaneDist, 2, 6, 210) == 0);
  CHECK(bank->lock_step(0, kLaneDist, 2, 6, 220) == 0);
  CHECK(p.motion[0][6 * kMotionPointsPerStep] == 220);
  CHECK(bank->undo() == 0);
  CHECK(same(bank->pattern(0), start));  // lock and lane gone together
  CHECK_FALSE(bank->can_undo());
  CHECK(bank->redo() == 0);
  CHECK(bank->pattern(0).lanes[0].target == kLaneDist);
  CHECK(bank->pattern(0).motion[0][6 * kMotionPointsPerStep] == 220);
}

TEST_CASE("Bank lock_step with no free lane changes nothing; unlock_step removes a lock; kNoLock is an unlock") {
  auto bank = make_bank();
  for (unsigned l = 0; l < kMotionLanes; ++l) {
    bank->set_plock(0, l, 0, 10);
  }
  bank->clear_history();
  const Pattern full = bank->pattern(0);
  CHECK(bank->lock_step(0, kLaneDelay, 2, 3, 50) == -1);
  CHECK(same(bank->pattern(0), full));
  CHECK_FALSE(bank->can_undo());
  CHECK(bank->lock_step(0, kLaneDelay, 2, 3, kNoLock) == -1);  // nothing drives it: nothing to remove, no claim
  CHECK(same(bank->pattern(0), full));

  // A lock on an existing lane, then removed.
  CHECK(bank->lock_step(0, kLaneVoice, 4, 7, 90) == 4);
  CHECK(lane_has_motion(bank->pattern(0), 4));
  CHECK(bank->unlock_step(0, kLaneVoice, 4, 7));
  CHECK(bank->pattern(0).motion[4][7 * kMotionPointsPerStep] == kNoLock);
  CHECK(bank->pattern(0).motion[4][0] == 10);  // the other lock stays
  CHECK_FALSE(bank->unlock_step(0, kLaneVoice, 4, 7));  // already none
  CHECK_FALSE(bank->unlock_step(0, kLaneGrowl, 1, 7));   // no lane
  CHECK(bank->lock_step(0, kLaneVoice, 4, 0, 90) == 4);
  CHECK(bank->lock_step(0, kLaneVoice, 4, 0, kNoLock) == 4);  // "--": the same as unlock_step
  CHECK(bank->pattern(0).motion[4][0] == kNoLock);
}

TEST_CASE("the regen and resize wrappers leave the motion alone except resize") {
  auto bank = make_bank();
  for (unsigned i = 0; i < 16; ++i) {
    bank->set_note(0, i, 40 + i);
    bank->set_flag(0, i, kStepRest, false);
    bank->set_plock(0, 1, i, static_cast<std::uint8_t>(i * 5));
  }
  const Pattern start = bank->pattern(0);
  bank->regen_rhythm(0, GenParams{}, 8);
  CHECK(same_motion(bank->pattern(0), start));
  bank->regen_notes(0, GenParams{}, 8);
  CHECK(same_motion(bank->pattern(0), start));
  bank->resize(0, 32);
  CHECK_FALSE(same_motion(bank->pattern(0), start));
  CHECK(bank->pattern(0).motion[1][20 * kMotionPointsPerStep] == 4 * 5);
}
