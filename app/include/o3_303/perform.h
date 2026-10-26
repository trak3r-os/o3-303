// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The pure parts of the performance gestures (docs/M3B-PLAN.md C4, R1, R7, R8): what the four performance buttons can be assigned to
// and which actions are on for a state of the buttons, the filter ladders' positions, the tempo nudge, the scale lock of the keyboard
// and the text of a trig condition. No engine, no screen: tests/perform_test.cpp checks them, main_screen_perf.cpp applies them.

#include <o3sdk/core/scales.h>
#include <o3_303/pattern.h>

#include <cstdint>

namespace o3_303 {

// ---- C4: the four performance buttons -----------------------------------------------------------------------------------------------
//
// Note, Instr, Vol and Fx (buttons 0..3 below) each do one action while held; Shift + the button latches it (and again unlatches). Two
// buttons may carry the same action: it is on while either is. The assignment is Wiring::perf (pages.h), edited on the PERF page.

enum class PerfAction : std::uint8_t {
  Mute,      // no new notes (the playhead goes on)
  AccAll,    // every note accented
  SlideAll,  // every step slides
  OctUp,     // the running pattern an octave higher (the keyboard's live transpose + 12)
  Fill,      // the pattern plays its fill variant (the variant is chosen on the PERF page)
  Throw,     // the delay send opens to unity
  Freeze,    // the beat freeze (master insert 0)
};
inline constexpr unsigned kPerfActionCount = 7;
inline constexpr unsigned kPerfButtons = 4;  // Note, Instr, Vol, Fx

// The defaults: Note = MUTE, Instr = ACC ALL, Vol = FILL, Fx = FREEZE (docs/M3B-PLAN.md C4; SLIDE ALL and THROW are one assignment away).
inline constexpr std::uint8_t kDefaultPerf[kPerfButtons] = {
    static_cast<std::uint8_t>(PerfAction::Mute), static_cast<std::uint8_t>(PerfAction::AccAll), static_cast<std::uint8_t>(PerfAction::Fill),
    static_cast<std::uint8_t>(PerfAction::Freeze)};

// The names on the PERF page's strip (at most eight characters) and in the header's chips (as short as they can be).
inline constexpr const char* kPerfActionNames[kPerfActionCount] = {"MUTE", "ACC ALL", "SLD ALL", "OCT UP", "FILL", "THROW", "FREEZE"};
inline constexpr const char* kPerfActionChips[kPerfActionCount] = {"MUTE", "ACC", "SLD", "OCT+", "FILL", "THROW", "FREEZE"};

constexpr std::uint8_t perf_bit(PerfAction a) noexcept { return static_cast<std::uint8_t>(1u << static_cast<unsigned>(a)); }

// The actions that are on (a bit per PerfAction): those of the buttons that are held or latched (`held` and `latched`: bit b = button b).
// An assignment outside 0..kPerfActionCount-1 does nothing.
constexpr std::uint8_t perf_actions_on(const std::uint8_t (&assign)[kPerfButtons], unsigned held, unsigned latched) noexcept {
  unsigned on = 0;
  for (unsigned b = 0; b < kPerfButtons; ++b) {
    if (((held | latched) >> b & 1u) != 0u && assign[b] < kPerfActionCount) {
      on |= 1u << assign[b];
    }
  }
  return static_cast<std::uint8_t>(on);
}

// ---- R1: the pad filter ladders -----------------------------------------------------------------------------------------------------
//
// Hold KEYS: the 12 pads of row 2 are 12 positions of Cutoff, those of row 3 of Reso (Shift: Env Mod). A position is a place on the
// knob (its normalised position, so a log cutoff is a log ladder): the first pad is the minimum, the last the maximum.

inline constexpr unsigned kLadderSteps = 12;
inline constexpr float kLadderSlewMs = 30.0f;  // a tap jumps there through a 30 ms slew

constexpr float ladder_norm(unsigned col) noexcept {
  return col >= kLadderSteps - 1u ? 1.0f : static_cast<float>(col) / static_cast<float>(kLadderSteps - 1u);
}
// The pad nearest to a normalised position (0..1; out of range is clamped).
constexpr unsigned ladder_col(float norm) noexcept {
  if (!(norm > 0.0f)) {
    return 0;
  }
  if (norm >= 1.0f) {
    return kLadderSteps - 1u;
  }
  return static_cast<unsigned>(norm * static_cast<float>(kLadderSteps - 1u) + 0.5f);
}

// ---- R7: the tempo nudge ------------------------------------------------------------------------------------------------------------
//
// Hold Master and turn: the tempo moves one percent a detent, at most 3 % either way; letting Master go gives the base tempo back.

inline constexpr int kNudgeMaxPct = 3;

constexpr int clamp_nudge(int pct) noexcept { return pct < -kNudgeMaxPct ? -kNudgeMaxPct : (pct > kNudgeMaxPct ? kNudgeMaxPct : pct); }

// `base` (milli-BPM) moved by `pct` percent (clamped to the nudge's range). Integer arithmetic, exact at 0 %.
constexpr std::uint32_t nudged_tempo(std::uint32_t base_mbpm, int pct) noexcept {
  return static_cast<std::uint32_t>(static_cast<std::uint64_t>(base_mbpm) * static_cast<std::uint64_t>(100 + clamp_nudge(pct)) / 100u);
}

// ---- R8: the scale-locked keyboard ---------------------------------------------------------------------------------------------------

// Whether `note` belongs to scale index `scale` (o3sdk's core::kScaleDef: 0 No scale and 1 Chromatic have every note) over `root` (a
// pitch class, 0 = C). An index past the table is "no scale".
constexpr bool in_scale(int note, unsigned scale, unsigned root) noexcept {
  if (scale >= o3sdk::core::kScaleCount) {
    return true;
  }
  const int degree = ((note - static_cast<int>(root)) % 12 + 12) % 12;
  return (o3sdk::core::kScaleDef[scale] >> degree & 1u) != 0u;
}

// ---- R5: how a trig condition reads -------------------------------------------------------------------------------------------------

// "ALWAYS", "50%", "1:2", "FILL", "!FILL", "FIRST", "!FIRST" into `out` (cap at least 8; shorter is cut). Returns its length. A reserved
// value reads as ALWAYS, as it plays.
unsigned cond_text(std::uint8_t cond, char* out, unsigned cap) noexcept;

}  // namespace o3_303
