// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The pure parts of the performance gestures (docs/M3B-PLAN.md C4, R1, R7, R8): what the four performance buttons can be assigned to
// and which actions are on for a state of the buttons, the filter ladders' positions, the tempo nudge, the scale lock of the keyboard
// and the text of a trig condition; and, for the PERFORM screen (P1), the pad map and which actions are on for buttons and pads. No engine, no
// screen: tests/perform_test.cpp checks them, live_actions.cpp, main_screen_perf.cpp and perform_screen.cpp apply them.

#include <o3sdk/app/panel.h>
#include <o3sdk/core/scales.h>
#include <o3_303/pattern.h>
#include <o3_303/seq/wobble.h>

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
  Wobble,    // M3c: the wobble runs while it is on; a pad of the bottom row picks its rate (wobble_pad_rate)
  Roll,      // M3c: the freeze in ROLL mode (a retriggered loop, the build-up roll)
  Stop,      // M3c: the freeze in STOP mode (the tape stop)
  Reverse,   // M3c: the freeze in REVERSE mode
};
inline constexpr unsigned kPerfActionCount = 11;
inline constexpr unsigned kPerfButtons = 4;  // Note, Instr, Vol, Fx

// The defaults: Note = MUTE, Instr = ACC ALL, Vol = FILL, Fx = FREEZE (docs/M3B-PLAN.md C4; SLIDE ALL and THROW are one assignment away).
inline constexpr std::uint8_t kDefaultPerf[kPerfButtons] = {
    static_cast<std::uint8_t>(PerfAction::Mute), static_cast<std::uint8_t>(PerfAction::AccAll), static_cast<std::uint8_t>(PerfAction::Fill),
    static_cast<std::uint8_t>(PerfAction::Freeze)};

// The names on the PERF page's strip (at most eight characters) and in the header's chips (as short as they can be).
inline constexpr const char* kPerfActionNames[kPerfActionCount] = {"MUTE", "ACC ALL", "SLD ALL", "OCT UP", "FILL", "THROW", "FREEZE",
                                                                  "WOBBLE", "ROLL", "STOP", "REV"};
inline constexpr const char* kPerfActionChips[kPerfActionCount] = {"MUTE", "ACC", "SLD", "OCT+", "FILL", "THROW", "FREEZE", "WOB", "ROLL", "STOP", "REV"};

constexpr std::uint16_t perf_bit(PerfAction a) noexcept { return static_cast<std::uint16_t>(1u << static_cast<unsigned>(a)); }

// The actions that are on (a bit per PerfAction): those of the buttons that are held or latched (`held` and `latched`: bit b = button b).
// An assignment outside 0..kPerfActionCount-1 does nothing.
constexpr std::uint16_t perf_actions_on(const std::uint8_t (&assign)[kPerfButtons], unsigned held, unsigned latched) noexcept {
  unsigned on = 0;
  for (unsigned b = 0; b < kPerfButtons; ++b) {
    if (((held | latched) >> b & 1u) != 0u && assign[b] < kPerfActionCount) {
      on |= 1u << assign[b];
    }
  }
  return static_cast<std::uint16_t>(on);
}

// ---- P1: the live actions of the PERFORM screen (docs/UX-303.md 5.3, 6.3) ---------------------------------------------------------------
//
// The same eleven actions are also on 48 pads. A pad is held or latched per action (a bit per PerfAction); the four colour buttons stay
// held or latched per button (above). An action is on while a button that carries it or its own pad is held or latched.

inline constexpr std::uint16_t kAllActionsMask = static_cast<std::uint16_t>((1u << kPerfActionCount) - 1u);

constexpr std::uint16_t live_actions_on(const std::uint8_t (&assign)[kPerfButtons], unsigned button_held, unsigned button_latched,
                                        std::uint16_t pad_held, std::uint16_t pad_latched) noexcept {
  return static_cast<std::uint16_t>((perf_actions_on(assign, button_held, button_latched) | pad_held | pad_latched) & kAllActionsMask);
}

// ---- M3c W: the wobble pad ------------------------------------------------------------------------------------------------------------
//
// While the WOBBLE action is on, the bottom row of pads (row 4) is a rate picker instead of the lower octave: column c is rate c of
// seq/wobble.h (1/1 1/2 1/4 1/4T 1/8 1/8T 1/16 1/16T 1/32 1/8D 1/16D), column 11 has none. Its LEDs show Sequencer::wobble_rate_now().

// The wobble rate (a seq::WobbleRate) `pad` picks, or -1 when it is not one of the picker's pads.
constexpr int wobble_pad_rate(unsigned pad) noexcept {
  if (pad >= o3sdk::app::kPadCount || o3sdk::app::pad_row(pad) != o3sdk::app::kPadRows - 1u) {
    return -1;
  }
  const unsigned col = o3sdk::app::pad_col(pad);
  return col < seq::kWobbleRateCount ? static_cast<int>(col) : -1;
}

