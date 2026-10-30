// SPDX-License-Identifier: GPL-3.0-or-later
// The pattern's M3b contracts (app/include/o3_303/pattern.h, docs/M3B-PLAN.md "Contracts"): the ratchet bits, the trig condition
// encoding, the lane table and the mixer's fixed ranges, the size and alignment; and that the code that builds, copies, generates and
// edits patterns carries the new fields (a step moves with its condition, a copy keeps the lanes, a fresh or generated pattern has the
// defaults, so M3's behaviour is the default).
#include <doctest/doctest.h>

#include <o3_303/pattern.h>
#include <o3_303/seq/bank.h>
#include <o3_303/seq/gen.h>
#include <o3_303/seq/ops.h>
#include <o3sdk/dsp/engine.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <type_traits>

using namespace o3_303;
namespace dsp = o3sdk::dsp;

namespace {

Pattern fresh() {
  Pattern p;
  clear(p);
  return p;
}

// A pattern whose steps all play, note = 40 + index, and each step i has its own ratchet and condition.
Pattern marked() {
  Pattern p = fresh();
  for (unsigned i = 0; i < kMaxSteps; ++i) {
    p.steps[i].note = static_cast<std::uint8_t>(40u + i);
    p.steps[i].flags = with_ratchet(0, i % 4u);
    p.steps[i].cond = cond_probability(1u + i);  // 1..64
  }
  return p;
}

bool same(const Pattern& a, const Pattern& b) { return std::memcmp(&a, &b, sizeof(Pattern)) == 0; }

}  // namespace

// ---- Layout -------------------------------------------------------------------------------------------------------------------

// The cross-checks with the engine: a drift in o3sdk's MixField breaks the build, not the sound.
static_assert(static_cast<unsigned>(dsp::MixField::Level) == kMixerLevel && static_cast<unsigned>(dsp::MixField::Pan) == kMixerPan &&
                  static_cast<unsigned>(dsp::MixField::SendA) == kMixerSendA && static_cast<unsigned>(dsp::MixField::SendB) == kMixerSendB &&
                  static_cast<unsigned>(dsp::MixField::Mute) == kMixerMute,
              "the mixer lane ids are dsp::MixField's values");
static_assert(dsp::kMixFieldCount == kMixerFieldCount, "the mixer has the fields pattern.h gives ranges to");

TEST_CASE("Pattern: 2,260 bytes, 4-byte aligned, a multiple of 4, laid out for word access") {
  CHECK(sizeof(Step) == 3);
  CHECK(sizeof(LaneRef) == 2);
  CHECK(sizeof(Pattern) == 2260);  // was 2,180 bytes (Step {note, flags}, no lanes)
  CHECK(sizeof(Pattern) % 4 == 0);
  CHECK(alignof(Pattern) == 4);
  CHECK(offsetof(Pattern, steps) == 0);
  CHECK(offsetof(Pattern, motion) == 196);
  CHECK(offsetof(Pattern, motion) % 4 == 0);
  CHECK(sizeof(Pattern::motion[0]) % 4 == 0);
  CHECK(offsetof(Pattern, lanes) == 196 + 8 * 256);
  CHECK(offsetof(Pattern, lanes) + sizeof(Pattern::lanes) == sizeof(Pattern));  // no padding at the end
  CHECK(std::is_trivial<Pattern>::value);
  CHECK(std::is_standard_layout<Pattern>::value);
  // An array of them keeps every element 4-byte aligned (the stride is a multiple of 4).
  Pattern two[2];
  CHECK(reinterpret_cast<std::uintptr_t>(&two[1]) % 4 == 0);
  CHECK(sizeof(two) == 2 * sizeof(Pattern));
}

// ---- The ratchet --------------------------------------------------------------------------------------------------------------

