// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The wobble's tables and wave shapes (docs/M3C-PLAN.md, W): pure data and maths; the sequencer's hook runs them (sequencer.h, "Wobble").

#include <cmath>
#include <cstdint>

namespace o3_303::seq {

inline constexpr unsigned kWobbleRateCount = 11;
inline constexpr unsigned kWobbleShapeCount = 6;
inline constexpr int kWobbleRateNone = -1;  // wobble_rate_override(): the pattern's / set_wobble()'s rate applies

// The rates, as indices (the order of the table below; a motion lane on the wobble rate carries them as an Enum 0..10).
enum WobbleRate : std::uint8_t { kWobble1_1, kWobble1_2, kWobble1_4, kWobble1_4T, kWobble1_8, kWobble1_8T, kWobble1_16, kWobble1_16T, kWobble1_32, kWobble1_8D, kWobble1_16D };
// The ticks (o3sdk::dsp::kPpqn = 960 to a quarter note) in one cycle of each rate: a whole note 3840, a triplet two thirds of its note, a
// dotted note three halves of its note.
inline constexpr std::uint16_t kWobbleTicks[kWobbleRateCount] = {3840, 1920, 960, 640, 480, 320, 240, 160, 120, 720, 360};
inline constexpr const char* kWobbleRateNames[kWobbleRateCount] = {"1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/8D", "1/16D"};

enum WobbleShape : std::uint8_t { kWobbleSine, kWobbleTriangle, kWobbleSawDown, kWobbleSawUp, kWobbleSquare, kWobbleSampleHold };
inline constexpr const char* kWobbleShapeNames[kWobbleShapeCount] = {"SINE", "TRI", "SAW DN", "SAW UP", "SQUARE", "S&H"};

// One cycle of a shape, -1..1, at `phase` (Q0.32 turns). Sine and triangle start at 0 and rise; the saw down starts at +1, the saw up at -1,
// the square at +1 for the first half; the sample and hold is `held` (the value the hook drew when the cycle began).
inline float wobble_wave(unsigned shape, std::uint32_t phase, float held) noexcept {
  const float u = static_cast<float>(phase) * (1.0f / 4294967296.0f);
  switch (shape) {
    case kWobbleSine:
      return std::sin(6.2831853f * u);
    case kWobbleTriangle:
      return u < 0.25f ? 4.0f * u : (u < 0.75f ? 2.0f - 4.0f * u : 4.0f * u - 4.0f);
    case kWobbleSawDown:
      return 1.0f - 2.0f * u;
    case kWobbleSawUp:
      return 2.0f * u - 1.0f;
    case kWobbleSquare:
      return u < 0.5f ? 1.0f : -1.0f;
    default:
      return held;
  }
}

}  // namespace o3_303::seq
