// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The 7-segment displays of the obsidian skin's header (docs/SKIN-PLAN.md 2.1, section 3): a string to the cells of the `seg` bitmap and the place of the decimal
// point. Pure: no drawing, no state; tests/skin_core_test.cpp holds it. The bitmap has 20 cells, in the order of gen_skin.py's SEG_CHARS: 0-9, then A b C d E F G H, then "-" and
// the blank (the ghost 8 alone).

#include <cstdint>

namespace o3_303::skin {

inline constexpr unsigned kSegCellBlank = 19;
inline constexpr unsigned kSegCellDash = 18;
inline constexpr unsigned kSegMaxCells = 6;

// The cell that shows `c`: '0'..'9' are 0..9, 'A'..'H' are 10..17 (upper case only: the lower case b, d of the glyph set are written B, D), '-' is 18; every other
// character, the space included, is the blank.
constexpr unsigned seg_cell(char c) noexcept {
  if (c >= '0' && c <= '9') {
    return static_cast<unsigned>(c - '0');
  }
  if (c >= 'A' && c <= 'H') {
    return 10u + static_cast<unsigned>(c - 'A');
  }
  return c == '-' ? kSegCellDash : kSegCellBlank;
}

// A display's content: one cell per character, the decimal point after the cell `dot` (-1: none).
struct SegRun {
  std::uint8_t cell[kSegMaxCells] = {};
  unsigned n = 0;
  int dot = -1;
};

// "125.0" is four cells (1, 2, 5, 0) with the point after the third; " 95.5" a blank first. A '.' belongs to the character before it (a leading one, or a second
// one, is dropped), and characters past kSegMaxCells are ignored.
constexpr SegRun seg_text(const char* s) noexcept {
  SegRun run;
  for (; s != nullptr && *s != '\0'; ++s) {
    if (*s == '.') {
      if (run.n != 0u && run.dot < 0) {
        run.dot = static_cast<int>(run.n) - 1;
      }
      continue;
    }
    if (run.n < kSegMaxCells) {
      run.cell[run.n++] = static_cast<std::uint8_t>(seg_cell(*s));
    }
  }
  return run;
}

}  // namespace o3_303::skin
