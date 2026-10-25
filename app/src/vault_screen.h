// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// File: the GEN vault browser (docs/DESIGN-303.md section 4, docs/M3-PLAN.md "GEN and the vault"): 8 banks of 64 slots, Replicant 303's
// preset generator. Browse a slot (its pattern is drawn as on the 303 screen, its patch and tempo written out), GEN another take on it
// with a new seed, and load it into the current pattern and the sound.
//
//   F1..F8           bank A..H                       encoder        slot (Shift: the seed)
//   LEFT / RIGHT     slot -1 / +1                    UP / DOWN      slot -8 / +8 (a row of the grid)
//   pads             slot = pad (Shift: pad + 16)    REC            GEN: a new seed, the same slot
//   PARAMS           what a load takes: BOTH, PATTERN, SOUND
//   FILE             load the slot shown, back to the 303 screen        PATTERN   back without loading

#include "main_screen.h"
#include "rig.h"

#include <o3sdk/app/screen.h>
#include <o3_303/seq/gen.h>

#include <cstdint>

namespace o3_303 {

class VaultScreen final : public o3sdk::app::Screen {
 public:
  void init(Rig& rig, MainScreen& main) noexcept;

  const char* title() const noexcept override { return "VAULT"; }
  void enter(o3sdk::app::Context& ctx) noexcept override;
  void input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept override;
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  void soft_keys(o3sdk::app::Context& ctx, o3sdk::app::SoftKeys& keys) noexcept override;
  void leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept override;

  unsigned bank() const noexcept { return bank_; }
  unsigned slot() const noexcept { return slot_; }
  std::uint32_t seed() const noexcept { return seed_; }
  LoadWhat target() const noexcept { return what_; }
  const seq::gen::Preset& preview() const noexcept { return preview_; }

 private:
  void refresh() noexcept;
  void move_slot(int by) noexcept;
  void load_and_return(o3sdk::app::Context& ctx) noexcept;

  Rig* rig_ = nullptr;
  MainScreen* main_ = nullptr;
  unsigned bank_ = 0;
  unsigned slot_ = 0;
  std::uint32_t seed_ = 0;  // 0: the vault's own content of the slot; else the seed GEN used
  LoadWhat what_ = LoadWhat::Both;
  seq::gen::Preset preview_;
};

}  // namespace o3_303
