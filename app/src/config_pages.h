// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The app's pages of the system Config screen (docs/UX-303.md 5.12, docs/UX-PLAN.md C1): "303" (AUTOSAVE, KEYS AT BOOT, LADDER SLEW, PAD LEGEND, RESET PROJECT) and "DEEP" (FMT MIX,
// CMB TUNE, CMB FB, WOB RESET, DLY MODE), through the kit's App::config_pages / config_page / config_get / config_set / config_run (o3sdk/app/config_page.h). The kit keeps the values
// in system.o3s (APPV, keyed by config_keys.h) and gives them back through set() once at boot, after the app's setup(); the tables are constexpr data in config_pages.cpp.
//
// Where each value lives, so that the kit's copy and the app's never disagree:
//   AUTOSAVE      the project service's autosave period (a setting: it outlives the session)
//   KEYS AT BOOT  this object; TRNSP is set from it while the boot window is open (the kit's boot read), and only then: a later change is the next boot's
//   LADDER SLEW   LiveActions::ladder_slew_ms (HOME's and PERFORM's ladder taps)
//   PAD LEGEND    frame::pad_legend (the PADS line's text on every screen)
//   DEEP          the units' own parameters (Controls, as INSTRUMENT's keys write them) and the wobble's reset: the engine is the truth, which is also what the project saves.
//                 At a boot load the project wins: it writes every sound parameter, and ProjectService::settings_moved keeps the kit's values from counting as the player's edit.
//
// Main loop only (the kit asks in the main loop; get() must be cheap: it reads a value or one engine parameter).

#include "rig.h"

#include <o3sdk/app/config_page.h>
#include <o3sdk/app/context.h>

#include <cstdint>

namespace o3_303 {

class LiveActions;
class ProjectService;

class ConfigPages {
 public:
  // setup(): the values this object and the live actions hold are their defaults again (the app object outlives a run in the simulator's tests; the autosave period is the project
  // service's and stays, see ConfigPages::init) and the boot window opens. After the live actions' init.
  void init(Rig& rig, LiveActions& live, ProjectService& project) noexcept;
  // The first update(): the kit's boot read is over.
  void boot_over() noexcept { boot_ = false; }

  unsigned pages() const noexcept;
  bool page(unsigned index, o3sdk::app::ConfigPage& out) const noexcept;
  float get(const o3sdk::app::Context& ctx, std::uint16_t key) const noexcept;
  void set(o3sdk::app::Context& ctx, std::uint16_t key, float value) noexcept;
  void run(o3sdk::app::Context& ctx, std::uint16_t key) noexcept;

 private:
  Rig* rig_ = nullptr;
  LiveActions* live_ = nullptr;
  ProjectService* project_ = nullptr;
  std::uint8_t keys_at_boot_ = 0;  // 0 PLAY, 1 TRANSPOSE
  bool boot_ = true;
};

}  // namespace o3_303
