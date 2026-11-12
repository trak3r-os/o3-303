// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The tract's vowels as the app names them (docs/TRACT-KNOBS-PLAN.md 3.3, 11.6): the unit's VOWEL knob is 0..1 and morphs A, E, I, O, U at 0, 0.25, 0.5, 0.75 and 1
// (units/tract/include/o3_303/units/tract/tract.h). The read-out, the live pads and the wobble's MOUTH share this table. Pure, main loop and tests.

#include <o3sdk/app/panel.h>

#include <cstdint>

namespace o3_303 {

inline constexpr unsigned kTractVowelCount = 5;
inline constexpr char kTractVowelLetter[kTractVowelCount] = {'A', 'E', 'I', 'O', 'U'};

// The knob's value (0..1) of vowel `index` (0 = A .. 4 = U); the last one for an index out of range.
constexpr float tract_vowel_point(unsigned index) noexcept { return static_cast<float>(index < kTractVowelCount ? index : kTractVowelCount - 1u) * 0.25f; }

// The vowel nearest to `value` (0..1; a value outside it is clamped).
constexpr unsigned tract_nearest_vowel(float value) noexcept {
  const float v = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
  return static_cast<unsigned>(v * 4.0f + 0.5f);
}

// Whether `value` is on a vowel's point as the read-out shows it: the percent rounds to 0, 25, 50, 75 or 100.
constexpr bool tract_on_vowel(float value) noexcept {
  const float v = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
  return static_cast<unsigned>(v * 100.0f + 0.5f) % 25u == 0u;  // the same rounding as the read-out's percent
}

// The read-out of a VOWEL value: "E" on a vowel's own point, "E 37" between (the nearest vowel, then the position on the knob in percent; the unit's "%" follows
// in the caption, so the box reads "E 37%"). `out` is terminated; the length is returned. cap of 8 or more holds any of them.
inline unsigned tract_vowel_text(float value, char* out, unsigned cap) noexcept {
  if (cap == 0u) {
    return 0;
  }
  const float v = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
  unsigned n = 0;
  auto put = [&](char c) {
    if (n + 1u < cap) {
      out[n++] = c;
    }
  };
  put(kTractVowelLetter[tract_nearest_vowel(v)]);
  if (!tract_on_vowel(v)) {
    const unsigned pct = static_cast<unsigned>(v * 100.0f + 0.5f);
    put(' ');
    if (pct >= 100u) {
      put(static_cast<char>('0' + pct / 100u));
    }
    if (pct >= 10u) {
      put(static_cast<char>('0' + (pct / 10u) % 10u));
    }
    put(static_cast<char>('0' + pct % 10u));
  }
  out[n] = '\0';
  return n;
}

}  // namespace o3_303
