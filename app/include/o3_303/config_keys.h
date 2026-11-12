// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The keys of the app's Config page "303" (docs/UX-303.md 5.12). A key is the item's identity in the kit's settings file (system.o3s, APPV): it is fixed here and
// never reused or renumbered, like a unit's parameter id: an older settings file keeps meaning what it meant. Key 6 is reserved (SEQ MIDI OUT, deferred, D16); key 7 is SKIN.
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
inline constexpr std::uint16_t kSkin = 7;          // CLASSIC, OBSIDIAN: the look of the screens that have a skin (CLASSIC until SK8; docs/SKIN-PLAN.md)
inline constexpr std::uint16_t kKeys = 8;          // ALL, IN SCALE: whether the keys outside the scale (HOME's Shift + F6) play (ALL, the default) or do nothing (S3 of docs/REVIEW-303.md)
inline constexpr std::uint16_t kKnobSpeed = 9;     // X1, X2, X3, X4, X6, X8: how far a detent of the encoder moves a continuous knob (X1, the default; kKnobSpeedMult)

// Keys 20..24 were the page "DEEP" (FMT MIX, CMB TUNE, CMB FB, WOB RESET, DLY MODE): retired 2026-11-01 (docs/MODS-PLAN.md 7), never reused. A settings file that holds them loads with them dropped.
// 20: retired (the growl's Formant Mix)   21: retired (Comb Tune)   22: retired (Comb Feedback)   23: retired (WobbleSettings::reset)   24: retired (the delay's Mode)

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

// KNOB SPEED's places (the value is the index) and the factor each one is.
inline constexpr unsigned kKnobSpeedChoices = 6;
inline constexpr std::uint8_t kKnobSpeedMult[kKnobSpeedChoices] = {1, 2, 3, 4, 6, 8};

inline constexpr float kLadderSlewMinMs = 10.0f;
inline constexpr float kLadderSlewMaxMs = 100.0f;

}  // namespace o3_303::config
