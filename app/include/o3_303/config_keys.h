// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The keys of the app's Config pages "303" and "DEEP" (docs/UX-303.md 5.12). A key is the item's identity in the kit's settings file (system.o3s, APPV): it is fixed here and
// never reused or renumbered, like a unit's parameter id: an older settings file keeps meaning what it meant. Key 6 is reserved (SEQ MIDI OUT, deferred, D16).
// Pure data and the two conversions of AUTOSAVE: no engine, no screen, so a host test holds them.

#include <cstdint>

namespace o3_303::config {

// Page "303".
inline constexpr std::uint16_t kAutosave = 1;      // OFF, 10 S, 30 S (the default 10 S)
inline constexpr std::uint16_t kKeysAtBoot = 2;    // PLAY, TRANSPOSE: TRNSP off or on at boot
inline constexpr std::uint16_t kLadderSlew = 3;    // 10..100 ms (30)
inline constexpr std::uint16_t kPadLegend = 4;     // OFF, ON: the PADS line's text (ON)
inline constexpr std::uint16_t kResetProject = 5;  // an action
// 6: reserved.

// Page "DEEP": the sound settings no INSTRUMENT page has. They are also in the project (SND), which wins at a boot load (D18).
inline constexpr std::uint16_t kFmtMix = 20;       // the growl's Formant Mix
inline constexpr std::uint16_t kCmbTune = 21;      // the growl's Comb Tune
inline constexpr std::uint16_t kCmbFb = 22;        // the growl's Comb Feedback
inline constexpr std::uint16_t kWobReset = 23;     // OFF, ON (WobbleSettings::reset)
inline constexpr std::uint16_t kDlyMode = 24;      // the delay's Mode (MONO, PING)

// AUTOSAVE's three places (the value is the index) and the period each one is, in milliseconds (0: OFF).
inline constexpr unsigned kAutosaveOff = 0;
inline constexpr unsigned kAutosave10s = 1;
inline constexpr unsigned kAutosave30s = 2;
inline constexpr unsigned kAutosaveChoices = 3;

constexpr std::uint32_t autosave_period_ms(unsigned choice) noexcept {
  return choice == kAutosave10s ? 10'000u : (choice == kAutosave30s ? 30'000u : 0u);
}
// The place a period belongs to (the nearest: 0 is OFF, up to 20 s is 10 S, longer is 30 S).
constexpr unsigned autosave_choice(std::uint32_t period_ms) noexcept {
  return period_ms == 0u ? kAutosaveOff : (period_ms <= 20'000u ? kAutosave10s : kAutosave30s);
}

inline constexpr float kLadderSlewMinMs = 10.0f;
inline constexpr float kLadderSlewMaxMs = 100.0f;

}  // namespace o3_303::config
