// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The TRACT line: one row of 64 vowel steps for each pattern (docs/TRACT-KNOBS-PLAN.md, A1b). A step is "-" (follow the VOWEL knob) or one
// of A E I O U, plus its own GLD bit (the mouth glides to the next step's vowel over this step). The line has its own length, SAME as the
// pattern's (the default) or 4, 8, 16, 32, 64, so it can run polymetric against the notes on the same clock. Whether the line is played
// at all (LINE on / off) is project-wide and lives in the SND chunk (kAppTractLine, project.cpp), not here.
//
// The line is the last member of Pattern, so a pattern copy, paste, DUP>NEXT, undo and the song carry it with the notes. The project file
// stores it in a chunk of its own (TRCT), and the PATS records keep their size (kPatBytes in pattern.h).
//
// Plain data and constexpr helpers; the editing entry points are the Bank's (bank.h: tract_step, set_tract_step, set_tract_length,
// clear_tract), the playing side is the Sequencer's (sequencer.h: set_tract_line, tract_line_on, tract_playhead).

#include <cstddef>
#include <cstdint>

namespace o3_303 {

inline constexpr unsigned kTractSteps = 64;

// A step byte: the vowel in bits 0-2 (kTractFollow = the knob; 1..5 = A E I O U), the GLD bit in bit 7, the rest zero. GLD acts
// only on a vowel step (a "-" step never moves the mouth).
inline constexpr std::uint8_t kTractFollow = 0;
inline constexpr std::uint8_t kTractA = 1;
inline constexpr std::uint8_t kTractE = 2;
inline constexpr std::uint8_t kTractI = 3;
inline constexpr std::uint8_t kTractO = 4;
inline constexpr std::uint8_t kTractU = 5;
inline constexpr std::uint8_t kTractVowels = 5;      // the vowels A..U (the values 1..kTractVowels)
inline constexpr std::uint8_t kTractVowelMask = 0x07u;
inline constexpr std::uint8_t kTractGlide = 0x80u;   // GLD: glide to the next step's vowel over this step
inline constexpr std::uint8_t kTractStepMask = kTractVowelMask | kTractGlide;

// The line's length: kTractSame follows the pattern's length, the others fix it.
inline constexpr std::uint8_t kTractSame = 0;

struct TractLine {
  std::uint8_t step[kTractSteps];
  std::uint8_t len;          // kTractSame, 4, 8, 16, 32 or 64
  std::uint8_t reserved[3];  // zero; keeps the line a whole number of words
};

static_assert(sizeof(TractLine) == 68 && alignof(TractLine) == 1, "TractLine is 68 bytes: the TRCT chunk and Pattern's size depend on it");

constexpr bool tract_valid_length(unsigned n) noexcept { return n == 4u || n == 8u || n == 16u || n == 32u || n == 64u; }
constexpr bool tract_valid_len_field(unsigned n) noexcept { return n == kTractSame || tract_valid_length(n); }

constexpr std::uint8_t tract_vowel(std::uint8_t step) noexcept { return static_cast<std::uint8_t>(step & kTractVowelMask); }
constexpr bool tract_glide(std::uint8_t step) noexcept { return (step & kTractGlide) != 0; }
constexpr std::uint8_t tract_make(std::uint8_t vowel, bool glide) noexcept {
  return static_cast<std::uint8_t>((vowel <= kTractVowels ? vowel : kTractFollow) | (glide ? kTractGlide : 0u));
}

// The steps the line runs through: its own length, or the pattern's when it is SAME (or holds something else).
constexpr unsigned tract_length(const TractLine& t, unsigned pattern_length) noexcept {
  return tract_valid_length(t.len) ? t.len : pattern_length;
}

// The line's step under the pattern's step counter `n` (steps played since the pattern started a pass, counted from 0 and not wrapped to
// the pattern's length): the line wraps on its own length. `length` is tract_length(), a power of two.
constexpr unsigned tract_index(unsigned n, unsigned length) noexcept { return n & (length - 1u); }

// A vowel step's position in the unit's VOWEL knob (units/tract: A = 0, E = .25, I = .5, O = .75, U = 1); 0 for "-".
constexpr float tract_vowel_value(std::uint8_t vowel) noexcept {
  return vowel >= kTractA && vowel <= kTractVowels ? static_cast<float>(vowel - kTractA) * 0.25f : 0.0f;
}

// Nothing set: every step "-" without GLD, SAME. Also what clear(Pattern&) leaves.
constexpr void tract_clear(TractLine& t) noexcept {
  for (std::uint8_t& s : t.step) {
    s = kTractFollow;
  }
  t.len = kTractSame;
  for (std::uint8_t& r : t.reserved) {
    r = 0;
  }
}

// Does any step hold a vowel? (A "-" step with GLD set does not count: it has no vowel to play.)
constexpr bool tract_has_vowels(const TractLine& t) noexcept {
  for (const std::uint8_t s : t.step) {
    if (tract_vowel(s) != kTractFollow) {
      return true;
    }
  }
  return false;
}

// Forces a line read from a file into the legal set: a vowel above U becomes "-", the unused bits go, a length that is not kTractSame
// or one of 4, 8, 16, 32, 64 becomes SAME. Returns true when it changed something.
constexpr bool tract_sanitize(TractLine& t) noexcept {
  bool changed = false;
  for (std::uint8_t& s : t.step) {
    const std::uint8_t v = tract_vowel(s) <= kTractVowels ? tract_vowel(s) : kTractFollow;
    const std::uint8_t clean = static_cast<std::uint8_t>(v | (s & kTractGlide));
    changed = changed || clean != s;
    s = clean;
  }
  if (!tract_valid_len_field(t.len)) {
    t.len = kTractSame;
    changed = true;
  }
  for (std::uint8_t& r : t.reserved) {
    changed = changed || r != 0;
    r = 0;
  }
  return changed;
}

}  // namespace o3_303
