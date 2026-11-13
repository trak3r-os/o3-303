// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The four sounds the colour keys switch (E1b, docs/PADS-TRACT-PLAN.md E): Note is MODS (the voice's Devil Fish mods against the stock 303), Instr GROWL, Vol (FX1) TRACT and Fx (FX2) WOBBLE.
// A plain press of the key flips its sound from every screen; the screens show it as a header chip (E1c). The functions are the one gesture: the keys, the tests and the chips use them.
// Main loop only.

#include "rig.h"

#include <o3sdk/app/context.h>
#include <o3sdk/dsp/engine.h>

#include <cstdint>

namespace o3_303 {

enum class Sound : std::uint8_t { Mods, Growl, Tract, Wobble };

inline constexpr unsigned kSoundCount = 4;

// The sound's key on the panel: Note, Instr, Vol, Fx = 0..3.
constexpr unsigned sound_key(Sound s) noexcept { return static_cast<unsigned>(s); }
constexpr std::uint8_t sound_bit(Sound s) noexcept { return static_cast<std::uint8_t>(1u << static_cast<unsigned>(s)); }

// Flips the sound and says which way on the status line ("GROWL ON"); MODS ends STOCK as toggle_stock does, GROWL and TRACT flip their Mix between 0 and its last value (Item::GrowlOn, TractOn),
// WOBBLE latches or releases PERFORM's WOB action (LiveActions::toggle_latch).
void toggle_sound(Rig& rig, o3sdk::app::Context& ctx, Sound s) noexcept;
// Whether the sound is heard now.
bool sound_on(const Rig& rig, const o3sdk::dsp::Engine& e, Sound s) noexcept;
// Every sound that is on, a bit per Sound (sound_bit).
std::uint8_t sound_bits(const Rig& rig, const o3sdk::dsp::Engine& e) noexcept;

}  // namespace o3_303