TEST_CASE("ratchet: bits 4-5 of the flags, 0 = one hit .. 3 = four hits, the other bits untouched") {
  static_assert(kStepRatchetShift == 4 && kStepRatchetMask == 0x30 && kMaxRatchet == 3);
  static_assert((kStepRatchetMask & (kStepAccent | kStepSlide | kStepTie | kStepRest)) == 0, "the ratchet does not overlap the flags");
  static_assert(ratchet_of(0) == 0 && ratchet_hits(0) == 1);
  static_assert(with_ratchet(0, 3) == 0x30 && ratchet_hits(0x30) == 4);

  for (unsigned f = 0; f < 256; ++f) {
    const auto flags = static_cast<std::uint8_t>(f);
    CHECK(ratchet_of(flags) == ((f >> 4) & 3u));
    CHECK(ratchet_hits(flags) == ratchet_of(flags) + 1u);
    for (unsigned r = 0; r <= kMaxRatchet; ++r) {
      const std::uint8_t w = with_ratchet(flags, r);
      CHECK(ratchet_of(w) == r);                                // round trip
      CHECK((w & ~kStepRatchetMask) == (flags & ~kStepRatchetMask));  // everything else kept
    }
    CHECK(ratchet_of(with_ratchet(flags, 4)) == kMaxRatchet);   // clamped, not wrapped
    CHECK(ratchet_of(with_ratchet(flags, 1000)) == kMaxRatchet);
    CHECK(with_ratchet(flags, ratchet_of(flags)) == flags);
  }
  // The flag bits the code tests do not see it: a step with a ratchet still reads as the same accent / slide / tie / rest.
  CHECK((with_ratchet(kStepAccent | kStepSlide, 2) & kStepAccent) != 0);
  CHECK((with_ratchet(kStepRest, 3) & kStepRest) != 0);
  CHECK((with_ratchet(kStepAccent, 3) & (kStepSlide | kStepTie | kStepRest)) == 0);
}

// ---- The trig condition -------------------------------------------------------------------------------------------------------

TEST_CASE("condition: the named values and the layout of A:B") {
  static_assert(kCondAlways == 0 && kCondMaxPercent == 99 && kCondRatio == 0x80);
  static_assert(kCondFill == 0xC0 && kCondNotFill == 0xC1 && kCondFirst == 0xC2 && kCondNotFirst == 0xC3);
  static_assert(cond_ratio(1, 1) == 0x80);
  static_assert(cond_ratio(2, 3) == (0x80 | (1 << 3) | 2));
  static_assert(cond_ratio(8, 8) == 0xBF);
  static_assert(cond_ratio(1, 8) == (0x80 | 7));
  static_assert(cond_ratio(7, 8) == (0x80 | (6 << 3) | 7));

  CHECK(decode_cond(0).kind == CondKind::Always);
  CHECK(decode_cond(kCondFill).kind == CondKind::Fill);
  CHECK(decode_cond(kCondNotFill).kind == CondKind::NotFill);
  CHECK(decode_cond(kCondFirst).kind == CondKind::First);
  CHECK(decode_cond(kCondNotFirst).kind == CondKind::NotFirst);

  const Cond p = decode_cond(35);
  CHECK(p.kind == CondKind::Probability);
  CHECK(p.a == 35);

  const Cond r = decode_cond(0x8A);  // 2:3
  CHECK(r.kind == CondKind::Ratio);
  CHECK(r.a == 2);
  CHECK(r.b == 3);
}

TEST_CASE("condition: every probability and every A:B round-trips through the encoding") {
  for (unsigned percent = 1; percent <= 99; ++percent) {
    const std::uint8_t c = cond_probability(percent);
    CHECK(c == percent);
    const Cond d = decode_cond(c);
    CHECK(d.kind == CondKind::Probability);
    CHECK(d.a == percent);
    CHECK(encode_cond(d) == c);
  }
  unsigned ratios = 0;
  for (unsigned b = 1; b <= 8; ++b) {
    for (unsigned a = 1; a <= b; ++a) {
      const std::uint8_t c = cond_ratio(a, b);
      CHECK(c >= 0x80);
      CHECK(c <= 0xBF);
      const Cond d = decode_cond(c);
      CHECK(d.kind == CondKind::Ratio);
      CHECK(d.a == a);
      CHECK(d.b == b);
      CHECK(encode_cond(d) == c);
      ++ratios;
    }
  }
  CHECK(ratios == 36);
}

