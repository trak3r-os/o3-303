// SPDX-License-Identifier: GPL-3.0-or-later
#include "sound_buttons.h"

#include "live_actions.h"

namespace o3_303 {

using namespace o3sdk;

namespace {

constexpr Item item_of(Sound s) noexcept {
  switch (s) {
    case Sound::Mods:
      return Item::Mods;
    case Sound::Growl:
      return Item::GrowlOn;
    case Sound::Tract:
      return Item::TractOn;
    case Sound::Wobble:
      break;
  }
  return Item::WobOn;
}

constexpr const char* message_of(Sound s, bool on) noexcept {
  switch (s) {
    case Sound::Mods:
      return on ? "MODS ON" : "MODS OFF";
    case Sound::Growl:
      return on ? "GROWL ON" : "GROWL OFF";
    case Sound::Tract:
      return on ? "TRACT ON" : "TRACT OFF";
    case Sound::Wobble:
      break;
  }
  return on ? "WOB ON" : "WOB OFF";
}

}  // namespace

bool sound_on(const Rig& rig, const dsp::Engine& e, Sound s) noexcept {
  return rig.controls.get(e, Entry{item_of(s), 0, nullptr}) >= 0.5f;
}

std::uint8_t sound_bits(const Rig& rig, const dsp::Engine& e) noexcept {
  std::uint8_t bits = 0;
  for (unsigned s = 0; s < kSoundCount; ++s) {
    if (sound_on(rig, e, static_cast<Sound>(s))) {
      bits = static_cast<std::uint8_t>(bits | sound_bit(static_cast<Sound>(s)));
    }
  }
  return bits;
}

void toggle_sound(Rig& rig, app::Context& ctx, Sound s) noexcept {
  switch (s) {
    case Sound::Mods:
      toggle_stock(rig, ctx);  // says MODS ON / OFF (or that the write was refused) itself
      return;
    case Sound::Growl:
    case Sound::Tract: {
      const bool before = sound_on(rig, ctx.engine(), s);
      rig.controls.set(ctx.engine(), Entry{item_of(s), 0, nullptr}, before ? 0.0f : 1.0f);
      say_on_status(rig, ctx, message_of(s, sound_on(rig, ctx.engine(), s)), false);
      return;
    }
    case Sound::Wobble:
      if (rig.live != nullptr) {
        rig.live->toggle_latch(ctx, PerfAction::Wobble);
      }
      say_on_status(rig, ctx, message_of(s, sound_on(rig, ctx.engine(), s)), false);
      return;
  }
}

}  // namespace o3_303
