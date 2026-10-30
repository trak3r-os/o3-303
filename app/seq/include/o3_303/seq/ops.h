// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The editing operations: pure functions on a Pattern (o3_303/pattern.h), no state, no allocation, safe on any copy. The project's
// undo history and the render-side handoff are the Bank's job (bank.h): call these through Bank::edit() to edit a project pattern.
//
// Conventions. `step` is 0..kMaxSteps-1 and is clamped; an operation that works on "the pattern" works on the steps below
// `length` (steps above it are hidden data, kept, and come back when the length grows). Motion moves with its steps (four points a
// step). "Playing step" = a step that is neither rest nor tie.

#include <o3sdk/core/param.h>
#include <o3_303/pattern.h>

#include <cstdint>

namespace o3_303::seq {

inline constexpr std::uint8_t kMaxNote = 127;
inline constexpr std::uint8_t kMaxPoint = 254;  // a motion point is 0..kMaxPoint; kNoLock (255) means "the knob's value"

// ---- One step ---------------------------------------------------------------------------------------------------------------

// The note, clamped to 0..127. The flags are untouched (a rest stays a rest).
void set_note(Pattern& p, unsigned step, unsigned note) noexcept;
// One flag bit on or off. REST and TIE exclude each other: turning one on turns the other off.
void set_flag(Pattern& p, unsigned step, std::uint8_t flag, bool on) noexcept;
void toggle_flag(Pattern& p, unsigned step, std::uint8_t flag) noexcept;
bool has_flag(const Pattern& p, unsigned step, std::uint8_t flag) noexcept;
// REC entry: the step takes `note` and exactly `flags` (kStepAccent | kStepSlide, or kStepRest / kStepTie for those entries; for
// those two the note is kept), with no ratchet and no condition. Returns the next cursor: step + 1, or 0 at the end of the length.
unsigned write_advance(Pattern& p, unsigned step, unsigned note, std::uint8_t flags) noexcept;

// The ratchet of a step (flags bits 4-5, pattern.h): 0 = one hit .. kMaxRatchet (3) = four hits; more is clamped. The other flags stay.
// A ratchet on a rest or a tie step is kept (it comes back when the step plays again) and is not played.
void set_ratchet(Pattern& p, unsigned step, unsigned ratchet) noexcept;
unsigned get_ratchet(const Pattern& p, unsigned step) noexcept;
// The trig condition of a step (pattern.h: kCondAlways, 1..99, cond_ratio(), kCondFill ..). A reserved value is stored as kCondAlways, so
// a step never holds data the sequencer would not understand. The note and the flags stay.
void set_cond(Pattern& p, unsigned step, std::uint8_t cond) noexcept;
std::uint8_t get_cond(const Pattern& p, unsigned step) noexcept;
// The encoder's walk through the conditions: `cond` moved `detents` places (negative: back) along one list, clamped at both ends. The list:
// always; the probabilities 10, 20 .. 90 %; the ratios A:B for B = 2..8 and A = 1..B (35 of them); FILL, NOT FILL, FIRST, NOT FIRST
// (kCondStepCount = 49 entries). A value that is not on the list (a probability of 33 %, a reserved byte) counts as the nearest entry
// below it on the list (always for a reserved byte), so a stray value from the data still lets the encoder move.
inline constexpr unsigned kCondStepCount = 49;
std::uint8_t cond_at(unsigned index) noexcept;       // the entry (an index past the end gives the last)
unsigned cond_index(std::uint8_t cond) noexcept;     // where `cond` stands on the list, 0..kCondStepCount-1
std::uint8_t step_cond(std::uint8_t cond, int detents) noexcept;

// Insert: the steps from `at` to the end of the length move one later (the last falls off) and `at` becomes a rest.
// Delete: the steps after `at` move one earlier and the last step of the length becomes a rest. Motion moves with them.
void insert_step(Pattern& p, unsigned at) noexcept;
void delete_step(Pattern& p, unsigned at) noexcept;

// A step with its parameter locks (the step clipboard).
struct StepClip {
  Step step;
  std::uint8_t motion[kMotionLanes][kMotionPointsPerStep];
};
StepClip copy_step(const Pattern& p, unsigned step) noexcept;
void paste_step(Pattern& p, unsigned step, const StepClip& clip) noexcept;

// ---- The pattern ------------------------------------------------------------------------------------------------------------

// Rotates the steps (and their motion) inside the length by `steps`: positive moves them later (to the right), the end wraps to the
// start.
void rotate(Pattern& p, int steps) noexcept;
// Moves the note of every step of the length by `semitones`, rests and ties included (a rest keeps the pitch it will have when it is
// written again). false (nothing changed) when any step of the length would leave 0..127, so the intervals survive. This changes the
// notes; the playback transpose of the pattern is set_transpose().
bool transpose_notes(Pattern& p, int semitones) noexcept;
// The steps (with their flags and motion points) in reverse order inside the length.
void reverse(Pattern& p) noexcept;
// Pitch mirror: every playing note becomes lowest + highest - note, so the pattern keeps its register.
void invert(Pattern& p) noexcept;
// Length x 2, the steps and motion copied into the new half. false (unchanged) at 64.
bool double_length(Pattern& p) noexcept;
// Every step a rest (no ratchet, no condition), no motion; length, swing, gate, transpose and the lanes stay. (o3_303::clear() is the
// fresh pattern instead.)
void clear_pattern(Pattern& p) noexcept;

// One layer of the steps, for the gestures that rotate a layer alone (hold ACC / SLIDE + left / right, docs/M3B-PLAN.md R3).
enum class Layer : std::uint8_t {
  Accent,   // the accent bits
  Slide,    // the slide bits
  Tie,      // the tie bits
  Rest,     // the rest bits (the rhythm: which steps sound)
  Note,     // the pitches
  Ratchet,  // the ratchets
  Cond,     // the trig conditions
};
inline constexpr unsigned kLayerCount = 7;
// Rotates ONE layer of the steps inside the length by `steps` (positive moves it later, the end wraps to the start); every other layer
// of every step, and the motion, stay where they are. Tie and rest exclude each other (set_flag): when a rotated tie lands on a rest, or a
// rotated rest on a tie, the layer that was rotated wins and the other bit of that step goes. A rotation by a multiple of the length
// changes nothing.
void rotate_layer(Pattern& p, Layer layer, int steps) noexcept;

// ---- Fills (docs/M3B-PLAN.md R3): the variant the sequencer plays while FILL is held ----------------------------------------------------
//
// Pure functions on a COPY of the playing pattern (the stored pattern is never touched), deterministic from the seed. They work on the
// steps below the length, and every one keeps the length, swing, gate, transpose, motion and lanes.
enum class FillVariant : std::uint8_t {
  Mutate,     // about 40 % of the playing steps change (a note borrowed from another step of the pattern, so the fill stays in its key; an
              // octave up or down; a toggled accent; a toggled slide) and about 30 % of the rests become notes. Ties and step 0 stay.
  OctUp,      // every note an octave higher (clamped to 127)
  Ratchet,    // every playing step is at least a double hit, and those in the last quarter of the pattern four hits: a roll into the downbeat
  AccRotate,  // the accent layer rotated one step later
};
inline constexpr unsigned kFillVariantCount = 4;

void fill_mutate(Pattern& p, std::uint32_t seed) noexcept;
void fill_octave_up(Pattern& p) noexcept;
void fill_ratchet(Pattern& p) noexcept;
// `variant` applied to `p` (`seed` only matters to Mutate).
void apply_fill(Pattern& p, FillVariant variant, std::uint32_t seed) noexcept;

// 8, 16, 32 or 64; false (unchanged) for anything else.
bool set_length(Pattern& p, unsigned length) noexcept;
void set_swing(Pattern& p, int percent) noexcept;      // 0..kMaxSwing
void set_gate(Pattern& p, int percent) noexcept;       // 5..100
void set_transpose(Pattern& p, int semitones) noexcept;  // -24..+24, applied at playback

// ---- Motion and parameter locks ---------------------------------------------------------------------------------------------

// A point (0..kMaxPoint, or kNoLock) of `lane` (0..7: the voice's ids 0..7) at `step`, quarter `point` (0..3).
void set_motion_point(Pattern& p, unsigned lane, unsigned step, unsigned point, std::uint8_t value) noexcept;
// A p-lock: all four points of the step.
void set_plock(Pattern& p, unsigned lane, unsigned step, std::uint8_t value) noexcept;
void clear_plock(Pattern& p, unsigned lane, unsigned step) noexcept;  // the step's four points of the lane
void clear_step_motion(Pattern& p, unsigned step) noexcept;           // every lane of the step
void clear_lane_motion(Pattern& p, unsigned lane) noexcept;           // the whole lane
void clear_motion(Pattern& p) noexcept;                               // all lanes
bool step_has_motion(const Pattern& p, unsigned step) noexcept;
bool lane_has_motion(const Pattern& p, unsigned lane) noexcept;

// A point and a parameter value, through the descriptor's curve (a knob position): 0 = min, kMaxPoint = max. Enum descriptors snap
// to their entries.
std::uint8_t value_to_point(const o3sdk::core::ParamDesc& d, float value) noexcept;
float point_to_value(const o3sdk::core::ParamDesc& d, std::uint8_t point) noexcept;

// The lane of the pattern that drives (target, id) (pattern.h: kLaneVoice .. kLaneSeq), the lowest if two do; -1 if none does.
int find_lane(const Pattern& p, std::uint8_t target, std::uint8_t id) noexcept;

// ---- Generators -------------------------------------------------------------------------------------------------------------

struct GenParams {
  std::uint8_t scale = 18;        // index into o3sdk's core::kScaleDef (core/scales.h): 18 = Penta Min
  std::uint8_t root = 0;          // pitch class, 0 (C) .. 11
  std::uint8_t low = 24;          // note range of the result (C1 .. C3)
  std::uint8_t high = 48;
  std::uint8_t density = 75;      // percent of steps that sound
  std::uint8_t accent_pct = 25;   // percent of sounding steps with accent
  std::uint8_t slide_pct = 20;    // percent of sounding steps that slide
};

// A new line over the length: it replaces the flags and notes of those steps (ties, ratchets and conditions go; step 0 always
// sounds); motion, the lanes and the steps above the length are untouched. Deterministic from the seed.
void random_fill(Pattern& p, const GenParams& g, std::uint32_t seed) noexcept;
// Changes about `amount_pct` percent of the steps of the length: a new note from the scale, a rest toggled by the density, a
// re-rolled accent or slide.
void mutate(Pattern& p, const GenParams& g, std::uint32_t seed, unsigned amount_pct) noexcept;

// ---- The UX operations (docs/UX-303.md 2.3, 5.1, 5.2, decisions D2 and D8): pure logic the screens only wire up ---------------------

// RHYTHM: keeps every pitch (a rest and a tie keep the one under them), the ratchets and the conditions, and redraws, over the length,
// whether each step is a rest, a tie, an accent or a slide, from `g.density`, `g.accent_pct` and `g.slide_pct`: step 0 always sounds, the
// others sound with the density, a sounding step is accented and slides with the percentages (as random_fill does; no ties are drawn, so
// the old ones go). Deterministic from the seed. Motion, the lanes and the steps above the length are untouched. true: something changed.
bool regen_rhythm(Pattern& p, const GenParams& g, std::uint32_t seed) noexcept;
// NOTES: keeps the rhythm, every flag, ratchet and condition, and redraws the pitch of every sounding step (not a rest, not a tie: those
// keep theirs) over the length from the scale and the range of `g` (`scale`, `root`, `low`, `high`), with random_fill's draw (a repeated
// note now and then). Deterministic from the seed. Motion, the lanes and the steps above the length are untouched. true: something changed.
bool regen_notes(Pattern& p, const GenParams& g, std::uint32_t seed) noexcept;

// The PLAYS list of a step (D8): one list that holds a ratchet or a condition, never both. The 17 entries, in the order the encoder walks:
//   0..3   x1 x2 x3 x4      the ratchet 0..3, no condition
//   4..6   75% 50% 25%      a probability
//   7..12  1:2 2:2 1:4 2:4 3:4 4:4
//   13..16 FILL, NOT FILL, FIRST, NOT FIRST
inline constexpr unsigned kPlaysCount = 17;
struct PlaysEntry {
  std::uint8_t ratchet;  // 0..kMaxRatchet
  std::uint8_t cond;     // kCondAlways, a probability, cond_ratio(), kCondFill ..
};
PlaysEntry plays_at(unsigned index) noexcept;  // an index past the end gives the last entry
// The entry a step shows: a step with a (valid) condition shows the condition, whatever its ratchet (old data); one without shows its
// ratchet. A condition that is not on the list is shown by the nearest entry: a probability by the nearest of 75 / 50 / 25 %, an A:B by
// the entry with the nearest cycle B (2 or 4; B = 3 goes to 2) and then the nearest position A/B. A reserved condition byte plays as
// always, so it shows the ratchet.
unsigned plays_index(std::uint8_t flags, std::uint8_t cond) noexcept;
// false: the stored value is not on the list (a probability of 33 %, a 3:8, a reserved byte): the screen shows it as itself.
bool plays_exact(std::uint8_t flags, std::uint8_t cond) noexcept;
// The encoder's walk: the entry `detents` places from the one shown (clamped at both ends). From a stray value the first detent, in either
// direction, lands on the nearest entry; the rest continue from there. 0 detents give the entry shown.
unsigned plays_step(std::uint8_t flags, std::uint8_t cond, int detents) noexcept;
// The step takes entry `index` (past the end gives the last): the ratchet or the condition is set and the other cleared. true: changed.
bool set_plays(Pattern& p, unsigned step, unsigned index) noexcept;

// The GATE list of a step: what the step does, from the flags (the ratchet bits are not part of it).
//   0 REST (rest)   1 NOTE (none)   2 ACC (accent)   3 SLD (slide)   4 ACC+SLD (both)   5 TIE (tie)
inline constexpr unsigned kGateListCount = 6;
inline constexpr unsigned kGateRest = 0;
inline constexpr unsigned kGateNote = 1;
inline constexpr unsigned kGateAcc = 2;
inline constexpr unsigned kGateSld = 3;
inline constexpr unsigned kGateAccSld = 4;
inline constexpr unsigned kGateTie = 5;
// The entry the flags show: a rest shows REST (it wins over a stray tie bit), then a tie shows TIE, then accent and slide.
unsigned gate_list_index(std::uint8_t flags) noexcept;
// The kStep* bits of an entry (a rest and a tie carry no accent or slide); an index past the end gives the last entry's (TIE).
std::uint8_t gate_list_flags(unsigned index) noexcept;
// The step takes entry `index`: its accent, slide, tie and rest bits are replaced by the entry's; the ratchet, note and condition stay.
// true: changed.
bool set_gate_list(Pattern& p, unsigned step, unsigned index) noexcept;

// D2: the lane of `p` that drives (target, id) if there is one (find_lane); else the first lane with no motion point, re-pointed at
// (target, id); else -1 (all eight in use). Changes the pattern only in the second case.
int claim_lane(Pattern& p, std::uint8_t target, std::uint8_t id) noexcept;

// The length of the pattern, 8, 16, 32 or 64 (false, unchanged, for anything else or the length it has). Growing copies the steps below the
// old length, with their motion, over and over into the new part (8 to 32: the 8 steps three more times; 16 to 32 is double_length());
// shrinking truncates: the cut steps become rests (no note, ratchet or condition) and their motion points are cleared, so a lock above the
// new length cannot keep a lane in use. Swing, gate, transpose and the lanes stay. true: changed.
bool resize(Pattern& p, unsigned length) noexcept;

}  // namespace o3_303::seq
