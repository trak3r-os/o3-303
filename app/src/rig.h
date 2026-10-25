// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the screens share: the sequencer and its bank, the units' ids, the pages' controls. The app owns one and hands every screen a
// pointer; nothing in it is touched by the render context (the sequencer's own atomics and rings are the way across).

#include <o3sdk/dsp/engine.h>
#include <o3_303/pages.h>
#include <o3_303/seq/bank.h>
#include <o3_303/seq/gen.h>
#include <o3_303/seq/sequencer.h>

#include <cstdint>

namespace o3_303 {

struct Rig {
  seq::Bank* bank = nullptr;
  seq::Sequencer* seq = nullptr;
  Wiring wiring;
  Controls controls;
};

// What a vault load takes from a preset.
enum class LoadWhat : std::uint8_t {
  Both,     // the pattern with its tempo, and the sound
  Pattern,  // the pattern (swing is part of it) and the tempo
  Sound,    // the patch: the voice's parameters and the distortion's
};

// Loads `preset` (docs/M3-PLAN.md "Contracts": a 16-step pattern, a patch, tempo and swing): the pattern replaces the selected pattern
// (one undoable edit), the patch goes to the voice and the distortion as parameter values, the tempo to the engine. Master and the
// level are not part of a preset: loading never jumps the volume of a set.
void load_preset(o3sdk::dsp::Engine& e, Rig& rig, const seq::gen::Preset& preset, LoadWhat what) noexcept;

}  // namespace o3_303