TEST_CASE("condition: all 256 bytes: the defined ones round-trip, the reserved ones play as always") {
  unsigned defined = 0;
  for (unsigned v = 0; v < 256; ++v) {
    const auto c = static_cast<std::uint8_t>(v);
    const Cond d = decode_cond(c);
    if (cond_valid(c)) {
      ++defined;
      CHECK(encode_cond(d) == c);  // decode then encode gives the byte back
    } else {
      // Reserved: no kind but Always, so the step plays as if it had no condition.
      CHECK(d.kind == CondKind::Always);
      CHECK(d.a == 0);
      CHECK(d.b == 0);
      CHECK(encode_cond(d) == kCondAlways);
    }
    // The ranges the contract names.
    const bool expect_defined = v == 0 || (v >= 1 && v <= 99) || (v >= 0xC0 && v <= 0xC3) ||
                                (v >= 0x80 && v <= 0xBF && (((v >> 3) & 7u) <= (v & 7u)));
    CHECK(cond_valid(c) == expect_defined);
  }
  CHECK(defined == 1 + 99 + 36 + 4);

  // Spot checks of the reserved values: 100..0x7F, the ratios with a > b, 0xC4 and up.
  for (unsigned v : {100u, 101u, 127u, 0x7Fu, 0x88u /* 2:1 */, 0xA0u /* 5:1 */, 0xB8u /* 8:1 */, 0xC4u, 0xC5u, 0xE0u, 0xFFu}) {
    CAPTURE(v);
    CHECK_FALSE(cond_valid(static_cast<std::uint8_t>(v)));
    CHECK(decode_cond(static_cast<std::uint8_t>(v)).kind == CondKind::Always);
  }
}

TEST_CASE("condition: an invalid request encodes as always") {
  CHECK(cond_probability(0) == kCondAlways);    // never is a rest, not a probability
  CHECK(cond_probability(100) == kCondAlways);  // certain is no condition
  CHECK(cond_probability(255) == kCondAlways);
  CHECK(cond_probability(100000) == kCondAlways);
  CHECK(cond_ratio(0, 4) == kCondAlways);
  CHECK(cond_ratio(5, 4) == kCondAlways);  // a > b
  CHECK(cond_ratio(1, 9) == kCondAlways);  // b > 8
  CHECK(cond_ratio(9, 9) == kCondAlways);
  CHECK(cond_ratio(0, 0) == kCondAlways);
  CHECK(encode_cond(Cond{CondKind::Probability, 0, 0}) == kCondAlways);
  CHECK(encode_cond(Cond{CondKind::Probability, 100, 0}) == kCondAlways);
  CHECK(encode_cond(Cond{CondKind::Ratio, 3, 2}) == kCondAlways);
  CHECK(encode_cond(Cond{CondKind::Fill, 7, 7}) == kCondFill);  // the operands of the others are ignored
}

// ---- The lanes ----------------------------------------------------------------------------------------------------------------

TEST_CASE("lanes: the default table is the SOUND page, and a fresh pattern has it") {
  static_assert(kMotionLanes == 8);
  static_assert(kLaneNone == 0 && kLaneVoice == 1 && kLaneDist == 2 && kLaneDelay == 3 && kLaneMixer == 4);
  // M3c's targets are appended (the contract of docs/M3C-PLAN.md), and the Pattern is as large and laid out as before.
  static_assert(kLaneGrowl == 5 && kLaneSeq == 6 && kLaneTargetCount == 7);
  static_assert(kSeqWobbleRate == 0 && kSeqWobbleDepth == 1 && kSeqValueCount == 2);
  static_assert(sizeof(Pattern) == 2260 && offsetof(Pattern, motion) == 196 && offsetof(Pattern, lanes) == 2244);

  const Pattern p = fresh();
  for (unsigned i = 0; i < 7; ++i) {  // Cutoff, Reso, Env Mod, Decay, Accent, Wave, Tune: the acid_voice ids 0..6
    CHECK(p.lanes[i].target == kLaneVoice);
    CHECK(p.lanes[i].id == i);
  }
  CHECK(p.lanes[7].target == kLaneMixer);
  CHECK(p.lanes[7].id == static_cast<std::uint8_t>(dsp::MixField::Level));
  for (unsigned i = 0; i < kMotionLanes; ++i) {
    CHECK(p.lanes[i].target == kDefaultLanes[i].target);
    CHECK(p.lanes[i].id == kDefaultLanes[i].id);
    CHECK(p.lanes[i].target < kLaneTargetCount);
  }
}

