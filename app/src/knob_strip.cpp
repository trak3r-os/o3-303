// SPDX-License-Identifier: GPL-3.0-or-later
#include "knob_strip.h"

#include "step_view.h"

#include <cstring>

namespace o3_303::view {

namespace {

using ui::Color;
using ui::Face;
using ui::Rect;

int clamp_w(float norm, int w) noexcept {
  const float n = norm < 0.0f ? 0.0f : (norm > 1.0f ? 1.0f : norm);
  return static_cast<int>(n * static_cast<float>(w) + 0.5f);
}

}  // namespace

void draw_knobs(ui::Renderer& r, const Rect& area, const Knob (&knobs)[kKnobs]) noexcept {
  const ui::Theme& theme = r.theme();
  const int cell_w = area.w / kKnobs;
  Rect box[kKnobs];
  for (int k = 0; k < kKnobs; ++k) {
    box[k] = Rect{area.x + k * cell_w + 3, area.y + kKnobStripTopH, cell_w - 6, area.h - kKnobStripTopH - 2};
  }

  // The grounds, then the values as fills of them: the strong one is the key the encoder drives.
  translucent(r, 72, [&] {
    for (int k = 0; k < kKnobs; ++k) {
      if (!knobs[k].blank) {
        r.panel(box[k], Color::Empty);
      }
    }
  });
  translucent(r, 120, [&] {
    for (int k = 0; k < kKnobs; ++k) {
      const int w = knobs[k].blank || knobs[k].focus ? 0 : clamp_w(knobs[k].norm, box[k].w);
      if (w > 0) {
        r.panel(Rect{box[k].x, box[k].y, w, box[k].h}, Color::Sel);
      }
    }
  });
  translucent(r, 225, [&] {
    for (int k = 0; k < kKnobs; ++k) {
      const int w = knobs[k].blank || !knobs[k].focus ? 0 : clamp_w(knobs[k].norm, box[k].w);
      if (w > 0) {
        r.panel(Rect{box[k].x, box[k].y, w, box[k].h}, Color::Sel);
      }
    }
  });
  for (int k = 0; k < kKnobs; ++k) {
    if (knobs[k].blank) {
      continue;
    }
    if (knobs[k].held) {
      r.frame(box[k], Color::Accent);
    } else if (knobs[k].focus) {
      r.frame(box[k], Color::Instr);
    }
  }

  // The names: the F-number small, the label next to it, both dim.
  for (int k = 0; k < kKnobs; ++k) {
    if (knobs[k].blank) {
      continue;
    }
    r.text(box[k].x + 4, box[k].y + 3, knobs[k].fkey, Color::TextDim, Face::Micro);
    char label[12];
    unsigned n = 0;
    const int room = (box[k].w - 22) / 8;  // after "F1" and its gap
    for (; knobs[k].label[n] != '\0' && static_cast<int>(n) < room && n + 1u < sizeof label; ++n) {
      label[n] = knobs[k].label[n];
    }
    label[n] = '\0';
    r.text(box[k].x + 20, ui::caps_y(theme, Face::Label, box[k].y, 16), label, Color::TextDim, Face::Label);
  }

  // The values, bright: the Display face when the text fits it, else the Data face; the unit small after it when there is room.
  for (int pass = 0; pass < 2; ++pass) {  // pass 0 the values, pass 1 the locked ones (Accent)
    for (int k = 0; k < kKnobs; ++k) {
      if (knobs[k].blank || knobs[k].locked != (pass == 1)) {
        continue;
      }
      const int room = box[k].w - 8;
      char text[16];
      unsigned len = static_cast<unsigned>(std::strlen(knobs[k].value));
      len = len > sizeof text - 1u ? static_cast<unsigned>(sizeof text - 1u) : len;
      std::memcpy(text, knobs[k].value, len);
      text[len] = '\0';
      const Face face = static_cast<int>(len) * 16 <= room ? Face::Display : Face::Data;
      const int cw = face == Face::Display ? 16 : 10;
      while (len > 0u && static_cast<int>(len) * cw > room) {
        text[--len] = '\0';
      }
      const int top = box[k].y + 16;
      r.text(box[k].x + 5, ui::caps_y(theme, face, top, box[k].h - 17), text, pass == 1 ? Color::Accent : Color::Text, face);
      const int unit_w = static_cast<int>(std::strlen(knobs[k].unit)) * 6;
      const int used = static_cast<int>(len) * cw;
      if (unit_w > 0 && used + 4 + unit_w <= room) {
        r.text(box[k].x + 5 + used + 4, box[k].y + box[k].h - 16, knobs[k].unit, Color::TextDim, Face::Micro);
      }
    }
  }
}

}  // namespace o3_303::view
