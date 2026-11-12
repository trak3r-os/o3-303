// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The app's page of the system Config screen (docs/UX-303.md 5.12, docs/UX-PLAN.md C1): "303" (AUTOSAVE, KEYS AT BOOT, LADDER SLEW, PAD LEGEND, RESET PROJECT, SKIN, KEYS), through the kit's App::config_pages / config_page / config_get / config_set / config_run (o3sdk/app/config_page.h). The kit keeps the values
// in system.o3s (APPV, keyed by config_keys.h) and gives them back through set() once at boot, after the app's setup(); the tables are constexpr data in config_pages.cpp.
//
// Where each value lives, so that the kit's copy and the app's never disagree:
//   AUTOSAVE      the project service's autosave period (a setting: it outlives the session)
//   KEYS AT BOOT  this object; TRNSP is set from it while the boot window is open (the kit's boot read), and only then: a later change is the next boot's
//   LADDER SLEW   LiveActions::ladder_slew_ms (HOME's and PERFORM's ladder taps)
//   PAD LEGEND    frame::pad_legend (the PADS line's text on every screen)
//   KEYS          Wiring::keys_in_scale (a setting, not the project's: ALL by default, IN SCALE locks the keys to the scale on F5)
//   SKIN          skin::on (skin/skin.h): the obsidian look of the screens that have one; OBSIDIAN by default (SK8)
// No value of this page is sound: the project is the only owner of the sound. The page DEEP (FMT MIX, CMB TUNE, CMB FB, WOB RESET, DLY MODE: keys 20..24) was retired on 2026-11-01 (docs/MODS-PLAN.md 7):
// the growl's and the wobble's are keys of GROWL and WOBBLE, DLY MODE is MASTER's F5. A settings file that still holds them loads with them dropped (the kit drops a value of an item the app no longer
// has), and they leave at its next save.
//
// Main loop only (the kit asks in the main loop; get() must be cheap: it reads a value).

#include <o3sdk/app/config_page.h>
#include <o3sdk/app/context.h>

#include <cstdint>

namespace o3_303 {

class LiveActions;
class ProjectService;
struct Wiring;

class ConfigPages {
 public:
  // setup(): the values this object and the live actions hold are their defaults again (the app object outlives a run in the simulator's tests; the autosave period is the project
  // service's and stays, see ConfigPages::init) and the boot window opens. After the live actions' init.
  void init(LiveActions& live, ProjectService& project, Wiring& wiring) noexcept;
  // The first update(): the kit's boot read is over.
  void boot_over() noexcept { boot_ = false; }

  unsigned pages() const noexcept;
  bool page(unsigned index, o3sdk::app::ConfigPage& out) const noexcept;
  float get(const o3sdk::app::Context& ctx, std::uint16_t key) const noexcept;
  void set(o3sdk::app::Context& ctx, std::uint16_t key, float value) noexcept;
  void run(o3sdk::app::Context& ctx, std::uint16_t key) noexcept;

 private:
  LiveActions* live_ = nullptr;
  ProjectService* project_ = nullptr;
  Wiring* wiring_ = nullptr;
  std::uint8_t keys_at_boot_ = 0;  // 0 PLAY, 1 TRANSPOSE
  bool boot_ = true;
};

}  // namespace o3_303
