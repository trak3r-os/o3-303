// The ladder rows (backlog item 8): HOME's FILT rows 3 and 4 and PERFORM's row 3 are one kind of row. A row follows the latched knob or holds a
// knob of its own (Wiring::ladder, stored as a lane target and parameter), a tap jumps the knob to one of twelve places (a lane slew, or a direct
// set for a switch), and with REC on it writes the jump as motion. Everything here is main loop.
#pragma once

#include "rig.h"
#include <o3_303/pages.h>
#include <o3sdk/core/param.h>
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

// True for a knob whose pads are its values (see ladder_places).
bool ladder_is_list(const o3sdk::core::ParamDesc& d) noexcept;

// How many of the twelve pads a knob uses: one a value for a list (an Enum) or a whole-number knob of twelve values or fewer, 12 equal steps of its own curve for the rest.
// The pads after the last are dark and a tap on one does nothing.
unsigned ladder_places(const o3sdk::core::ParamDesc& d) noexcept;

// The value pad `col` (below ladder_places) stands for: the list's value, or the knob's value at ladder_norm(col). `span` (Controls::live_span; null for none):
// the twelve pads cover that part of the knob's travel instead of all of it (F1 of the review: CUTOFF's 200 to 2,394 Hz on RANGE 303).
float ladder_value(const o3sdk::core::ParamDesc& d, unsigned col, const KnobSpan* span = nullptr) noexcept;

// The pad nearest to `value` (over `span` as above).
unsigned ladder_pos(const o3sdk::core::ParamDesc& d, float value, const KnobSpan* span = nullptr) noexcept;

// The twelve LED levels of a row: a bar up to the pad the value is at (kLedMid), that pad at kLedOn, dark after it; a list draws its pads dim, the chosen one at kLedOn, dark after
// the last; `blink` (the lit half of the blink while an F key is held: a tap assigns) draws every pad kLedOn. `d` null (a row that holds nothing): all dark.
void ladder_levels(const o3sdk::core::ParamDesc* d, float value, bool blink, std::uint8_t (&out)[kLadderSteps], const KnobSpan* span = nullptr) noexcept;

// The entry a row drives now: the held knob, or `latched` for a row that follows. Blank when the held knob has no entry on any page.
Entry ladder_entry(const Controls& controls, const Wiring& wiring, unsigned row, const Entry& latched) noexcept;

// Hold an F key and tap a pad of the row: the row holds `knob`; again with the knob it holds, the row returns to its default.
LadderAssign ladder_assign(Wiring& wiring, const Controls& controls, unsigned row, const Entry& knob) noexcept;

// Shift + FILT: both HOME rows back to CUTOFF and RESO.
void ladder_reset_home(Wiring& wiring) noexcept;

// KNOBS (PERFORM's second page): the four rows' ids, top to bottom. Rows 3 and 4 are HOME's FILT rows.
inline constexpr unsigned kKnobsRows = 4;
inline constexpr unsigned kKnobsRowId[kKnobsRows] = {kLadderKnobs1, kLadderKnobs2, kLadderHome3, kLadderHome4};

// Shift + F8 on KNOBS: its four rows back to ENV MOD, DECAY, CUTOFF and RESO (HOME's two with them: they are the same rows).
void ladder_reset_knobs(Wiring& wiring) noexcept;

// The list the Shift + F1..F4 binding walks: every knob a lane can drive and a page of the app shows, in family order (SOUND, FX, MODDED NOTES, MODDED TONE, GROWL), a knob once.
unsigned ladder_knob_count(const Controls& controls) noexcept;
Entry ladder_knob_at(const Controls& controls, unsigned index) noexcept;
// Moves `row` by `delta` knobs along that list (wrapping) from the knob it holds (from the first end when it holds none of them, or follows) and returns the entry it holds now.
Entry ladder_walk(Wiring& wiring, const Controls& controls, unsigned row, int delta) noexcept;

// "CUTOFF", or "LATCHED: DECAY" for a row that follows. At most cap-1 characters, cut at the end.
void ladder_caption(const o3sdk::dsp::Engine& e, const Controls& controls, const Wiring& wiring, unsigned row, const Entry& latched, char* out, unsigned cap) noexcept;

// A short name of what a row drives, for the pads line and the strip: the held knob's name, "LATCHED" for a row that follows.
const char* ladder_short_name(const o3sdk::dsp::Engine& e, const Controls& controls, const Wiring& wiring, unsigned row) noexcept;

// A tap on place `col` of a row whose entry is `entry`. `rec_motion`: REC is on while the pattern plays. `sel`: the pattern shown.
LadderTap ladder_tap(o3sdk::app::Context& ctx, Rig& rig, bool rec_motion, unsigned sel, const Entry& entry, unsigned col) noexcept;

}  // namespace o3_303
