// SPDX-License-Identifier: GPL-3.0-or-later
#include "config_pages.h"

#include "frame.h"
#include "live_actions.h"
#include "project_service.h"
#include "skin/skin.h"

#include <o3_303/config_keys.h>
#include <o3_303/pages.h>

namespace o3_303 {

namespace {

using namespace o3sdk;
using core::ParamCurve;
using core::ParamUnit;

// ---- The descriptors (name, unit text, names, knots, min, max, default, detents, coarse, list count, display, curve, flags, page) ---------------------

constexpr const char* kAutosaveNames[config::kAutosaveChoices] = {"OFF", "10 S", "30 S"};
constexpr const char* kKeysNames[] = {"PLAY", "TRANSPOSE"};
constexpr const char* kOffOnNames[] = {"OFF", "ON"};
constexpr const char* kSkinNames[] = {"CLASSIC", "OBSIDIAN"};
constexpr const char* kKeysScaleNames[] = {"ALL", "IN SCALE"};

constexpr core::ParamDesc kAutosave = {"AUTOSAVE", "", kAutosaveNames, nullptr, 0.0f, 2.0f, 1.0f, 2, 1, config::kAutosaveChoices, ParamUnit::Enum, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kKeysAtBoot = {"KEYS AT BOOT", "", kKeysNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kLadderSlew = {"LADDER SLEW", "ms", nullptr, nullptr, config::kLadderSlewMinMs, config::kLadderSlewMaxMs, kLadderSlewMs, 18, 2, 0, ParamUnit::Int,
                                         ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kPadLegend = {"PAD LEGEND", "", kOffOnNames, nullptr, 0.0f, 1.0f, 1.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kSkin = {"SKIN", "", kSkinNames, nullptr, 0.0f, 1.0f, 1.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kKeys = {"KEYS", "", kKeysScaleNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0};

// ---- The pages -----------------------------------------------------------------------------------------------------------------------------

constexpr app::ConfigItem k303Items[] = {
    {config::kAutosave, "AUTOSAVE", &kAutosave},
    {config::kKeysAtBoot, "KEYS AT BOOT", &kKeysAtBoot},
    {config::kLadderSlew, "LADDER SLEW", &kLadderSlew},
    {config::kPadLegend, "PAD LEGEND", &kPadLegend},
    {config::kResetProject, "RESET PROJECT", nullptr},
    {config::kSkin, "SKIN", &kSkin},
    {config::kKeys, "KEYS", &kKeys},
};
constexpr app::ConfigPage kPages[1] = {
    {"303", k303Items, sizeof k303Items / sizeof k303Items[0]},
};
static_assert(sizeof k303Items / sizeof k303Items[0] <= app::kMaxConfigItems, "every item is shown");

unsigned round_index(float v, unsigned last) noexcept {
  const float c = v < 0.0f ? 0.0f : v;
  const unsigned i = static_cast<unsigned>(c + 0.5f);
  return i > last ? last : i;
}

}  // namespace

void ConfigPages::init(LiveActions& live, ProjectService& project, Wiring& wiring) noexcept {
  live_ = &live;
  project_ = &project;
  wiring_ = &wiring;
  wiring.keys_in_scale = false;  // ALL
  keys_at_boot_ = 0;
  boot_ = true;
  live.set_ladder_slew_ms(kLadderSlewMs);
  frame::set_pad_legend(true);
  skin::set_on(true);  // OBSIDIAN (the kit's settings read puts the saved choice over it)
  // (The autosave period is not reset: the project service keeps it across its own init() on purpose, as a setting that outlives the session; a unit boots with the default
  // and the kit's settings put the saved one over it.)
}

unsigned ConfigPages::pages() const noexcept { return 1; }

bool ConfigPages::page(unsigned index, app::ConfigPage& out) const noexcept {
  if (index >= 1u) {
    return false;
  }
  out = kPages[index];
  return true;
}

float ConfigPages::get(const app::Context& /*ctx*/, std::uint16_t key) const noexcept {
  switch (key) {
    case config::kAutosave:
      return static_cast<float>(config::autosave_choice(project_->autosave_period()));
    case config::kKeysAtBoot:
      return static_cast<float>(keys_at_boot_);
    case config::kLadderSlew:
      return live_->ladder_slew_ms();
    case config::kPadLegend:
      return frame::pad_legend() ? 1.0f : 0.0f;
    case config::kSkin:
      return skin::on() ? 1.0f : 0.0f;
    case config::kKeys:
      return wiring_->keys_in_scale ? 1.0f : 0.0f;
    default:
      break;
  }
  return 0.0f;  // a key the app does not have (the retired 20..24, config_keys.h): the kit never asks it
}

void ConfigPages::set(app::Context& ctx, std::uint16_t key, float value) noexcept {
  switch (key) {
    case config::kAutosave:
      project_->set_autosave_period(config::autosave_period_ms(round_index(value, config::kAutosaveChoices - 1u)));
      return;
    case config::kKeysAtBoot:
      keys_at_boot_ = static_cast<std::uint8_t>(round_index(value, 1u));
      if (boot_) {
        live_->set_trnsp(keys_at_boot_ == 1u);
      }
      return;
    case config::kLadderSlew: {
      const float ms = value < config::kLadderSlewMinMs ? config::kLadderSlewMinMs : (value > config::kLadderSlewMaxMs ? config::kLadderSlewMaxMs : value);
      live_->set_ladder_slew_ms(ms);
      return;
    }
    case config::kPadLegend:
      frame::set_pad_legend(value >= 0.5f);
      ctx.redraw();
      return;
    case config::kSkin:
      skin::set_on(value >= 0.5f);
      ctx.redraw();
      return;
    case config::kKeys:
      wiring_->keys_in_scale = value >= 0.5f;
      ctx.redraw();
      return;
    default:
      break;  // a retired key (20..24) is inert: its value is dropped, never applied (config_keys.h)
  }
}

void ConfigPages::run(app::Context& ctx, std::uint16_t key) noexcept {
  if (key == config::kResetProject) {
    (void)project_->reset_session(ctx);
  }
}

}  // namespace o3_303
