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
// The types are trivial on purpose: the project's patterns are an O3SDK_BIG array in the SDRAM, which must be zero-initialised
// (no default member initialisers, no constructor). All-zero bits are not a usable pattern: clear() one before use.

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

inline constexpr std::uint8_t kDefaultNote = 36;  // C2, the middle of Replicant's range (C1..C3 = 24..48)

inline constexpr std::uint8_t kMaxSwing = 66;     // percent (Replicant: 0..0.66)
inline constexpr std::uint8_t kDefaultGate = 50;  // percent of a step (Replicant's fixed half step)

// Motion: one lane per parameter of the SOUND page (the voice's ids 0..7, in page order), four points a step. A point is the
// parameter's normalised value 0..254 (0 = the descriptor's minimum, 254 = its maximum), kNoLock where the knob's value applies.
inline constexpr unsigned kMotionLanes = 8;
inline constexpr unsigned kMotionPointsPerStep = 4;
inline constexpr std::uint8_t kNoLock = 0xFF;

struct Step {
  std::uint8_t note;   // MIDI note before the pattern's transpose
  std::uint8_t flags;  // kStepAccent | kStepSlide | kStepTie | kStepRest
};

struct Pattern {
  Step steps[kMaxSteps];
  std::uint8_t length;     // one of kLengths
  std::uint8_t swing;      // percent, 0..kMaxSwing
  std::int8_t transpose;   // semitones, -24..+24
  std::uint8_t gate;       // percent, 5..100
  std::uint8_t motion[kMotionLanes][kMaxSteps * kMotionPointsPerStep];
};

static_assert(std::is_trivial<Pattern>::value, "Pattern lives in an O3SDK_BIG array: it must stay trivial");
// About 2.2 KB; the 48 patterns of a project are about 106 KB of SDRAM (docs/M3-PLAN.md, "Memory").
static_assert(sizeof(Pattern) <= 2240, "Pattern grew: check the project's SDRAM budget in docs/M3-PLAN.md");

constexpr bool valid_length(unsigned n) noexcept { return n == 8u || n == 16u || n == 32u || n == 64u; }

// The fresh pattern: 16 steps, every one a rest with C2 under it (a new project has no pattern that sounds; writing a note sets its
// pitch and clears the rest), no swing, no transpose, the default gate, no motion.
constexpr void clear(Pattern& p) noexcept {
  for (Step& s : p.steps) {
    s.note = kDefaultNote;
    s.flags = kStepRest;
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
}

}  // namespace o3_303
