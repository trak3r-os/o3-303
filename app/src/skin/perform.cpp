// SPDX-License-Identifier: GPL-3.0-or-later
#include "perform.h"

#include "frow.h"
#include "panel.h"
#include "skin.h"

#include "../frame.h"

#include <o3sdk/ui/components.h>
#include <o3sdk/ui/plot/canvas.h>

#include <cstring>

namespace o3_303::skin {

namespace {

using ui::Color;
using ui::Face;
using ui::Rect;
namespace app = o3sdk::app;

// The glass (docs/SKIN-PLAN.md 2.1: y 66 to 384, inset by its rim): a label column, the four rows of lamps, the line of chips, a hint, the PADS line.
constexpr int kGlassBottom = 384;
constexpr int kLabelX = 10;
constexpr int kLampX = 84;       // the first cap's box; the caps' faces are one pixel in
constexpr int kLampPitch = 59;
constexpr int kLampW = 58;
constexpr int kLampH = 40;
constexpr int kRowY0 = 76;
constexpr int kRowPitch = 62;
constexpr int kCapInset = 1;     // the cap's face inside the sprite's box
constexpr int kCapW = kLampW - 2;
constexpr int kCapH = kLampH - 4;  // the sprite's lower rows are the cap's lip and its shadow
constexpr int kChipX = 14;
constexpr int kChipY = 318;
constexpr int kChipW = 184;
constexpr int kChipGap = 12;
constexpr int kChipH = 22;
constexpr int kHintY = 346;
constexpr int kPadsY = kGlassBottom - frame::kPadsLineH - 2;  // the PADS line: the glass's last 16 px, above its rim
constexpr unsigned kRows = 4;
constexpr unsigned kRowCaptionChars = 12;                         // the 6x12 caption in the label column (76 px)

constexpr int char_w(Face f) noexcept { return f == Face::Data ? 10 : (f == Face::Label ? 8 : 6); }

// A lamp's label in the biggest face that fits the cap with a margin: 8x16 to six characters, 6x12 for seven ("ALL OFF").
Face label_face(const char* s) noexcept { return static_cast<int>(std::strlen(s)) * char_w(Face::Label) <= kCapW - 8 ? Face::Label : Face::Micro; }

// The colour of each button's chip: the buttons' colours of the HOME rows (NOTE teal, GATE amber on Instr, PLAYS purple on Vol, LOCK cyan on Fx), as step_view.cpp names them.
constexpr Color kButtonColour[kPerfButtons] = {Color::Vol, Color::Accent, Color::Fx, Color::Instr};

// A lamp's caps, tinted by its tone. The mask's face runs from 0.8 to 0.6 of the tint, so the tints are brighter than the cap should look. An off pad is a dark cap that still shows its edge.
constexpr ui::Rgb kTintOff{56, 54, 64};
constexpr ui::Rgb kTintDim{118, 120, 136};
constexpr ui::Rgb kTintMid{40, 120, 238};  // the theme's Sel
constexpr ui::Rgb kTintOn{78, 232, 98};    // the theme's Play

// The frame round a latched action's cap (an accent ring, then the glass-dark recess it sits in).
void latched_frame(Panel& p, const Rect& box) noexcept {
  p.round_rect(Layer::HeldRim, Rect{box.x + kCapInset - 2, box.y + kCapInset - 2, kCapW + 4, kCapH + 4}, 8 * 16, theme().rgb(Color::Accent));
  p.round_rect(Layer::WindowFace, Rect{box.x + kCapInset, box.y + kCapInset, kCapW, kCapH}, 6 * 16, pal::kWindow);
}

// The caps of rows [0, rows), the latched ones framed first, then one pass a tone (the chip's colour is set once a pass), and their labels.
void queue_lamps(Panel& p, const PerformView& v, unsigned rows) noexcept {
  const unsigned count = rows * app::kPadCols;
  for (unsigned pad = 0; pad < count; ++pad) {
    if (v.lamp[pad].latched) {
      latched_frame(p, lamp_box(app::pad_row(pad), app::pad_col(pad)));
    }
  }
  for (unsigned t = 0; t < 4; ++t) {
    const LampTone tone = static_cast<LampTone>(t);
    for (unsigned pad = 0; pad < count; ++pad) {
      if (lamp_tone(v.lamp[pad].level) == tone) {
        const Rect b = lamp_box(app::pad_row(pad), app::pad_col(pad));
        p.sprite(Layer::ActKey, skin_assets::Id::Lamp, 0, b.x, b.y, lamp_tint(tone));
      }
    }
  }
  for (unsigned pad = 0; pad < count; ++pad) {
    const char* label = v.lamp[pad].label;
    if (label[0] == '\0') {
      continue;
    }
    const Face face = label_face(label);
    const Rect b = lamp_box(app::pad_row(pad), app::pad_col(pad));
    const int w = static_cast<int>(std::strlen(label)) * char_w(face);
    p.text(b.x + kLampW / 2 - w / 2, ui::caps_y(theme(), face, b.y + kCapInset, kCapH), label, lamp_ink(lamp_tone(v.lamp[pad].level)), face);
  }
}

// One of the four colour buttons: a window with the button's name on a tab of its colour and the action in that colour; held, the whole chip is filled in the colour with dark ink; latched, it
// is filled and ringed in white as well.
void queue_chip(Panel& p, unsigned i, const PerformChip& c) noexcept {
  const Rect box{kChipX + static_cast<int>(i) * (kChipW + kChipGap), kChipY, kChipW, kChipH};
  const Color colour = kButtonColour[i];
  const int name_w = static_cast<int>(std::strlen(c.name)) * char_w(Face::Label) + 12;
  if (c.state == ChipState::Idle) {
    window(p, box, 4);
    p.round_rect(Layer::Tab, Rect{box.x + 2, box.y, name_w, box.h - 3}, 3 * 16, theme().rgb(colour));
  } else {  // no window under a filled chip: the fill covers it, and the line's box pixels (4.4) are the budget of the worst frame
    p.round_rect(Layer::Fill, Rect{box.x + 1, box.y, box.w - 2, box.h - 1}, 3 * 16, theme().rgb(colour));
    if (c.state == ChipState::Latched) {
      p.round_rect(Layer::HeldRim, Rect{box.x - 2, box.y - 3, box.w + 4, box.h + 5}, 6 * 16, theme().rgb(Color::Text));
    }
  }
  const int ty = ui::caps_y(theme(), Face::Label, box.y, box.h - 1);
  const Color ink = c.state == ChipState::Idle ? colour : Color::AccentText;
  p.text(box.x + 2 + 6, ty, c.name, Color::AccentText, Face::Label);
  p.text(box.x + 2 + name_w + 8, ty, c.action, ink, Face::Label);
}

}  // namespace

Rect lamp_box(unsigned row, unsigned col) noexcept {
  return Rect{kLampX + static_cast<int>(col) * kLampPitch, kRowY0 + static_cast<int>(row) * kRowPitch, kLampW, kLampH};
}

ui::Rgb lamp_tint(LampTone tone) noexcept {
  switch (tone) {
    case LampTone::Off: return kTintOff;
    case LampTone::Dim: return kTintDim;
    case LampTone::Mid: return kTintMid;
    case LampTone::On: return kTintOn;
  }
  return kTintOff;
}

Color lamp_ink(LampTone tone) noexcept {
  switch (tone) {
    case LampTone::Off: return Color::TextDim;
    case LampTone::Dim: return Color::Text;
    case LampTone::Mid: return Color::SelText;
    case LampTone::On: return Color::PlayText;
  }
  return Color::TextDim;
}

void draw_perform(ui::Renderer& r, const PerformView& v, bool setup, const frame::StatusView& status, const HeaderInputs& in, const view::Knob (&knobs)[view::kKnobs], bool shift_row,
                  const char* pads_line) noexcept {
  Panel& panel = skin::panel();
  panel.clear(in.covered);
  queue_panel(panel, kHeaderH, kGlassBottom);

  HeaderView header;
  fill_header(header, status, in);
  draw_header(panel, header);
  FRowView frow;
  fill_frow(frow, knobs, shift_row);
  draw_frow(panel, frow);

  const unsigned rows = kRows;
  queue_lamps(panel, v, rows);
  for (unsigned i = 0; i < kPerfButtons; ++i) {
    queue_chip(panel, i, v.chip[i]);
  }

  // The row names, and under each what the row is now (the screen fills them: SNAPSHOTS in place of REPEAT on SETUP, what the keys do).
  char name[kRows][kRowCaptionChars + 1];
  char caption[kRows][kRowCaptionChars + 1];
  for (unsigned row = 0; row < kRows; ++row) {
    cut_to(name[row], sizeof name[row], v.row_name[row], kRowCaptionChars);
    cut_to(caption[row], sizeof caption[row], v.row_caption[row], kRowCaptionChars);
  }
  char knob_name[kRows][kRowCaptionChars + 1];  // the panel keeps the text's pointer until it draws: one buffer a row
  char knob_value[kRows][kRowCaptionChars + 1];
  for (unsigned row = 0; row < rows; ++row) {
    const int y = kRowY0 + static_cast<int>(row) * kRowPitch;
    if (v.knobs) {  // KNOBS: "ROW 1", then the row's knob and where it stands
      static constexpr const char* kKnobRowNames[kRows] = {"ROW 1", "ROW 2", "ROW 3", "ROW 4"};
      cut_to(knob_name[row], sizeof knob_name[row], v.row_knob[row], kRowCaptionChars);
      panel.text(kLabelX, y + 5, kKnobRowNames[row], Color::ChromeDim, Face::Label);
      panel.text(kLabelX, y + 22, knob_name[row], Color::TextDim, Face::Micro);
      cut_to(knob_value[row], sizeof knob_value[row], v.row_value[row], kRowCaptionChars);
      panel.text(kLabelX, y + 33, knob_value[row], Color::Text, Face::Micro);
      continue;
    }
    panel.text(kLabelX, y + 5, name[row], Color::ChromeDim, Face::Label);
    if (caption[row][0] != '\0') {
      panel.text(kLabelX, y + 25, caption[row], Color::TextDim, Face::Micro);
    }
  }
  // The hint under the lamps: the gestures, and on SETUP what the F keys set (the colour buttons' and the Fill's choices are made there).
  panel.text(kChipX, kHintY, setup ? "F1-F4 BUTTONS . F5 FILL . F6-F8 FREEZE, ROLL, STOP . ROW 2 SNAPSHOTS" : "SHIFT + BUTTON OR PAD = LATCH", Color::TextDim, Face::Micro);

  ui::plot::Canvas canvas(r);
  canvas.open();
  panel.flush_shapes(canvas);
  canvas.close();
  panel.flush_text(r);

  frame::draw_pads_line(r, Rect{kLabelX - 4, kPadsY, ui::kScreenW - 2 * (kLabelX - 4), frame::kPadsLineH}, pads_line);
  draw_ballast(r, ballast());  // `skin ballast N` (SU1): after everything, so the list is that much longer
}

}  // namespace o3_303::skin
