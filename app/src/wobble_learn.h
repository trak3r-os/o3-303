// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// WOBBLE's knobs, MIDI-learnable (docs/PADS-TRACT-PLAN.md D1). The kit binds a controller to a (unit, parameter) of the engine; WOBBLE's values are app values (WobbleSettings, UX-303 D15).
// This unit is the proxy: attached after the tract (id 6), never routed and in no chain, so its events are dropped at the end of the block and process() does nothing. It holds six parameters
// that mirror the settings (RATE, SHAPE, DEPTH, RESET, TARGET in WobbleField's order, and WOB, the latch of PERFORM's wobble action as param 5): Midi::learn can arm them, a learned controller moves
// them through the engine, and O3_303App::midi_learned_moved writes the new value to the settings (Controls::set) or flips the latch. The mirror runs from the app's update(), so Pickup and the
// kit's relative mode read the real value. Main loop only, but process(): the engine owns the unit's render call.

#include <o3sdk/dsp/engine.h>
#include <o3sdk/dsp/unit.h>
#include <o3_303/pages.h>

#include <cstdint>

namespace o3_303 {

// The proxy's parameter table: WobbleField's five, then WOB. `id` past the end gives null. The Controls descriptors of Item::Wobble and Item::WobOn are these same rows.
const o3sdk::core::ParamDesc* wobble_learn_desc(std::uint16_t id) noexcept;

class WobbleLearn final : public o3sdk::dsp::Unit {
 public:
  static constexpr std::uint16_t kWob = kWobbleFieldCount;  // param 5: WOB, the wobble action's latch
  static constexpr std::uint16_t kParamCount = kWobbleFieldCount + 1u;

  const o3sdk::dsp::UnitInfo& info() const noexcept override;
  void prepare(float sample_rate) noexcept override;
  void process(const o3sdk::dsp::ProcessArgs& args) noexcept override;

  // Main loop: the engine's six values follow the settings and the latch. Only a field that differs from what was last written is written (no event a block when nothing moved).
  void mirror(o3sdk::dsp::Engine& e, o3sdk::dsp::UnitId self, const WobbleSettings& settings, bool wob_on) noexcept;

 private:
  float last_[kParamCount] = {};  // what mirror() last wrote (main loop); the table's defaults after prepare()
};

}  // namespace o3_303