// ---- M3c R: ROLL, STOP and REV ride the beat freeze ---------------------------------------------------------------------------------------
//
// FREEZE, ROLL, STOP and REV are the four modes of one unit, so one of them is on at a time: the one that went on first OWNS the unit until it
// goes off (Active off; the Mode is left as it is, so no write touches the unit while it leaves). One that goes on while another owns it does
// nothing, and one still held when the owner lets go is not picked up (a press is an edge): press it again. An action writes the unit's Mode
// (Freeze::ModeValue: FREEZE 0, ROLL 1, STOP 2, REVERSE 3) just before Active, so the mode is the one the entry latches.

// The Freeze Mode value of an action, -1 for an action that is not one of the four.
constexpr int perf_freeze_mode(PerfAction a) noexcept {
  switch (a) {
    case PerfAction::Freeze:
      return 0;
    case PerfAction::Roll:
      return 1;
    case PerfAction::Stop:
      return 2;
    case PerfAction::Reverse:
      return 3;
    default:
      return -1;
  }
}

// What to write to the freeze unit: Mode (when `set_mode`) and then Active (when `set_active`).
struct FreezeCmd {
  bool set_mode = false;
  unsigned mode = 0;
  bool set_active = false;
  bool active = false;
};

class FreezeOwner {
 public:
  // `on`: the actions that are on now (perf_actions_on); `changed`: those whose state changed since the last call.
  FreezeCmd step(std::uint16_t on, std::uint16_t changed) noexcept;
  int owner() const noexcept { return owner_; }  // a PerfAction, -1 when none
  void reset() noexcept { owner_ = -1; }

 private:
  int owner_ = -1;
};

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

// ---- P1: the PERFORM pad map ----------------------------------------------------------------------------------------------------------
//
//   col      1     2     3     4     5     6     7     8     9     10    11    12
//   row 1   MUTE  ACC   SLD   OCT+  FILL  THRW  FRZ   ROLL  STOP  REV   WOB   ALL OFF        actions (the order of the mockup, not the enum's)
//   row 2    1     2     3     4     5     6     7     8    NEW   VARY  UNDO  REDO            snapshots 1..8, then the pattern variations
//   row 3   the ladder: 12 places of the latched sound knob (while WOBBLE is on: the wobble's rate, columns 1..11)
//   row 4   the keys: one octave, C..B

enum class PerformRole : std::uint8_t { Action, AllOff, Snapshot, New, Vary, Undo, Redo, Ladder, Key, None };

struct PerformPad {
  PerformRole role = PerformRole::None;
  std::uint8_t index = 0;  // Action: a PerfAction; Snapshot: the slot 0..7; Ladder and Key: the column 0..11
};

// Row 1, column by column, columns 1..11 (column 12 is ALL OFF).
inline constexpr PerfAction kPerformActionOrder[kPerfActionCount] = {
    PerfAction::Mute, PerfAction::AccAll,  PerfAction::SlideAll, PerfAction::OctUp, PerfAction::Fill,   PerfAction::Throw,
    PerfAction::Freeze, PerfAction::Roll,  PerfAction::Stop,     PerfAction::Reverse, PerfAction::Wobble};

constexpr PerformPad perform_pad(unsigned pad) noexcept {
  if (pad >= o3sdk::app::kPadCount) {
    return PerformPad{};
  }
  const unsigned row = o3sdk::app::pad_row(pad);
  const unsigned col = o3sdk::app::pad_col(pad);
  switch (row) {
    case 0:
      return col < kPerfActionCount ? PerformPad{PerformRole::Action, static_cast<std::uint8_t>(kPerformActionOrder[col])}
                                    : PerformPad{PerformRole::AllOff, 0};
    case 1:
      if (col < 8u) {
        return PerformPad{PerformRole::Snapshot, static_cast<std::uint8_t>(col)};
      }
      return PerformPad{col == 8u ? PerformRole::New : (col == 9u ? PerformRole::Vary : (col == 10u ? PerformRole::Undo : PerformRole::Redo)), 0};
    case 2:
      return PerformPad{PerformRole::Ladder, static_cast<std::uint8_t>(col)};
    default:
      return PerformPad{PerformRole::Key, static_cast<std::uint8_t>(col)};
  }
}

// The pad of an action on row 1 (past the panel for a value that is no action).
constexpr unsigned perform_action_pad(PerfAction a) noexcept {
  for (unsigned col = 0; col < kPerfActionCount; ++col) {
    if (kPerformActionOrder[col] == a) {
      return col;
    }
  }
  return o3sdk::app::kPadCount;
}

// While WOBBLE is on, row 3 is the rate picker (not the bottom row of the 303 screen): the rate (a seq::WobbleRate) `pad` picks, -1 for any other
// pad and for column 12.
constexpr int perform_wobble_rate(unsigned pad) noexcept {
  if (pad >= o3sdk::app::kPadCount || o3sdk::app::pad_row(pad) != 2u) {
    return -1;
  }
  const unsigned col = o3sdk::app::pad_col(pad);
  return col < seq::kWobbleRateCount ? static_cast<int>(col) : -1;
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
