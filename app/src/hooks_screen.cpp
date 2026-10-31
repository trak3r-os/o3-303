// SPDX-License-Identifier: GPL-3.0-or-later
#include "hooks_screen.h"

#include "help_texts.h"
#include "knob_strip.h"
#include "step_view.h"

#include <o3sdk/app/context.h>
#include <o3sdk/app/widgets.h>
#include <o3sdk/ui/components.h>

#include <cstring>

namespace o3_303 {

namespace {

using namespace o3sdk;
using ui::Color;
using ui::Face;
using ui::Rect;

constexpr int kRows = 4;
constexpr int kCols = 12;
constexpr int kGap = 8;
constexpr int kCellGap = 4;

}  // namespace

void HooksScreen::help(const app::Context& ctx, app::Help& h) const noexcept {
  (void)ctx;
  help_texts::fill(h, help_texts::text(help_texts::Id::Hooks));
}

void HooksScreen::draw(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  (void)ctx;
  frame::StatusView status;
  status.keys = false;
  std::strcpy(status.enc, "-");
  if (rig_ != nullptr) {
    fill_take_status(*rig_, status);
  }
  status.save_failed = rig_ != nullptr && rig_->project.failed;
  frame::draw_status(r, Rect{area.x, area.y, area.w, frame::kStatusH}, status);

  // The 4 x 12 grid of the 48 pads, every cell empty, and the line that says why.
  const int top = area.y + frame::kStatusH + 2 * kGap;
  const int bottom = area.y + area.h - app::kKnobStripH - kGap;
  const int line_h = 28;
  const int grid_h = bottom - top - line_h - kGap;
  const int grid_w = area.w - 2 * kGap;
  const int cw = (grid_w - (kCols - 1) * kCellGap) / kCols;
  const int ch = (grid_h - (kRows - 1) * kCellGap) / kRows;
  view::translucent(r, 72, [&] {
    for (int row = 0; row < kRows; ++row) {
      for (int col = 0; col < kCols; ++col) {
        r.panel(Rect{area.x + kGap + col * (cw + kCellGap), top + row * (ch + kCellGap), cw, ch}, Color::Empty);
      }
    }
  });
  const char* text = "HOOKS ARRIVES WITH M4 . SAMPLE RECORDER WORKS NOW";
  const int ty = top + grid_h + kGap;
  r.text(area.x + (area.w - r.text_width(text, Face::Label)) / 2, ui::caps_y(r.theme(), Face::Label, ty, line_h), text, Color::TextDim, Face::Label);

  const Rect strip{area.x, area.y + area.h - app::kKnobStripH, area.w, app::kKnobStripH};
  frame::draw_pads_line(r, Rect{strip.x, strip.y, strip.w, frame::kPadsLineH}, "ALL PADS DARK . HOOKS ARRIVES WITH M4");
  view::Knob row[view::kKnobs];  // an empty F row: no box is drawn
  for (view::Knob& k : row) {
    k.blank = true;
  }
  view::draw_knobs(r, strip, row);
}

}  // namespace o3_303