TEST_CASE("lanes: the mixer's ranges are the engine's limits") {
  auto e = std::make_unique<dsp::Engine>();
  for (unsigned f = 0; f < kMixerFieldCount; ++f) {
    CAPTURE(f);
    const auto field = static_cast<dsp::MixField>(f);
    REQUIRE(e->set_mix(0, field, 1.0e6f));
    CHECK(e->mix(0, field) == doctest::Approx(kMixerRange[f].max));
    REQUIRE(e->set_mix(0, field, -1.0e6f));
    CHECK(e->mix(0, field) == doctest::Approx(kMixerRange[f].min));
  }
}

TEST_CASE("lanes: a mixer point maps linearly over its range and round-trips") {
  static_assert(kMotionMaxPoint == 254);
  CHECK(mixer_point_to_value(kMixerLevel, 0) == 0.0f);
  CHECK(mixer_point_to_value(kMixerLevel, 254) == 1.5f);  // the end points are exact
  CHECK(mixer_point_to_value(kMixerPan, 0) == -1.0f);
  CHECK(mixer_point_to_value(kMixerPan, 127) == doctest::Approx(0.0f).epsilon(1e-6));
  CHECK(mixer_point_to_value(kMixerPan, 254) == 1.0f);
  CHECK(mixer_point_to_value(kMixerSendA, 254) == 1.5f);
  CHECK(mixer_point_to_value(kMixerSendB, 254) == 1.5f);
  CHECK(mixer_point_to_value(kMixerMute, 0) == 0.0f);
  CHECK(mixer_point_to_value(kMixerMute, 254) == 1.0f);
  CHECK(mixer_point_to_value(kMixerLevel, 169) == doctest::Approx(1.0f).epsilon(0.005));  // unity is near point 169
  CHECK(mixer_point_to_value(kMixerLevel, kNoLock) == 1.5f);  // not a value, but never out of range
  CHECK(mixer_point_to_value(kMixerFieldCount, 100) == 0.0f);

  for (unsigned f = 0; f < kMixerFieldCount; ++f) {
    CAPTURE(f);
    for (unsigned pt = 0; pt <= kMotionMaxPoint; ++pt) {
      CHECK(mixer_value_to_point(f, mixer_point_to_value(f, static_cast<std::uint8_t>(pt))) == pt);
    }
    CHECK(mixer_value_to_point(f, kMixerRange[f].min - 5.0f) == 0);
    CHECK(mixer_value_to_point(f, kMixerRange[f].max + 5.0f) == kMotionMaxPoint);
    CHECK(mixer_value_to_point(f, std::nanf("")) == 0);
  }
  CHECK(mixer_value_to_point(kMixerLevel, 1.0f) == 169);  // 1 / 1.5 * 254 = 169.33
  CHECK(mixer_value_to_point(kMixerFieldCount, 1.0f) == 0);
}

// ---- What builds, copies, generates and edits patterns ----------------------------------------------------------------------

TEST_CASE("clear(): no ratchet, no condition, the default lanes; the all-zero pattern is not it") {
  const Pattern p = fresh();
  for (const Step& s : p.steps) {
    CHECK(s.flags == kStepRest);
    CHECK(s.cond == kCondAlways);
    CHECK(ratchet_of(s.flags) == 0);
  }
  Pattern z;
  std::memset(&z, 0, sizeof(z));
  CHECK_FALSE(same(z, p));
}

TEST_CASE("the vault: every generated and factory preset has no ratchets, no conditions and the default lanes") {
  auto preset = std::make_unique<seq::gen::Preset>();
  bool plain = true;
  bool lanes = true;
  const auto look = [&](const Pattern& p) {
    for (const Step& s : p.steps) {
      plain = plain && s.cond == kCondAlways && (s.flags & (kStepRatchetMask | 0xC0u)) == 0;  // no ratchet, reserved bits clear
    }
    for (unsigned i = 0; i < kMotionLanes; ++i) {
      lanes = lanes && p.lanes[i].target == kDefaultLanes[i].target && p.lanes[i].id == kDefaultLanes[i].id;
    }
  };
  for (unsigned bank = 0; bank < seq::gen::kBanks; ++bank) {
    for (unsigned slot = 0; slot < seq::gen::kSlots; ++slot) {
      seq::gen::factory(*preset, bank, slot);
      look(preset->pattern);
      seq::gen::generate(*preset, bank, slot, 1234u + slot);
      look(preset->pattern);
    }
  }
  CHECK(plain);
  CHECK(lanes);
}

