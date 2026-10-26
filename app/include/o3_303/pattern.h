// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A o3-303 pattern (docs/DESIGN-303.md section 6, docs/M3-PLAN.md "Contracts"): plain data that the sequencer plays, the screens
// draw, the editing tools change and the preset generator fills. The step model is Replicant 303's (third_party/replicant303/DSP/
// Pattern.h: gate, note, accent, slide) plus a tie; the length is 8, 16 (the default), 32 or 64 steps.
//
//   accent  the note is accented (the voice gets dsp::kNoteAccent)
//   slide   the note is held into the next step and glides to its pitch (the next note gets dsp::kNoteSlide, no new envelope)
//   tie     no new note: the note before goes on sounding through this step (its pitch is ignored)
//   rest    the gate is closed: nothing sounds, a held note is released (Replicant's gate = false)
//
// A step with neither tie nor rest plays its note for `gate` percent of a step, or the whole step when it slides.
//
// M3b adds, on top of that (docs/M3B-PLAN.md, "Contracts"): a ratchet per step (flags bits 4-5: one to four hits), a trig condition
// per step (`cond`: always, a probability, A:B, FILL, NOT FILL, FIRST, NOT FIRST) and a table of motion lanes per pattern (`lanes`:
// which parameter each lane drives). All three are zero or the SOUND page by default, which is exactly M3's pattern.
//
// The types are trivial on purpose: the project's patterns are an O3SDK_BIG array in the SDRAM, which must be zero-initialised
// (no default member initialisers, no constructor). All-zero bits are not a usable pattern: clear() one before use.

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace o3_303 {

inline constexpr unsigned kMaxSteps = 64;
inline constexpr std::uint8_t kLengths[] = {8, 16, 32, 64};
inline constexpr std::uint8_t kDefaultLength = 16;
inline constexpr unsigned kPatternCount = 48;  // a project: one pattern per pad when Pattern is held

inline constexpr std::uint8_t kStepAccent = 1u << 0;
inline constexpr std::uint8_t kStepSlide = 1u << 1;
inline constexpr std::uint8_t kStepTie = 1u << 2;
inline constexpr std::uint8_t kStepRest = 1u << 3;

// flags bits 4-5: the ratchet, 0 = one hit (a plain step) .. 3 = four hits in the step. Bits 6-7 are reserved and stay 0.
inline constexpr unsigned kStepRatchetShift = 4;
inline constexpr std::uint8_t kStepRatchetMask = 0x3u << kStepRatchetShift;
inline constexpr unsigned kMaxRatchet = 3;

inline constexpr std::uint8_t kDefaultNote = 36;  // C2, the middle of Replicant's range (C1..C3 = 24..48)

inline constexpr std::uint8_t kMaxSwing = 66;     // percent (Replicant: 0..0.66)
inline constexpr std::uint8_t kDefaultGate = 50;  // percent of a step (Replicant's fixed half step)

// Motion: kMotionLanes lanes, four points a step. A point is the normalised value 0..254 of the lane's target (0 = the minimum of its
// range, 254 = the maximum), kNoLock where the knob's value applies. Which parameter a lane drives is the pattern's `lanes` table.
inline constexpr unsigned kMotionLanes = 8;
inline constexpr unsigned kMotionPointsPerStep = 4;
inline constexpr std::uint8_t kNoLock = 0xFF;
inline constexpr std::uint8_t kMotionMaxPoint = 254;  // the same value as seq::kMaxPoint (ops.h)

// ---- The trig condition (Step::cond) ----------------------------------------------------------------------------------------------
//
//   0                          always (no condition)
//   1..99                      a probability in percent
//   0x80 | (a - 1) << 3 | (b - 1)   A:B with 1 <= a <= b <= 8: plays on pass A of every B passes of the pattern
//   0xC0 FILL, 0xC1 NOT FILL   while the fill is held / while it is not
//   0xC2 FIRST, 0xC3 NOT FIRST on the first pass of the pattern / on every other
//
// Any other value is reserved (100..0x7F, the ratios with a > b, 0xC4..0xFF) and plays as always, so data from a later version
// degrades to a plain step. The helpers are pure; the evaluation (the random draw, the pass counter, the fill state) is the sequencer's.

inline constexpr std::uint8_t kCondAlways = 0;
inline constexpr std::uint8_t kCondMaxPercent = 99;
inline constexpr std::uint8_t kCondRatio = 0x80;  // the A:B base: the eight-by-eight table fills 0x80..0xBF
inline constexpr std::uint8_t kCondFill = 0xC0;
inline constexpr std::uint8_t kCondNotFill = 0xC1;
inline constexpr std::uint8_t kCondFirst = 0xC2;
inline constexpr std::uint8_t kCondNotFirst = 0xC3;
inline constexpr unsigned kCondMaxCycle = 8;  // A:B: 1 <= a <= b <= kCondMaxCycle

enum class CondKind : std::uint8_t { Always, Probability, Ratio, Fill, NotFill, First, NotFirst };

// A condition decoded. Probability: a = percent (1..99). Ratio: a of b. The others: a = b = 0.
struct Cond {
  CondKind kind;
  std::uint8_t a;
  std::uint8_t b;
};

// 1..99 -> itself. 0 and 100 or more are not probabilities (a step that never plays is a rest; one that always plays has no
// condition): kCondAlways.
constexpr std::uint8_t cond_probability(unsigned percent) noexcept {
  return (percent >= 1u && percent <= kCondMaxPercent) ? static_cast<std::uint8_t>(percent) : kCondAlways;
}

// A:B. Outside 1 <= a <= b <= kCondMaxCycle: kCondAlways.
constexpr std::uint8_t cond_ratio(unsigned a, unsigned b) noexcept {
  return (a >= 1u && a <= b && b <= kCondMaxCycle) ? static_cast<std::uint8_t>(kCondRatio | ((a - 1u) << 3) | (b - 1u)) : kCondAlways;
}

// The value of `c`; a reserved value decodes as Always.
constexpr Cond decode_cond(std::uint8_t c) noexcept {
  if (c >= 1u && c <= kCondMaxPercent) {
    return Cond{CondKind::Probability, c, 0};
  }
  if (c >= kCondRatio && c < kCondFill) {
    const auto a = static_cast<std::uint8_t>(((c >> 3) & 7u) + 1u);
    const auto b = static_cast<std::uint8_t>((c & 7u) + 1u);
    return a <= b ? Cond{CondKind::Ratio, a, b} : Cond{CondKind::Always, 0, 0};
  }
  switch (c) {
    case kCondFill:
      return Cond{CondKind::Fill, 0, 0};
    case kCondNotFill:
      return Cond{CondKind::NotFill, 0, 0};
    case kCondFirst:
      return Cond{CondKind::First, 0, 0};
    case kCondNotFirst:
      return Cond{CondKind::NotFirst, 0, 0};
    default:
      return Cond{CondKind::Always, 0, 0};
  }
}

// The byte of a condition (the inverse of decode_cond for every non-reserved value; an invalid Probability or Ratio is kCondAlways).
constexpr std::uint8_t encode_cond(const Cond& c) noexcept {
  switch (c.kind) {
    case CondKind::Probability:
      return cond_probability(c.a);
    case CondKind::Ratio:
      return cond_ratio(c.a, c.b);
    case CondKind::Fill:
      return kCondFill;
    case CondKind::NotFill:
      return kCondNotFill;
    case CondKind::First:
      return kCondFirst;
    case CondKind::NotFirst:
      return kCondNotFirst;
    case CondKind::Always:
      break;
  }
  return kCondAlways;
}

// true: a defined value (0, 1..99, a valid A:B, 0xC0..0xC3); false: reserved, which plays as always.
constexpr bool cond_valid(std::uint8_t c) noexcept { return c == kCondAlways || decode_cond(c).kind != CondKind::Always; }

// ---- The ratchet (Step::flags bits 4-5) ----------------------------------------------------------------------------------------------

constexpr unsigned ratchet_of(std::uint8_t flags) noexcept { return (flags & kStepRatchetMask) >> kStepRatchetShift; }  // 0..3
constexpr unsigned ratchet_hits(std::uint8_t flags) noexcept { return ratchet_of(flags) + 1u; }                           // 1..4
// `flags` with the ratchet set to `ratchet` (clamped to kMaxRatchet); the other bits are kept.
constexpr std::uint8_t with_ratchet(std::uint8_t flags, unsigned ratchet) noexcept {
  const unsigned r = ratchet > kMaxRatchet ? kMaxRatchet : ratchet;
  return static_cast<std::uint8_t>((flags & ~static_cast<unsigned>(kStepRatchetMask)) | (r << kStepRatchetShift));
}

// ---- The motion lanes (Pattern::lanes) -----------------------------------------------------------------------------------------------
//
// A lane drives one parameter: `target` says whose, `id` which. A motion point is that parameter's range 0..kMotionMaxPoint, through
// the descriptor's curve (the units' parameters) or the fixed range below (the mixer).

inline constexpr std::uint8_t kLaneNone = 0;   // the lane is unused (its points are ignored)
inline constexpr std::uint8_t kLaneVoice = 1;  // the voice: id = the tb303 parameter id
inline constexpr std::uint8_t kLaneDist = 2;   // the distortion insert: id = its parameter id
inline constexpr std::uint8_t kLaneDelay = 3;  // the delay: id = its parameter id
inline constexpr std::uint8_t kLaneMixer = 4;  // slot 0's mixer: id = o3sdk::dsp::MixField (the mixer ids below)
inline constexpr std::uint8_t kLaneGrowl = 5;  // M3c: the growl (units/growl), slot 0's insert 1: id = its parameter id
inline constexpr std::uint8_t kLaneSeq = 6;    // M3c: the sequencer's own performance values: id = the kSeq* ids below
inline constexpr unsigned kLaneTargetCount = 7;

// The values of kLaneSeq (a motion lane on them sequences the wobble per step): the wobble rate is an Enum 0..10 (seq/wobble.h: 1/1 ... 1/16D),
// the depth 0..1. A point of kNoLock leaves the rate and depth that set_wobble() and the pad override say.
inline constexpr std::uint8_t kSeqWobbleRate = 0;
inline constexpr std::uint8_t kSeqWobbleDepth = 1;
inline constexpr unsigned kSeqValueCount = 2;

// The ids of the mixer's fields: the values of o3sdk::dsp::MixField, which this header does not include (checked against it in
// tests/pattern_test.cpp).
inline constexpr std::uint8_t kMixerLevel = 0;
inline constexpr std::uint8_t kMixerPan = 1;
inline constexpr std::uint8_t kMixerSendA = 2;
inline constexpr std::uint8_t kMixerSendB = 3;
inline constexpr std::uint8_t kMixerMute = 4;
inline constexpr unsigned kMixerFieldCount = 5;

struct LaneRange {
  float min;
  float max;
};

// What point 0 and point kMotionMaxPoint mean for each mixer field (the engine's own limits, o3sdk dsp/engine.h): the linear gains
// (level and the sends) 0..1.5 with 1 = unity, pan -1..1, mute 0..1 (the engine reads 0.5 and above as muted). The mapping is linear.
inline constexpr LaneRange kMixerRange[kMixerFieldCount] = {
    {0.0f, 1.5f},   // Level
    {-1.0f, 1.0f},  // Pan
    {0.0f, 1.5f},   // SendA
    {0.0f, 1.5f},   // SendB
    {0.0f, 1.0f},   // Mute
};

// A mixer field and a motion point (0..kMotionMaxPoint; more is clamped), linear over kMixerRange. An unknown field gives 0.
constexpr float mixer_point_to_value(unsigned field, std::uint8_t point) noexcept {
  if (field >= kMixerFieldCount) {
    return 0.0f;
  }
  const unsigned pt = point > kMotionMaxPoint ? kMotionMaxPoint : point;
  const LaneRange r = kMixerRange[field];
  return r.min + (r.max - r.min) * (static_cast<float>(pt) / static_cast<float>(kMotionMaxPoint));  // point 0 and 254 give min and max exactly
}

// The nearest point to `value` (clamped to the range). An unknown field gives 0.
constexpr std::uint8_t mixer_value_to_point(unsigned field, float value) noexcept {
  if (field >= kMixerFieldCount) {
    return 0;
  }
  const LaneRange r = kMixerRange[field];
  const float n = (value - r.min) / (r.max - r.min);
  const float x = n * static_cast<float>(kMotionMaxPoint) + 0.5f;
  if (!(x >= 0.0f)) {  // below the range, or NaN
    return 0;
  }
  return x >= static_cast<float>(kMotionMaxPoint) ? kMotionMaxPoint : static_cast<std::uint8_t>(static_cast<int>(x));
}

struct LaneRef {
  std::uint8_t target;  // kLaneNone .. kLaneSeq
  std::uint8_t id;
};

// The default lanes are the SOUND page: the voice's ids 0..6 (Cutoff, Reso, Env Mod, Decay, Accent, Wave, Tune) and slot 0's level.
inline constexpr LaneRef kDefaultLanes[kMotionLanes] = {
    {kLaneVoice, 0}, {kLaneVoice, 1}, {kLaneVoice, 2}, {kLaneVoice, 3},
    {kLaneVoice, 4}, {kLaneVoice, 5}, {kLaneVoice, 6}, {kLaneMixer, kMixerLevel},
};

// ---- The pattern --------------------------------------------------------------------------------------------------------------------

struct Step {
  std::uint8_t note;   // MIDI note before the pattern's transpose
  std::uint8_t flags;  // kStepAccent | kStepSlide | kStepTie | kStepRest, and the ratchet in bits 4-5 (ratchet_of / with_ratchet)
  std::uint8_t cond;   // the trig condition (see above); 0 = always
};

// 4-byte aligned and a multiple of 4 bytes long: the Bank copies, swaps and scans patterns a word at a time.
struct alignas(4) Pattern {
  Step steps[kMaxSteps];
  std::uint8_t length;     // one of kLengths
  std::uint8_t swing;      // percent, 0..kMaxSwing
  std::int8_t transpose;   // semitones, -24..+24
  std::uint8_t gate;       // percent, 5..100
  std::uint8_t motion[kMotionLanes][kMaxSteps * kMotionPointsPerStep];
  LaneRef lanes[kMotionLanes];  // what each motion lane drives (kDefaultLanes in a fresh pattern)
};

static_assert(std::is_trivial<Pattern>::value, "Pattern lives in an O3SDK_BIG array: it must stay trivial");
static_assert(std::is_standard_layout<Pattern>::value, "Pattern's layout is read with offsetof and copied in words");
static_assert(alignof(Pattern) >= 4 && sizeof(Pattern) % 4 == 0, "the Bank moves patterns in words: 4-byte aligned, a multiple of 4 long");
static_assert(offsetof(Pattern, motion) % 4 == 0 && sizeof(Pattern::motion[0]) % 4 == 0,
              "the Bank scans each motion lane in words");
// 2,260 bytes; the 48 patterns of a project are about 108 KB of SDRAM (docs/M3-PLAN.md, "Memory").
static_assert(sizeof(Pattern) <= 2260, "Pattern grew: check the project's SDRAM budget in docs/M3-PLAN.md");

constexpr bool valid_length(unsigned n) noexcept { return n == 8u || n == 16u || n == 32u || n == 64u; }

// The fresh pattern: 16 steps, every one a rest with C2 under it (a new project has no pattern that sounds; writing a note sets its
// pitch and clears the rest), no ratchet, no condition, no swing, no transpose, the default gate, no motion, the default lanes.
constexpr void clear(Pattern& p) noexcept {
  for (Step& s : p.steps) {
    s.note = kDefaultNote;
    s.flags = kStepRest;
    s.cond = kCondAlways;
  }
  p.length = kDefaultLength;
  p.swing = 0;
  p.transpose = 0;
  p.gate = kDefaultGate;
  for (auto& lane : p.motion) {
    for (auto& point : lane) {
      point = kNoLock;
    }
  }
  for (unsigned i = 0; i < kMotionLanes; ++i) {
    p.lanes[i] = kDefaultLanes[i];
  }
}

}  // namespace o3_303
