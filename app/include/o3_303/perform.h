// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The pure parts of the performance gestures (docs/M3B-PLAN.md C4, R1, R7, R8): what the four performance buttons can be assigned to
// and which actions are on for a state of the buttons, the filter ladders' positions, the tempo nudge, the scale lock of the keyboard
// and the text of a trig condition; and, for the PERFORM screen (P1), the pad map and which actions are on for buttons and pads. No engine, no
// screen: tests/perform_test.cpp checks them, live_actions.cpp, home_map.cpp, master_screen.cpp and perform_screen.cpp apply them.

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
  Wobble,    // M3c: the wobble runs while it is on; a pad of row 3 picks its rate (kWobblePadRates)
  Roll,      // M3c: the freeze in ROLL mode (a retriggered loop, the build-up roll)
  Stop,      // M3c: the freeze in STOP mode (the tape stop)
  Reverse,   // M3c: the freeze in REVERSE mode
  OctDown,   // H1: the running pattern an octave lower (the keyboard's live transpose - 12); appended, so the saved assignments keep their meaning
};
inline constexpr unsigned kPerfActionCount = 12;
inline constexpr unsigned kPerfButtons = 4;  // Note, Instr, Vol, Fx

// The defaults: Note = MUTE, Instr = ACC ALL, Vol = FILL, Fx = FREEZE (docs/M3B-PLAN.md C4; SLIDE ALL and THROW are one assignment away).
inline constexpr std::uint8_t kDefaultPerf[kPerfButtons] = {
    static_cast<std::uint8_t>(PerfAction::Mute), static_cast<std::uint8_t>(PerfAction::AccAll), static_cast<std::uint8_t>(PerfAction::Fill),
    static_cast<std::uint8_t>(PerfAction::Freeze)};

// The names on the PERF page's strip (at most eight characters) and in the header's chips (as short as they can be).
inline constexpr const char* kPerfActionNames[kPerfActionCount] = {"MUTE", "ACC ALL", "SLD ALL", "OCT UP", "FILL", "THROW", "FREEZE",
                                                                  "WOBBLE", "ROLL", "STOP", "REV", "OCT DOWN"};
inline constexpr const char* kPerfActionChips[kPerfActionCount] = {"MUTE", "ACC", "SLD", "OCT+", "FILL", "THROW", "FREEZE", "WOB", "ROLL", "STOP", "REV", "OCT-"};

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
// The same actions are also on pads (H1: the line and tone actions on row 1, the freeze unit's rates on row 2, the wobble's rates on row 3). A pad is held or latched per action (a bit per PerfAction); the four colour buttons stay
// held or latched per button (above). An action is on while a button that carries it or its own pad is held or latched.

inline constexpr std::uint16_t kAllActionsMask = static_cast<std::uint16_t>((1u << kPerfActionCount) - 1u);

