// SPDX-License-Identifier: GPL-3.0-or-later
#include "knob_strip.h"

#include "step_view.h"

#include <cstring>

namespace o3_303::view {

namespace {

using ui::Color;
using ui::Face;
using ui::Rect;

constexpr std::uint8_t kDimAlpha = 120;  // a dim key's texts (Knob::dim)

int clamp_w(float norm, int w) noexcept {
  const float n = norm < 0.0f ? 0.0f : (norm > 1.0f ? 1.0f : norm);
  return static_cast<int>(n * static_cast<float>(w) + 0.5f);
}

// `s` cut to what fits `room` px in `face` (the cell widths of the three faces the strip uses), into `out`.
void fit_text(const char* s, int room, Face face, char* out, unsigned cap) noexcept {
  const int cw = face == Face::Display ? 16 : (face == Face::Data ? 10 : (face == Face::Label ? 8 : 6));
  unsigned n = 0;
  while (s[n] != '\0' && n + 1u < cap && static_cast<int>(n + 1u) * cw <= room) {
    out[n] = s[n];
    ++n;
  }
  out[n] = '\0';
}

// The biggest of Data, Label and Micro in which `s` fits `room` px.
Face biggest_face(const ui::Renderer& r, const char* s, int room) noexcept {
  return r.text_width(s, Face::Data) <= room ? Face::Data : (r.text_width(s, Face::Label) <= room ? Face::Label : Face::Micro);
}

}  // namespace

void draw_switch_face(ui::Renderer& r, const Rect& b, int face_top, int face_h, const Knob& k) noexcept {
  const bool on = k.norm >= 0.5f;
  const Rect chip{b.x + 4, face_top + 2, b.w - 8, face_h - 4};
  const auto put = [&] {
    char text[16];
    fit_text(k.value, chip.w - 6, Face::Micro, text, sizeof text);  // the longest a chip can hold, whatever the face
    const Face face = biggest_face(r, text, chip.w - 6);
    const int w = r.text_width(text, face);
    r.text(chip.x + (chip.w - w) / 2, ui::caps_y(r.theme(), face, chip.y, chip.h), text, on ? Color::SelText : (k.locked ? Color::Accent : Color::Text), face);
  };
  const auto ground = [&] {
    if (on) {
      r.panel(chip, Color::Sel);
    } else {
      r.frame(chip, Color::TextDim);
    }
  };
  if (k.dim) {
    translucent(r, kDimAlpha, [&] { ground(); put(); });
  } else {
    ground();
    put();
  }
}

void draw_action_face(ui::Renderer& r, const Rect& b, int face_top, int face_h, const Knob& k) noexcept {
  const Rect key{b.x + 4, face_top + 2, b.w - 8, face_h - 4};
  const auto put = [&] {
    r.frame(key, k.held ? Color::Accent : Color::TextDim);
    char hint[24];
    unsigned n = 0;
    for (; k.value[n] != '\0' && n + 1u < sizeof hint; ++n) {
      hint[n] = k.value[n];
    }
    if (k.unit[0] != '\0' && n != 0u && n + 2u < sizeof hint) {  // "3 TAPS"
      hint[n++] = ' ';
      for (unsigned u = 0; k.unit[u] != '\0' && n + 1u < sizeof hint; ++u) {
        hint[n++] = k.unit[u];
      }
    }
    hint[n] = '\0';
    char shown[24];
    fit_text(hint, key.w - 6, Face::Micro, shown, sizeof shown);
    const int hint_h = shown[0] != '\0' ? 12 : 0;
    char name[16];
    fit_text(k.label, key.w - 6, Face::Micro, name, sizeof name);
    const Face face = biggest_face(r, name, key.w - 6);
    const int w = r.text_width(name, face);
    r.text(key.x + (key.w - w) / 2, ui::caps_y(r.theme(), face, key.y, key.h - hint_h), name, Color::Text, face);
    if (hint_h != 0) {
      r.text(key.x + (key.w - r.text_width(shown, Face::Micro)) / 2, key.y + key.h - hint_h, shown, Color::TextDim, Face::Micro);
    }
  };
  if (k.dim) {
    translucent(r, kDimAlpha, put);
  } else {
    put();
  }
}

void draw_knobs(ui::Renderer& r, const Rect& area, const Knob (&knobs)[kKnobs]) noexcept {
  const ui::Theme& theme = r.theme();
  const int cell_w = area.w / kKnobs;
  Rect box[kKnobs];
  KeyKind kind[kKnobs];
  for (int k = 0; k < kKnobs; ++k) {
    kind[k] = key_kind(knobs[k]);
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
      const int w = kind[k] == KeyKind::Blank || knobs[k].focus || kind[k] == KeyKind::Switch ? 0 : (knobs[k].lit ? box[k].w : clamp_w(knobs[k].norm, box[k].w));
      if (w > 0) {
        r.panel(Rect{box[k].x, box[k].y, w, box[k].h}, Color::Sel);
      }
    }
  });
  translucent(r, 225, [&] {
    for (int k = 0; k < kKnobs; ++k) {
      const int w = kind[k] == KeyKind::Blank || !knobs[k].focus || kind[k] == KeyKind::Switch ? 0 : (knobs[k].lit ? box[k].w : clamp_w(knobs[k].norm, box[k].w));
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
    if (kind[k] == KeyKind::Action) {  // its name is in the key below, large
      const auto fk = [&] { r.text(box[k].x + 4, box[k].y + 3, knobs[k].fkey, Color::TextDim, Face::Micro); };
      if (knobs[k].dim) {
        translucent(r, kDimAlpha, fk);
      } else {
        fk();
      }
      continue;
    }
    char label[12];
    unsigned n = 0;
    const int room = (box[k].w - 22) / 8;  // after "F1" and its gap
    for (; knobs[k].label[n] != '\0' && static_cast<int>(n) < room && n + 1u < sizeof label; ++n) {
      label[n] = knobs[k].label[n];
    }
    label[n] = '\0';
    const auto put = [&] {
      r.text(box[k].x + 4, box[k].y + 3, knobs[k].fkey, Color::TextDim, Face::Micro);
      r.text(box[k].x + 20, ui::caps_y(theme, Face::Label, box[k].y, 16), label, Color::TextDim, Face::Label);
    };
    if (knobs[k].dim) {  // a key with nothing to do now: its caption fades, as the skin's key does (layout unchanged)
      translucent(r, kDimAlpha, put);
    } else {
      put();
    }
  }

  // The values, bright: the Display face when the text fits it, else the Data face; the unit small after it when there is room.
  for (int pass = 0; pass < 2; ++pass) {  // pass 0 the values, pass 1 the locked ones (Accent)
    for (int k = 0; k < kKnobs; ++k) {
      if (kind[k] == KeyKind::Blank || knobs[k].locked != (pass == 1)) {
        continue;
      }
      if (kind[k] != KeyKind::Knob) {  // a switch's chip and an action's key: one face for both looks of a kind, drawn once
        if (pass == 0) {
          (kind[k] == KeyKind::Switch ? draw_switch_face : draw_action_face)(r, box[k], box[k].y + 16, box[k].h - 17, knobs[k]);
        }
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
      const int unit_w = static_cast<int>(std::strlen(knobs[k].unit)) * 6;
      const int used = static_cast<int>(len) * cw;
      const auto put = [&] {
        r.text(box[k].x + 5, ui::caps_y(theme, face, top, box[k].h - 17), text, pass == 1 ? Color::Accent : (knobs[k].dim ? Color::TextDim : Color::Text), face);
        if (unit_w > 0 && used + 4 + unit_w <= room) {
          r.text(box[k].x + 5 + used + 4, box[k].y + box[k].h - 16, knobs[k].unit, Color::TextDim, Face::Micro);
        }
      };
      if (knobs[k].dim) {
        translucent(r, kDimAlpha, put);
      } else {
        put();
      }
    }
  }
}

}  // namespace o3_303::view
