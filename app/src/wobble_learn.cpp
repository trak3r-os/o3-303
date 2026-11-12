// SPDX-License-Identifier: GPL-3.0-or-later
#include "wobble_learn.h"

namespace o3_303 {

const o3sdk::dsp::UnitInfo& WobbleLearn::info() const noexcept {
  static const o3sdk::dsp::UnitInfo kInfo = {"WOBBLE", o3sdk::dsp::UnitKind::Effect, wobble_learn_desc(0), kParamCount, nullptr, 1};
  return kInfo;
}

void WobbleLearn::prepare(float /*sample_rate*/) noexcept {
  for (std::uint16_t i = 0; i < kParamCount; ++i) {
    last_[i] = wobble_learn_desc(0)[i].def;
  }
}

void WobbleLearn::process(const o3sdk::dsp::ProcessArgs& /*args*/) noexcept {}  // never routed: the engine does not call it

void WobbleLearn::mirror(o3sdk::dsp::Engine& e, o3sdk::dsp::UnitId self, const WobbleSettings& s, bool wob_on) noexcept {
  const float now[kParamCount] = {static_cast<float>(s.rate), static_cast<float>(s.shape), static_cast<float>(s.depth_pct), s.reset ? 1.0f : 0.0f, static_cast<float>(s.target), wob_on ? 1.0f : 0.0f};
  for (std::uint16_t i = 0; i < kParamCount; ++i) {
    if (now[i] != last_[i]) {
      last_[i] = now[i];
      (void)e.set_param(self, i, now[i]);
    }
  }
}

}  // namespace o3_303
