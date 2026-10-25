// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The pads as the keyboard of o3-303 (docs/DESIGN-303.md, section 2): the 12 columns are one chromatic octave, row 3 (the bottom
// row) the lower octave and row 2 the upper one; rows 0 and 1 are the step pads and play no note. Pure: no hardware, no state.

#include <o3sdk/app/panel.h>

#include <cstdint>

namespace o3_303 {

// The keyboard sits in the pattern's own register (a fresh step is C2, Replicant's lines run C1..C3): the bottom row is C2..B2 and the row
// above it C3..B3 at octave shift 0. The bottom left C is also the zero of the live transpose.
inline constexpr int kLowestNote = 36;  // C2, the bottom left pad at octave shift 0
inline constexpr int kMinOctave = -3;   // the bottom left pad down to C-1
inline constexpr int kMaxOctave = 3;    // the top row up to B6

// The MIDI note of `pad` (0..47, row by row from the top left) with the keyboard shifted by `octave` octaves, or -1 when the pad is
// not a key (a step pad, a pad past the panel) or the note falls outside 0..127.
constexpr int key_note(unsigned pad, int octave) noexcept {
  if (pad >= o3sdk::app::kPadCount) {
    return -1;
  }
  const unsigned row = o3sdk::app::pad_row(pad);
  if (row < o3sdk::app::kPadRows - 2u) {
    return -1;
  }
  const int octaves_up = static_cast<int>(o3sdk::app::kPadRows - 1u - row);  // 0 on the bottom row, 1 above it
  const int note = kLowestNote + 12 * (octave + octaves_up) + static_cast<int>(o3sdk::app::pad_col(pad));
  return note >= 0 && note <= 127 ? note : -1;
}

constexpr int clamp_octave(int octave) noexcept {
  return octave < kMinOctave ? kMinOctave : (octave > kMaxOctave ? kMaxOctave : octave);
}

// What a pad is (DESIGN-303 section 2): rows 0 and 1 hold the 16 step pads of a page on columns 0..7 (row 0 the first eight steps, row 1
// the next eight) and the modifier block on columns 8..11, rows 2 and 3 the keyboard.
//
//   col      8      9      10     11
//   row 0   ACC    SLIDE  OCT-   OCT+
//   row 1   TIE    REST   KEYS   MOTION
enum class PadRole : std::uint8_t { Step, Acc, Slide, OctDown, OctUp, Tie, Rest, Keys, Motion, Key, None };

constexpr PadRole pad_role(unsigned pad) noexcept {
  if (pad >= o3sdk::app::kPadCount) {
    return PadRole::None;
  }
  const unsigned row = o3sdk::app::pad_row(pad);
  const unsigned col = o3sdk::app::pad_col(pad);
  if (row >= 2u) {
    return PadRole::Key;
  }
  if (col < 8u) {
    return PadRole::Step;
  }
  constexpr PadRole kBlock[2][4] = {{PadRole::Acc, PadRole::Slide, PadRole::OctDown, PadRole::OctUp},
                                    {PadRole::Tie, PadRole::Rest, PadRole::Keys, PadRole::Motion}};
  return kBlock[row][col - 8u];
}

inline constexpr unsigned kPageSteps = 16;  // the step pads show 16 steps a page

// The pad of the k-th step of a page (0..15), or past the panel for k >= 16.
constexpr unsigned step_pad(unsigned k) noexcept {
  return k < kPageSteps ? (k / 8u) * o3sdk::app::kPadCols + (k % 8u) : o3sdk::app::kPadCount;
}

// The step of a page (0..15) a step pad is, or -1 when `pad` is not a step pad.
constexpr int pad_page_step(unsigned pad) noexcept {
  return pad_role(pad) == PadRole::Step ? static_cast<int>(o3sdk::app::pad_row(pad) * 8u + o3sdk::app::pad_col(pad)) : -1;
}

// The pad of a modifier role (Acc .. Motion), or past the panel for any other role.
constexpr unsigned role_pad(PadRole role) noexcept {
  for (unsigned pad = 0; pad < 24u; ++pad) {
    if (pad_role(pad) == role && role != PadRole::Step) {
      return pad;
    }
  }
  return o3sdk::app::kPadCount;
}

// Whether the note's pitch class is a natural (a white key).
constexpr bool is_natural(unsigned note) noexcept {
  const unsigned pc = note % 12u;
  return pc == 0u || pc == 2u || pc == 4u || pc == 5u || pc == 7u || pc == 9u || pc == 11u;
}

}  // namespace o3_303
