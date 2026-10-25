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

}  // namespace o3_303
