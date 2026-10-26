// SPDX-License-Identifier: GPL-3.0-or-later
#include "rig.h"

namespace o3_303 {

void load_preset(o3sdk::dsp::Engine& e, Rig& rig, const seq::gen::Preset& preset, LoadWhat what) noexcept {
  const seq::gen::ParamValues v = seq::gen::to_values(preset.patch);
  if (what != LoadWhat::Sound) {
    (void)rig.bank->replace(rig.bank->selected(), preset.pattern);
    (void)e.set_tempo(v.tempo_mbpm);
  }
  if (what != LoadWhat::Pattern) {
    for (unsigned i = 0; i < seq::gen::kVoiceValues; ++i) {
      (void)e.set_param(rig.wiring.voice, static_cast<std::uint16_t>(i), v.voice[i]);
    }
    for (unsigned i = 0; i < seq::gen::kDistValues; ++i) {
      (void)e.set_param(rig.wiring.dist, static_cast<std::uint16_t>(i), v.dist[i]);
    }
  }
}

void set_freeze(o3sdk::dsp::Engine& e, const Rig& rig, bool on) noexcept {
  (void)e.set_param(rig.wiring.freeze, Freeze::Active, on ? 1.0f : 0.0f);
}

bool freeze_on(const o3sdk::dsp::Engine& e, const Rig& rig) noexcept { return e.param(rig.wiring.freeze, Freeze::Active) >= 0.5f; }

void set_freeze_mode(o3sdk::dsp::Engine& e, const Rig& rig, Freeze::ModeValue mode) noexcept {
  (void)e.set_param(rig.wiring.freeze, Freeze::Mode, static_cast<float>(mode));
}

void set_freeze_length(o3sdk::dsp::Engine& e, const Rig& rig, Freeze::LengthValue length) noexcept {
  (void)e.set_param(rig.wiring.freeze, Freeze::Length, static_cast<float>(length));
}

Freeze::LengthValue freeze_length(const o3sdk::dsp::Engine& e, const Rig& rig) noexcept {
  const float v = e.param(rig.wiring.freeze, Freeze::Length) + 0.5f;
  return v >= 2.0f ? Freeze::Sixteenth : (v >= 1.0f ? Freeze::Eighth : Freeze::Quarter);
}

}  // namespace o3_303
