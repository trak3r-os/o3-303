// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// HOOKS (docs/UX-303.md 5.11): the placeholder the three Sample buttons (Sample Loader, Sample Playback, Sample Editor) open until M4: the 4 x 12 grid of empty cells, the line
// "HOOKS ARRIVES WITH M4 . SAMPLE RECORDER WORKS NOW", an empty F row, dark pads and "ENC > -". With M4 it becomes DESIGN-303 section 7. It takes no input of its own (the app
// still takes the global keys). The obsidian skin (skin/hooks.cpp, docs/SKIN-PLAN.md 2.4 and SK6) draws the same HooksView with no kit chrome: the grid as 48 dark lamps, the line, eight empty boxes;
// the classic drawing is draw_classic(). Main loop only.

#include "frame.h"
#include "hooks_view.h"
#include "rig.h"

#include <o3sdk/app/screen.h>

namespace o3_303 {

class HooksScreen final : public o3sdk::app::Screen {
 public:
  void init(Rig& rig) noexcept { rig_ = &rig; }

  const char* title() const noexcept override { return "HOOKS"; }
  // The obsidian skin (skin/skin.h): no kit chrome and the obsidian theme while it is active; otherwise the kit's.
  o3sdk::app::Chrome chrome(const o3sdk::app::Context& ctx) const noexcept override;
  const o3sdk::ui::Theme* theme(const o3sdk::app::Context& ctx) const noexcept override;
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  // Dark pads: leds() leaves the cleared set as it is.
  void leds(o3sdk::app::Context&, o3sdk::app::PadLeds&) noexcept override {}
  // HELP (Shift + Config, help_texts.h): what the screen is, and the global buttons; no pads.
  void help(const o3sdk::app::Context& ctx, o3sdk::app::Help& h) const noexcept override;

 private:
  void fill_status(frame::StatusView& status) const noexcept;
  void draw_classic(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;
  void draw_skinned(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r) noexcept;

  Rig* rig_ = nullptr;
};

}  // namespace o3_303
