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

}  // namespace o3_303::seq
