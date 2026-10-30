// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// HOOKS (docs/UX-303.md 5.11): the placeholder the three Sample buttons (Sample Loader, Sample Playback, Sample Editor) open until M4: the 4 x 12 grid of empty cells, the line
// "HOOKS ARRIVES WITH M4 . SAMPLE RECORDER WORKS NOW", an empty F row, dark pads and "ENC > -". With M4 it becomes DESIGN-303 section 7. It takes no input of its own (the app
// still takes the global keys). Main loop only.

#include "frame.h"
#include "rig.h"

#include <o3sdk/app/screen.h>

namespace o3_303 {

class HooksScreen final : public o3sdk::app::Screen {
 public:
  void init(Rig& rig) noexcept { rig_ = &rig; }

  const char* title() const noexcept override { return "HOOKS"; }
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  // Dark pads: leds() leaves the cleared set as it is.
  void leds(o3sdk::app::Context&, o3sdk::app::PadLeds&) noexcept override {}

 private:
  Rig* rig_ = nullptr;
};

}  // namespace o3_303
