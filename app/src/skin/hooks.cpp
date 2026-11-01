// SPDX-License-Identifier: GPL-3.0-or-later
#include "hooks.h"

#include "frow.h"
#include "panel.h"
#include "perform.h"
#include "skin.h"

#include "../frame.h"

#include <o3sdk/ui/plot/canvas.h>

#include <cstring>

namespace o3_303::skin {

namespace {

using ui::Color;
using ui::Face;
using ui::Rect;

constexpr int kGlassBottom = 384;
constexpr int kPadsY = kGlassBottom - frame::kPadsLineH - 2;  // the PADS line: the glass's last 16 px, above its rim
constexpr int kLampX = 46;     // (800 - (11 * 59 + 58)) / 2
constexpr int kLampPitch = 59;
constexpr int kLampW = 58;
constexpr int kLampH = 40;
constexpr int kRowY0 = 88;
constexpr int kRowPitch = 62;
constexpr int kLineY = 326;    // the sentence under the grid
constexpr unsigned kRows = 4;
constexpr unsigned kCols = 12;

}  // namespace

Rect hooks_lamp_box(unsigned row, unsigned col) noexcept {
  return Rect{kLampX + static_cast<int>(col) * kLampPitch, kRowY0 + static_cast<int>(row) * kRowPitch, kLampW, kLampH};
}

void draw_hooks(ui::Renderer& r, const HooksView& v, const frame::StatusView& status, const HeaderInputs& in) noexcept {
  Panel& panel = skin::panel();
  panel.clear();
  queue_panel(panel, kHeaderH, kGlassBottom);

  HeaderView header;
  fill_header(header, status, in);
  draw_header(panel, header);
  FRowView frow;
  fill_frow(frow, v.knob, false);
  draw_frow(panel, frow);

  for (unsigned row = 0; row < kRows; ++row) {
    for (unsigned col = 0; col < kCols; ++col) {
      const Rect b = hooks_lamp_box(row, col);
      panel.sprite(Layer::ActKey, skin_assets::Id::Lamp, 0, b.x, b.y, lamp_tint(LampTone::Off));
    }
  }
  const int w = static_cast<int>(std::strlen(v.line)) * 8;
  panel.text((ui::kScreenW - w) / 2, kLineY, v.line, Color::TextDim, Face::Label);

  ui::plot::Canvas canvas(r);
  canvas.open();
  panel.flush_shapes(canvas);
  canvas.close();
  panel.flush_text(r);

  frame::draw_pads_line(r, Rect{6, kPadsY, ui::kScreenW - 12, frame::kPadsLineH}, v.pads_line);
  draw_ballast(r, ballast());  // `skin ballast N` (SU1): after everything, so the list is that much longer
}

}  // namespace o3_303::skin
