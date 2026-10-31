// SPDX-License-Identifier: GPL-3.0-or-later
#include "config_pages.h"

#include "frame.h"
#include "live_actions.h"
#include "project_service.h"

#include <o3sdk/units/delay/delay.h>
#include <o3_303/config_keys.h>
#include <o3_303/units/growl/growl.h>

namespace o3_303 {

namespace {

using namespace o3sdk;
using core::ParamCurve;
using core::ParamUnit;

// ---- The descriptors (name, unit text, names, knots, min, max, default, detents, coarse, list count, display, curve, flags, page) ---------------------
// The DEEP ones repeat the units' own ranges and defaults (the kit needs constexpr tables; the units' are inside their .cpp): config_pages_test holds them equal.

constexpr const char* kAutosaveNames[config::kAutosaveChoices] = {"OFF", "10 S", "30 S"};
constexpr const char* kKeysNames[] = {"PLAY", "TRANSPOSE"};
constexpr const char* kOffOnNames[] = {"OFF", "ON"};
constexpr const char* kModeNames[] = {"MONO", "PING"};

constexpr core::ParamDesc kAutosave = {"AUTOSAVE", "", kAutosaveNames, nullptr, 0.0f, 2.0f, 1.0f, 2, 1, config::kAutosaveChoices, ParamUnit::Enum, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kKeysAtBoot = {"KEYS AT BOOT", "", kKeysNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kLadderSlew = {"LADDER SLEW", "ms", nullptr, nullptr, config::kLadderSlewMinMs, config::kLadderSlewMaxMs, kLadderSlewMs, 18, 2, 0, ParamUnit::Int,
                                         ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kPadLegend = {"PAD LEGEND", "", kOffOnNames, nullptr, 0.0f, 1.0f, 1.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0};

constexpr core::ParamDesc kFmtMix = {"FMT MIX", "%", nullptr, nullptr, 0.0f, 1.0f, 1.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kCmbTune = {"CMB TUNE", "st", nullptr, nullptr, -24.0f, 24.0f, 0.0f, 48, 12, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kCmbFb = {"CMB FB", "", nullptr, nullptr, -0.95f, 0.95f, 0.6f, 38, 5, 0, ParamUnit::Fixed1, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kWobReset = {"WOB RESET", "", kOffOnNames, nullptr, 0.0f, 1.0f, 1.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kDlyMode = {"DLY MODE", "", kModeNames, nullptr, 0.0f, 1.0f, 1.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0};

// ---- The pages -----------------------------------------------------------------------------------------------------------------------------

constexpr app::ConfigItem k303Items[] = {
    {config::kAutosave, "AUTOSAVE", &kAutosave},
    {config::kKeysAtBoot, "KEYS AT BOOT", &kKeysAtBoot},
    {config::kLadderSlew, "LADDER SLEW", &kLadderSlew},
    {config::kPadLegend, "PAD LEGEND", &kPadLegend},
    {config::kResetProject, "RESET PROJECT", nullptr},
};
constexpr app::ConfigItem kDeepItems[] = {
    {config::kFmtMix, "FMT MIX", &kFmtMix},   {config::kCmbTune, "CMB TUNE", &kCmbTune}, {config::kCmbFb, "CMB FB", &kCmbFb},
    {config::kWobReset, "WOB RESET", &kWobReset}, {config::kDlyMode, "DLY MODE", &kDlyMode},
};
constexpr app::ConfigPage kPages[2] = {
    {"303", k303Items, sizeof k303Items / sizeof k303Items[0]},
    {"DEEP", kDeepItems, sizeof kDeepItems / sizeof kDeepItems[0]},
};
static_assert(sizeof k303Items / sizeof k303Items[0] <= app::kMaxConfigItems && sizeof kDeepItems / sizeof kDeepItems[0] <= app::kMaxConfigItems, "every item is shown");

// The sound entry a DEEP key writes (the engine's, as INSTRUMENT's keys do); Blank for the other keys.
Entry deep_entry(std::uint16_t key) noexcept {
  switch (key) {
    case config::kFmtMix:
      return Entry{Item::Growl, static_cast<std::uint16_t>(Growl::FormantMix), nullptr};
    case config::kCmbTune:
      return Entry{Item::Growl, static_cast<std::uint16_t>(Growl::CombTune), nullptr};
    case config::kCmbFb:
      return Entry{Item::Growl, static_cast<std::uint16_t>(Growl::CombFeedback), nullptr};
    case config::kWobReset:
      return Entry{Item::Wobble, kWobbleReset, nullptr};
    case config::kDlyMode:
      return Entry{Item::Delay, static_cast<std::uint16_t>(units::Delay::Mode), nullptr};
    default:
      break;
  }
  return Entry{};
}

unsigned round_index(float v, unsigned last) noexcept {
  const float c = v < 0.0f ? 0.0f : v;
  const unsigned i = static_cast<unsigned>(c + 0.5f);
  return i > last ? last : i;
}

}  // namespace

void ConfigPages::init(Rig& rig, LiveActions& live, ProjectService& project) noexcept {
  rig_ = &rig;
  live_ = &live;
  project_ = &project;
  keys_at_boot_ = 0;
  boot_ = true;
  live.set_ladder_slew_ms(kLadderSlewMs);
  frame::set_pad_legend(true);
  // (The autosave period is not reset: the project service keeps it across its own init() on purpose, as a setting that outlives the session; a unit boots with the default
  // and the kit's settings put the saved one over it.)
}

unsigned ConfigPages::pages() const noexcept { return 2; }

bool ConfigPages::page(unsigned index, app::ConfigPage& out) const noexcept {
  if (index >= 2u) {
    return false;
  }
  out = kPages[index];
  return true;
}

float ConfigPages::get(const app::Context& ctx, std::uint16_t key) const noexcept {
  switch (key) {
    case config::kAutosave:
      return static_cast<float>(config::autosave_choice(project_->autosave_period()));
    case config::kKeysAtBoot:
      return static_cast<float>(keys_at_boot_);
    case config::kLadderSlew:
      return live_->ladder_slew_ms();
    case config::kPadLegend:
      return frame::pad_legend() ? 1.0f : 0.0f;
    default:
      break;
  }
  const Entry en = deep_entry(key);
  return en.item == Item::Blank ? 0.0f : rig_->controls.get(ctx.engine(), en);
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
    default:
      break;
  }
  const Entry en = deep_entry(key);
  if (en.item != Item::Blank) {
    rig_->controls.set(ctx.engine(), en, value);
    project_->settings_moved(ctx.engine());
  }
}

void ConfigPages::run(app::Context& ctx, std::uint16_t key) noexcept {
  if (key == config::kResetProject) {
    (void)project_->reset_session(ctx);
  }
}

}  // namespace o3_303
