// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// FILE LIBRARY (docs/UX-303.md 5.9, docs/UX-PLAN.md F3): the 8 banks of 64 generated lines (docs/DESIGN-303.md section 4, docs/M3-PLAN.md "GEN and the vault"), Replicant
// 303's preset generator, the screen File shows on its second press. Browse a slot (its line is drawn as on HOME, its patch and tempo written out) and load it
// into the current pattern and the sound (one UNDO). It never goes anywhere by itself: the loads leave you here, Pattern goes home, File goes back to PROJECTS.
//
//   F1..F8           bank A..H                       encoder        slot (Shift: the seed by hand, said on the status line)
//   pads 1..48       LOAD slots 0..47 of the bank    LEFT / RIGHT   slot -1 / +1
//   UP / DOWN        slot -8 / +8                    INSERT         LOAD the slot shown (the only way to the slots 48..63, which have no pad)
//   Shift + F1       NEW SEED (the same slot, another take)        Shift + F2     LOAD what: BOTH, LINE, SOUND
//
// The view logic (the texts) is apart from the drawing. Main loop only.

#include "rig.h"

#include <o3sdk/app/screen.h>
#include <o3_303/seq/gen.h>

#include <cstdint>

namespace o3_303 {

// "BOTH", "LINE", "SOUND": what a load takes.
const char* load_what_name(LoadWhat what) noexcept;
// The PADS line: "SLOTS 0-47 OF AMBER . TAP = LOAD . INSERT = LOAD THE SLOT SHOWN" (the grid and the LEDs are numbered 00..47).
unsigned library_pads_text(unsigned bank, char* out, unsigned cap) noexcept;

class VaultScreen final : public o3sdk::app::Screen {
 public:
  void init(Rig& rig) noexcept;

  const char* title() const noexcept override { return "FILE LIBRARY"; }
  void enter(o3sdk::app::Context& ctx) noexcept override;
  void input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept override;
  void update(o3sdk::app::Context& ctx) noexcept override;
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  void soft_keys(o3sdk::app::Context& ctx, o3sdk::app::SoftKeys& keys) noexcept override;
  void leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept override;

  unsigned bank() const noexcept { return bank_; }
  unsigned slot() const noexcept { return slot_; }
  std::uint32_t seed() const noexcept { return seed_; }
  LoadWhat target() const noexcept { return what_; }
  const seq::gen::Preset& preview() const noexcept { return preview_; }
  const char* toast() const noexcept { return toast_; }

 private:
  void refresh() noexcept;
  void move_slot(int by) noexcept;
  void load_slot(o3sdk::app::Context& ctx) noexcept;
  void say(o3sdk::app::Context& ctx, const char* text) noexcept;

  Rig* rig_ = nullptr;
  unsigned bank_ = 0;
  unsigned slot_ = 0;
  std::uint32_t seed_ = 0;  // 0: the vault's own content of the slot; else the seed NEW SEED (or Shift + turn) made
  LoadWhat what_ = LoadWhat::Both;
  seq::gen::Preset preview_;
  char toast_[40] = "";
  std::uint64_t toast_until_ms_ = 0;
  bool saving_ = false;               // Rig::project.saving as last seen: a SAVE is on (the status line says SAVING)
  std::uint32_t seen_notice_ = 0;     // Rig::project.notice_seq as last seen: a new notice is toasted
};

}  // namespace o3_303
