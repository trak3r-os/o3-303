// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A knob's pointer (docs/SKIN-PLAN.md 2.3 and 4.5): a normalised value to the two end points of the line drawn over the knob's cap. The pointer sweeps 300 degrees, from
// 7 o'clock (norm 0, 150 degrees left of straight up) through 12 o'clock (0.5) to 5 o'clock (1), as the ticks of the `knob42_scale` bitmap are laid. Pure, with no libm and no
// double: a Q15 sine table of a quarter turn at one degree, interpolated to half a degree. tests/skin_frow_test.cpp holds it.

#include <cstdint>

namespace o3_303::skin {

// A pointer's end points relative to the knob's centre, in 1/16 px (x to the right, y down).
struct Pointer {
  int x0_q4, y0_q4, x1_q4, y1_q4;
};

// sin(0..90 degrees) in Q15, a degree apart.
inline constexpr std::int32_t kSinQuarterQ15[91] = {
    0,     572,   1144,  1715,  2286,  2856,  3425,  3993,  4560,  5126,  5690,  6252,  6813,  7371,  7927,  8481,  9032,  9580,  10126, 10668, 11207, 11743, 12275, 12803,
    13328, 13848, 14365, 14876, 15384, 15886, 16384, 16877, 17364, 17847, 18324, 18795, 19261, 19720, 20174, 20622, 21063, 21498, 21926, 22348, 22763, 23170, 23571, 23965,
    24351, 24730, 25102, 25466, 25822, 26170, 26510, 26842, 27166, 27482, 27789, 28088, 28378, 28660, 28932, 29197, 29452, 29698, 29935, 30163, 30382, 30592, 30792, 30983,
    31164, 31336, 31499, 31651, 31795, 31928, 32052, 32166, 32270, 32365, 32449, 32524, 32588, 32643, 32688, 32723, 32748, 32763, 32767,
};

// sin of `half_degrees` half degrees (any integer; 720 is a full turn), in Q15: 32767 is 1.
constexpr std::int32_t sin_q15(int half_degrees) noexcept {
  int h = half_degrees % 720;
  h = h < 0 ? h + 720 : h;
  bool negative = false;
  if (h >= 360) {  // sin(a + 180) = -sin(a)
    h -= 360;
    negative = true;
  }
  h = h > 180 ? 360 - h : h;  // sin(180 - a) = sin(a)
  const int i = h / 2;
  std::int32_t v = kSinQuarterQ15[i];
  if ((h & 1) != 0) {  // half way between two degrees
    v = (v + kSinQuarterQ15[i + 1]) / 2;
  }
  return negative ? -v : v;
}

// The pointer's angle from straight up, clockwise, in half degrees: -300 at norm 0, 0 at 0.5, +300 at 1. A norm outside 0..1 is clamped, a NaN is 0.
constexpr int pointer_half_degrees(float norm) noexcept {
  const float n = !(norm > 0.0f) ? 0.0f : (norm > 1.0f ? 1.0f : norm);
  return static_cast<int>(n * 600.0f + 0.5f) - 300;
}

// `s` (Q15) times `r`, rounded half away from zero.
constexpr int mul_q15(std::int32_t s, int r) noexcept {
  const std::int64_t p = static_cast<std::int64_t>(s) * r;
  return static_cast<int>((p + (p >= 0 ? 16384 : -16384)) / 32768);
}

// knob42's pointer: from 0.16 of the cap's radius (21 px) to 1.8 px short of it, so 54 to 307 in 1/16 px.
inline constexpr int kKnob42InnerQ4 = 54;
inline constexpr int kKnob42OuterQ4 = 307;

constexpr Pointer pointer(float norm, int inner_q4 = kKnob42InnerQ4, int outer_q4 = kKnob42OuterQ4) noexcept {
  const int h = pointer_half_degrees(norm);
  const std::int32_t sx = sin_q15(h);           // sin(angle)
  const std::int32_t sy = -sin_q15(h + 180);    // -cos(angle): up is negative y
  return Pointer{mul_q15(sx, inner_q4), mul_q15(sy, inner_q4), mul_q15(sx, outer_q4), mul_q15(sy, outer_q4)};
}

}  // namespace o3_303::skin