TEST_CASE("ops: a step moves with its ratchet and condition (insert, delete, rotate, reverse, double, step clipboard)") {
  using namespace seq;
  const Pattern base = marked();  // step i: note 40 + i, ratchet i % 4, condition 1 + i

  Pattern p = base;
  insert_step(p, 4);
  CHECK(p.steps[4].cond == kCondAlways);  // the new step is a plain rest
  CHECK(p.steps[4].flags == kStepRest);
  for (unsigned i = 5; i < 16; ++i) {
    CHECK(p.steps[i].note == base.steps[i - 1].note);
    CHECK(p.steps[i].flags == base.steps[i - 1].flags);
    CHECK(p.steps[i].cond == base.steps[i - 1].cond);
  }
  CHECK(p.steps[3].cond == base.steps[3].cond);
  CHECK(p.steps[16].cond == base.steps[16].cond);  // past the length: untouched

  p = base;
  delete_step(p, 4);
  for (unsigned i = 4; i < 15; ++i) {
    CHECK(p.steps[i].note == base.steps[i + 1].note);
    CHECK(p.steps[i].flags == base.steps[i + 1].flags);
    CHECK(p.steps[i].cond == base.steps[i + 1].cond);
  }
  CHECK(p.steps[15].cond == kCondAlways);  // the freed last step of the length
  CHECK(p.steps[15].flags == kStepRest);

  p = base;
  rotate(p, 3);
  for (unsigned i = 0; i < 16; ++i) {
    const unsigned from = (i + 16u - 3u) % 16u;
    CHECK(p.steps[i].note == base.steps[from].note);
    CHECK(p.steps[i].flags == base.steps[from].flags);
    CHECK(p.steps[i].cond == base.steps[from].cond);
  }

  p = base;
  reverse(p);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(p.steps[i].note == base.steps[15 - i].note);
    CHECK(p.steps[i].flags == base.steps[15 - i].flags);
    CHECK(p.steps[i].cond == base.steps[15 - i].cond);
  }

  p = base;
  CHECK(double_length(p));
  CHECK(p.length == 32);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(p.steps[16 + i].flags == base.steps[i].flags);
    CHECK(p.steps[16 + i].cond == base.steps[i].cond);
  }

  p = base;
  const StepClip clip = copy_step(p, 6);
  paste_step(p, 9, clip);
  CHECK(p.steps[9].note == base.steps[6].note);
  CHECK(p.steps[9].flags == base.steps[6].flags);
  CHECK(p.steps[9].cond == base.steps[6].cond);

  // Pitch tools leave the fields alone.
  p = base;
  CHECK(transpose_notes(p, 2));
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(p.steps[i].flags == base.steps[i].flags);
    CHECK(p.steps[i].cond == base.steps[i].cond);
  }
  invert(p);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(p.steps[i].flags == base.steps[i].flags);
    CHECK(p.steps[i].cond == base.steps[i].cond);
  }
}

