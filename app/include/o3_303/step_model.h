// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the step view draws, as pure functions of a Pattern (no hardware, no drawing): how a step reads (a note, a tie that carries
// the note before it, a rest), how it joins its neighbours (a slide ramps into the next note, a tie joins bars), the pitch window of
// the lane, note names, and a few questions the pad LEDs and the pattern grid ask. Semantics are pattern.h's: rest = gate closed,
// tie = the note before goes on through this step, slide = held into the next step, which glides.

#include <o3_303/pattern.h>

#include <cstdint>

namespace o3_303 {

enum class Cell : std::uint8_t {
  Rest,  // nothing sounds here (also a tie with no note before it)
  Note,  // a note starts here
  Tie,   // the note before goes on sounding: `note` is its pitch
};

struct StepInfo {
  Cell kind = Cell::Rest;
  std::uint8_t note = 0;  // Note: the step's note; Tie: the pitch of the note it carries
  bool accent = false;    // Note only
  bool slide = false;     // the sound is held into the next step
  bool from_prev = false; // Tie: the bar joins the cell before it
  bool to_next = false;   // the sound goes on into the next step (a slide, or a tie after this step)
  int ramp_to = -1;       // a slide into a note: the pitch it glides to (else -1)
};

// Step `step` (0..length-1) of `p`, with its neighbours. A tie looks back through the ties before it, wrapping at the pattern's start
// (the sequencer holds a note across the loop), to the note it carries.
StepInfo describe(const Pattern& p, unsigned step) noexcept;

// Every step is a rest: the pattern holds nothing to play.
bool pattern_is_empty(const Pattern& p) noexcept;

// The notes that start a step in [0, length): lowest and highest. false when there are none.
bool pitch_range(const Pattern& p, int& lowest, int& highest) noexcept;

// Whether step `step` has a motion point on lane `lane` (any of its four points is a lock).
bool step_has_motion(const Pattern& p, unsigned lane, unsigned step) noexcept;
// Whether any step of `p` has a motion point on lane `lane` (lane >= kMotionLanes: any lane).
bool pattern_has_motion(const Pattern& p, unsigned lane) noexcept;

// The pitch lane: `rows` semitones from `lo` up, `px` pixels each.
struct PitchWindow {
  int lo = 0;
  int rows = 13;
  int px = 16;
  int hi() const noexcept { return lo + rows - 1; }
};

// The step view's lane (docs/M3B-PLAN.md S1): the pattern's real range [lowest, highest] and `pad` semitones each side, no more, in the tallest
// rows (16 px down to 5) that lane_h holds; the rows are centred in the lane, so a narrow pattern leaves empty lane at both ends. A range too
// wide for 5 px rows shows as many rows as fit, centred on it.
PitchWindow range_window(int lowest, int highest, int lane_h, int pad = 3) noexcept;

// ---- The drawer's rows (docs/UX-303.md 2.3 and 5.1): what the encoder can own on HOME, SETUP and INSTRUMENT besides a knob -------------------

// The row the encoder edits on the step (Note, Instr, Vol, Fx latch them, one each): the NOTE row, the GATE row, the PLAYS row, the LOCK row.
enum class DrawerRow : std::uint8_t { None, Note, Gate, Plays, Lock };
// The NOTE row walks C1..C5.
inline constexpr unsigned kRowNoteLow = 24;
inline constexpr unsigned kRowNoteHigh = 72;

// "REST", "NOTE", "ACC", "SLD", "ACC+SLD", "TIE" (seq::kGateListCount entries; an index past the end gives the last) and the glyph a step shows on the GATE
// row: ".", "-", "A", "S", "AS", "~".
const char* gate_name(unsigned index) noexcept;
const char* gate_glyph(unsigned index) noexcept;
// What the PLAYS row shows of a step (D8): its condition ("75%", "1:2", "FILL", "!FIRST": the stored value as itself), else its ratchet "x1".."x4".
// Returns the length written (cap bytes with the terminator).
unsigned plays_text(std::uint8_t flags, std::uint8_t cond, char* out, unsigned cap) noexcept;
// The STEP box: "STEP 5: D#2 ACC SLD x2", "STEP 6: REST", "STEP 7: TIE 1:2" (the note and its accent and slide, the ratchet when above x1, the condition),
// without the lock (the screen adds the latched knob's). Step `step` is clamped into the pattern. Returns the length written.
unsigned step_text(const Pattern& p, unsigned step, char* out, unsigned cap) noexcept;
// The lock of `step` on `lane` as a motion point: the first of its four points that has one, kNoLock when none (lane >= kMotionLanes: kNoLock).
std::uint8_t step_lock_point(const Pattern& p, unsigned lane, unsigned step) noexcept;

// "C2", "C#3", "A#-1"-style name of a MIDI note (C2 = 36: the octave is note / 12 - 1) into `out` (cap at least 5). Returns its length.
unsigned note_name(unsigned note, char* out, unsigned cap) noexcept;

// "A07": the pattern's name on the pads (48 patterns: A..D, 01..12, a row of 12 each).
void pattern_name(unsigned index, char* out, unsigned cap) noexcept;

}  // namespace o3_303
