// SPDX-License-Identifier: GPL-3.0-or-later
#include "step_view.h"

#include <o3_303/pad_map.h>

namespace o3_303::view {

namespace {

using namespace o3sdk;
using ui::Color;
using ui::Face;
using ui::Rect;

namespace dl = eve::dl;

struct Geo {
  Rect numbers;
  Rect lane;
  Rect flags;  // h == 0 in the roll
  int bottom;  // the bottom of everything the columns span
};

constexpr int kFlagGap = 3;

Geo geometry(const Rect& area, bool roll) noexcept {
  Geo g;
  g.numbers = Rect{area.x, area.y, area.w, kNumbersH};
  const int flags_h = roll ? 0 : kFlagRows * kFlagRowH;
  const int lane_y = area.y + kNumbersH + 2;
  const int lane_h = area.h - kNumbersH - 2 - (roll ? 0 : flags_h + kFlagGap);
  g.lane = Rect{area.x, lane_y, area.w, lane_h};
  g.flags = Rect{area.x, lane_y + lane_h + (roll ? 0 : kFlagGap), area.w, flags_h};
  g.bottom = g.flags.y + g.flags.h;
  return g;
}

int clampi(int v, int lo, int hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

// The top of the row of `note` in the lane (the rows are centred in the lane's height).
int row_top(const Rect& lane, const PitchWindow& w, int note) noexcept {
  const int oy = lane.y + (lane.h - w.rows * w.px) / 2;
  return oy + (w.hi() - clampi(note, w.lo, w.hi())) * w.px;
}

int cell_x(int i) noexcept { return kGridX + i * kCellW; }

unsigned length_of(const Pattern& p) noexcept { return p.length != 0u ? p.length : kDefaultLength; }

}  // namespace

unsigned fmt_uint(unsigned v, char* out) noexcept {
  char tmp[12];
  unsigned n = 0;
  do {
    tmp[n++] = static_cast<char>('0' + v % 10u);
    v /= 10u;
  } while (v != 0u && n < sizeof tmp);
  unsigned o = 0;
  while (n != 0u) {
    out[o++] = tmp[--n];
  }
  out[o] = '\0';
  return o;
}

int lane_height(int h, bool roll) noexcept {
  return h - kNumbersH - 2 - (roll ? 0 : kFlagRows * kFlagRowH + kFlagGap);
}

void draw_steps(ui::Renderer& r, const Rect& area, const Pattern& p, const Marks& m) noexcept {
  const ui::Theme& theme = r.theme();
  const Geo g = geometry(area, m.roll);
  const PitchWindow& w = m.window;
  const unsigned len = length_of(p);
  const int first = m.page * 16;
  const int span_y = g.numbers.y + g.numbers.h;  // the columns run from here to g.bottom
  const int span_h = g.bottom - span_y;

  // ---- The translucent layer: black-key rows, the steps past the length, the cursor's and the playhead's columns ----
  r.begin_raw();
  r.raw(dl::color_a(64));
  for (int n = w.lo; n <= w.hi(); ++n) {
    if (!is_natural(static_cast<unsigned>(n))) {
      r.panel(Rect{kGridX, row_top(g.lane, w, n), kGridW, w.px}, Color::Empty);
    }
  }
  r.raw(dl::color_a(90));
  for (int i = 0; i < 16; ++i) {
    if (static_cast<unsigned>(first + i) >= len) {
      r.panel(Rect{cell_x(i), span_y, kCellW, span_h}, Color::Empty);
    }
  }
  if (m.cursor >= first && m.cursor < first + 16) {
    r.raw(dl::color_a(70));
    r.panel(Rect{cell_x(m.cursor - first), span_y, kCellW, span_h}, Color::Sel);
  }
  if (m.playhead >= first && m.playhead < first + 16) {
    r.raw(dl::color_a(64));
    r.panel(Rect{cell_x(m.playhead - first), span_y, kCellW, span_h}, Color::Play);
  }
  r.end_raw();

  // ---- The gutter: names of the C rows, or the piano keys of the roll ----
  const int gx = area.x;
  if (m.roll) {
    for (int n = w.lo; n <= w.hi(); ++n) {
      const int rt = row_top(g.lane, w, n);
      const int h = w.px > 2 ? w.px - 1 : w.px;
      r.panel(Rect{gx + 2, rt, kGutterW - 6, h}, Color::Text);
    }
    for (int n = w.lo; n <= w.hi(); ++n) {
      if (!is_natural(static_cast<unsigned>(n))) {
        const int rt = row_top(g.lane, w, n);
        const int h = w.px > 2 ? w.px - 1 : w.px;
        r.panel(Rect{gx + 2, rt, 30, h}, Color::ChromeText);
      }
    }
    for (int n = w.lo; n <= w.hi(); ++n) {
      if (n % 12 == 0 && w.px >= 8) {
        char name[6];
        (void)note_name(static_cast<unsigned>(n), name, sizeof name);
        r.text(gx + 34, row_top(g.lane, w, n) + (w.px - 12) / 2, name, Color::Pane, Face::Micro);
      }
    }
  } else {
    for (int n = w.lo; n <= w.hi(); ++n) {
      if (n % 12 == 0) {
        const int rt = row_top(g.lane, w, n);
        char name[6];
        (void)note_name(static_cast<unsigned>(n), name, sizeof name);
        r.text(gx + 6, ui::caps_y(theme, Face::Label, rt, w.px), name, Color::TextDim, Face::Label);
        r.rule(kGridX, rt + w.px - 1, kGridW, ui::Axis::Horizontal, Color::Empty);
      }
    }
  }
  // The cursor step's note in the gutter, as a chip on its row.
  if (m.cursor >= 0) {
    const StepInfo ci = describe(p, static_cast<unsigned>(m.cursor));
    if (ci.kind != Cell::Rest && ci.note >= w.lo && ci.note <= w.hi() && w.px >= 8) {
      const int rt = row_top(g.lane, w, ci.note);
      const int chip_w = kGutterW - 6;
      r.panel(Rect{gx + 2, rt, chip_w, w.px - 1}, Color::Sel);
      char name[6];
      (void)note_name(ci.note, name, sizeof name);
      if (w.px >= 12) {
        r.text(gx + 6, ui::caps_y(theme, Face::Label, rt, w.px - 1), name, Color::SelText, Face::Label);
      } else {
        r.text(gx + 6, rt + (w.px - 1 - 12) / 2, name, Color::SelText, Face::Micro);
      }
    }
  }

  // ---- Beat rules ----
  for (int i = 0; i <= 16; i += 4) {
    r.rule(kGridX + i * kCellW - (i == 16 ? 1 : 0), span_y, span_h, ui::Axis::Vertical, Color::Empty);
  }

  // ---- The step numbers ----
  for (int i = 0; i < 16; ++i) {
    const int s = first + i;
    char num[4];
    fmt_uint(static_cast<unsigned>(s + 1), num);
    const bool past = static_cast<unsigned>(s) >= len;
    const bool is_cursor = s == m.cursor;
    const bool is_play = s == m.playhead;
    const Rect cell{cell_x(i) + 1, g.numbers.y, kCellW - 2, g.numbers.h};
    Color ink = past ? Color::Empty : ((i % 4) == 0 ? Color::Text : Color::TextDim);
    if (is_play) {
      r.panel(cell, Color::Play);
      ink = Color::PlayText;
    } else if (is_cursor) {
      r.panel(cell, Color::Sel);
      ink = Color::SelText;
    }
    const int tw = r.text_width(num, Face::Label);
    r.text(cell.x + (cell.w - tw) / 2, ui::caps_y(theme, Face::Label, cell.y, cell.h), num, ink, Face::Label);
    if ((m.held >> i & 1u) != 0u) {
      r.frame(cell, Color::Accent);
    } else if ((m.emphasis >> i & 1u) != 0u) {
      r.frame(cell, Color::TextDim);
    }
    if (is_play && is_cursor) {
      r.frame(cell, Color::Sel);
    }
  }
  const int page_count = static_cast<int>((len + 15u) / 16u);
  if (page_count > 1) {
    char label[10] = "PAGE ";
    unsigned n = 5;
    n += fmt_uint(static_cast<unsigned>(m.page + 1), label + n);
    label[n++] = '/';
    n += fmt_uint(static_cast<unsigned>(page_count), label + n);
    label[n] = '\0';
    r.text(gx + 3, g.numbers.y + 3, label, Color::TextDim, Face::Micro);
  }

  // ---- The bars: normal notes, then accents (one colour pass each), the ramps of the slides with them ----
  for (int pass = 0; pass < 2; ++pass) {
    const Color ink = pass == 0 ? Color::Instr : Color::Accent;
    for (int i = 0; i < 16; ++i) {
      const int s = first + i;
      if (static_cast<unsigned>(s) >= len) {
        break;
      }
      const StepInfo info = describe(p, static_cast<unsigned>(s));
      if (info.kind == Cell::Rest || info.accent != (pass == 1)) {
        continue;
      }
      const int note = clampi(info.note, w.lo, w.hi());
      const int rt = row_top(g.lane, w, note);
      const int h = w.px > 3 ? w.px - 1 : w.px;
      const int cx = cell_x(i);
      const int x0 = info.kind == Cell::Note ? cx + 2 : cx;
      int x1;
      if (info.to_next) {
        x1 = cx + kCellW;
      } else {
        const int full = kCellW - (info.kind == Cell::Note ? 4 : 2);
        x1 = x0 + (full * static_cast<int>(p.gate) / 100 > 4 ? full * static_cast<int>(p.gate) / 100 : 4);
      }
      r.panel(Rect{x0, rt, x1 - x0, h}, ink);
      if (info.slide && info.ramp_to >= 0 && i < 15 && static_cast<unsigned>(s + 1) < len) {
        const int rt2 = row_top(g.lane, w, info.ramp_to);
        if (rt2 == rt) {
          r.panel(Rect{cx + kCellW, rt, 2, h}, ink);
        } else {
          for (int j = 0; j < 5; ++j) {  // a staircase of 4 px columns, each reaching down or up to the next one
            const int ya = rt + (rt2 - rt) * j / 5;
            const int yb = rt + (rt2 - rt) * (j + 1) / 5;
            const int top = ya < yb ? ya : yb;
            const int bottom = (ya > yb ? ya : yb) + h;
            r.panel(Rect{cx + kCellW - 12 + j * 4, top, 4, bottom - top}, ink);
          }
        }
      }
    }
  }

  // ---- The note names above the bars (the step view) ----
  if (!m.roll) {
    for (int pass = 0; pass < 2; ++pass) {
      for (int i = 0; i < 16; ++i) {
        const int s = first + i;
        if (static_cast<unsigned>(s) >= len) {
          break;
        }
        const StepInfo info = describe(p, static_cast<unsigned>(s));
        const bool is_cursor = s == m.cursor;
        if (info.kind != Cell::Note || is_cursor != (pass == 1)) {
          continue;
        }
        char name[6];
        const unsigned n = note_name(info.note, name, sizeof name);
        const int tw = static_cast<int>(n) * 8;
        const int rt = row_top(g.lane, w, info.note);
        r.text(cell_x(i) + (kCellW - tw) / 2, rt - 14, name, is_cursor ? Color::Text : Color::TextDim, Face::Label);
      }
    }
    // The flag rows: ACC (orange), SLD (green), TIE (blue), with their names in the gutter.
    r.rule(kGridX, g.flags.y - 2, kGridW, ui::Axis::Horizontal, Color::Empty);
    static constexpr const char* kFlagNames[kFlagRows] = {"ACC", "SLD", "TIE"};
    static constexpr Color kFlagInk[kFlagRows] = {Color::Accent, Color::Vol, Color::Instr};
    for (int k = 0; k < kFlagRows; ++k) {
      const int ry = g.flags.y + k * kFlagRowH;
      r.text(gx + 6, ry, kFlagNames[k], Color::TextDim, Face::Micro);
      for (int i = 0; i < 16; ++i) {
        const int s = first + i;
        if (static_cast<unsigned>(s) >= len) {
          break;
        }
        const StepInfo info = describe(p, static_cast<unsigned>(s));
        bool on = false;
        if (k == 0) {
          on = info.kind == Cell::Note && info.accent;
        } else if (k == 1) {
          on = info.kind != Cell::Rest && info.slide;
        } else {
          on = info.kind == Cell::Tie;
        }
        if (on) {
          r.panel(Rect{cell_x(i) + 8, ry + 2, kCellW - 16, kFlagRowH - 4}, kFlagInk[k]);
        }
      }
    }
  }
}

void draw_motion(ui::Renderer& r, const Rect& area, const Pattern& p, const Marks& m, unsigned lane, const char* name,
                 float knob_norm, bool focus_on_sound) noexcept {
  const unsigned len = length_of(p);
  const int first = m.page * 16;
  const int top = area.y + 4;
  const int bar_h = area.h - 8;
  const int base = top + bar_h;  // the baseline: bars grow up from it

  r.rule(area.x, area.y, area.w, ui::Axis::Horizontal, Color::Empty);
  r.text(area.x + 6, area.y + 6, "MOTION", Color::TextDim, Face::Micro);
  r.text(area.x + 6, area.y + 20, name, focus_on_sound ? Color::Fx : Color::TextDim, Face::Micro);

  r.begin_raw();
  r.raw(dl::color_a(90));
  for (int i = 0; i < 16; ++i) {
    if (static_cast<unsigned>(first + i) >= len) {
      r.panel(Rect{cell_x(i), area.y + 1, kCellW, area.h - 1}, Color::Empty);
    }
  }
  if (m.cursor >= first && m.cursor < first + 16) {
    r.raw(dl::color_a(70));
    r.panel(Rect{cell_x(m.cursor - first), area.y + 1, kCellW, area.h - 1}, Color::Sel);
  }
  if (m.playhead >= first && m.playhead < first + 16) {
    r.raw(dl::color_a(64));
    r.panel(Rect{cell_x(m.playhead - first), area.y + 1, kCellW, area.h - 1}, Color::Play);
  }
  r.end_raw();

  for (int i = 0; i <= 16; i += 4) {
    r.rule(kGridX + i * kCellW - (i == 16 ? 1 : 0), area.y + 1, area.h - 1, ui::Axis::Vertical, Color::Empty);
  }
  // Where the knob stands: the value every step without a lock plays.
  const float kn = knob_norm < 0.0f ? 0.0f : (knob_norm > 1.0f ? 1.0f : knob_norm);
  r.rule(kGridX, base - 1 - static_cast<int>(kn * static_cast<float>(bar_h - 2)), kGridW, ui::Axis::Horizontal, Color::TextDim);

  if (lane < kMotionLanes) {
    for (int i = 0; i < 16; ++i) {
      const int s = first + i;
      if (static_cast<unsigned>(s) >= len) {
        break;
      }
      for (unsigned q = 0; q < kMotionPointsPerStep; ++q) {
        const std::uint8_t v = p.motion[lane][static_cast<unsigned>(s) * kMotionPointsPerStep + q];
        if (v == kNoLock) {
          continue;
        }
        const int h = 2 + static_cast<int>(v) * (bar_h - 2) / 254;
        r.panel(Rect{cell_x(i) + static_cast<int>(q) * kPointW + 1, base - h, kPointW - 2, h}, Color::Fx);
      }
    }
  }
  if (m.playhead >= first && m.playhead < first + 16) {
    const int x = cell_x(m.playhead - first) + m.playpoint * kPointW;
    r.panel(Rect{x, area.y + 1, 2, area.h - 1}, Color::Play);
  }
}

}  // namespace o3_303::view