TEST_CASE("ops: flag edits keep the ratchet, a rewritten step and a cleared pattern lose ratchet and condition, the lanes stay") {
  using namespace seq;
  Pattern p = marked();
  const Step before = p.steps[5];  // ratchet 1, condition 6
  REQUIRE(ratchet_of(before.flags) == 1);

  set_flag(p, 5, kStepAccent, true);
  set_flag(p, 5, kStepSlide, true);
  set_flag(p, 5, kStepAccent, false);
  toggle_flag(p, 5, kStepTie);
  toggle_flag(p, 5, kStepRest);
  set_flag(p, 5, kStepRest, false);
  CHECK(ratchet_of(p.steps[5].flags) == 1);  // the flag operations work on their own bits
  CHECK(p.steps[5].cond == before.cond);
  set_flag(p, 5, kStepRatchetMask, true);    // the ratchet is not one of their flags: ignored
  CHECK(ratchet_of(p.steps[5].flags) == 1);

  // REC entry replaces the step wholesale.
  (void)write_advance(p, 5, 70, kStepAccent);
  CHECK(p.steps[5].note == 70);
  CHECK(p.steps[5].flags == kStepAccent);
  CHECK(p.steps[5].cond == kCondAlways);

  // A generated line replaces the flags of the steps of the length, ratchets and conditions with them.
  Pattern g = marked();
  g.steps[40].cond = cond_probability(50);  // above the length: untouched
  g.steps[40].flags = with_ratchet(0, 3);
  random_fill(g, GenParams{}, 7u);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(g.steps[i].cond == kCondAlways);
    CHECK(ratchet_of(g.steps[i].flags) == 0);
  }
  CHECK(g.steps[40].cond == cond_probability(50));
  CHECK(ratchet_of(g.steps[40].flags) == 3);

  // mutate() re-rolls single bits of some steps: it keeps the ratchet and the condition of the steps it touches.
  Pattern m = marked();
  mutate(m, GenParams{}, 11u, 100);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(ratchet_of(m.steps[i].flags) == i % 4u);
    CHECK(m.steps[i].cond == cond_probability(1u + i));
  }

  // clear_pattern(): every step a plain rest; the lane table is the pattern's configuration, like its swing, and stays.
  Pattern c = marked();
  c.lanes[2] = LaneRef{kLaneDist, 3};
  clear_pattern(c);
  for (const Step& s : c.steps) {
    CHECK(s.flags == kStepRest);
    CHECK(s.cond == kCondAlways);
  }
  CHECK(c.lanes[2].target == kLaneDist);
  CHECK(c.lanes[2].id == 3);
  CHECK(c.lanes[0].target == kLaneVoice);
}

TEST_CASE("bank: the new fields survive the word copies (snapshot, undo, redo, replace, paste) and an edit of only a lane is an edit") {
  using namespace seq;
  auto bank = std::make_unique<Bank>();
  bank->init();
  for (unsigned i = 0; i < kPatternCount; ++i) {
    CHECK(same(bank->pattern(i), fresh()));
  }

  Pattern src = marked();
  src.lanes[1] = LaneRef{kLaneDelay, 2};
  src.lanes[7] = LaneRef{kLaneMixer, kMixerSendA};
  REQUIRE(bank->replace(4, src));
  CHECK(same(bank->pattern(4), src));

  auto copy = std::make_unique<Pattern>();
  std::uint32_t version = 0;
  REQUIRE(bank->snapshot(4, *copy, &version));
  CHECK(same(*copy, src));  // every byte, the lanes at the end included
  CHECK(copy->lanes[7].id == kMixerSendA);
  CHECK(copy->steps[63].cond == cond_probability(64));

  // Only a lane changes: the edit is recorded, the history swaps all 2,260 bytes back and forth.
  CHECK(bank->edit(4, [](Pattern& p) { p.lanes[3] = LaneRef{kLaneDist, 1}; }));
  CHECK(bank->pattern(4).lanes[3].target == kLaneDist);
  CHECK_FALSE(bank->has_motion(4));  // the lanes are not motion
  CHECK(bank->undo() == 4);
  CHECK(same(bank->pattern(4), src));
  CHECK(bank->redo() == 4);
  CHECK(bank->pattern(4).lanes[3].id == 1);
  CHECK(bank->undo() == 4);

  // Only a condition (or a ratchet) changes.
  CHECK(bank->edit(4, [](Pattern& p) { p.steps[10].cond = kCondFill; }));
  CHECK(bank->edit(4, [](Pattern& p) { p.steps[10].flags = with_ratchet(p.steps[10].flags, 3); }));
  CHECK(bank->pattern(4).steps[10].cond == kCondFill);
  CHECK(ratchet_hits(bank->pattern(4).steps[10].flags) == 4);
  CHECK(bank->undo() == 4);
  CHECK(bank->undo() == 4);
  CHECK(same(bank->pattern(4), src));

  // Whole-pattern clipboard.
  bank->copy_pattern(4);
  REQUIRE(bank->paste_pattern(9));
  CHECK(same(bank->pattern(9), src));
  CHECK(bank->undo() == 9);
  CHECK(same(bank->pattern(9), fresh()));
}
