// The ladder rows (backlog item 8): HOME's FILT rows 3 and 4 and PERFORM's row 3 are one kind of row. A row follows the latched knob or holds a
// knob of its own (Wiring::ladder, stored as a lane target and parameter), a tap jumps the knob to one of twelve places (a lane slew, or a direct
// set for a switch), and with REC on it writes the jump as motion. Everything here is main loop.
#pragma once

#include "rig.h"
#include <o3_303/pages.h>
#include <cstdint>

namespace o3_303 {

enum class LadderAssign : std::uint8_t {
  NoLane,  // the knob is not one a lane can drive: the row is unchanged
  Held,    // the row now holds the knob
  Default  // the row held that knob already: it is back to its default
};

struct LadderTap {
  bool lanes_full = false;  // REC was on and all eight lanes drive something else: nothing was written
  int lane = -1;            // the lane the row's knob has in the pattern shown (-1 when none)
};

// The entry a row drives now: the held knob, or `latched` for a row that follows. Blank when the held knob has no entry on any page.
Entry ladder_entry(const Controls& controls, const Wiring& wiring, unsigned row, const Entry& latched) noexcept;

// Hold an F key and tap a pad of the row: the row holds `knob`; again with the knob it holds, the row returns to its default.
LadderAssign ladder_assign(Wiring& wiring, const Controls& controls, unsigned row, const Entry& knob) noexcept;

// Shift + FILT: both HOME rows back to CUTOFF and RESO.
void ladder_reset_home(Wiring& wiring) noexcept;

// "CUTOFF", or "LATCHED: DECAY" for a row that follows. At most cap-1 characters, cut at the end.
void ladder_caption(const o3sdk::dsp::Engine& e, const Controls& controls, const Wiring& wiring, unsigned row, const Entry& latched, char* out, unsigned cap) noexcept;

// A short name of what a row drives, for the pads line and the strip: the held knob's name, "LATCHED" for a row that follows.
const char* ladder_short_name(const o3sdk::dsp::Engine& e, const Controls& controls, const Wiring& wiring, unsigned row) noexcept;

// A tap on place `col` of a row whose entry is `entry`. `rec_motion`: REC is on while the pattern plays. `sel`: the pattern shown.
LadderTap ladder_tap(o3sdk::app::Context& ctx, Rig& rig, bool rec_motion, unsigned sel, const Entry& entry, unsigned col) noexcept;

}  // namespace o3_303