constexpr std::uint16_t live_actions_on(const std::uint8_t (&assign)[kPerfButtons], unsigned button_held, unsigned button_latched,
                                        std::uint16_t pad_held, std::uint16_t pad_latched) noexcept {
  return static_cast<std::uint16_t>((perf_actions_on(assign, button_held, button_latched) | pad_held | pad_latched) & kAllActionsMask);
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

// ---- H1: the PERFORM pad map ----------------------------------------------------------------------------------------------------------
//
//   col      1     2     3     4     5     6     7     8     9     10    11    12
//   row 1   MUTE  ACC   SLD   OCT-  OCT+  FILL  THRW  .     .     .     .     ALL OFF        the line and the tone (the four dark pads are H2's punches)
//   row 2   FRZ   RPT   RPT   RPT   RPT   BLD   BLD   STOP  STOP  REV   .     .              the beat freeze's rates (one unit, one owner); the last two are dark
//   row 3   1/4   1/8   1/16  1/32  1/4T  1/8T  1/16T 1/8D  1/16D 1/2   1/1   .              the wobble's rates (kWobblePadRates)
//   row 4   the keys: one octave, C..B
//
// PERFORM SETUP carries the same map with row 2 swapped for the snapshots (perform_setup_pad).

enum class PerformRole : std::uint8_t { Action, AllOff, Snapshot, New, Vary, Undo, Redo, Repeat, WobRate, Key, None };

struct PerformPad {
  PerformRole role = PerformRole::None;
  std::uint8_t index = 0;  // Action: a PerfAction; Snapshot: the slot 0..7; Repeat, WobRate and Key: the column
};

// Row 1, column by column, columns 1..7 (column 12 is ALL OFF).
inline constexpr unsigned kLineActionCount = 7;
inline constexpr PerfAction kPerformActionOrder[kLineActionCount] = {PerfAction::Mute,   PerfAction::AccAll, PerfAction::SlideAll, PerfAction::OctDown,
                                                                    PerfAction::OctUp, PerfAction::Fill,   PerfAction::Throw};

// Row 2: what a pad does to the beat freeze (units/freeze). `value` is the unit's parameter for the action (Freeze: Length 0..2, Roll: Roll Rate
// 0..3, Stop: Stop Time 0..4, Reverse: none) and `accel` the Accelerate of a roll (0 off, 1 beat, 2 bar): live_actions.cpp static_asserts them
// against the unit's enums. `label` is the lamp's text (at most seven characters; RPT n is a roll of 1/n notes).
struct RepeatPad {
  PerfAction action;
  std::uint8_t value;
  std::uint8_t accel;
  const char* label;
};
inline constexpr unsigned kRepeatPadCount = 10;
inline constexpr RepeatPad kRepeatPads[kRepeatPadCount] = {
    {PerfAction::Freeze, 0, 0, "FRZ 1/4"}, {PerfAction::Roll, 0, 0, "RPT 8"}, {PerfAction::Roll, 1, 0, "RPT 16"},
    {PerfAction::Roll, 2, 0, "RPT 32"},    {PerfAction::Roll, 3, 0, "RPT 64"},   {PerfAction::Roll, 0, 1, "BLD BT"},
    {PerfAction::Roll, 0, 2, "BLD BAR"},   {PerfAction::Stop, 2, 0, "STP 1/2"},  {PerfAction::Stop, 4, 0, "STP 2"},
    {PerfAction::Reverse, 0, 0, "REV"}};

// Row 3 (and the WOBBLE screen's row 3 and the home map's WOB row, E2c): the wobble rate (a seq::WobbleRate) of each column, 1/4 first.
inline constexpr unsigned kWobblePadCount = seq::kWobbleRateCount;
inline constexpr std::uint8_t kWobblePadRates[kWobblePadCount] = {seq::kWobble1_4,  seq::kWobble1_8,  seq::kWobble1_16, seq::kWobble1_32,
                                                                 seq::kWobble1_4T, seq::kWobble1_8T, seq::kWobble1_16T, seq::kWobble1_8D,
                                                                 seq::kWobble1_16D, seq::kWobble1_2, seq::kWobble1_1};

constexpr PerformPad perform_pad(unsigned pad) noexcept {
  if (pad >= o3sdk::app::kPadCount) {
    return PerformPad{};
  }
  const unsigned row = o3sdk::app::pad_row(pad);
  const unsigned col = o3sdk::app::pad_col(pad);
  switch (row) {
    case 0:
      if (col < kLineActionCount) {
        return PerformPad{PerformRole::Action, static_cast<std::uint8_t>(kPerformActionOrder[col])};
      }
      return col == 11u ? PerformPad{PerformRole::AllOff, 0} : PerformPad{};  // columns 8..11: H2's punches, dark until built
    case 1:
      return col < kRepeatPadCount ? PerformPad{PerformRole::Repeat, static_cast<std::uint8_t>(col)} : PerformPad{};
    case 2:
      return col < kWobblePadCount ? PerformPad{PerformRole::WobRate, static_cast<std::uint8_t>(col)} : PerformPad{};
    default:
      return PerformPad{PerformRole::Key, static_cast<std::uint8_t>(col)};
  }
}

// PERFORM SETUP's map: PERFORM's with row 2 the snapshots 1..8 and then the pattern variations.
constexpr PerformPad perform_setup_pad(unsigned pad) noexcept {
  if (pad < o3sdk::app::kPadCount && o3sdk::app::pad_row(pad) == 1u) {
    const unsigned col = o3sdk::app::pad_col(pad);
    if (col < 8u) {
      return PerformPad{PerformRole::Snapshot, static_cast<std::uint8_t>(col)};
    }
    return PerformPad{col == 8u ? PerformRole::New : (col == 9u ? PerformRole::Vary : (col == 10u ? PerformRole::Undo : PerformRole::Redo)), 0};
  }
  return perform_pad(pad);
}

// The pad of an action on row 1 (past the panel for an action that has none there).
constexpr unsigned perform_action_pad(PerfAction a) noexcept {
  for (unsigned col = 0; col < kLineActionCount; ++col) {
    if (kPerformActionOrder[col] == a) {
      return col;
    }
  }
  return o3sdk::app::kPadCount;
}

// Row 3: the wobble rate (a seq::WobbleRate) `pad` picks, -1 for any other pad.
constexpr int perform_wobble_rate(unsigned pad) noexcept {
  const PerformPad pp = perform_pad(pad);
  return pp.role == PerformRole::WobRate ? static_cast<int>(kWobblePadRates[pp.index]) : -1;
}

// ---- H1: held and latched columns of a row of pads ------------------------------------------------------------------------------------
//
// PadStack is the order in which a row's pads went on (at most 12 columns, a column once): the last one pressed wins, and when it goes the one
// below it takes over. PadGroup is a row's state: the columns held (the pad is down) and latched (Shift + a press; a tap on a latched one lets
// it go), with their stack. Main loop only.

class PadStack {
 public:
  static constexpr unsigned kCap = 12;
  // `col` goes on top (a column already in the stack moves there).
  void push(unsigned col) noexcept {
    remove(col);
    if (col < kCap && n_ < kCap) {
      col_[n_++] = static_cast<std::uint8_t>(col);
    }
  }
  void remove(unsigned col) noexcept {
    unsigned w = 0;
    for (unsigned i = 0; i < n_; ++i) {
      if (col_[i] != col) {
        col_[w++] = col_[i];
      }
    }
    n_ = static_cast<std::uint8_t>(w);
  }
  bool contains(unsigned col) const noexcept {
    for (unsigned i = 0; i < n_; ++i) {
      if (col_[i] == col) {
        return true;
      }
    }
    return false;
  }
  unsigned size() const noexcept { return n_; }
  void clear() noexcept { n_ = 0; }
  // The column nearest the top whose bit is in `mask`, -1 when none.
  int top_in(unsigned mask) const noexcept {
    for (unsigned i = n_; i > 0; --i) {
      if ((mask >> col_[i - 1u] & 1u) != 0u) {
        return col_[i - 1u];
      }
    }
    return -1;
  }

 private:
  std::uint8_t col_[kCap] = {};
  std::uint8_t n_ = 0;
};

class PadGroup {
 public:
  // A press: a latched column lets go; else Shift latches it and a plain press holds it.
  void press(unsigned col, bool shift) noexcept {
    if (col >= PadStack::kCap) {
      return;
    }
    const unsigned bit = 1u << col;
    if ((latched_ & bit) != 0u) {
      latched_ &= ~bit;
      prune();
    } else if (shift) {
      latched_ |= bit;
      stack_.push(col);
    } else {
      held_ |= bit;
      stack_.push(col);
    }
  }
  void release(unsigned col) noexcept {
    held_ &= ~(1u << col);
    prune();
  }
  void release_held() noexcept {
    held_ = 0;
    prune();
  }
  void clear_latched() noexcept {
    latched_ = 0;
    prune();
  }
  void reset() noexcept {
    held_ = 0;
    latched_ = 0;
    stack_.clear();
  }
  unsigned held() const noexcept { return held_; }
  unsigned latched() const noexcept { return latched_; }
  unsigned active() const noexcept { return held_ | latched_; }
  // The column that wins among those whose bit is in `mask` (all of them by default), -1 when none is on.
  int top(unsigned mask = 0xFFFFu) const noexcept { return stack_.top_in(active() & mask); }

 private:
  void prune() noexcept {
    for (unsigned c = 0; c < PadStack::kCap; ++c) {
      if ((active() >> c & 1u) == 0u) {
        stack_.remove(c);
      }
    }
  }
  PadStack stack_;
  unsigned held_ = 0;
  unsigned latched_ = 0;
};

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
