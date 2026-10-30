// SPDX-License-Identifier: GPL-3.0-or-later
#include "step_view.h"

#include <o3_303/pad_map.h>
#include <o3_303/perform.h>
#include <o3_303/seq/ops.h>

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
  Rect rows;   // the GATE, PLAYS and LOCK rows
  int bottom;  // the bottom of everything the columns span
};

constexpr int kRowGap = 3;

Geo geometry(const Rect& area) noexcept {
  Geo g;
  g.numbers = Rect{area.x, area.y, area.w, kNumbersH};
  const int rows_h = kRows * kRowH;
  const int lane_y = area.y + kNumbersH + 2;
  const int lane_h = area.h - kNumbersH - 2 - (rows_h + kRowGap);
  g.lane = Rect{area.x, lane_y, area.w, lane_h};
  g.rows = Rect{area.x, lane_y + lane_h + kRowGap, area.w, rows_h};
  g.bottom = g.rows.y + g.rows.h;
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

int lane_height(int h) noexcept { return h - kNumbersH - 2 - (kRows * kRowH + kRowGap); }

void draw_steps(ui::Renderer& r, const Rect& area, const Pattern& p, const Marks& m) noexcept {
  const ui::Theme& theme = r.theme();
  const Geo g = geometry(area);
  const PitchWindow& w = m.window;
  const unsigned len = length_of(p);
  const int first = m.page * 16;
  const int span_y = g.numbers.y + g.numbers.h;  // the columns run from here to g.bottom
  const int span_h = g.bottom - span_y;

  // ---- The translucent layer: black-key rows, the beat columns, the steps past the length, the cursor's and the playhead's columns ----
  // The playhead's column is the one saturated thing: the beats (1, 5, 9, 13) are a lighter ground, the cursor's column only a lighter tint.
  r.begin_raw();
  r.raw(dl::color_a(64));
  for (int n = w.lo; n <= w.hi(); ++n) {
    if (!is_natural(static_cast<unsigned>(n))) {
      r.panel(Rect{kGridX, row_top(g.lane, w, n), kGridW, w.px}, Color::Empty);
    }
  }
  r.raw(dl::color_a(26));
  for (int i = 0; i < 16; i += 4) {
    r.panel(Rect{cell_x(i), span_y, kCellW, span_h}, Color::Text);
  }
  r.raw(dl::color_a(90));
  for (int i = 0; i < 16; ++i) {
    if (static_cast<unsigned>(first + i) >= len) {
      r.panel(Rect{cell_x(i), span_y, kCellW, span_h}, Color::Empty);
    }
  }
  if (m.cursor >= first && m.cursor < first + 16) {
    r.raw(dl::color_a(40));
    r.panel(Rect{cell_x(m.cursor - first), span_y, kCellW, span_h}, Color::Text);
  }
  if (m.playhead >= first && m.playhead < first + 16) {
    r.raw(dl::color_a(112));
    r.panel(Rect{cell_x(m.playhead - first), span_y, kCellW, span_h}, Color::Play);
  }
  r.end_raw();

  // ---- The gutter: the NOTE caption and the names of the C rows ----
  const int gx = area.x;
  {
    const bool owned = m.row == DrawerRow::Note;
    if (owned) {
      r.panel(Rect{gx + 2, g.lane.y, kGutterW - 4, kRowH - 2}, Color::Sel);
    }
    r.text(gx + 6, g.lane.y + 1, "NOTE", owned ? Color::SelText : Color::TextDim, Face::Micro);
    const StepInfo cur = m.cursor >= 0 ? describe(p, static_cast<unsigned>(m.cursor)) : StepInfo{};
    for (int n = w.lo; n <= w.hi(); ++n) {
      if (n % 12 == 0) {
        const int rt = row_top(g.lane, w, n);
        char name[6];
        (void)note_name(static_cast<unsigned>(n), name, sizeof name);
        const bool under_caption = rt < g.lane.y + kRowH;  // the lane's top left is the NOTE caption's
        if (!under_caption && !(cur.kind != Cell::Rest && cur.note == n && w.px >= 8)) {  // the cursor's chip below names its own row
          r.text(gx + 6, ui::caps_y(theme, Face::Label, rt, w.px), name, Color::Text, Face::Label);
        }
        r.rule(kGridX, rt + w.px - 1, kGridW, ui::Axis::Horizontal, Color::TextDim);  // the octave lines are the bright ones
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
      const unsigned hits = ratchet_hits(p.steps[s].flags);  // a ratchet is the bar in its hits (a tie has none: the sequencer ignores it)
      if (info.kind == Cell::Note && hits > 1u && x1 - x0 >= 5 * static_cast<int>(hits)) {
        const int part = x1 - x0;
        for (unsigned k = 0; k < hits; ++k) {
          const int xa = x0 + part * static_cast<int>(k) / static_cast<int>(hits);
          const int xb = x0 + part * static_cast<int>(k + 1u) / static_cast<int>(hits) - 2;
          r.panel(Rect{xa, rt, xb - xa, h}, ink);
        }
      } else {
        r.panel(Rect{x0, rt, x1 - x0, h}, ink);
      }
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

  // ---- The note names above the bars ----
  for (int pass = 0; pass < 2; ++pass) {
    for (int i = 0; i < 16; ++i) {
      const int s = first + i;
      if (static_cast<unsigned>(s) >= len) {
        break;
      }
      const StepInfo info = describe(p, static_cast<unsigned>(s));
      const bool is_cursor = s == m.cursor;
      if (info.kind != Cell::Note || is_cursor != (pass == 1) || info.note % 12 == 0) {  // a C is named by its octave line
        continue;
      }
      char name[6];
      const unsigned n = note_name(info.note, name, sizeof name);
      const int tw = static_cast<int>(n) * 8;
      const int rt = row_top(g.lane, w, info.note);
      r.text(cell_x(i) + (kCellW - tw) / 2, rt - 14, name, is_cursor ? Color::Text : Color::TextDim, Face::Label);
    }
  }

  // ---- The rows: GATE (a glyph a step), PLAYS (the condition, else the ratchet) and LOCK (the latched knob's lock) ----
  r.rule(kGridX, g.rows.y - 2, kGridW, ui::Axis::Horizontal, Color::Empty);
  static constexpr const char* kRowNames[kRows] = {"GATE", "PLAYS", "LOCK"};
  static constexpr DrawerRow kRowIds[kRows] = {DrawerRow::Gate, DrawerRow::Plays, DrawerRow::Lock};
  for (int k = 0; k < kRows; ++k) {
    const int ry = g.rows.y + k * kRowH;
    const bool owned = m.row == kRowIds[k];
    if (owned) {
      r.panel(Rect{gx + 2, ry, kGutterW - 4, kRowH - 2}, Color::Sel);
    }
    r.text(gx + 6, ry + 1, kRowNames[k], owned ? Color::SelText : Color::TextDim, Face::Micro);
  }
  const int gate_y = g.rows.y;
  const int plays_y = g.rows.y + kRowH;
  const int lock_y = g.rows.y + 2 * kRowH;
  const auto dot = [&](int i, int ry, Color ink) { r.panel(Rect{cell_x(i) + kCellW / 2 - 1, ry + kRowH - 5, 2, 2}, ink); };
  const auto centred = [&](int i, int ry, const char* text, Face face, Color ink) {
    r.text(cell_x(i) + (kCellW - r.text_width(text, face)) / 2, face == Face::Micro ? ry + 1 : ui::caps_y(theme, face, ry, kRowH), text, ink, face);
  };
  const int shown = len > static_cast<unsigned>(first) ? static_cast<int>(len) - first : 0;
  const int cells = shown < 16 ? shown : 16;
  // One pass a colour (a colour is a word of the display list; a cell each would not fit).
  // GATE: a rest is a dot, the others a glyph: REST dim, NOTE plain, ACC and ACC+SLD orange, SLD green, TIE blue.
  static constexpr Color kGateInk[seq::kGateListCount] = {Color::Empty, Color::Text, Color::Accent, Color::Vol, Color::Accent, Color::Instr};
  static constexpr Color kGatePasses[5] = {Color::Empty, Color::Text, Color::Accent, Color::Vol, Color::Instr};
  for (const Color pass : kGatePasses) {
    for (int i = 0; i < cells; ++i) {
      const unsigned gi = seq::gate_list_index(p.steps[first + i].flags);
      if (kGateInk[gi] != pass) {
        continue;
      }
      if (gi == seq::kGateRest) {
        dot(i, gate_y, pass);
      } else {
        centred(i, gate_y, gate_glyph(gi), Face::Label, pass);
      }
    }
  }
  // PLAYS: x1 dim, a ratchet blue, a condition in the lock colour.
  for (unsigned pass = 0; pass < 3; ++pass) {
    for (int i = 0; i < cells; ++i) {
      const Step& st = p.steps[first + i];
      const bool cond = st.cond != kCondAlways && cond_valid(st.cond);
      const unsigned kind = cond ? 2u : (ratchet_hits(st.flags) > 1u ? 1u : 0u);
      if (kind != pass) {
        continue;
      }
      char text[12];
      (void)plays_text(st.flags, st.cond, text, sizeof text);
      centred(i, plays_y, text, Face::Micro, pass == 0u ? Color::Empty : (pass == 1u ? Color::Instr : Color::Fx));
    }
  }
  // LOCK: the latched knob's lock on each step, a dot where it has none.
  for (int i = 0; i < cells; ++i) {
    if (m.lock[i][0] == '\0') {
      dot(i, lock_y, Color::Empty);
    }
  }
  for (int i = 0; i < cells; ++i) {
    if (m.lock[i][0] != '\0') {
      centred(i, lock_y, m.lock[i], Face::Micro, Color::Fx);
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
  r.raw(dl::color_a(26));
  for (int i = 0; i < 16; i += 4) {
    r.panel(Rect{cell_x(i), area.y + 1, kCellW, area.h - 1}, Color::Text);
  }
  r.raw(dl::color_a(90));
  for (int i = 0; i < 16; ++i) {
    if (static_cast<unsigned>(first + i) >= len) {
      r.panel(Rect{cell_x(i), area.y + 1, kCellW, area.h - 1}, Color::Empty);
    }
  }
  if (m.cursor >= first && m.cursor < first + 16) {
    r.raw(dl::color_a(40));
    r.panel(Rect{cell_x(m.cursor - first), area.y + 1, kCellW, area.h - 1}, Color::Text);
  }
  if (m.playhead >= first && m.playhead < first + 16) {
    r.raw(dl::color_a(112));
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
    // The faint curve: what the lane plays at each of the page's points (its lock, or the knob where it has none), as runs of one level
    // joined by short rises, under the bars; and ticks at both ends of the grid at the lowest and highest lock of the whole pattern.
    int level[16 * kMotionPointsPerStep];
    unsigned points = 0;
    const int knob_y = base - 1 - static_cast<int>(kn * static_cast<float>(bar_h - 2));
    for (int i = 0; i < 16 && static_cast<unsigned>(first + i) < len; ++i) {
      for (unsigned q = 0; q < kMotionPointsPerStep; ++q) {
        const std::uint8_t v = p.motion[lane][static_cast<unsigned>(first + i) * kMotionPointsPerStep + q];
        level[points++] = v == kNoLock ? knob_y : base - 1 - static_cast<int>(v) * (bar_h - 2) / 254;
      }
    }
    unsigned ends[2] = {255u, 0u};  // the lowest and the highest lock of the pattern
    for (unsigned j = 0; j < len * kMotionPointsPerStep; ++j) {
      const unsigned v = p.motion[lane][j];
      if (v != kNoLock) {
        ends[0] = v < ends[0] ? v : ends[0];
        ends[1] = v > ends[1] ? v : ends[1];
      }
    }
    translucent(r, 96, [&] {
      for (unsigned j = 0; j < points;) {
        unsigned k = j + 1u;
        while (k < points && level[k] == level[j]) {
          ++k;
        }
        const int x0 = kGridX + static_cast<int>(j) * kPointW;
        r.rule(x0, level[j], static_cast<int>(k - j) * kPointW, ui::Axis::Horizontal, Color::Text);
        if (k < points) {  // the join to the next run
          const int ya = level[j] < level[k] ? level[j] : level[k];
          const int yb = level[j] < level[k] ? level[k] : level[j];
          r.rule(x0 + static_cast<int>(k - j) * kPointW, ya, yb - ya + 1, ui::Axis::Vertical, Color::Text);
        }
        j = k;
      }
    });
    if (ends[0] <= ends[1]) {
      for (const unsigned v : ends) {
        const int y = base - 1 - static_cast<int>(v) * (bar_h - 2) / 254;
        r.rule(kGridX - 7, y, 6, ui::Axis::Horizontal, Color::Fx);
        r.rule(kGridX + kGridW + 1, y, 6, ui::Axis::Horizontal, Color::Fx);
      }
    }
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
