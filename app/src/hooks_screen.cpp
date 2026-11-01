// SPDX-License-Identifier: GPL-3.0-or-later
#include "hooks_screen.h"

#include "help_texts.h"
#include "knob_strip.h"
#include "live_actions.h"
#include "skin/header.h"
#include "skin/hooks.h"
#include "skin/skin.h"
#include "step_view.h"

#include <o3sdk/app/context.h>
#include <o3sdk/app/widgets.h>
#include <o3sdk/dsp/engine.h>
#include <o3sdk/hal/cycles.h>
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

// The status line's, and the header's: the take speaks here as on every screen; no keyboard (the pads are dark).
void HooksScreen::fill_status(frame::StatusView& status) const noexcept {
  status.keys = false;
  std::strcpy(status.enc, "-");
  if (rig_ != nullptr) {
    fill_take_status(*rig_, status);
  }
  status.save_failed = rig_ != nullptr && rig_->project.failed;
}

app::Chrome HooksScreen::chrome(const app::Context& ctx) const noexcept { return skin::active(ctx) ? app::Chrome::None : app::Chrome::Full; }

const ui::Theme* HooksScreen::theme(const app::Context& ctx) const noexcept { return skin::active(ctx) ? &skin::theme() : nullptr; }

void HooksScreen::draw(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  if (skin::active(ctx)) {
    draw_skinned(ctx, r);
  } else {
    draw_classic(ctx, r, area);
  }
}

// The obsidian drawing (skin/hooks.cpp): the same view the classic drawing takes, and the header's status as the classic status line takes it (the performance actions that are on are the header's too).
void HooksScreen::draw_skinned(app::Context& ctx, ui::Renderer& r) noexcept {
  const std::uint32_t t0 = o3sdk::hal::cycles();
  frame::StatusView status;
  fill_status(status);
  skin::HeaderInputs in;
  in.title = title();
  if (rig_ != nullptr) {
    status.actions = rig_->live->on();
    in.pattern = rig_->bank->selected();
    in.playing = rig_->seq->playhead().playing;
    for (unsigned b = 0; b < kPerfButtons; ++b) {
      in.perf[b] = rig_->wiring.perf[b];
    }
  }
  in.tempo_mbpm = ctx.engine().tempo();
  in.now_ms = ctx.uptime_ms();
  const HooksView view;
  skin::draw_hooks(r, view, status, in);
  skin::note_frame(static_cast<std::uint32_t>(r.words()), (o3sdk::hal::cycles() - t0) / skin::kCyclesPerUs, ctx.uptime_ms());
}

void HooksScreen::draw_classic(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  (void)ctx;
  frame::StatusView status;
  fill_status(status);
  frame::draw_status(r, Rect{area.x, area.y, area.w, frame::kStatusH}, status);

  // The 4 x 12 grid of the 48 pads, every cell empty, and the line that says why.
  const HooksView view;
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
  const int ty = top + grid_h + kGap;
  r.text(area.x + (area.w - r.text_width(view.line, Face::Label)) / 2, ui::caps_y(r.theme(), Face::Label, ty, line_h), view.line, Color::TextDim, Face::Label);

  const Rect strip{area.x, area.y + area.h - app::kKnobStripH, area.w, app::kKnobStripH};
  frame::draw_pads_line(r, Rect{strip.x, strip.y, strip.w, frame::kPadsLineH}, view.pads_line);
  view::draw_knobs(r, strip, view.knob);  // an empty F row: no box is drawn
}

}  // namespace o3_303
