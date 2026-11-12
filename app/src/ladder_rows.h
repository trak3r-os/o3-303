// The ladder rows (backlog item 8): HOME's FILT rows 3 and 4 and PERFORM's row 3 (HOME's row 3 again: KN1) are one kind of row. A row holds a
// knob (Wiring::ladder, stored as a lane target and parameter), a tap jumps the knob to one of twelve places (a lane slew, or a direct
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
  bool not_recorded = false;  // KN4: REC was on but the knob is a row-only one (the wobble's settings, MODS, GROWL, TRACT, WOB): it was set, nothing was written
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

// The entry a row drives now: the knob it holds. Blank when that knob has no entry on any page.
Entry ladder_entry(const Controls& controls, const Wiring& wiring, unsigned row) noexcept;

// Hold an F key and tap a pad of the row: the row holds `knob`; again with the knob it holds, the row returns to its default.
LadderAssign ladder_assign(Wiring& wiring, const Controls& controls, unsigned row, const Entry& knob) noexcept;

// Shift + FILT: both HOME rows (rows 3 and 4 of the set in use) back to that set's factory knobs.
void ladder_reset_home(Wiring& wiring) noexcept;

// KNOBS (PERFORM's second page): the four rows' ids, top to bottom. Rows 3 and 4 are HOME's FILT rows. Which knobs they hold is the set in use (KN3, Wiring::knob_sets).
inline constexpr unsigned kKnobsRows = kKnobsPerSet;
inline constexpr unsigned kKnobsRowId[kKnobsRows] = {kLadderKnobs1, kLadderKnobs2, kLadderHome3, kLadderHome4};

// The row's place (0..3) in a set, or kKnobsRows for the retired slot and an unknown row.
unsigned knob_row_pos(unsigned row) noexcept;
// The knob a set's row holds when the set is factory-filled.
LaneRef knob_factory(unsigned set, unsigned pos) noexcept;
// Make set `set` (0..3, clamped) the one in use: the four live rows (Wiring::ladder) take its knobs.
void knob_set_use(Wiring& wiring, unsigned set) noexcept;
// Put set `set` back to its factory knobs (a copy of the four rows for UNDO is the caller's); the live rows follow when it is the one in use.
void knob_set_factory(Wiring& wiring, unsigned set) noexcept;
// What a set is called while it holds its factory knobs ("FILTER", "MODDED", "GROWL", "WOBBLE"), and whether it does (a re-pointed row makes it the player's own).
const char* knob_set_name(unsigned set) noexcept;
bool knob_set_is_factory(const Wiring& wiring, unsigned set) noexcept;

// The list the hold + Up/Down binding walks: every knob a lane can drive and a page of the app shows, in family order (SOUND, FX, MODDED NOTES, MODDED TONE, GROWL, TRACT, WOBBLE); KN4: with the row-only knobs (RowOnlyTarget), a knob once.
unsigned ladder_knob_count(const Controls& controls) noexcept;
Entry ladder_knob_at(const Controls& controls, unsigned index) noexcept;
// Moves `row` by `delta` knobs along that list (wrapping) from the knob it holds (from the first end when it holds none of them) and returns the entry it holds now. `family` (if not null) gets the
// name of the family that knob is in.
Entry ladder_walk(Wiring& wiring, const Controls& controls, unsigned row, int delta, const char** family = nullptr) noexcept;
// Shift + Up/Down: moves `row` to the first knob of the next (delta > 0) or previous family; returns the entry it holds now. `family` (if not null) gets the family's name.
Entry ladder_walk_family(Wiring& wiring, const Controls& controls, unsigned row, int delta, const char** family) noexcept;

// "CUTOFF": the name of the knob the row holds. At most cap-1 characters, cut at the end.
void ladder_caption(const o3sdk::dsp::Engine& e, const Controls& controls, const Wiring& wiring, unsigned row, char* out, unsigned cap) noexcept;

// A short name of what a row drives, for the pads line and the strip: the held knob's name.
const char* ladder_short_name(const o3sdk::dsp::Engine& e, const Controls& controls, const Wiring& wiring, unsigned row) noexcept;

// A tap on place `col` of a row whose entry is `entry`. `rec_motion`: REC is on while the pattern plays. `sel`: the pattern shown.
LadderTap ladder_tap(o3sdk::app::Context& ctx, Rig& rig, bool rec_motion, unsigned sel, const Entry& entry, unsigned col) noexcept;

}  // namespace o3_303
